// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
SPDX-License-Identifier: OLDAP-2.8
SPDX-License-Identifier: ISC
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
#	include <Mib/Core/PlatformSpecific/WindowsOptional>
#endif

uint32 fg_MalterlibAttributesToMode(NMib::NFile::EFileAttrib _Attributes, uint32 _Mode = 0644);
#ifndef DPlatformFamily_Windows
int fg_GetUnixOpenFlags();
void fg_SetUnixHandleOptions(int _File);
#endif

namespace NMib::NDatabase
{
	using namespace NPrivate;

	/** @brief Create an LMDB environment handle.
	 *
	 * This function allocates memory for a #CMdbEnv structure. To release
	 * the allocated memory and discard the handle, call #f_Close().
	 * Before the handle may be used, it must be opened using #f_Open().
	 * Various other options may also need to be set before opening the handle,
	 * e.g. #f_SetMapSize(), #f_SetMaxReaders(), #f_SetMaxDatabases(),
	 * depending on usage requirements.
	 */
	DMibDatabaseInfrequentSection CDatabaseEnvironment::CDatabaseEnvironment()
	{
		NStorage::TCUniquePointer<CDatabaseEnvironment_Internal> pEnvironment = fg_Construct();

		pEnvironment->m_MaxReaders = DDefaultReaders;
		pEnvironment->m_MaxDatabases = DCoreDbs;
		pEnvironment->m_nDatabases = DCoreDbs;
		pEnvironment->m_FileHandle = INVALID_HANDLE_VALUE;
		pEnvironment->m_LockFileHandle = INVALID_HANDLE_VALUE;
		pEnvironment->m_MetaFileHandle = INVALID_HANDLE_VALUE;
#ifdef MDB_USE_POSIX_SEM
		pEnvironment->m_ReaderMutex = SEM_FAILED;
		pEnvironment->m_WriterMutex = SEM_FAILED;
#elif defined DMdbUseSysvSem
		pEnvironment->m_ReaderMutex->m_SemaphoreID = -1;
		pEnvironment->m_WriterMutex->m_SemaphoreID = -1;
#endif
		pEnvironment->m_ProcessID = getpid();
		pEnvironment->m_OSPageSize = NSys::fg_Mem_PageSize();

		mp_pEnvironment = fg_Move(pEnvironment);
	}

	CDatabaseEnvironment::CDatabaseEnvironment(CDatabaseEnvironment &&_Other) = default;
	CDatabaseEnvironment &CDatabaseEnvironment::operator = (CDatabaseEnvironment &&_Other) = default;

	/** @brief Open an environment handle.
	 *
	 * If this function fails, #f_Close() must be called to discard the #CMdbEnv handle.
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
	 *		#CMdbTxn objects instead of to threads. I.e. #f_Reset() keeps
	 *		the slot reseved for the #CMdbTxn object. A thread may use parallel
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
	 *	<li>#DMdbVersionMismatch - the version of the LMDB library doesn't match the
	 *	version that created the database environment.
	 *	<li>#DMdbInvalid - the environment file headers are corrupted.
	 *	<li>ENOENT - the directory specified by the path parameter doesn't exist.
	 *	<li>EACCES - the user didn't have permission to access the environment files.
	 *	<li>EAGAIN - the environment was locked by another process.
	 * </ul>
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Open(NStr::CStr const &_Path, EDatabaseEnvironmentFlag _Flags, NFile::EFileAttrib _FileAttributes, uint32 _PageSize)
	{
		uint32 Flags = _Flags;
		uint32 Mode = 0;
#ifndef DPlatformFamily_Windows
		Mode = fg_MalterlibAttributesToMode(_FileAttributes);
#endif
		auto &Environment = *mp_pEnvironment;

		using namespace NPrivate;

		int Exclusive = -1;

		if (Environment.m_FileHandle != INVALID_HANDLE_VALUE)
			DMibError("Environment has already been opened");
		if (Flags & ~(EDatabaseEnvironmentFlag_Changable | EDatabaseEnvironmentFlag_Changeless))
			DMibError("Invalid flags specified");

#ifdef DMibDatabase_Always64BitSupport
		if (Flags & EDatabaseEnvironmentFlag_WriteMap)
			Flags ^= EDatabaseEnvironmentFlag_WriteMap; // silently ignore WRITEMAP in 32 bit mode
		if (Flags & EDatabaseEnvironmentFlag_FixedMap)
			DMibError("Fixed map not supported with DMibDatabase_Always64BitSupport");
#endif

		Flags |= Environment.m_Flags;

		auto Cleanup = g_OnScopeExit / [&]()
			{
				Environment.f_Close0(Exclusive);
			}
		;

		int Result = 0;
#ifdef DMibDatabase_Always64BitSupport
#	ifdef DPlatformFamily_Windows
		Environment.m_ReadOnlyPagesMutex = CreateMutex(NULL, FALSE, NULL);
		if (!Environment.m_ReadOnlyPagesMutex)
		{
			Result = DErrCode();
			DMibError(NStr::fg_Format("Error initing rmutex {}", fg_StrError(Result)));
		}
#	else
		Result = pthread_mutex_init(&Environment.m_ReadOnlyPagesMutex, NULL);
		if (Result)
			DMibError(NStr::fg_Format("Error initing rmutex {}", fg_StrError(Result)));
#	endif
#endif
		Flags |= DMdbEnvActive; // tell f_Close0() to clean up

		if (Flags & EDatabaseEnvironmentFlag_ReadOnly)
		{
			/* silently ignore WRITEMAP when we're only getting read access */
			Flags &= ~EDatabaseEnvironmentFlag_WriteMap;
		}
		else
		{
			Environment.m_pFreePages = fg_MidlAlloc(DMdbIdlUmMax);
			Environment.m_pDirtyList = (CMdbId2L)NMemory::fg_AllocInitZero(DMdbIdlUmSize * sizeof(CMdbId2));
		}

		Environment.m_Flags = Flags;

#ifdef DMibDatabase_Always64BitSupport
		if (Result)
			DMibError(NStr::fg_Format("Error opening mutex {}", fg_StrError(Result)));

		{
			Environment.m_pReadOnlyPages = (NPrivate::CMdbId3L)NMemory::fg_Alloc(DMdbErpageSize * sizeof(NPrivate::CMdbId3));
			Environment.m_pReadOnlyPages[0].m_ID = 0;
			Environment.m_ReadOnlyPageCheck = DMdbErpageSize / 2;
		}
