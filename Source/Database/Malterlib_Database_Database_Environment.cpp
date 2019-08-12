// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2019 Howard Chu, Symas Corp. All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted only as authorized by the OpenLDAP
 Public License.

 A copy of this license is available in the file LICENSE.OpenLDAP in the
 top-level directory of the distribution or, alternatively, at
 <http://www.OpenLDAP.org/license.html>.

 This code is derived from btree.c written by Martin Hedenfalk.

 Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.

 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "Malterlib_Database_Database.h"
#include "Malterlib_Database_Database_Internal.h"

#ifdef DPlatformFamily_Windows
#include <Mib/Core/PlatformSpecific/WindowsOptional>
#endif

uint32 fg_MalterlibAttributesToMode(NMib::NFile::EFileAttrib _Attributes, uint32 _Mode = 0644);

namespace NMib::NDatabase
{
	using namespace NPrivate;

	/** @brief Create an LMDB environment handle.
	 *
	 * This function allocates memory for a #MDB_env structure. To release
	 * the allocated memory and discard the handle, call #f_Close().
	 * Before the handle may be used, it must be opened using #f_Open().
	 * Various other options may also need to be set before opening the handle,
	 * e.g. #f_SetMapSize(), #f_SetMaxReaders(), #f_SetMaxDatabases(),
	 * depending on usage requirements.
	 */
	DMibDatabaseInfrequentSection CDatabaseEnvironment::CDatabaseEnvironment()
	{
		NStorage::TCUniquePointer<CDatabaseEnvironment_Internal> pEnvironment = fg_Construct();

		NMemory::fg_MemClear(*pEnvironment);

		pEnvironment->m_MaxReaders = DEFAULT_READERS;
		pEnvironment->me_maxdbs = CORE_DBS;
		pEnvironment->me_numdbs = CORE_DBS;
		pEnvironment->me_fd = INVALID_HANDLE_VALUE;
		pEnvironment->me_lfd = INVALID_HANDLE_VALUE;
		pEnvironment->me_mfd = INVALID_HANDLE_VALUE;
	#ifdef MDB_USE_POSIX_SEM
		pEnvironment->me_rmutex = SEM_FAILED;
		pEnvironment->me_wmutex = SEM_FAILED;
	#elif defined MDB_USE_SYSV_SEM
		pEnvironment->me_rmutex->semid = -1;
		pEnvironment->me_wmutex->semid = -1;
	#endif
		pEnvironment->me_pid = getpid();
		GET_PAGESIZE(pEnvironment->me_os_psize);

		mp_pEnvironment = fg_Move(pEnvironment);
	}

	CDatabaseEnvironment::CDatabaseEnvironment(CDatabaseEnvironment &&_Other) = default;
	CDatabaseEnvironment &CDatabaseEnvironment::operator =(CDatabaseEnvironment &&_Other) = default;

	/** @brief Open an environment handle.
	 *
	 * If this function fails, #f_Close() must be called to discard the #MDB_env handle.
	 * @param[in] path The directory in which the database files reside. This
	 * directory must already exist and be writable.
	 * @param[in] flags Special options for this environment. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * Flags set by f_SetFlags() are also used.
	 * <ul>
	 *	<li>#EDatabaseEnvironmentFlag_FixedMap
	 *      use a fixed address for the mmap region. This flag must be specified
	 *      when creating the environment, and is stored persistently in the environment.
	 *		If successful, the memory map will always reside at the same virtual address
	 *		and pointers used to reference data items in the database will be constant
	 *		across multiple invocations. This option may not always work, depending on
	 *		how the operating system has allocated memory to shared libraries and other uses.
	 *		The feature is highly experimental.
	 *	<li>#EDatabaseEnvironmentFlag_NoSubdir
	 *		By default, LMDB creates its environment in a directory whose
	 *		pathname is given in \b path, and creates its data and lock files
	 *		under that directory. With this option, \b path is used as-is for
	 *		the database main data file. The database lock file is the \b path
	 *		with "-lock" appended.
	 *	<li>#EDatabaseEnvironmentFlag_ReadOnly
	 *		Open the environment in read-only mode. No write operations will be
	 *		allowed. LMDB will still modify the lock file - except on read-only
	 *		filesystems, where LMDB does not use locks.
	 *	<li>#EDatabaseEnvironmentFlag_WriteMap
	 *		Use a writeable memory map unless EDatabaseEnvironmentFlag_ReadOnly is set. This uses
	 *		fewer mallocs but loses protection from application bugs
	 *		like wild pointer writes and other bad updates into the database.
	 *		This may be slightly faster for DBs that fit entirely in RAM, but
	 *		is slower for DBs larger than RAM.
	 *		Incompatible with nested transactions.
	 *		Do not mix processes with and without EDatabaseEnvironmentFlag_WriteMap on the same
	 *		environment.  This can defeat durability (#f_Sync etc).
	 *	<li>#EDatabaseEnvironmentFlag_NoMetaSync
	 *		Flush system buffers to disk only once per transaction, omit the
	 *		metadata flush. Defer that until the system flushes files to disk,
	 *		or next non-EDatabaseEnvironmentFlag_ReadOnly commit or #f_Sync(). This optimization
	 *		maintains database integrity, but a system crash may undo the last
	 *		committed transaction. I.e. it preserves the ACI (atomicity,
	 *		consistency, isolation) but not D (durability) database property.
	 *		This flag may be changed at any time using #f_SetFlags().
	 *	<li>#EDatabaseEnvironmentFlag_NoSync
	 *		Don't flush system buffers to disk when committing a transaction.
	 *		This optimization means a system crash can corrupt the database or
	 *		lose the last transactions if buffers are not yet flushed to disk.
	 *		The risk is governed by how often the system flushes dirty buffers
	 *		to disk and how often #f_Sync() is called.  However, if the
	 *		filesystem preserves write order and the #EDatabaseEnvironmentFlag_WriteMap flag is not
	 *		used, transactions exhibit ACI (atomicity, consistency, isolation)
	 *		properties and only lose D (durability).  I.e. database integrity
	 *		is maintained, but a system crash may undo the final transactions.
	 *		Note that (#EDatabaseEnvironmentFlag_NoSync | #EDatabaseEnvironmentFlag_WriteMap) leaves the system with no
	 *		hint for when to write transactions to disk, unless #f_Sync()
	 *		is called. (#EDatabaseEnvironmentFlag_WriteMapAsync | #EDatabaseEnvironmentFlag_WriteMap) may be preferable.
	 *		This flag may be changed at any time using #f_SetFlags().
	 *	<li>#EDatabaseEnvironmentFlag_WriteMapAsync
	 *		When using #EDatabaseEnvironmentFlag_WriteMap, use asynchronous flushes to disk.
	 *		As with #EDatabaseEnvironmentFlag_NoSync, a system crash can then corrupt the
	 *		database or lose the last transactions. Calling #f_Sync()
	 *		ensures on-disk database integrity until next commit.
	 *		This flag may be changed at any time using #f_SetFlags().
	 *	<li>#EDatabaseEnvironmentFlag_NoTls
	 *		Don't use Thread-Local Storage. Tie reader locktable slots to
	 *		#MDB_txn objects instead of to threads. I.e. #f_Reset() keeps
	 *		the slot reseved for the #MDB_txn object. A thread may use parallel
	 *		read-only transactions. A read-only transaction may span threads if
	 *		the user synchronizes its use. Applications that multiplex many
	 *		user threads over individual OS threads need this option. Such an
	 *		application must also serialize the write transactions in an OS
	 *		thread, since LMDB's write locking is unaware of the user threads.
	 *	<li>#EDatabaseEnvironmentFlag_NoLock
	 *		Don't do any locking. If concurrent access is anticipated, the
	 *		caller must manage all concurrency itself. For proper operation
	 *		the caller must enforce single-writer semantics, and must ensure
	 *		that no readers are using old transactions while a writer is
	 *		active. The simplest approach is to use an exclusive lock so that
	 *		no readers may be active at all when a writer begins.
	 *	<li>#EDatabaseEnvironmentFlag_NoReadAhead
	 *		Turn off readahead. Most operating systems perform readahead on
	 *		read requests by default. This option turns it off if the OS
	 *		supports it. Turning it off may help random read performance
	 *		when the DB is larger than RAM and system RAM is full.
	 *		The option is not implemented on Windows.
	 *	<li>#EDatabaseEnvironmentFlag_NoMemoryInit
	 *		Don't initialize malloc'd memory before writing to unused spaces
	 *		in the data file. By default, memory for pages written to the data
	 *		file is obtained using NMemory::fg_Alloc. While these pages may be reused in
	 *		subsequent transactions, freshly malloc'd pages will be initialized
	 *		to zeroes before use. This avoids persisting leftover data from other
	 *		code (that used the heap and subsequently freed the memory) into the
	 *		data file. Note that many other system libraries may allocate
	 *		and free memory from the heap for arbitrary uses. E.g., stdio may
	 *		use the heap for file I/O buffers. This initialization step has a
	 *		modest performance cost so some applications may want to disable
	 *		it using this flag. This option can be a problem for applications
	 *		which handle sensitive data like passwords, and it makes memory
	 *		checkers like Valgrind noisy. This flag is not needed with #EDatabaseEnvironmentFlag_WriteMap,
	 *		which writes directly to the mmap instead of using NMemory::fg_Alloc for pages. The
	 *		initialization is also skipped if #EDatabaseWriteFlag_ReserveSpace is used; the
	 *		caller is expected to overwrite all of the memory that was
	 *		reserved in that case.
	 *		This flag may be changed at any time using #f_SetFlags().
	 *	<li>#EDatabaseEnvironmentFlag_PreviousSnapshot
	 *		Open the environment with the previous snapshot rather than the latest
	 *		one. This loses the latest transaction, but may help work around some
	 *		types of corruption. If opened with write access, this must be the
	 *		only process using the environment. This flag is automatically reset
	 *		after a write transaction is successfully committed.
	 * </ul>
	 * @param[in] mode The UNIX permissions to set on created files and semaphores.
	 * This parameter is ignored on Windows.
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_VERSION_MISMATCH - the version of the LMDB library doesn't match the
	 *	version that created the database environment.
	 *	<li>#MDB_INVALID - the environment file headers are corrupted.
	 *	<li>ENOENT - the directory specified by the path parameter doesn't exist.
	 *	<li>EACCES - the user didn't have permission to access the environment files.
	 *	<li>EAGAIN - the environment was locked by another process.
	 * </ul>
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Open(NStr::CStr const &_Path, EDatabaseEnvironmentFlag _Flags, NFile::EFileAttrib _FileAttributes, uint32 _PageSize)
	{
		uint32 flags = _Flags;
		uint32 Mode = 0;
#ifndef DPlatformFamily_Windows
		Mode = fg_MalterlibAttributesToMode(_FileAttributes);
#endif
		auto &Environment = *mp_pEnvironment;

		using namespace NPrivate;

		int excl = -1;

		if (Environment.me_fd != INVALID_HANDLE_VALUE)
			DMibError("Environment has already been opened");
		if (flags & ~(EDatabaseEnvironmentFlag_Changable|EDatabaseEnvironmentFlag_Changeless))
			DMibError("Invalid flags specified");

	#ifdef DMibDatabase_Always64BitSupport
		if (flags & EDatabaseEnvironmentFlag_WriteMap)
			flags ^= EDatabaseEnvironmentFlag_WriteMap; // silently ignore WRITEMAP in 32 bit mode
		if (flags & EDatabaseEnvironmentFlag_FixedMap)
			DMibError("Fixed map not supported with DMibDatabase_Always64BitSupport");
	#endif

		flags |= Environment.me_flags;

		auto Cleanup = g_OnScopeExit > [&]()
			{
				Environment.mdb_env_close0(excl);
			}
		;

		int rc;
	#ifdef DMibDatabase_Always64BitSupport
		#ifdef DPlatformFamily_Windows
			me_rpmutex = CreateMutex(NULL, FALSE, NULL);
			if (!me_rpmutex) {
				rc = ErrCode();
				DMibError(NStr::fg_Format("Error initing rmutex {}", mdb_strerror(rc)));
			}
		#else
			rc = pthread_mutex_init(&Environment.me_rpmutex, NULL);
			if (rc)
				DMibError(NStr::fg_Format("Error initing rmutex {}", mdb_strerror(rc)));
		#endif
	#endif
		flags |= MDB_ENV_ACTIVE;	/* tell mdb_env_close0() to clean up */