#endif

		Environment.m_Path = _Path;
		Environment.m_StaticDasabaseInfo.f_SetLen(Environment.m_MaxDatabases);
		Environment.m_pDatabaseFlags = (uint16 *)NMemory::fg_AllocInitZero(Environment.m_MaxDatabases * sizeof(uint16));
		Environment.m_pDatabaseSequences = (uint32 *)NMemory::fg_AllocInitZero(Environment.m_MaxDatabases * sizeof(uint32));
		Environment.m_StaticDasabaseInfo[DFreeDbi].m_fKeyCompare = fg_CmpLong; // aligned EDatabaseSubFlag_IntegerKeys

		/* For RDONLY, get lockfile after we know datafile exists */
		if (!(Flags & (EDatabaseEnvironmentFlag_ReadOnly | EDatabaseEnvironmentFlag_NoLock)))
		{
			Environment.f_SetupLocks(_Path, Mode, &Exclusive);
			if ((Flags & EDatabaseEnvironmentFlag_PreviousSnapshot) && !Exclusive)
				DMibError(NStr::fg_Format("Previous snapshot needs exclusive access: {}", fg_StrError(EAGAIN)));
		}

		Result = Environment.f_FOpen(_Path, (Flags & EDatabaseEnvironmentFlag_ReadOnly) ? EMdbFOpenType_ReadOnly : EMdbFOpenType_ReadWrite, Mode, &Environment.m_FileHandle);
		if (Result)
			DMibError(NStr::fg_Format("Error opening file {}", fg_StrError(Result)));

		if ((Flags & (EDatabaseEnvironmentFlag_ReadOnly | EDatabaseEnvironmentFlag_NoLock)) == EDatabaseEnvironmentFlag_ReadOnly)
			Environment.f_SetupLocks(_Path, Mode, &Exclusive);

		Environment.f_Open2(Flags & EDatabaseEnvironmentFlag_PreviousSnapshot, _PageSize);

		if (!(Flags & (EDatabaseEnvironmentFlag_ReadOnly | EDatabaseEnvironmentFlag_WriteMap)))
		{
			/* Synchronous fd for meta writes. Needed even with
			 * EDatabaseEnvironmentFlag_NoSync/EDatabaseEnvironmentFlag_NoMetaSync, in case these get reset.
			 */
			Result = Environment.f_FOpen(_Path, EMdbFOpenType_Meta, Mode, &Environment.m_MetaFileHandle);
			if (Result)
				DMibError(NStr::fg_Format("Error opening meta sync file {}", fg_StrError(Result)));
		}

		DMibDatabaseTrace("opened dbenv {}", (void *)&Environment);
		if (Exclusive > 0 && !(Flags & EDatabaseEnvironmentFlag_PreviousSnapshot))
		{
			Result = Environment.f_ShareLocks(&Exclusive);
			if (Result)
				DMibError(NStr::fg_Format("Error share locks {}", fg_StrError(Result)));
		}

		if (!(Flags & EDatabaseEnvironmentFlag_ReadOnly))
		{
			CMdbTxn *pTransaction;
			umint TransactionSize = sizeof(CMdbTxn);
			umint Size = TransactionSize + Environment.m_MaxDatabases * (sizeof(CMdbDb) + sizeof(CMdbCursor *) + sizeof(uint32) + 1);
			Environment.m_pScratchBuffer = NMemory::fg_AllocInitZero(Environment.m_PageSize);
			pTransaction = (CMdbTxn *)NMemory::fg_AllocInitZero(Size);
			auto Cleanup1 = g_OnScopeExit / [&]()
				{
					NMemory::fg_Free(pTransaction, Size);
				}
			;
			pTransaction->m_pDatabases = (CMdbDb *)((char *)pTransaction + TransactionSize);
			pTransaction->m_pCursors = (CMdbCursor **)(pTransaction->m_pDatabases + Environment.m_MaxDatabases);
			pTransaction->m_pDatabaseSequences = (uint32 *)(pTransaction->m_pCursors + Environment.m_MaxDatabases);
			pTransaction->m_pDatabaseFlags = (unsigned char *)(pTransaction->m_pDatabaseSequences + Environment.m_MaxDatabases);
			pTransaction->m_pEnvironment = &Environment;
#ifdef DMibDatabase_Always64BitSupport
			pTransaction->m_pReadOnlyPages = (NPrivate::CMdbId3L)NMemory::fg_Alloc(DMdbTrpageSize * sizeof(CMdbId3));
			pTransaction->m_pReadOnlyPages[0].m_ID = 0;
			pTransaction->m_ReadOnlyPageCheck = DMdbTrpageSize / 2;
#endif
			pTransaction->m_pDatabaseInfos = Environment.m_StaticDasabaseInfo.f_GetArray();
			pTransaction->m_Flags = DMdbTxnFinished;
			Environment.m_pPreallocatedTransaction = pTransaction;
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

		HANDLE NewFileHandle = INVALID_HANDLE_VALUE; // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		int Result = Environment.f_FOpen(_Path, EMdbFOpenType_Copy, 0666, &NewFileHandle);

		if (Result)
			DMibError(NStr::fg_Format("Error opening file {}", fg_StrError(Result)));

		auto Cleanup = g_OnScopeExit / [&]()
			{
				int Error = close(NewFileHandle);
				(void)Error;
				DMibFastCheck(Error >= 0);
			}
		;

		f_Copy((void *)(umint)NewFileHandle, _Flags);
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
	 * @param[in] _FileHandle The filedescriptor to write the copy to. It must
	 * have already been opened for Write access.
	 * @param[in] flags Special options for this operation.
	 * See #f_Copy() for options.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Copy(void *_pFileHandle, EDatabaseEnvironmentCopyFlag _Flags)
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		if (_Flags & EDatabaseEnvironmentCopyFlag_Compact)
			Environment.f_CopyCompact((HANDLE)(umint)_pFileHandle);
		else
			Environment.f_Copy((HANDLE)(umint)_pFileHandle);
	}

	/** @brief Return statistics about the LMDB environment.
	 *
	 * @param[out] stat The address of an #CDatabaseStatistics structure
	 *	where the statistics will be copied
	 */
	CDatabaseStatistics DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Statistics()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		using namespace NPrivate;
		CMdbMeta *pMeta;

		pMeta = Environment.f_PickMeta();

		return Environment.f_DatabaseStatistics(&pMeta->m_Databases[DMainDbi]);
	}

	CDatabaseSizeStatistics CDatabaseEnvironment::f_SizeStatistics()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		using namespace NPrivate;
		CMdbMeta *pMeta;
		pMeta = Environment.f_PickMeta();

		return Environment.f_SizeStatistics(&pMeta->m_Databases[DMainDbi]);
	}

	/** @brief Return information about the LMDB environment.
	 *
	 * @param[out] stat The address of an #CDatabaseEnvironmentInfo structure
	 *	where the information will be copied
	 */
	CDatabaseEnvironmentInfo DMibDatabaseInfrequentSection CDatabaseEnvironment::f_Info()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		return Environment.f_EnvironmentInfo();
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
		CMdbMeta *pMeta = Environment.f_PickMeta();
		Environment.f_Sync0(_bForce, pMeta->m_LastPageNumber + 1);
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
		CMdbPage *pPage;

		auto &Environment = *mp_pEnvironment;

		while ((pPage = Environment.m_pReusablePages) != nullptr)
		{
			Environment.m_pReusablePages = pPage->m_pNext;
			NMemory::fg_Free(pPage, Environment.m_PageSize);
		}

		Environment.f_Close0(0);
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
		Environment.m_Flags &= ~_RemoveFlags;
		Environment.m_Flags |= _AddFlags;
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
		return (EDatabaseEnvironmentFlag)Environment.m_Flags & (EDatabaseEnvironmentFlag_Changable | EDatabaseEnvironmentFlag_Changeless);
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
		return Environment.m_Path;
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
	void * DMibDatabaseInfrequentSection CDatabaseEnvironment::f_GetFD()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		return (void *)(umint)Environment.m_FileHandle;
	}

	NAtomic::TCAtomic<umint> *CDatabaseEnvironment::f_GetReferencesPointer()
	{
		auto &Environment = *mp_pEnvironment;
		return &Environment.m_nReferences;
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
	 * beyond the range of the current mapsize, #f_TxnBegin() will
	 * return #DMdbMapResized. This function may be called with a size
	 * of zero to adopt the new size.
	 *
	 * Any attempt to set a size smaller than the space already consumed
	 * by the environment will be silently changed to the current size of the used space.
	 * @param[in] _Size The size in bytes
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified, or the environment has
	 *		an active write transaction.
	 * </ul>
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_SetMapSize(CDatabaseOffset _Size)
	{
		fp_CheckOpened();

		using namespace NPrivate;
		/* If env is already open, caller is responsible for making
		 * sure there are no active txns.
		 */

		auto &Environment = *mp_pEnvironment;

		if (Environment.m_pMap)
		{
			CMdbMeta *pMeta;
#ifndef DMibDatabase_Always64BitSupport
			void *pOldAddress;
			int Result;
#endif
			if (Environment.m_pTransaction)
				DMibError("Cannot set map size when write transaction is open");
			pMeta = Environment.f_PickMeta();
			if (!_Size)
				_Size = pMeta->m_MapSize;
			{
				/* Silently round up to minimum if the size is too small */
				CDatabaseOffset MinSize = (pMeta->m_LastPageNumber + 1) * Environment.m_PageSize;
				if (_Size < MinSize)
					_Size = MinSize;
			}
#ifndef DMibDatabase_Always64BitSupport
			/* For DMibDatabase_Always64BitSupport this bit is a noop since we dynamically remap
			 * chunks of the DB anyway.
			 */
			munmap(Environment.m_pMap, Environment.m_MapSize);
			Environment.m_MapSize = _Size;
			pOldAddress = (Environment.m_Flags & EDatabaseEnvironmentFlag_FixedMap) ? Environment.m_pMap : nullptr;
			Result = Environment.f_Map(pOldAddress);
			if (Result)
				DMibError(NStr::fg_Format("Failed to re-map environment: {}", fg_StrError(Result)));
#endif /* !DMibDatabase_Always64BitSupport */
		}
		Environment.m_MapSize = _Size;
		if (Environment.m_PageSize)
			Environment.m_nMaxPages = Environment.m_MapSize / Environment.m_PageSize;
	}

	/** @brief Set the maximum number of threads/reader slots for the environment.
	 *
	 * This defines the number of slots in the lock table that is used to track readers in the
	 * the environment. The default is 126.
	 * Starting a read-only transaction normally ties a lock table slot to the
	 * current thread until the environment closes or the thread exits. If
	 * EDatabaseEnvironmentFlag_NoTls is in use, #f_TxnBegin() instead ties the slot to the
	 * CMdbTxn object until it or the #CMdbEnv object is destroyed.
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
		if (Environment.m_pMap)
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
	 * @param[in] _nDatabases The maximum number of databases
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified, or the environment is already open.
	 * </ul>
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment::f_SetMaxDatabases(CDatabaseSubID _nDatabases)
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		if (Environment.m_pMap)
			DMibError("Max number of databases can only be set before environment has been opened");
		Environment.m_MaxDatabases = _nDatabases + DCoreDbs;
	}

	/** @brief Get the maximum size of keys and #EDatabaseSubFlag_SortedDuplicates data we can write.
	 *
	 * Depends on the compile-time constant #DMdbMaxKeySize. Default 511.
	 * See @ref CDatabaseValue.
	 * @return The maximum size of a key we can write
	 */
	umint DMibDatabaseInfrequentSection CDatabaseEnvironment::f_MaxKeySize()
	{
		fp_CheckOpened();

		auto &Environment = *mp_pEnvironment;
		(void)Environment;
		return DEnvMaxkey(&Environment);
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
		umint nReaders = Environment.m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		CMdbReader *pReaders = Environment.m_pLocksMemoryMap->m_Readers;
		for (umint iReader = 0; iReader < nReaders; iReader++)
		{
			auto &Reader = pReaders[iReader];
			auto OwningProcessID = Reader.m_OwningProcessID.f_Load();
			if (OwningProcessID)
			{
				CTransactionID TransactionID = Reader.m_BeginReadTransactionID.f_Load();

				NStr::CStr Data;
				if (TransactionID == (CTransactionID)-1)
					Data = NStr::fg_Format("{sj10} {} -\n", OwningProcessID, (void *)(umint)OwningProcessID);
				else
					Data = NStr::fg_Format("{sj10} {} {}\n", OwningProcessID, (void *)(umint)OwningProcessID, TransactionID);
				auto &Info = ReaderInfos.f_Insert();
				Info.m_Description = fg_Move(Data);
				Info.m_iReaderSlot = iReader;
			}
		}
		return ReaderInfos;
	}

	/** @brief Check for stale entries in the reader lock table.
	 *
	 * @param[out] dead Number of stale slots that were cleared
	 * @return 0 on success, non-zero on failure.
	 */
	umint DMibDatabaseInfrequentSection CDatabaseEnvironment::f_CheckForStaleReaders()
	{
		fp_CheckOpened();

		umint nDead = 0;
#ifdef DMdbRobustSupported
		auto &Environment = *mp_pEnvironment;
		if (Environment.m_pLocksMemoryMap)
			Environment.f_ReaderCheck0(false, &nDead);
#endif
		return nDead;
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

	void CDatabaseEnvironment_Internal::f_Sync0(bool _bForce, NPrivate::CPgno _nPages)
	{
		int Result = 0;
		if (m_Flags & EDatabaseEnvironmentFlag_ReadOnly)
			DMibError("Access denied. Read only databasae environment.");
		if (_bForce || !DFIsSet(m_Flags, EDatabaseEnvironmentFlag_NoSync))
		{
			if (m_Flags & EDatabaseEnvironmentFlag_WriteMap)
			{
				int Flags = ((m_Flags & EDatabaseEnvironmentFlag_WriteMapAsync) && !_bForce) ? MS_ASYNC : MS_SYNC;
				if (DMdbMsync(m_pMap, m_PageSize * _nPages, Flags))
					Result = DErrCode();
#ifdef DPlatformFamily_Windows
				else if (Flags == MS_SYNC && DMdbFDataSync(m_FileHandle))
					Result = DErrCode();
#endif
			}
			else
			{
				if (DMdbFDataSync(m_FileHandle))
					Result = DErrCode();
			}
		}
		if (Result != DMdbSuccess)
			DMibError(NStr::fg_Format("Error syncing database: {}", fg_StrError(Result)));
	}

	/** Read the environment parameters of a DB environment before
	 * mapping it into memory.
	 * @param[in] _bPrevious whether to read the backup meta page
	 * @param[out] _pMeta address of where to store the meta information
	 * @return 0 on success, non-zero on failure.
	 */
	bool DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_ReadHeader(int _bPrevious, NPrivate::CMdbMeta *_pMeta)
	{
		using namespace NPrivate;
		CMdbMetaBuf PageBuffer;
		CMdbPage *pPage;
		CMdbMeta *pMeta;
		int iMeta, Offset;
		constexpr aint c_Size = sizeof(PageBuffer);

		/* We don't know the page size yet, so use a minimum value.
		 * Read both meta pages so we can use the latest one.
		 */

		for (iMeta = Offset = 0; iMeta < DNumMetas; iMeta++, Offset += _pMeta->m_Databases[DFreeDbi].m_Pad)
		{
#ifdef DPlatformFamily_Windows
			DWORD nBytesRead;
			OVERLAPPED Overlapped;
			memset(&Overlapped, 0, sizeof(Overlapped));
			Overlapped.Offset = Offset;
			int Result = ReadFile(m_FileHandle, &PageBuffer, c_Size, &nBytesRead, &Overlapped) ? (int)nBytesRead : -1;
			if (Result == -1 && DErrCode() == ERROR_HANDLE_EOF)
				Result = 0;
#else
			int Result = pread(m_FileHandle, &PageBuffer, c_Size, Offset);
#endif
			if (Result != c_Size)
			{
				if (Result == 0 && Offset == 0)
					return true;
				Result = Result < 0 ? (int)DErrCode() : DMdbInvalid;
				if (Result == ENOENT)
					return true;
				DMibError(NStr::fg_Format("Error reading environment header: {}", fg_StrError(Result)));
			}

			pPage = (CMdbPage *)&PageBuffer;

			if (!DFIsSet(pPage->m_Flags, DPMeta))
				DMibError(NStr::fg_Format("Header page '{}' is not a meta page", pPage->m_PageNumber));

			pMeta = (CMdbMeta *)DMetaData(pPage);
			if (pMeta->m_Magic != DMdbMagic)
				DMibError(NStr::fg_Format("Header page '{}' has invalid magic: 0x{nfh,sf0,sj4}", pPage->m_PageNumber, pMeta->m_Magic));

			if (pMeta->m_Version != DMdbDataVersion)
			{
				DMibError
					(
						NStr::fg_Format("Header page '{}' has invalid data version: 0x{nfh,sf0,sj8} expected 0x{nfh,sf0,sj8}", pPage->m_PageNumber, pMeta->m_Version, uint32(DMdbDataVersion))
					)
				;
			}

			if
			(
				Offset == 0
				||
				(
					_bPrevious
					? pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) < _pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed)
					: pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) > _pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed)
				)
			)
			{
				*_pMeta = *pMeta;
			}
		}
		return false;
	}

	/** Fill in most of the zeroed #CMdbMeta for an empty database environment */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_InitMeta0(CMdbMeta *_pMeta)
	{
		_pMeta->m_Magic = DMdbMagic;
		_pMeta->m_Version = DMdbDataVersion;
		_pMeta->m_MapSize = m_MapSize;
		_pMeta->m_Databases[DFreeDbi].m_Pad = m_PageSize;
		_pMeta->m_LastPageNumber = DNumMetas - 1;
		_pMeta->m_Databases[DFreeDbi].m_Flags = m_Flags & 0xffff;
		_pMeta->m_Databases[DFreeDbi].m_Flags |= EDatabaseSubFlag_IntegerKeys; // this is m_Databases[DFreeDbi].m_Flags
		_pMeta->m_Databases[DFreeDbi].m_RootPage = DPInvalid;
		_pMeta->m_Databases[DMainDbi].m_RootPage = DPInvalid;
	}

	/** Write the environment parameters of a freshly created DB environment.
	 * @param[in] _pMeta the #CMdbMeta to write
	 * @return 0 on success, non-zero on failure.
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_InitMeta(CMdbMeta *_pMeta)
	{
		CMdbPage *pFirstPage, *pSecondPage;
		uint32 PageSize;
#ifdef DPlatformFamily_Windows
		DWORD nWritten;
		OVERLAPPED Overlapped;
		memset(&Overlapped, 0, sizeof(Overlapped));
		auto fDoWrite = [&](auto &o_Result, auto _FileHandle, auto _pData, auto _Size, auto _Position)
			{
				Overlapped.Offset = _Position;
				o_Result = WriteFile(_FileHandle, _pData, _Size, &nWritten, &Overlapped);
			}
		;
#else
		int nWritten;
		auto fDoWrite = [&](auto &o_Result, auto _FileHandle, auto _pData, auto _Size, auto _Position)
			{
				do
				{
					nWritten = pwrite(_FileHandle, _pData, _Size, _Position);
					if (nWritten == -1 && DErrCode() == EINTR)
						continue;
					o_Result = (nWritten >= 0);
					break;
				}
				while (1);
			}
		;
#endif

		DPuts("writing new meta page");

		PageSize = m_PageSize;

		pFirstPage = (CMdbPage *)NMemory::fg_AllocInitZero(DNumMetas * PageSize);
		auto Cleanup = g_OnScopeExit / [&]()
			{
				NMemory::fg_Free(pFirstPage, DNumMetas * PageSize);
			}
		;
		pFirstPage->m_PageNumber = 0;
		pFirstPage->m_Flags = DPMeta;
		*(CMdbMeta *)DMetaData(pFirstPage) = *_pMeta;

		pSecondPage = (CMdbPage *)((char *)pFirstPage + PageSize);
		pSecondPage->m_PageNumber = 1;
		pSecondPage->m_Flags = DPMeta;
		*(CMdbMeta *)DMetaData(pSecondPage) = *_pMeta;

		{
			int Result = 0;
			fDoWrite(Result, m_FileHandle, pFirstPage, PageSize * DNumMetas, 0);
			if (!Result)
				Result = DErrCode();
			else if ((unsigned)nWritten != PageSize * DNumMetas)
				Result = ENOSPC;
			else
				Result = 0;

			if (Result)
				DMibError(NStr::fg_Format("Error writing meta page: {}", fg_StrError(Result)));
		}
	}

	/** Update the environment info to commit a transaction.
	 * @param[in] _pTransaction the transaction that's being committed
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_EnvWriteMeta(CMdbTxn *_pTransaction)
	{
		CMdbEnv *pEnvironment;
		CMdbMeta Meta, MetaBackup, *pMeta;
		unsigned Flags;
		CDatabaseOffset MapSize;
		off_t Offset;
		int Result, Length, iMeta;
		char *pBuffer;
		HANDLE MetaFileHandle; // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
#ifdef DPlatformFamily_Windows
		OVERLAPPED Overlapped;
#else
		int Result2;
#endif

		iMeta = _pTransaction->m_TransactionID & 1;
		DMibDatabaseTrace("writing meta page {} for root page {}", iMeta, _pTransaction->m_pDatabases[DMainDbi].m_RootPage);

		pEnvironment = _pTransaction->m_pEnvironment;
		Flags = _pTransaction->m_Flags | pEnvironment->m_Flags;
		pMeta = pEnvironment->m_Metas[iMeta];
		MapSize = pEnvironment->m_Metas[iMeta ^ 1]->m_MapSize;
		/* Persist any increases of mapsize config */
		if (MapSize < pEnvironment->m_MapSize)
			MapSize = pEnvironment->m_MapSize;

		if (Flags & EDatabaseEnvironmentFlag_WriteMap)
		{
			pMeta->m_MapSize = MapSize;
			pMeta->m_Databases[DFreeDbi] = _pTransaction->m_pDatabases[DFreeDbi];
			pMeta->m_Databases[DMainDbi] = _pTransaction->m_pDatabases[DMainDbi];
			pMeta->m_LastPageNumber = _pTransaction->m_NextPageNumber - 1;
#if (__GNUC__ * 100 + __GNUC_MINOR__ >= 404) && /* TODO: portability */	\
	!(defined(__i386__) || defined(__x86_64__))
			/* LY: issue a memory barrier, if not x86. ITS#7969 */
			__sync_synchronize();
#endif
			pMeta->m_TransactionID.f_Store(_pTransaction->m_TransactionID, NAtomic::gc_MemoryOrder_Relaxed);
			if (!(Flags & (EDatabaseEnvironmentFlag_NoMetaSync | EDatabaseEnvironmentFlag_NoSync)))
			{
				unsigned MetaSize = pEnvironment->m_PageSize;
				Result = (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMapAsync) ? MS_ASYNC : MS_SYNC;
				pBuffer = (char *)pMeta - DPageHeaderSize;
#ifndef DPlatformFamily_Windows	/* POSIX msync() requires ptr = start of OS page */
				Result2 = (pBuffer - pEnvironment->m_pMap) & (pEnvironment->m_OSPageSize - 1);
				pBuffer -= Result2;
				MetaSize += Result2;
#endif
				if (DMdbMsync(pBuffer, MetaSize, Result))
				{
					Result = DErrCode();
					goto fail;
				}
			}
			goto done;
		}
		MetaBackup.m_TransactionID.f_Store(pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed), NAtomic::gc_MemoryOrder_Relaxed);
		MetaBackup.m_LastPageNumber = pMeta->m_LastPageNumber;

		Meta.m_MapSize = MapSize;
		Meta.m_Databases[DFreeDbi] = _pTransaction->m_pDatabases[DFreeDbi];
		Meta.m_Databases[DMainDbi] = _pTransaction->m_pDatabases[DMainDbi];
		Meta.m_LastPageNumber = _pTransaction->m_NextPageNumber - 1;
		Meta.m_TransactionID.f_Store(_pTransaction->m_TransactionID, NAtomic::gc_MemoryOrder_Relaxed);

		Offset = offsetof(CMdbMeta, m_MapSize);
		pBuffer = (char *)&Meta + Offset;
		Length = sizeof(CMdbMeta) - Offset;
		Offset += (char *)pMeta - pEnvironment->m_pMap;

		/* Write to the SYNC fd unless MDB_NOSYNC/MDB_NOMETASYNC.
		 * (m_MetaFileHandle goes to the same file as m_FileHandle, but writing to it
		 * also syncs to disk.  Avoids a separate fdatasync() call.)
		 */
		MetaFileHandle = (Flags & (EDatabaseEnvironmentFlag_NoSync | EDatabaseEnvironmentFlag_NoMetaSync)) ? pEnvironment->m_FileHandle : pEnvironment->m_MetaFileHandle;
#ifdef DPlatformFamily_Windows
		{
			memset(&Overlapped, 0, sizeof(Overlapped));
			Overlapped.Offset = Offset;
			if (!WriteFile(MetaFileHandle, pBuffer, Length, (DWORD *)&Result, &Overlapped))
				Result = -1;
		}
#else
	retry_write:
		Result = pwrite(MetaFileHandle, pBuffer, Length, Offset);
#endif
		if (Result != Length)
		{
			Result = Result < 0 ? DErrCode() : EIO;
#ifndef DPlatformFamily_Windows
			if (Result == EINTR)
				goto retry_write;
#endif
			DPuts("write failed, disk error?");
			/* On a failure, the pagecache still contains the new data.
			 * Write some old data back, to prevent it from being used.
			 * Use the non-SYNC fd; we know it will fail anyway.
			 */
			Meta.m_LastPageNumber = MetaBackup.m_LastPageNumber;
			Meta.m_TransactionID.f_Store(MetaBackup.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed), NAtomic::gc_MemoryOrder_Relaxed);
#ifdef DPlatformFamily_Windows
			memset(&Overlapped, 0, sizeof(Overlapped));
			Overlapped.Offset = Offset;
			WriteFile(pEnvironment->m_FileHandle, pBuffer, Length, nullptr, &Overlapped);