		if (flags & EDatabaseEnvironmentFlag_ReadOnly) {
			/* silently ignore WRITEMAP when we're only getting read access */
			flags &= ~EDatabaseEnvironmentFlag_WriteMap;
		} else {
			Environment.me_free_pgs = mdb_midl_alloc(MDB_IDL_UM_MAX);
			Environment.me_dirty_list = (MDB_ID2L)NMemory::fg_AllocInitZero(MDB_IDL_UM_SIZE * sizeof(MDB_ID2));
		}

		Environment.me_flags = flags;

	#ifdef DMibDatabase_Always64BitSupport
		if (rc)
			DMibError(NStr::fg_Format("Error opening mutex {}", mdb_strerror(rc)));

		{
			Environment.me_rpages = (NPrivate::MDB_ID3L)NMemory::fg_Alloc(MDB_ERPAGE_SIZE * sizeof(NPrivate::MDB_ID3));
			Environment.me_rpages[0].mid = 0;
			Environment.me_rpcheck = MDB_ERPAGE_SIZE/2;
		}
	#endif

		Environment.me_path = _Path;
		Environment.m_StaticDasabaseInfo.f_SetLen(Environment.me_maxdbs);
		Environment.me_dbflags = (uint16 *)NMemory::fg_AllocInitZero(Environment.me_maxdbs * sizeof(uint16));
		Environment.me_dbiseqs = (uint32 *)NMemory::fg_AllocInitZero(Environment.me_maxdbs * sizeof(uint32));
		Environment.m_StaticDasabaseInfo[FREE_DBI].m_fKeyCompare = mdb_cmp_long; /* aligned EDatabaseSubFlag_IntegerKeys */

		/* For RDONLY, get lockfile after we know datafile exists */
		if (!(flags & (EDatabaseEnvironmentFlag_ReadOnly|EDatabaseEnvironmentFlag_NoLock)))
		{
			Environment.mdb_env_setup_locks(_Path, Mode, &excl);
			if ((flags & EDatabaseEnvironmentFlag_PreviousSnapshot) && !excl)
				DMibError(NStr::fg_Format("Previous snapshot needs exclusive access: {}", mdb_strerror(EAGAIN)));
		}

		rc = Environment.mdb_fopen(_Path, (flags & EDatabaseEnvironmentFlag_ReadOnly) ? MDB_O_RDONLY : MDB_O_RDWR, Mode, &Environment.me_fd);
		if (rc)
			DMibError(NStr::fg_Format("Error opening file {}", mdb_strerror(rc)));

		if ((flags & (EDatabaseEnvironmentFlag_ReadOnly|EDatabaseEnvironmentFlag_NoLock)) == EDatabaseEnvironmentFlag_ReadOnly)
			Environment.mdb_env_setup_locks(_Path, Mode, &excl);

		Environment.mdb_env_open2(flags & EDatabaseEnvironmentFlag_PreviousSnapshot, _PageSize);

		if (!(flags & (EDatabaseEnvironmentFlag_ReadOnly|EDatabaseEnvironmentFlag_WriteMap))) {
			/* Synchronous fd for meta writes. Needed even with
			 * EDatabaseEnvironmentFlag_NoSync/EDatabaseEnvironmentFlag_NoMetaSync, in case these get reset.
			 */
			rc = Environment.mdb_fopen(_Path, MDB_O_META, Mode, &Environment.me_mfd);
			if (rc)
				DMibError(NStr::fg_Format("Error opening meta sync file {}", mdb_strerror(rc)));
		}

		DMibDatabaseTrace("opened dbenv {}", (void *)&Environment);
		if (excl > 0 && !(flags & EDatabaseEnvironmentFlag_PreviousSnapshot))
		{
			rc = Environment.mdb_env_share_locks(&excl);
			if (rc)
				DMibError(NStr::fg_Format("Error share locks {}", mdb_strerror(rc)));
		}

		if (!(flags & EDatabaseEnvironmentFlag_ReadOnly))
		{
			MDB_txn *txn;
			int tsize = sizeof(MDB_txn), size = tsize + Environment.me_maxdbs * (sizeof(MDB_db)+sizeof(MDB_cursor *)+sizeof(uint32)+1);
			Environment.me_pbuf = NMemory::fg_AllocInitZero(Environment.me_psize);
			txn = (MDB_txn *)NMemory::fg_AllocInitZero(size);
			auto Cleanup1 = g_OnScopeExit > [&]()
				{
					NMemory::fg_FreeNoSize(txn);
				}
			;
			txn->mt_dbs = (MDB_db *)((char *)txn + tsize);
			txn->mt_cursors = (MDB_cursor **)(txn->mt_dbs + Environment.me_maxdbs);
			txn->mt_dbiseqs = (uint32 *)(txn->mt_cursors + Environment.me_maxdbs);
			txn->mt_dbflags = (unsigned char *)(txn->mt_dbiseqs + Environment.me_maxdbs);
			txn->mt_env = &Environment;
#ifdef DMibDatabase_Always64BitSupport
			txn->mt_rpages = (NPrivate::MDB_ID3L)NMemory::fg_Alloc(MDB_TRPAGE_SIZE * sizeof(MDB_ID3));
			txn->mt_rpages[0].mid = 0;
			txn->mt_rpcheck = MDB_TRPAGE_SIZE/2;
#endif
			txn->mt_dbxs = Environment.m_StaticDasabaseInfo.f_GetArray();
			txn->mt_flags = MDB_TXN_FINISHED;
			Environment.me_txn0 = txn;
			Cleanup1.f_Clear();
		}