#else
			Result2 = pwrite(pEnvironment->m_FileHandle, pBuffer, Length, Offset);
			(void)Result2; // Silence warnings. We don't care about pwrite's return value
#endif
		fail:
			pEnvironment->m_Flags |= DMdbFatalError;
			return Result;
		}
		/* MIPS has cache coherency issues, this is a no-op everywhere else */
		DCacheFlush(pEnvironment->m_pMap + Offset, Length, DCACHE);
	done:
		/* Memory ordering issues are irrelevant; since the entire writer
		 * is wrapped by wmutex, all of these changes will become visible
		 * after the wmutex is unlocked. Since the DB is multi-version,
		 * readers will get consistent data regardless of how fresh or
		 * how stale their view of these values is.
		 */
		if (pEnvironment->m_pLocksMemoryMap)
			pEnvironment->m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(_pTransaction->m_TransactionID, NAtomic::gc_MemoryOrder_Release);

		return DMdbSuccess;
	}

	/** Check both meta pages to see which one is newer.
	 * @return newest #CMdbMeta.
	 */
	NPrivate::CMdbMeta *CDatabaseEnvironment_Internal::f_PickMeta() const
	{
		CMdbMeta * const *pMetas = m_Metas;
		return pMetas
			[
				(pMetas[0]->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) < pMetas[1]->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed))
				^ ((m_Flags & EDatabaseEnvironmentFlag_PreviousSnapshot) != 0)
			]
		;
	}

#ifdef DPlatformFamily_Windows
	/** @brief Map a result from an NTAPI call to WIN32. */
	DWORD NPrivate::fg_Nt2Win32(NTSTATUS _Status)
	{
		OVERLAPPED Overlapped = {0};
		DWORD nBytesTransferred;
		Overlapped.Internal = _Status;
		GetOverlappedResult(nullptr, &Overlapped, &nBytesTransferred, FALSE);
		return GetLastError();
	}
#endif

	bool DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_IsInsideMemoryMap(uint8 const *_pData)
	{
#ifdef DMibDatabase_Always64BitSupport
		return false; // TODO
#else
		return _pData >= (uint8 const *)m_pMap && _pData < ((uint8 const *)m_pMap + this->m_MapSize);
#endif
	}

	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_Map(void *_pAddress)
	{
		CMdbPage *pPage;
		uint32 Flags = m_Flags;
#ifdef DPlatformFamily_Windows
		int Status;
		int Access = SECTION_MAP_READ;
		HANDLE pSection;
		void *pView;
		SIZE_T ViewSize;
		ULONG PageProtection = PAGE_READONLY, SectionProtection, AllocationType;

		if (Flags & EDatabaseEnvironmentFlag_WriteMap)
		{
			Access |= SECTION_MAP_WRITE;
			PageProtection = PAGE_READWRITE;
		}
		if (Flags & EDatabaseEnvironmentFlag_ReadOnly)
		{
			SectionProtection = PAGE_READONLY;
			ViewSize = 0;
			AllocationType = 0;
		}
		else
		{
			SectionProtection = PAGE_READWRITE;
			ViewSize = m_MapSize;
			AllocationType = MEM_RESERVE;
		}

		Status = g_fDatabase_NtCreateSection(&pSection, Access, nullptr, nullptr, SectionProtection, SEC_RESERVE, m_FileHandle);
		if (Status)
			return fg_Nt2Win32(Status);
		pView = _pAddress;
#	ifdef DMibDatabase_Always64BitSupport
		ViewSize = DNumMetas * m_PageSize;
#	endif
		Status = g_fDatabase_NtMapViewOfSection(pSection, GetCurrentProcess(), &pView, 0, 0, nullptr, &ViewSize, ViewUnmap, AllocationType, PageProtection);
#	ifdef DMibDatabase_Always64BitSupport
		m_pFileMappingHandle = pSection;
#	else
		g_fDatabase_NtClose(pSection);
#	endif
		if (Status)
			return fg_Nt2Win32(Status);
		m_pMap = (char *)pView;
#else
#	ifdef DMibDatabase_Always64BitSupport
		(void)Flags;
		m_pMap = (char *)mmap(_pAddress, DNumMetas * m_PageSize, PROT_READ, MAP_SHARED, m_FileHandle, 0);
		if (m_pMap == MAP_FAILED)
		{
			m_pMap = nullptr;
			return DErrCode();
		}
#	else
		int Protection = PROT_READ;
		if (Flags & EDatabaseEnvironmentFlag_WriteMap)
		{
			Protection |= PROT_WRITE;
			if (ftruncate(m_FileHandle, m_MapSize) < 0)
				return DErrCode();
		}
		m_pMap = (char *)mmap(_pAddress, m_MapSize, Protection, MAP_SHARED, m_FileHandle, 0);
		if (m_pMap == MAP_FAILED)
		{
			m_pMap = nullptr;
			return DErrCode();
		}

		if (Flags & EDatabaseEnvironmentFlag_NoReadAhead)
		{
			/* Turn off readahead. It's harmful when the DB is larger than RAM. */
#		ifdef MADV_RANDOM
			madvise(m_pMap, m_MapSize, MADV_RANDOM);
#		else
#			ifdef POSIX_MADV_RANDOM
			posix_madvise(m_pMap, m_MapSize, POSIX_MADV_RANDOM);
#			endif /* POSIX_MADV_RANDOM */
#		endif /* MADV_RANDOM */
		}
#	endif /* DPlatformFamily_Windows */

		/* Can happen because the address argument to mmap() is just a
		 * hint.  mmap() can pick another, e.g. if the range is in use.
		 * The MAP_FIXED flag would prevent that, but then mmap could
		 * instead unmap existing pages to make room for the new map.
		 */
		if (_pAddress && m_pMap != _pAddress)
			return EBUSY; // TODO: Make a new MDB_* error code?
#endif

		pPage = (CMdbPage *)m_pMap;
		m_Metas[0] = (CMdbMeta *)DMetaData(pPage);
		m_Metas[1] = (CMdbMeta *)((char *)m_Metas[0] + m_PageSize);

		return DMdbSuccess;
	}

	NStr::CStr CDatabaseEnvironment_Internal::f_GetFileName(NStr::CStr const &_FileName, NPrivate::EMdbFOpenType _FileType)
	{
		/** Filename suffixes [datafile,lockfile][without,with MDB_NOSUBDIR] */
		static constexpr ch8 const *c_Suffixes[2][2] =
			{
				{"/MalterlibDB.mibdb", ""}
				, {"/MalterlibDB.lock", ".lock"}
			}
		;

		return _FileName + c_Suffixes[_FileType == EMdbFOpenType_Locks][(m_Flags & EDatabaseEnvironmentFlag_NoSubdir) != 0];
	}

	/** Open an LMDB file.
	 * @param[in] env	The LMDB environment.
	 * @param[in,out] _FileName	Path
	 * @param[in] _FileType	Determines file type, access mode, etc.
	 * @param[in] _Mode	The Unix permissions for the file, if we create it.
	 * @param[out] o_pFileHandle	Resulting file handle.
	 * @return 0 on success, non-zero on failure.
	 */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_FOpen(NStr::CStr const &_FileName, NPrivate::EMdbFOpenType _FileType, NPrivate::CMdbMode _Mode, HANDLE *o_pFileHandle)
	{
		int Result = DMdbSuccess;
		HANDLE FileHandle; // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
#ifdef DPlatformFamily_Windows
		DWORD Access, ShareMode, Disposition, Attributes;
#endif

		NStr::CStr FileName = f_GetFileName(_FileName, _FileType);

		/* The directory must already exist.  Usually the file need not.
		 * EMdbFOpenType_Meta requires the file because we already created it using
		 * EMdbFOpenType_ReadWrite.  EMdbFOpenType_Copy must not overwrite an existing file.
		 *
		 * With EMdbFOpenType_Copy we do not want the OS to cache the writes, since
		 * the source data is already in the OS cache.
		 *
		 * The lockfile needs FD_CLOEXEC (close file descriptor on exec*())
		 * to avoid the flock() issues noted under Caveats in lmdb.h.
		 * Also set it for other filehandles which the user cannot get at
		 * and close himself, which he may need after fork().  I.e. all but
		 * m_FileHandle, which programs do use via mdb_env_get_fd().
		 */

#ifdef DPlatformFamily_Windows
		Access = GENERIC_READ | GENERIC_WRITE;
		ShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
		Disposition = OPEN_ALWAYS;
		Attributes = FILE_ATTRIBUTE_NORMAL;
		switch (_FileType)
		{
		case EMdbFOpenType_ReadOnly: // read-only datafile
			Access = GENERIC_READ;
			Disposition = OPEN_EXISTING;
			break;
		case EMdbFOpenType_Meta: // for writing metapages
			Access = GENERIC_WRITE;
			Disposition = OPEN_EXISTING;
			Attributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH;
			break;
		case EMdbFOpenType_Copy: // mdb_env_copy() & co
			Access = GENERIC_WRITE;
			ShareMode = 0;
			Disposition = CREATE_NEW;
			Attributes = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
			break;
		default: break; // silence gcc -Wswitch (not all enum values handled)
		}
		FileHandle = CreateFileW(NStr::CWStr(FileName).f_GetStr(), Access, ShareMode, NULL, Disposition, Attributes, NULL);
#else
		FileHandle = open(FileName.f_GetStr(), (_FileType & EMdbFOpenType_Mask) | fg_GetUnixOpenFlags(), _Mode);
#endif

		if (FileHandle == INVALID_HANDLE_VALUE)
			Result = DErrCode();
#ifndef DPlatformFamily_Windows
		else
		{
			fg_SetUnixHandleOptions(FileHandle);

			if (_FileType == EMdbFOpenType_Copy && m_PageSize >= m_OSPageSize)
			{
				/* This may require buffer alignment.  There is no portable
				 * way to ask how much, so we require OS pagesize alignment.
				 */
#	ifdef F_NOCACHE	/* __APPLE__ */
				(void)fcntl(FileHandle, F_NOCACHE, 1);
#	elif defined O_DIRECT
				/* open(...O_DIRECT...) would break on filesystems without
				 * O_DIRECT support (ITS#7682). Try to set it here instead.
				 */
				int Flags;
				if ((Flags = fcntl(FileHandle, F_GETFL)) != -1)
					(void)fcntl(FileHandle, F_SETFL, Flags | O_DIRECT);
#	endif
			}
		}
#endif	/* !DPlatformFamily_Windows */

		*o_pFileHandle = FileHandle;
		return Result;
	}