		Cleanup.f_Clear();
	}

	/** @brief Copy an LMDB environment to the specified path, with options.
	 *
	 * This function may be used to make a backup of an existing environment.
	 * No lockfile is created, since it gets recreated at need.
	 * @note This call can trigger significant file size growth if run in
	 * parallel with write transactions, because it employs a read-only
	 * transaction. See long-lived transactions under @ref caveats_sec.
	 * @param[in] path The directory in which the copy will reside. This
	 * directory must already exist and be writable but must otherwise be
	 * empty.
	 * @param[in] flags Special options for this operation. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * <ul>
	 *	<li>#EDatabaseEnvironmentCopyFlag_Compact - Perform compaction while copying: omit free
	 *		pages and sequentially renumber all pages in output. This option
	 *		consumes more CPU and runs more slowly than the default.
	 *		Currently it fails if the environment has suffered a page leak.
	 * </ul>
	 * @return A non-zero error value on failure and 0 on success.
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Copy(NStr::CStr const &_Path, EDatabaseEnvironmentCopyFlag _Flags)
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;

		HANDLE newfd = INVALID_HANDLE_VALUE;
		int rc = Environment.mdb_fopen(_Path, MDB_O_COPY, 0666, &newfd);

		if (rc)
			DMibError(NStr::fg_Format("Error opening file {}", mdb_strerror(rc)));

		auto Cleanup = g_OnScopeExit > [&]()
			{
				int Error = close(newfd);
				(void)Error;
				DMibFastCheck(Error >= 0);
			}
		;

		f_Copy((void *)(mint)newfd, _Flags);
	}

	/** @brief Copy an LMDB environment to the specified file descriptor,
	 *	with options.
	 *
	 * This function may be used to make a backup of an existing environment.
	 * No lockfile is created, since it gets recreated at need. See
	 * #f_Copy() for further details.
	 * @note This call can trigger significant file size growth if run in
	 * parallel with write transactions, because it employs a read-only
	 * transaction. See long-lived transactions under @ref caveats_sec.
	 * @param[in] fd The filedescriptor to write the copy to. It must
	 * have already been opened for Write access.
	 * @param[in] flags Special options for this operation.
	 * See #f_Copy() for options.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Copy(void *_FileHandle, EDatabaseEnvironmentCopyFlag _Flags)
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		if (_Flags & EDatabaseEnvironmentCopyFlag_Compact)
			Environment.f_CopyCompact((HANDLE)(mint)_FileHandle);
		else
			Environment.f_Copy((HANDLE)(mint)_FileHandle);
	}

	/** @brief Return statistics about the LMDB environment.
	 *
	 * @param[out] stat The address of an #CDatabaseStatistics structure
	 * 	where the statistics will be copied
	 */
	CDatabaseStatistics DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Statistics()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		using namespace NPrivate;
		MDB_meta *meta;

		meta = Environment.mdb_env_pick_meta();

		return Environment.f_DatabaseStatistics(&meta->mm_dbs[MAIN_DBI]);
	}

	/** @brief Return information about the LMDB environment.
	 *
	 * @param[out] stat The address of an #CDatabaseEnvironmentInfo structure
	 * 	where the information will be copied
	 */
	CDatabaseEnvironmentInfo DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Info()
	{
		fp_CheckOpened();

		using namespace NPrivate;
		MDB_meta *meta;
		auto &Environment = *mp_pEnvironment;

		CDatabaseEnvironmentInfo Info;

		meta = Environment.mdb_env_pick_meta();
		Info.m_pMapAddress = meta->mm_address;
		Info.m_LastPageID = meta->mm_last_pg;
		Info.m_LastCommittedTransactionID = meta->mm_txnid;

		Info.m_MapSize = Environment.m_MapSize;
		Info.m_MaxReaders = Environment.m_MaxReaders;
		Info.m_nReaders = Environment.m_pLocksMemoryMap ? Environment.m_pLocksMemoryMap->mti_numreaders : 0;
		return Info;
	}

	/** @brief Flush the data buffers to disk.
	 *
	 * Data is always written to disk when #f_Commit() is called,
	 * but the operating system may keep it buffered. LMDB always flushes
	 * the OS buffers upon commit as well, unless the environment was
	 * opened with #EDatabaseEnvironmentFlag_NoSync or in part #EDatabaseEnvironmentFlag_NoMetaSync. This call is
	 * not valid if the environment was opened with #EDatabaseEnvironmentFlag_ReadOnly.
	 * @param[in] force If non-zero, force a synchronous flush.  Otherwise
	 *  if the environment has the #EDatabaseEnvironmentFlag_NoSync flag set the flushes
	 *	will be omitted, and with #EDatabaseEnvironmentFlag_WriteMapAsync they will be asynchronous.
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EACCES - the environment is read-only.
	 *	<li>EINVAL - an invalid parameter was specified.
	 *	<li>EIO - an error occurred during synchronization.
	 * </ul>
	 */
	void CDatabaseEnvironment::f_Sync(bool _bForce)
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;

		using namespace NPrivate;
		MDB_meta *m = Environment.mdb_env_pick_meta();
		Environment.mdb_env_sync0(_bForce, m->mm_last_pg+1);
	}

	/** @brief Close the environment and release the memory map.
	 *
	 * Only a single thread may call this function. All transactions, databases,
	 * and cursors must already be closed before calling this function. Attempts to
	 * use any such handles after calling this function will cause a SIGSEGV.
	 * The environment handle will be freed and must not be used again after this call.
	 */
	DMibDatabaseInfrequentSection CDatabaseEnvironment::~CDatabaseEnvironment()
	{
		if (!mp_pEnvironment)
			return;

		f_Close();
	}

	bool CDatabaseEnvironment::f_IsOpen() const
	{
		return !!mp_pEnvironment;
	}

	void CDatabaseEnvironment::fp_CheckOpened()
	{
		if (!mp_pEnvironment)
			DMibError("Environment has not been opened");
	}

	void CDatabaseEnvironment::f_Close()
	{
		fp_CheckOpened();

		using namespace NPrivate;
		MDB_page *dp;

		auto &Environment = *mp_pEnvironment;

		while ((dp = Environment.me_dpages) != nullptr)
		{
			Environment.me_dpages = dp->mp_p.p_next;
			NMemory::fg_FreeNoSize(dp);
		}

		Environment.mdb_env_close0(0);
		mp_pEnvironment.f_Clear();
	}


	/** @brief Set environment flags.
	 *
	 * This may be used to set some flags in addition to those from
	 * #f_Open(), or to unset these flags.  If several threads
	 * change the flags at the same time, the result is undefined.
	 * @param[in] flags The flags to change, bitwise OR'ed together
	 * @param[in] onoff A non-zero value sets the flags, zero clears them.
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_ChangeFlags(EDatabaseEnvironmentFlag _AddFlags, EDatabaseEnvironmentFlag _RemoveFlags)
	{
		fp_CheckOpened();

		if ((_RemoveFlags | _AddFlags) & ~EDatabaseEnvironmentFlag_Changable)
			DMibError("Trying to change non-changeable flag");

		auto &Environment = *mp_pEnvironment;
		Environment.me_flags &= ~_RemoveFlags;
		Environment.me_flags |= _AddFlags;
	}

	/** @brief Get environment flags.
	 *
	 * @param[out] flags The address of an integer to store the flags
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	EDatabaseEnvironmentFlag DMibDatabaseInfrequentSection CDatabaseEnvironment::f_GetFlags()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		return (EDatabaseEnvironmentFlag)Environment.me_flags & (EDatabaseEnvironmentFlag_Changable | EDatabaseEnvironmentFlag_Changeless);
	}

	/** @brief Return the path that was used in #f_Open().
	 *
	 * @param[out] path Address of a string pointer to contain the path. This
	 * is the actual string in the environment, not a copy. It should not be
	 * altered in any way.
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	NStr::CStr DMibDatabaseInfrequentSection CDatabaseEnvironment::f_GetPath()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		return Environment.me_path;
	}

	/** @brief Return the filedescriptor for the given environment.
	 *
	 * This function may be called after fork(), so the descriptor can be
	 * closed before exec*().  Other LMDB file descriptors have FD_CLOEXEC.
	 * (Until LMDB 0.9.18, only the lockfile had that.)
	 *
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void *DMibDatabaseInfrequentSection CDatabaseEnvironment::f_GetFD()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		return (void *)(mint)Environment.me_fd;
	}

	/** @brief Set the size of the memory map to use for this environment.
	 *
	 * The size should be a multiple of the OS page size. The default is
	 * 10485760 bytes. The size of the memory map is also the maximum size
	 * of the database. The value should be chosen as large as possible,
	 * to accommodate future growth of the database.
	 * This function should be called after #CDatabaseEnvironment::CDatabaseEnvironment() and before #f_Open().
	 * It may be called at later times if no transactions are active in
	 * this process. Note that the library does not check for this condition,
	 * the caller must ensure it explicitly.
	 *
	 * The new size takes effect immediately for the current process but
	 * will not be persisted to any others until a write transaction has been
	 * committed by the current process. Also, only mapsize increases are
	 * persisted into the environment.
	 *
	 * If the mapsize is increased by another process, and data has grown
	 * beyond the range of the current mapsize, #mdb_txn_begin() will
	 * return #MDB_MAP_RESIZED. This function may be called with a size
	 * of zero to adopt the new size.
	 *
	 * Any attempt to set a size smaller than the space already consumed
	 * by the environment will be silently changed to the current size of the used space.
	 * @param[in] size The size in bytes
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified, or the environment has
	 *   	an active write transaction.
	 * </ul>
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_SetMapSize(CDatabaseOffset size)
	{
		fp_CheckOpened();

		using namespace NPrivate;
		/* If env is already open, caller is responsible for making
		 * sure there are no active txns.
		 */

		auto &Environment = *mp_pEnvironment;

		if (Environment.me_map) {
			MDB_meta *meta;
	#ifndef DMibDatabase_Always64BitSupport
			void *old;
			int rc;
	#endif
			if (Environment.me_txn)
				DMibError("Cannot set map size when write transaction is open");
			meta = Environment.mdb_env_pick_meta();
			if (!size)
				size = meta->mm_mapsize;
			{
				/* Silently round up to minimum if the size is too small */
				CDatabaseOffset minsize = (meta->mm_last_pg + 1) * Environment.me_psize;
				if (size < minsize)
					size = minsize;
			}
	#ifndef DMibDatabase_Always64BitSupport
			/* For DMibDatabase_Always64BitSupport this bit is a noop since we dynamically remap
			 * chunks of the DB anyway.
			 */
			munmap(Environment.me_map, Environment.m_MapSize);
			Environment.m_MapSize = size;
			old = (Environment.me_flags & EDatabaseEnvironmentFlag_FixedMap) ? Environment.me_map : nullptr;
			rc = Environment.mdb_env_map(old);
			if (rc)
				DMibError(NStr::fg_Format("Failed to re-map environment: {}", mdb_strerror(rc)));
	#endif /* !DMibDatabase_Always64BitSupport */
		}
		Environment.m_MapSize = size;
		if (Environment.me_psize)
			Environment.me_maxpg = Environment.m_MapSize / Environment.me_psize;
	}

	/** @brief Set the maximum number of threads/reader slots for the environment.
	 *
	 * This defines the number of slots in the lock table that is used to track readers in the
	 * the environment. The default is 126.
	 * Starting a read-only transaction normally ties a lock table slot to the
	 * current thread until the environment closes or the thread exits. If
	 * EDatabaseEnvironmentFlag_NoTls is in use, #mdb_txn_begin() instead ties the slot to the
	 * MDB_txn object until it or the #MDB_env object is destroyed.
	 * This function may only be called after #CDatabaseEnvironment::CDatabaseEnvironment() and before #f_Open().
	 * @param[in] readers The maximum number of reader lock table slots
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified, or the environment is already open.
	 * </ul>
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_SetMaxReaders(uint32 _nReaders)
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		if (Environment.me_map)
			DMibError("Max number of readers can only be set before environment has been opened");
		if (_nReaders < 1)
			DMibError("A minimum of 1 reader is required");
		Environment.m_MaxReaders = _nReaders;
	}

	/** @brief Get the maximum number of threads/reader slots for the environment.
	 *
	 * @return The number of readers
	 */
	uint32 DMibDatabaseInfrequentSection CDatabaseEnvironment::f_MaxReaders()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		return Environment.m_MaxReaders;
	}

	/** @brief Set the maximum number of named databases for the environment.
	 *
	 * This function is only needed if multiple databases will be used in the
	 * environment. Simpler applications that use the environment as a single
	 * unnamed database can ignore this option.
	 * This function may only be called after #CDatabaseEnvironment::CDatabaseEnvironment() and before #f_Open().
	 *
	 * Currently a moderate number of slots are cheap but a huge number gets
	 * expensive: 7-120 words per transaction, and every #f_OpenDatabase()
	 * does a linear search of the opened slots.
	 * @param[in] dbs The maximum number of databases
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified, or the environment is already open.
	 * </ul>
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_SetMaxDatabases(CDatabaseSubID dbs)
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		if (Environment.me_map)
			DMibError("Max number of databases can only be set before environment has been opened");
		Environment.me_maxdbs = dbs + CORE_DBS;
	}

	/** @brief Get the maximum size of keys and #EDatabaseSubFlag_SortedDuplicates data we can write.
	 *
	 * Depends on the compile-time constant #MDB_MAXKEYSIZE. Default 511.
	 * See @ref CDatabaseValue.
	 * @return The maximum size of a key we can write
	 */
	mint DMibDatabaseInfrequentSection CDatabaseEnvironment::f_MaxKeySize()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		(void)Environment;
		return ENV_MAXKEY(&Environment);
	}

	/** @brief Dump the entries in the reader lock table.
	 *
	 * @param[in] _fEnumerate Function to call with information about readers
	 * @return < 0 on failure, >= 0 on success.
	 */
	NContainer::TCVector<CDatabaseReaderInfo> DMibDatabaseInfrequentSection CDatabaseEnvironment::f_GetReaderInfos()
	{
		fp_CheckOpened();

		using namespace NPrivate;
		auto &Environment = *mp_pEnvironment;

		if (!Environment.m_pLocksMemoryMap)
			return {};

		NContainer::TCVector<CDatabaseReaderInfo> ReaderInfos;
		mint nReaders = Environment.m_pLocksMemoryMap->mti_numreaders;
		MDB_reader *pReaders = Environment.m_pLocksMemoryMap->mti_readers;
		for (mint i = 0; i < nReaders; i++)
		{
			auto &Reader = pReaders[i];
			if (Reader.m_OwningProcessID)
			{
				CTransactionID TransactionID = Reader.m_BeginReadTransactionID;

				NStr::CStr Data;
				if (TransactionID == (CTransactionID)-1)
					Data = NStr::fg_Format("{sj10} {} -\n",(int)Reader.m_OwningProcessID, (void *)(mint)Reader.m_OwningThreadID);
				else
					Data = NStr::fg_Format("{sj10} {} {}\n",(int)Reader.m_OwningProcessID, (void *)(mint)Reader.m_OwningThreadID, TransactionID);
				auto &Info = ReaderInfos.f_Insert();
				Info.m_Description = fg_Move(Data);
				Info.m_iReaderSlot = i;
			}
		}
		return ReaderInfos;
	}

	/** @brief Check for stale entries in the reader lock table.
	 *
	 * @param[out] dead Number of stale slots that were cleared
	 * @return 0 on success, non-zero on failure.
	 */
	mint DMibDatabaseInfrequentSection CDatabaseEnvironment::f_CheckForStaleReaders()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;

		mint nDead = false;
		if (Environment.m_pLocksMemoryMap)
			Environment.mdb_reader_check0(false, &nDead);
		return nDead;
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------


	void CDatabaseEnvironment_Internal::mdb_env_sync0(bool _bForce, NPrivate::pgno_t numpgs)
	{
		int rc = 0;
		if (me_flags & EDatabaseEnvironmentFlag_ReadOnly)
			DMibError("Access denied. Read only databasae environment.");
		if (_bForce || !F_ISSET(me_flags, EDatabaseEnvironmentFlag_NoSync))
		{
			if (me_flags & EDatabaseEnvironmentFlag_WriteMap)
			{
				int flags = ((me_flags & EDatabaseEnvironmentFlag_WriteMapAsync) && !_bForce) ? MS_ASYNC : MS_SYNC;
				if (MDB_MSYNC(me_map, me_psize * numpgs, flags))
					rc = ErrCode();
	#ifdef DPlatformFamily_Windows
				else if (flags == MS_SYNC && MDB_FDATASYNC(me_fd))
					rc = ErrCode();
	#endif
			}
			else
			{
				if (MDB_FDATASYNC(me_fd))
					rc = ErrCode();
			}
		}
		if (rc != MDB_SUCCESS)
			DMibError(NStr::fg_Format("Error syncing database: {}", mdb_strerror(rc)));
	}

	/** Read the environment parameters of a DB environment before
	 * mapping it into memory.
	 * @param[in] prev whether to read the backup meta page
	 * @param[out] meta address of where to store the meta information
	 * @return 0 on success, non-zero on failure.
	 */
	bool DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_read_header(int prev, NPrivate::MDB_meta *meta)
	{
		using namespace NPrivate;
		MDB_metabuf	pbuf;
		MDB_page	*p;
		MDB_meta	*m;
		int			i, off;
		enum { Size = sizeof(pbuf) };

		/* We don't know the page size yet, so use a minimum value.
		 * Read both meta pages so we can use the latest one.
		 */

		for (i=off=0; i<NUM_METAS; i++, off += meta->mm_dbs[FREE_DBI].md_pad) {
	#ifdef DPlatformFamily_Windows
			DWORD len;
			OVERLAPPED ov;
			memset(&ov, 0, sizeof(ov));
			ov.Offset = off;
			int rc = ReadFile(me_fd, &pbuf, Size, &len, &ov) ? (int)len : -1;
			if (rc == -1 && ErrCode() == ERROR_HANDLE_EOF)
				rc = 0;
	#else
			int rc = pread(me_fd, &pbuf, Size, off);
	#endif
			if (rc != Size)
			{
				if (rc == 0 && off == 0)
					return true;
				rc = rc < 0 ? (int) ErrCode() : MDB_INVALID;
				if (rc == ENOENT)
					return true;
				DMibError(NStr::fg_Format("Error reading environment header: {}", mdb_strerror(rc)));
			}

			p = (MDB_page *)&pbuf;

			if (!F_ISSET(p->mp_flags, P_META))
				DMibError(NStr::fg_Format("Header page '{}' is not a meta page", p->mp_p.p_pgno));

			m = (MDB_meta *)METADATA(p);
			if (m->mm_magic != MDB_MAGIC)
				DMibError(NStr::fg_Format("Header page '{}' has invalid magic: 0x{nfh,sf0,sj4}", p->mp_p.p_pgno, m->mm_magic));

			if (m->mm_version != MDB_DATA_VERSION)
				DMibError(NStr::fg_Format("Header page '{}' has invalid data version: 0x{nfh,sf0,sj8} expected 0x{nfh,sf0,sj8}", p->mp_p.p_pgno, m->mm_version, uint32(MDB_DATA_VERSION)));

			if (off == 0 || (prev ? m->mm_txnid < meta->mm_txnid : m->mm_txnid > meta->mm_txnid))
				*meta = *m;
		}
		return false;
	}

	/** Fill in most of the zeroed #MDB_meta for an empty database environment */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_init_meta0(MDB_meta *meta)
	{
		meta->mm_magic = MDB_MAGIC;
		meta->mm_version = MDB_DATA_VERSION;
		meta->mm_mapsize = m_MapSize;
		meta->mm_dbs[FREE_DBI].md_pad = me_psize;
		meta->mm_last_pg = NUM_METAS-1;
		meta->mm_dbs[FREE_DBI].md_flags = me_flags & 0xffff;
		meta->mm_dbs[FREE_DBI].md_flags |= EDatabaseSubFlag_IntegerKeys; /* this is mm_dbs[FREE_DBI].md_flags */
		meta->mm_dbs[FREE_DBI].md_root = P_INVALID;
		meta->mm_dbs[MAIN_DBI].md_root = P_INVALID;
	}

	/** Write the environment parameters of a freshly created DB environment.
	 * @param[in] meta the #MDB_meta to write
	 * @return 0 on success, non-zero on failure.
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_init_meta(MDB_meta *meta)
	{
		MDB_page *p, *q;
		uint32	 psize;
	#ifdef DPlatformFamily_Windows
		DWORD len;
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		auto fDoWrite = [&](auto &rc, auto fd, auto ptr, auto size, auto pos)
			{
				ov.Offset = pos;
				rc = WriteFile(fd, ptr, size, &len, &ov);
			}
		;
	#else
		int len;
		auto fDoWrite = [&](auto &rc, auto fd, auto ptr, auto size, auto pos)
			{
				do
				{
					len = pwrite(fd, ptr, size, pos);
					if (len == -1 && ErrCode() == EINTR)
						continue;
					rc = (len >= 0);
					break;
				} while(1);
			}
		;
	#endif

		DPUTS("writing new meta page");

		psize = me_psize;

		p = (MDB_page *)NMemory::fg_AllocInitZero(NUM_METAS * psize);
		auto Cleanup = g_OnScopeExit > [&]()
			{
				NMemory::fg_FreeNoSize(p);
			}
		;
		p->mp_p.p_pgno = 0;
		p->mp_flags = P_META;
		*(MDB_meta *)METADATA(p) = *meta;

		q = (MDB_page *)((char *)p + psize);
		q->mp_p.p_pgno = 1;
		q->mp_flags = P_META;
		*(MDB_meta *)METADATA(q) = *meta;

		{
			int rc = 0;
 			fDoWrite(rc, me_fd, p, psize * NUM_METAS, 0);
			if (!rc)
				rc = ErrCode();
			else if ((unsigned) len != psize * NUM_METAS)
				rc = ENOSPC;
			else
				rc = 0;

			if (rc)
				DMibError(NStr::fg_Format("Error writing meta page: {}", mdb_strerror(rc)));
		}
	}

	/** Update the environment info to commit a transaction.
	 * @param[in] txn the transaction that's being committed
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_env_write_meta(MDB_txn *txn)
	{
		MDB_env *env;
		MDB_meta	meta, metab, *mp;
		unsigned flags;
		CDatabaseOffset mapsize;
		off_t off;
		int rc, len, toggle;
		char *ptr;
		HANDLE mfd;
	#ifdef DPlatformFamily_Windows
		OVERLAPPED ov;
	#else
		int r2;
	#endif

		toggle = txn->mt_txnid & 1;
		DMibDatabaseTrace("writing meta page {} for root page {}", toggle, txn->mt_dbs[MAIN_DBI].md_root);

		env = txn->mt_env;
		flags = txn->mt_flags | env->me_flags;
		mp = env->me_metas[toggle];
		mapsize = env->me_metas[toggle ^ 1]->mm_mapsize;
		/* Persist any increases of mapsize config */
		if (mapsize < env->m_MapSize)
			mapsize = env->m_MapSize;

		if (flags & EDatabaseEnvironmentFlag_WriteMap) {
			mp->mm_mapsize = mapsize;
			mp->mm_dbs[FREE_DBI] = txn->mt_dbs[FREE_DBI];
			mp->mm_dbs[MAIN_DBI] = txn->mt_dbs[MAIN_DBI];
			mp->mm_last_pg = txn->mt_next_pgno - 1;
	#if (__GNUC__ * 100 + __GNUC_MINOR__ >= 404) && /* TODO: portability */	\
		!(defined(__i386__) || defined(__x86_64__))
			/* LY: issue a memory barrier, if not x86. ITS#7969 */
			__sync_synchronize();
	#endif
			mp->mm_txnid = txn->mt_txnid;
			if (!(flags & (EDatabaseEnvironmentFlag_NoMetaSync|EDatabaseEnvironmentFlag_NoSync))) {
				unsigned meta_size = env->me_psize;
				rc = (env->me_flags & EDatabaseEnvironmentFlag_WriteMapAsync) ? MS_ASYNC : MS_SYNC;
				ptr = (char *)mp - PAGEHDRSZ;
	#ifndef DPlatformFamily_Windows	/* POSIX msync() requires ptr = start of OS page */
				r2 = (ptr - env->me_map) & (env->me_os_psize - 1);
				ptr -= r2;
				meta_size += r2;
	#endif
				if (MDB_MSYNC(ptr, meta_size, rc)) {
					rc = ErrCode();
					goto fail;
				}
			}
			goto done;
		}
		metab.mm_txnid = mp->mm_txnid;
		metab.mm_last_pg = mp->mm_last_pg;

		meta.mm_mapsize = mapsize;
		meta.mm_dbs[FREE_DBI] = txn->mt_dbs[FREE_DBI];
		meta.mm_dbs[MAIN_DBI] = txn->mt_dbs[MAIN_DBI];
		meta.mm_last_pg = txn->mt_next_pgno - 1;
		meta.mm_txnid = txn->mt_txnid;

		off = offsetof(MDB_meta, mm_mapsize);
		ptr = (char *)&meta + off;
		len = sizeof(MDB_meta) - off;
		off += (char *)mp - env->me_map;

		/* Write to the SYNC fd unless MDB_NOSYNC/MDB_NOMETASYNC.
		 * (me_mfd goes to the same file as me_fd, but writing to it
		 * also syncs to disk.  Avoids a separate fdatasync() call.)
		 */
		mfd = (flags & (EDatabaseEnvironmentFlag_NoSync|EDatabaseEnvironmentFlag_NoMetaSync)) ? env->me_fd : env->me_mfd;
	#ifdef DPlatformFamily_Windows
		{
			memset(&ov, 0, sizeof(ov));
			ov.Offset = off;
			if (!WriteFile(mfd, ptr, len, (DWORD *)&rc, &ov))
				rc = -1;
		}
	#else
	retry_write:
		rc = pwrite(mfd, ptr, len, off);
	#endif
		if (rc != len) {
			rc = rc < 0 ? ErrCode() : EIO;
	#ifndef DPlatformFamily_Windows
			if (rc == EINTR)
				goto retry_write;
	#endif
			DPUTS("write failed, disk error?");
			/* On a failure, the pagecache still contains the new data.
			 * Write some old data back, to prevent it from being used.
			 * Use the non-SYNC fd; we know it will fail anyway.
			 */
			meta.mm_last_pg = metab.mm_last_pg;
			meta.mm_txnid = metab.mm_txnid;
	#ifdef DPlatformFamily_Windows
			memset(&ov, 0, sizeof(ov));
			ov.Offset = off;
			WriteFile(env->me_fd, ptr, len, nullptr, &ov);
	#else
			r2 = pwrite(env->me_fd, ptr, len, off);
			(void)r2;	/* Silence warnings. We don't care about pwrite's return value */
	#endif
	fail:
			env->me_flags |= MDB_FATAL_ERROR;
			return rc;
		}
		/* MIPS has cache coherency issues, this is a no-op everywhere else */
		CACHEFLUSH(env->me_map + off, len, DCACHE);
	done:
		/* Memory ordering issues are irrelevant; since the entire writer
		 * is wrapped by wmutex, all of these changes will become visible
		 * after the wmutex is unlocked. Since the DB is multi-version,
		 * readers will get consistent data regardless of how fresh or
		 * how stale their view of these values is.
		 */
		if (env->m_pLocksMemoryMap)
			env->m_pLocksMemoryMap->mti_txnid = txn->mt_txnid;

		return MDB_SUCCESS;
	}

	/** Check both meta pages to see which one is newer.
	 * @return newest #MDB_meta.
	 */
	NPrivate::MDB_meta *CDatabaseEnvironment_Internal::mdb_env_pick_meta() const
	{
		MDB_meta *const *metas = me_metas;
		return metas[ (metas[0]->mm_txnid < metas[1]->mm_txnid) ^
			((me_flags & EDatabaseEnvironmentFlag_PreviousSnapshot) != 0) ]
		;
	}

	#ifdef DPlatformFamily_Windows
	/** @brief Map a result from an NTAPI call to WIN32. */
	DWORD NPrivate::mdb_nt2win32(NTSTATUS st)
	{
		OVERLAPPED o = {0};
		DWORD br;
		o.Internal = st;
		GetOverlappedResult(nullptr, &o, &br, FALSE);
		return GetLastError();
	}
	#endif

	bool DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_IsInsideMemoryMap(uint8 const *_pData)
	{
	#ifdef DMibDatabase_Always64BitSupport
		return false; // TODO
	#else
		return _pData >= (uint8 const *)me_map && _pData < ((uint8 const *)me_map + this->m_MapSize);
	#endif
	}

	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_map(void *addr)
	{
		MDB_page *p;
		uint32 flags = me_flags;
	#ifdef DPlatformFamily_Windows
		int rc;
		int access = SECTION_MAP_READ;
		HANDLE mh;
		void *map;
		SIZE_T msize;
		ULONG pageprot = PAGE_READONLY, secprot, alloctype;

		if (flags & EDatabaseEnvironmentFlag_WriteMap) {
			access |= SECTION_MAP_WRITE;
			pageprot = PAGE_READWRITE;
		}
		if (flags & EDatabaseEnvironmentFlag_ReadOnly) {
			secprot = PAGE_READONLY;
			msize = 0;
			alloctype = 0;
		} else {
			secprot = PAGE_READWRITE;
			msize = m_MapSize;
			alloctype = MEM_RESERVE;
		}

		rc = g_fDatabase_NtCreateSection(&mh, access, nullptr, nullptr, secprot, SEC_RESERVE, me_fd);
		if (rc)
			return mdb_nt2win32(rc);
		map = addr;
	#ifdef DMibDatabase_Always64BitSupport
		msize = NUM_METAS * me_psize;
	#endif
		rc = g_fDatabase_NtMapViewOfSection(mh, GetCurrentProcess(), &map, 0, 0, nullptr, &msize, ViewUnmap, alloctype, pageprot);
	#ifdef DMibDatabase_Always64BitSupport
		me_fmh = mh;
	#else
		g_fDatabase_NtClose(mh);
	#endif
		if (rc)
			return mdb_nt2win32(rc);
		me_map = (char *)map;
	#else
	#ifdef DMibDatabase_Always64BitSupport
		(void) flags;
		me_map = (char *)mmap(addr, NUM_METAS * me_psize, PROT_READ, MAP_SHARED,
			me_fd, 0);
		if (me_map == MAP_FAILED) {
			me_map = nullptr;
			return ErrCode();
		}
	#else
		int prot = PROT_READ;
		if (flags & EDatabaseEnvironmentFlag_WriteMap) {
			prot |= PROT_WRITE;
			if (ftruncate(me_fd, m_MapSize) < 0)
				return ErrCode();
		}
		me_map = (char *)mmap(addr, m_MapSize, prot, MAP_SHARED, me_fd, 0);
		if (me_map == MAP_FAILED) {
			me_map = nullptr;
			return ErrCode();
		}

		if (flags & EDatabaseEnvironmentFlag_NoReadAhead) {
			/* Turn off readahead. It's harmful when the DB is larger than RAM. */
	#ifdef MADV_RANDOM
			madvise(me_map, m_MapSize, MADV_RANDOM);
	#else
	#ifdef POSIX_MADV_RANDOM
			posix_madvise(me_map, m_MapSize, POSIX_MADV_RANDOM);
	#endif /* POSIX_MADV_RANDOM */
	#endif /* MADV_RANDOM */
		}
	#endif /* DPlatformFamily_Windows */

		/* Can happen because the address argument to mmap() is just a
		 * hint.  mmap() can pick another, e.g. if the range is in use.
		 * The MAP_FIXED flag would prevent that, but then mmap could
		 * instead unmap existing pages to make room for the new map.
		 */
		if (addr && me_map != addr)
			return EBUSY;	/* TODO: Make a new MDB_* error code? */
	#endif

		p = (MDB_page *)me_map;
		me_metas[0] = (MDB_meta *)METADATA(p);
		me_metas[1] = (MDB_meta *)((char *)me_metas[0] + me_psize);

		return MDB_SUCCESS;
	}

	NStr::CStr CDatabaseEnvironment_Internal::mdb_getfilename(NStr::CStr const &_FileName, NPrivate::mdb_fopen_type which)
	{
		/** Filename suffixes [datafile,lockfile][without,with MDB_NOSUBDIR] */
		static constexpr ch8 const *pSuffixes[2][2] =
			{
				{"/MalterlibDB.mibdb", ""}
				, {"/MalterlibDB.lock", ".lock"}
			}
		;

		return _FileName + pSuffixes[which==MDB_O_LOCKS][(me_flags & EDatabaseEnvironmentFlag_NoSubdir) != 0];
	}

	/** Open an LMDB file.
	 * @param[in] env	The LMDB environment.
	 * @param[in,out] _FileName	Path
	 * @param[in] which	Determines file type, access mode, etc.
	 * @param[in] mode	The Unix permissions for the file, if we create it.
	 * @param[out] res	Resulting file handle.
	 * @return 0 on success, non-zero on failure.
	 */
	int DMibDatabaseInfrequentSection
	CDatabaseEnvironment_Internal::mdb_fopen(NStr::CStr const &_FileName,
		NPrivate::mdb_fopen_type which, NPrivate::mdb_mode_t mode,
		HANDLE *res)
	{
		int rc = MDB_SUCCESS;
		HANDLE fd;
	#ifdef DPlatformFamily_Windows
		DWORD acc, share, disp, attrs;
	#else
		int flags;
	#endif

		NStr::CStr FileName = mdb_getfilename(_FileName, which);

		/* The directory must already exist.  Usually the file need not.
		 * MDB_O_META requires the file because we already created it using
		 * MDB_O_RDWR.  MDB_O_COPY must not overwrite an existing file.
		 *
		 * With MDB_O_COPY we do not want the OS to cache the writes, since
		 * the source data is already in the OS cache.
		 *
		 * The lockfile needs FD_CLOEXEC (close file descriptor on exec*())
		 * to avoid the flock() issues noted under Caveats in lmdb.h.
		 * Also set it for other filehandles which the user cannot get at
		 * and close himself, which he may need after fork().  I.e. all but
		 * me_fd, which programs do use via mdb_env_get_fd().
		 */

	#ifdef DPlatformFamily_Windows
		acc = GENERIC_READ|GENERIC_WRITE;
		share = FILE_SHARE_READ|FILE_SHARE_WRITE;
		disp = OPEN_ALWAYS;
		attrs = FILE_ATTRIBUTE_NORMAL;
		switch (which) {
		case MDB_O_RDONLY:			/* read-only datafile */
			acc = GENERIC_READ;
			disp = OPEN_EXISTING;
			break;
		case MDB_O_META:			/* for writing metapages */
			acc = GENERIC_WRITE;
			disp = OPEN_EXISTING;
			attrs = FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH;
			break;
		case MDB_O_COPY:			/* mdb_env_copy() & co */
			acc = GENERIC_WRITE;
			share = 0;
			disp = CREATE_NEW;
			attrs = FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH;
			break;
		default: break;	/* silence gcc -Wswitch (not all enum values handled) */
		}
		fd = CreateFileW(NStr::CWStr(FileName).f_GetStr(), acc, share, NULL, disp, attrs, NULL);
	#else
		fd = open(FileName.f_GetStr(), which & MDB_O_MASK, mode);
	#endif

		if (fd == INVALID_HANDLE_VALUE)
			rc = ErrCode();
	#ifndef DPlatformFamily_Windows
		else {
			if (which != MDB_O_RDONLY && which != MDB_O_RDWR) {
				/* Set CLOEXEC if we could not pass it to open() */
				if (!MDB_CLOEXEC && (flags = fcntl(fd, F_GETFD)) != -1)
					(void) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
			}
			if (which == MDB_O_COPY && me_psize >= me_os_psize) {
				/* This may require buffer alignment.  There is no portable
				 * way to ask how much, so we require OS pagesize alignment.
				 */
	# ifdef F_NOCACHE	/* __APPLE__ */
				(void) fcntl(fd, F_NOCACHE, 1);
	# elif defined O_DIRECT
				/* open(...O_DIRECT...) would break on filesystems without
				 * O_DIRECT support (ITS#7682). Try to set it here instead.
				 */
				if ((flags = fcntl(fd, F_GETFL)) != -1)
					(void) fcntl(fd, F_SETFL, flags | O_DIRECT);
	# endif
			}
		}
	#endif	/* !DPlatformFamily_Windows */

		*res = fd;
		return rc;
	}

	#ifdef DPlatformFamily_Windows		
	NtCreateSectionFunc *NPrivate::g_fDatabase_NtCreateSection = nullptr;
	NtMapViewOfSectionFunc *NPrivate::g_fDatabase_NtMapViewOfSection = nullptr;
	NtCloseFunc *NPrivate::g_fDatabase_NtClose = nullptr;
	#endif

	/** Further setup required for opening an LMDB environment
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_open2(int prev, uint32 _PageSize)
	{
		uint32 flags = me_flags;
		int newenv = 0;
		MDB_meta meta;

	#ifdef DPlatformFamily_Windows		
		if (NLocal::g_VersionInfo.dwMajorVersion >= 6)
			me_pidquery = MDB_PROCESS_QUERY_LIMITED_INFORMATION;
		else
			me_pidquery = PROCESS_QUERY_INFORMATION;
		/* Grab functions we need from NTDLL */
		if (!g_fDatabase_NtCreateSection) {
			HMODULE h = GetModuleHandleW(L"NTDLL.DLL");
			if (!h)
				DMibError("Could not find NTDLL.DLL");
			g_fDatabase_NtClose = (NtCloseFunc *)GetProcAddress(h, "NtClose");
			if (!g_fDatabase_NtClose)
				DMibError("Could not find NtClose");
			g_fDatabase_NtMapViewOfSection = (NtMapViewOfSectionFunc *)GetProcAddress(h, "NtMapViewOfSection");
			if (!g_fDatabase_NtMapViewOfSection)
				DMibError("Could not find NtMapViewOfSection");
			g_fDatabase_NtCreateSection = (NtCreateSectionFunc *)GetProcAddress(h, "NtCreateSection");
			if (!g_fDatabase_NtCreateSection)
				DMibError("Could not find NtCreateSection");
		}
	#endif /* DPlatformFamily_Windows */

		if (fg_BrokenFSync(this, me_fd))
			me_flags |= MDB_FSYNCONLY;

		if (mdb_env_read_header(prev, &meta))
		{
			DPUTS("new mdbenv");
			newenv = 1;
			if (_PageSize)
				me_psize = _PageSize;
			else
				me_psize = me_os_psize;

			if (me_psize > MAX_PAGESIZE)
				me_psize = MAX_PAGESIZE;
			memset(&meta, 0, sizeof(meta));
			mdb_env_init_meta0(&meta);
			meta.mm_mapsize = DEFAULT_MAPSIZE;
		}
		else
			me_psize = meta.mm_dbs[FREE_DBI].md_pad;

		/* Was a mapsize configured? */
		if (!m_MapSize)
			m_MapSize = meta.mm_mapsize;
		{
			/* Make sure mapsize >= committed data size.  Even when using
			 * mm_mapsize, which could be broken in old files (ITS#7789).
			 */
			CDatabaseOffset minsize = (meta.mm_last_pg + 1) * meta.mm_dbs[FREE_DBI].md_pad;
			if (m_MapSize < minsize)
				m_MapSize = minsize;
		}
		meta.mm_mapsize = m_MapSize;

		if (newenv && !(flags & EDatabaseEnvironmentFlag_FixedMap))
		{
			/* mdb_env_map() may grow the datafile.  Write the metapages
			 * first, so the file will be valid if initialization fails.
			 * Except with FIXEDMAP, since we do not yet know mm_address.
			 * We could fill in mm_address later, but then a different
			 * program might end up doing that - one with a memory layout
			 * and map address which does not suit the main program.
			 */
			mdb_env_init_meta(&meta);
			newenv = 0;
		}
	#ifdef DPlatformFamily_Windows
		/* For FIXEDMAP, make sure the file is non-empty before we attempt to map it */
		if (newenv)
		{
			char dummy = 0;
			DWORD len;
			int rc = WriteFile(me_fd, &dummy, 1, &len, nullptr);
			if (!rc)
			{
				rc = ErrCode();
				DMibError(NStr::fg_Format("Error writing file (dummy data): {}", mdb_strerror(rc)));
			}
		}
	#endif

		{
			int rc = mdb_env_map((flags & EDatabaseEnvironmentFlag_FixedMap) ? meta.mm_address : nullptr);
			if (rc)
				DMibError(NStr::fg_Format("Failed to map environment: {}", mdb_strerror(rc)));
		}

		if (newenv)
		{
			if (flags & EDatabaseEnvironmentFlag_FixedMap)
				meta.mm_address = me_map;
			mdb_env_init_meta(&meta);
		}

		me_maxfree_1pg = (me_psize - PAGEHDRSZ) / sizeof(pgno_t) - 1;
		me_nodemax = (((me_psize - PAGEHDRSZ) / MDB_MINKEYS) & -2)
			- sizeof(indx_t);
	#if !(MDB_MAXKEYSIZE)
		me_maxkey = me_nodemax - (NODESIZE + sizeof(MDB_db));
	#endif
		me_maxpg = m_MapSize / me_psize;

		if (m_pLocksMemoryMap)
			m_pLocksMemoryMap->mti_txnid = meta.mm_txnid;

	#if DMibDatabaseDebug
		{
			MDB_meta *meta = mdb_env_pick_meta();
			MDB_db *db = &meta->mm_dbs[MAIN_DBI];

			DMibDatabaseTrace("opened database version {}, pagesize {}", meta->mm_version, me_psize);
			DMibDatabaseTrace("using meta page {}",    (int) (meta->mm_txnid & 1));
			DMibDatabaseTrace("depth: {}",             db->md_depth);
			DMibDatabaseTrace("entries: {}",        db->md_entries);
			DMibDatabaseTrace("branch pages: {}",   db->md_branch_pages);
			DMibDatabaseTrace("leaf pages: {}",     db->md_leaf_pages);
			DMibDatabaseTrace("overflow pages: {}", db->md_overflow_pages);
			DMibDatabaseTrace("root: {}",           db->md_root);
		}
	#endif
	}

	/** Destroy resources from f_Open(), clear our readers & DBIs */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_close0(int excl)
	{
		int i;

		if (!(me_flags & MDB_ENV_ACTIVE))
			return;

		/* Doing this here since me_dbxs may not exist during f_Close */
		if (!m_StaticDasabaseInfo.f_IsEmpty())
		{
			for (i = me_maxdbs; --i >= CORE_DBS; )
				NMemory::fg_FreeNoSize((ch8 *)m_StaticDasabaseInfo[i].m_DatabaseName.m_pData);
			m_StaticDasabaseInfo.f_Clear();
		}

		NMemory::fg_FreeNoSize(me_pbuf);
		NMemory::fg_FreeNoSize(me_dbiseqs);
		NMemory::fg_FreeNoSize(me_dbflags);
		me_path.f_Clear();
		NMemory::fg_FreeNoSize(me_dirty_list);
	#ifdef DMibDatabase_Always64BitSupport
		if (me_txn0 && me_txn0->mt_rpages)
			NMemory::fg_FreeNoSize(me_txn0->mt_rpages);
		if (me_rpages) {
			MDB_ID3L el = me_rpages;
			unsigned int x;
			for (x=1; x<=el[0].mid; x++)
				munmap(el[x].mptr, el[x].mcnt * me_psize);
			NMemory::fg_FreeNoSize(el);
		}
	#endif
		NMemory::fg_FreeNoSize(me_txn0);
		mdb_midl_free(me_free_pgs);

		if (me_flags & MDB_ENV_TXKEY) {
			pthread_key_delete(me_txkey);
	#ifdef DPlatformFamily_Windows
			/* Delete our key from the global list */
			for (i=0; i<mdb_tls_nkeys; i++)
				if (mdb_tls_keys[i] == me_txkey) {
					mdb_tls_keys[i] = mdb_tls_keys[mdb_tls_nkeys-1];
					mdb_tls_nkeys--;
					break;
				}
	#endif
		}

		if (me_map) {
	#ifdef DMibDatabase_Always64BitSupport
			munmap(me_map, NUM_METAS*me_psize);
	#else
			munmap(me_map, m_MapSize);
	#endif
		}
		if (me_mfd != INVALID_HANDLE_VALUE)
			(void) close(me_mfd);
		if (me_fd != INVALID_HANDLE_VALUE)
			(void) close(me_fd);
		if (m_pLocksMemoryMap) {
			CProcessID pid = getpid();
			/* Clearing readers is done in this function because
			 * me_txkey with its destructor must be disabled first.
			 *
			 * We skip the the reader mutex, so we touch only
			 * data owned by this process (me_close_readers and
			 * our readers), and clear each reader atomically.
			 */
			for (i = me_close_readers; --i >= 0; )
				if (m_pLocksMemoryMap->mti_readers[i].m_OwningProcessID == pid)
					m_pLocksMemoryMap->mti_readers[i].m_OwningProcessID = 0;
	#ifdef DPlatformFamily_Windows
			if (me_rmutex) {
				CloseHandle(me_rmutex);
				if (me_wmutex) CloseHandle(me_wmutex);
			}
			/* Windows automatically destroys the mutexes when
			 * the last handle closes.
			 */
	#elif defined(MDB_USE_POSIX_SEM)
			if (me_rmutex != SEM_FAILED) {
				sem_close(me_rmutex);
				if (me_wmutex != SEM_FAILED)
					sem_close(me_wmutex);
				/* If we have the filelock:  If we are the
				 * only remaining user, clean up semaphores.
				 */
				if (excl == 0)
					mdb_env_excl_lock(&excl);
				if (excl > 0) {
					sem_unlink(MUTEXNAME(this, 'r'));
					sem_unlink(MUTEXNAME(this, 'w'));
				}
			}
	#elif defined(MDB_USE_SYSV_SEM)
			if (me_rmutex->semid != -1) {
				/* If we have the filelock:  If we are the
				 * only remaining user, clean up semaphores.
				 */
				if (excl == 0)
					mdb_env_excl_lock(&excl);
				if (excl > 0)
					semctl(me_rmutex->semid, 0, IPC_RMID);
			}
	#endif
			munmap((void *)m_pLocksMemoryMap, (m_MaxReaders-1)*sizeof(MDB_reader)+sizeof(CLocksMemoryMap));
		}
		if (me_lfd != INVALID_HANDLE_VALUE) {
	#ifdef DPlatformFamily_Windows
			if (excl >= 0) {
				/* Unlock the lockfile.  Windows would have unlocked it
				 * after closing anyway, but not necessarily at once.
				 */
				UnlockFile(me_lfd, 0, 0, 1, 0);
			}
	#endif
			(void) close(me_lfd);
		}
	#ifdef DMibDatabase_Always64BitSupport
	#ifdef DPlatformFamily_Windows
		if (me_fmh) CloseHandle(me_fmh);
		if (me_rpmutex) CloseHandle(me_rpmutex);
	#else
		pthread_mutex_destroy(&me_rpmutex);
	#endif
	#endif

		me_flags &= ~(MDB_ENV_ACTIVE|MDB_ENV_TXKEY);
	}

	/** Copy environment as-is. */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_Copy(HANDLE fd)
	{
	#ifdef DMibDatabase_Always64BitSupport
		DMibError("Not supported on 32 bit");
	#endif

		MDB_txn *txn = nullptr;
		mdb_mutexref_t wmutex = nullptr;
		CDatabaseOffset wsize, w3;
		char *ptr;

	#ifdef DPlatformFamily_Windows
		DWORD len, w2;
	#define DO_WRITE(rc, fd, ptr, w2, len)	rc = WriteFile(fd, ptr, w2, &len, nullptr)
	#else
		smint len;
		mint w2;
	#define DO_WRITE(rc, fd, ptr, w2, len)	len = write(fd, ptr, w2); rc = (len >= 0)
	#endif

		/* Do the lock/unlock of the reader mutex before starting the
		 * write txn.  Otherwise other read txns could block writers.
		 */
		auto Transaction = mdb_txn_begin(nullptr, EDatabaseEnvironmentFlag_ReadOnly);
		txn = Transaction.fp_GetInternal();

		{
			auto UnlockMutex = g_OnScopeExit > [&]
				{
					if (wmutex)
						UNLOCK_MUTEX(wmutex);
				}
			;

			if (m_pLocksMemoryMap) {
				/* We must start the actual read txn after blocking writers */
				mdb_txn_end(txn, MDB_END_RESET_TMP);

				/* Temporarily block writers until we snapshot the meta pages */
				{
					int rc;
					if (LOCK_MUTEX(rc, this, me_wmutex))
						DMibError(NStr::fg_Format("Write mutex lock failed: {}", mdb_strerror(rc)));
					wmutex = me_wmutex;
				}

				mdb_txn_renew0(txn);
			}

			wsize = me_psize * NUM_METAS;
			ptr = me_map;
			w2 = wsize;
			while (w2 > 0) {
				{
					int rc;
					DO_WRITE(rc, fd, ptr, w2, len);
					if (!rc)
					{
						rc = ErrCode();
						DMibError(NStr::fg_Format("Error writing (copy): {}", mdb_strerror(rc)));
						break;
					}
					else if (len > 0)
					{
						ptr += len;
						w2 -= len;
						continue;
					}
					else
						DMibError("Non-blocking or async handles are not supported");
				}
			}
		}

		w3 = txn->mt_next_pgno * me_psize;
		{
			CDatabaseOffset fsize = mdb_fsize(me_fd);
			if (w3 > fsize)
				w3 = fsize;
		}
		wsize = w3 - wsize;
		while (wsize > 0)
		{
			if (wsize > MAX_WRITE)
				w2 = MAX_WRITE;
			else
				w2 = wsize;
			{
				int rc;
				DO_WRITE(rc, fd, ptr, w2, len);
				if (!rc)
				{
					rc = ErrCode();
					DMibError(NStr::fg_Format("Error writing (copy): {}", mdb_strerror(rc)));
				}
				else if (len > 0)
				{
					ptr += len;
					wsize -= len;
					continue;
				}
				else
					DMibError("Non-blocking or async handles are not supported");
			}
		}
	}
}