#ifdef DPlatformFamily_Windows
	NtCreateSectionFunc *NPrivate::g_fDatabase_NtCreateSection = nullptr;
	NtMapViewOfSectionFunc *NPrivate::g_fDatabase_NtMapViewOfSection = nullptr;
	NtCloseFunc *NPrivate::g_fDatabase_NtClose = nullptr;
#endif

	/** Further setup required for opening an LMDB environment
	 */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_Open2(int _bPrevious, uint32 _PageSize)
	{
		uint32 Flags = m_Flags;
		int bNewEnvironment = 0;
		CMdbMeta Meta;

#ifdef DPlatformFamily_Windows
		if (NLocal::g_VersionInfo.dwMajorVersion >= 6)
			m_ProcessIDQuery = DMdbProcessQueryLimitedInformation;
		else
			m_ProcessIDQuery = PROCESS_QUERY_INFORMATION;
		/* Grab functions we need from NTDLL */
		if (!g_fDatabase_NtCreateSection)
		{
			HMODULE pNtDll = GetModuleHandleW(L"NTDLL.DLL");
			if (!pNtDll)
				DMibError("Could not find NTDLL.DLL");
			g_fDatabase_NtClose = (NtCloseFunc *)GetProcAddress(pNtDll, "NtClose");
			if (!g_fDatabase_NtClose)
				DMibError("Could not find NtClose");
			g_fDatabase_NtMapViewOfSection = (NtMapViewOfSectionFunc *)GetProcAddress(pNtDll, "NtMapViewOfSection");
			if (!g_fDatabase_NtMapViewOfSection)
				DMibError("Could not find NtMapViewOfSection");
			g_fDatabase_NtCreateSection = (NtCreateSectionFunc *)GetProcAddress(pNtDll, "NtCreateSection");
			if (!g_fDatabase_NtCreateSection)
				DMibError("Could not find NtCreateSection");
		}
#endif /* DPlatformFamily_Windows */

		if (fg_BrokenFSync(this, m_FileHandle))
			m_Flags |= DMdbFSyncOnly;

		if (f_ReadHeader(_bPrevious, &Meta))
		{
			DPuts("new mdbenv");
			bNewEnvironment = 1;
			if (_PageSize)
				m_PageSize = _PageSize;
			else
				m_PageSize = m_OSPageSize;

			if (m_PageSize > DMaxPageSize)
				m_PageSize = DMaxPageSize;
			memset(&Meta, 0, sizeof(Meta));
			f_InitMeta0(&Meta);
			Meta.m_MapSize = DDefaultMapSize;
		}
		else
			m_PageSize = Meta.m_Databases[DFreeDbi].m_Pad;

		/* Was a mapsize configured? */
		if (!m_MapSize)
			m_MapSize = Meta.m_MapSize;
		{
			/* Make sure mapsize >= committed data size.  Even when using
			 * m_MapSize, which could be broken in old files (ITS#7789).
			 */
			CDatabaseOffset MinSize = (Meta.m_LastPageNumber + 1) * Meta.m_Databases[DFreeDbi].m_Pad;
			if (m_MapSize < MinSize)
				m_MapSize = MinSize;
		}
		Meta.m_MapSize = m_MapSize;

		if (bNewEnvironment && !(Flags & EDatabaseEnvironmentFlag_FixedMap))
		{
			/* f_Map() may grow the datafile.  Write the metapages
			 * first, so the file will be valid if initialization fails.
			 * Except with FIXEDMAP, since we do not yet know m_pAddress.
			 * We could fill in m_pAddress later, but then a different
			 * program might end up doing that - one with a memory layout
			 * and map address which does not suit the main program.
			 */
			f_InitMeta(&Meta);
			bNewEnvironment = 0;
		}
#ifdef DPlatformFamily_Windows
		/* For FIXEDMAP, make sure the file is non-empty before we attempt to map it */
		if (bNewEnvironment)
		{
			char Dummy = 0;
			DWORD nWritten;
			int Result = WriteFile(m_FileHandle, &Dummy, 1, &nWritten, nullptr);
			if (!Result)
			{
				Result = DErrCode();
				DMibError(NStr::fg_Format("Error writing file (dummy data): {}", fg_StrError(Result)));
			}
		}
#endif

		{
			int Result = f_Map((Flags & EDatabaseEnvironmentFlag_FixedMap) ? Meta.m_pAddress : nullptr);
			if (Result)
				DMibError(NStr::fg_Format("Failed to map environment: {}", fg_StrError(Result)));
		}

		if (bNewEnvironment)
		{
			if (Flags & EDatabaseEnvironmentFlag_FixedMap)
				Meta.m_pAddress = m_pMap;
			f_InitMeta(&Meta);
		}

		m_nMaxFreePerPage = (m_PageSize - DPageHeaderSize) / sizeof(CPgno) - 1;
		m_MaxNodeSize = (((m_PageSize - DPageHeaderSize) / DMdbMinKeys) & -2) - sizeof(CIndx);
#if !(DMdbMaxKeySize)
		m_MaxKeySize = m_MaxNodeSize - (DNodeSize + sizeof(CMdbDb));
#endif
		m_nMaxPages = m_MapSize / m_PageSize;

		if (m_pLocksMemoryMap)
			m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(Meta.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed), NAtomic::gc_MemoryOrder_Release);

#if DMibDatabaseDebug
		{
			CMdbMeta *pMeta = f_PickMeta();
			CMdbDb *pDatabase = &pMeta->m_Databases[DMainDbi];

			DMibDatabaseTrace("opened database version {}, pagesize {}", pMeta->m_Version, m_PageSize);
			DMibDatabaseTrace("using meta page {}", (int)(pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & 1));
			DMibDatabaseTrace("depth: {}", pDatabase->m_Depth);
			DMibDatabaseTrace("entries: {}", pDatabase->m_nEntries);
			DMibDatabaseTrace("branch pages: {}", pDatabase->m_nBranchPages);
			DMibDatabaseTrace("leaf pages: {}", pDatabase->m_nLeafPages);
			DMibDatabaseTrace("overflow pages: {}", pDatabase->m_nOverflowPages);
			DMibDatabaseTrace("root: {}", pDatabase->m_RootPage);
		}
#endif
	}

	/** Destroy resources from f_Open(), clear our readers & DBIs */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_Close0(int _Exclusive)
	{
		int iIndex;

		if (!(m_Flags & DMdbEnvActive))
			return;

		/* Doing this here since m_StaticDasabaseInfo may not exist during f_Close */
		if (!m_StaticDasabaseInfo.f_IsEmpty())
		{
			for (iIndex = m_MaxDatabases; --iIndex >= DCoreDbs;)
			{
				auto &StaticInfo = m_StaticDasabaseInfo[iIndex];
				NMemory::fg_Free((void *)StaticInfo.m_DatabaseName.m_pData, StaticInfo.m_DatabaseName.m_Size + 1);
			}
			m_StaticDasabaseInfo.f_Clear();
		}

		NMemory::fg_Free(m_pScratchBuffer, m_PageSize);
		NMemory::fg_Free(m_pDatabaseSequences, m_MaxDatabases * sizeof(uint32));
		NMemory::fg_Free(m_pDatabaseFlags, m_MaxDatabases * sizeof(uint16));
		m_Path.f_Clear();
		NMemory::fg_Free(m_pDirtyList, DMdbIdlUmSize * sizeof(CMdbId2));
#ifdef DMibDatabase_Always64BitSupport
		if (m_pPreallocatedTransaction && m_pPreallocatedTransaction->m_pReadOnlyPages)
			NMemory::fg_Free(m_pPreallocatedTransaction->m_pReadOnlyPages, DMdbTrpageSize * sizeof(CMdbId3));
		if (m_pReadOnlyPages)
		{
			CMdbId3L pReadOnlyPages = m_pReadOnlyPages;
			unsigned int iEntry;
			for (iEntry = 1; iEntry <= pReadOnlyPages[0].m_ID; iEntry++)
				munmap(pReadOnlyPages[iEntry].m_pValue, pReadOnlyPages[iEntry].m_nPages * m_PageSize);
			NMemory::fg_Free(pReadOnlyPages, DMdbErpageSize * sizeof(NPrivate::CMdbId3));
		}
#endif
		{
			umint Size = sizeof(CMdbTxn) + m_MaxDatabases * (sizeof(CMdbDb) + sizeof(CMdbCursor *) + sizeof(uint32) + 1);
			NMemory::fg_Free(m_pPreallocatedTransaction, Size);
		}
		fg_MidlFree(m_pFreePages);

		if (m_Flags & DMdbEnvTxkey)
		{
			pthread_key_delete(m_ThreadKey);
#ifdef DPlatformFamily_Windows
			/* Delete our key from the global list */
			for (iIndex = 0; iIndex < g_nTlsKeys; iIndex++)
			{
				if (g_TlsKeys[iIndex] == m_ThreadKey)
				{
					g_TlsKeys[iIndex] = g_TlsKeys[g_nTlsKeys - 1];
					g_nTlsKeys--;
					break;
				}
			}
#endif
		}

		if (m_pMap)
		{
#ifdef DMibDatabase_Always64BitSupport
			munmap(m_pMap, DNumMetas*m_PageSize);
#else
			munmap(m_pMap, m_MapSize);
#endif
		}
		if (m_MetaFileHandle != INVALID_HANDLE_VALUE)
			(void)close(m_MetaFileHandle);
		if (m_FileHandle != INVALID_HANDLE_VALUE)
			(void)close(m_FileHandle);
		if (m_pLocksMemoryMap)
		{
			CProcessID ProcessID = getpid();
			/* Clearing readers is done in this function because
			 * m_ThreadKey with its destructor must be disabled first.
			 *
			 * We skip the the reader mutex, so we touch only
			 * data owned by this process (m_nCloseReaders and
			 * our readers), and clear each reader atomically.
			 */
			for (iIndex = m_nCloseReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed); --iIndex >= 0;)
			{
				if (m_pLocksMemoryMap->m_Readers[iIndex].m_OwningProcessID.f_Load() == ProcessID)
					m_pLocksMemoryMap->m_Readers[iIndex].m_OwningProcessID.f_Store(0);
			}
#ifdef DPlatformFamily_Windows
			if (m_ReaderMutex)
			{
				CloseHandle(m_ReaderMutex);
				if (m_WriterMutex)
					CloseHandle(m_WriterMutex);
			}
			/* Windows automatically destroys the mutexes when
			 * the last handle closes.
			 */
#elif defined(MDB_USE_POSIX_SEM)
			if (m_ReaderMutex != SEM_FAILED)
			{
				sem_close(m_ReaderMutex);
				if (m_WriterMutex != SEM_FAILED)
					sem_close(m_WriterMutex);
				/* If we have the filelock:  If we are the
				 * only remaining user, clean up semaphores.
				 */
				if (_Exclusive == 0)
					f_ExclLock(&_Exclusive);
				if (_Exclusive > 0)
				{
					sem_unlink(DMutexName(this, 'r'));
					sem_unlink(DMutexName(this, 'w'));
				}
			}
#elif defined(DMdbUseSysvSem)
			if (m_ReaderMutex->m_SemaphoreID != -1)
			{
				/* If we have the filelock:  If we are the
				 * only remaining user, clean up semaphores.
				 */
				if (_Exclusive == 0)
					f_ExclLock(&_Exclusive);
				if (_Exclusive > 0)
					semctl(m_ReaderMutex->m_SemaphoreID, 0, IPC_RMID);
			}
#endif
			munmap((void *)m_pLocksMemoryMap, (m_MaxReaders - 1) * sizeof(CMdbReader) + sizeof(CLocksMemoryMap));
		}
		if (m_LockFileHandle != INVALID_HANDLE_VALUE)
		{
#ifdef DPlatformFamily_Windows
			if (_Exclusive >= 0)
			{
				/* Unlock the lockfile.  Windows would have unlocked it
				 * after closing anyway, but not necessarily at once.
				 */
				UnlockFile(m_LockFileHandle, 0, 0, 1, 0);
			}
#endif
			(void)close(m_LockFileHandle);
		}
#ifdef DMibDatabase_Always64BitSupport
#	ifdef DPlatformFamily_Windows
		if (m_pFileMappingHandle)
			CloseHandle(m_pFileMappingHandle);
		if (m_ReadOnlyPagesMutex)
			CloseHandle(m_ReadOnlyPagesMutex);
#	else
		pthread_mutex_destroy(&m_ReadOnlyPagesMutex);
#	endif
#endif

		m_Flags &= ~(DMdbEnvActive | DMdbEnvTxkey);
	}

	/** Copy environment as-is. */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_Copy(HANDLE _FileHandle) // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
	{
#ifdef DMibDatabase_Always64BitSupport
		DMibError("Not supported on 32 bit");
#endif

		CMdbTxn *pTransaction = nullptr;
		CMdbMutexRef pWriterMutex = nullptr;
		CDatabaseOffset WriteSize, EndOffset;
		char *pData;

#ifdef DPlatformFamily_Windows
		DWORD nWritten, ChunkSize;
#	define DDoWrite(d_Rc, d_Fd, d_Ptr, d_W2, d_Len)	d_Rc = WriteFile(d_Fd, d_Ptr, d_W2, &d_Len, nullptr)
#else
		smint nWritten;
		umint ChunkSize;
#	define DDoWrite(d_Rc, d_Fd, d_Ptr, d_W2, d_Len)	d_Len = write(d_Fd, d_Ptr, d_W2); d_Rc = (d_Len >= 0)
#endif

		/* Do the lock/unlock of the reader mutex before starting the
		 * write txn.  Otherwise other read txns could block writers.
		 */
		auto Transaction = f_TxnBegin(nullptr, EDatabaseEnvironmentFlag_ReadOnly);
		pTransaction = Transaction.fp_GetInternal();

		{
			auto UnlockMutex = g_OnScopeExit / [&]
				{
					if (pWriterMutex)
						DUnlockMutex(pWriterMutex);
				}
			;

			if (m_pLocksMemoryMap)
			{
				/* We must start the actual read txn after blocking writers */
				fg_TxnEnd(pTransaction, gc_MDB_END_RESET_TMP);

				/* Temporarily block writers until we snapshot the meta pages */
				{
					int Result;
					if (DLockMutex(Result, this, m_WriterMutex))
						DMibError(NStr::fg_Format("Write mutex lock failed: {}", fg_StrError(Result)));
					pWriterMutex = m_WriterMutex;
				}

				fg_TxnRenew0(pTransaction);
			}

			WriteSize = m_PageSize * DNumMetas;
			pData = m_pMap;
			ChunkSize = WriteSize;
			while (ChunkSize > 0)
			{
				{
					int Result;
					DDoWrite(Result, _FileHandle, pData, ChunkSize, nWritten);
					if (!Result)
					{
						Result = DErrCode();
						DMibError(NStr::fg_Format("Error writing (copy): {}", fg_StrError(Result)));
						break;
					}
					else if (nWritten > 0)
					{
						pData += nWritten;
						ChunkSize -= nWritten;
						continue;
					}
					else
						DMibError("Non-blocking or async handles are not supported");
				}
			}
		}

		EndOffset = pTransaction->m_NextPageNumber * m_PageSize;
		{
			CDatabaseOffset FileSize = fg_FSize(m_FileHandle);
			if (EndOffset > FileSize)
				EndOffset = FileSize;
		}
		WriteSize = EndOffset - WriteSize;
		while (WriteSize > 0)
		{
			if (WriteSize > DMaxWrite)
				ChunkSize = DMaxWrite;
			else
				ChunkSize = WriteSize;
			{
				int Result;
				DDoWrite(Result, _FileHandle, pData, ChunkSize, nWritten);
				if (!Result)
				{
					Result = DErrCode();
					DMibError(NStr::fg_Format("Error writing (copy): {}", fg_StrError(Result)));
				}
				else if (nWritten > 0)
				{
					pData += nWritten;
					WriteSize -= nWritten;
					continue;
				}
				else
					DMibError("Non-blocking or async handles are not supported");
			}
		}
	}
}
