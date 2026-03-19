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

namespace NMib::NDatabase
{
	using namespace NPrivate;
	/** Set or check a pid lock. Set returns 0 on success.
	 * Check returns 0 if the process is certainly dead, nonzero if it may
	 * be alive (the lock exists or an error happened so we do not know).
	 *
	 * On Windows Pidset is a no-op, we merely check for the existence
	 * of the process with the given pid. On POSIX we use a single byte
	 * lock on the lockfile, set at an offset equal to the pid.
	 */
	int CDatabaseEnvironment_Internal::mdb_reader_pid(Pidlock_op op, CProcessID pid)
	{
	#if !(MDB_PIDLOCK)		/* Currently the same as defined(DPlatformFamily_Windows) */
		int ret = 0;
		HANDLE h;
		if (op == Pidcheck) {
			h = OpenProcess(me_pidquery, FALSE, pid);
			/* No documented "no such process" code, but other program use this: */
			if (!h)
				return ErrCode() != ERROR_INVALID_PARAMETER;
			/* A process exists until all handles to it close. Has it exited? */
			ret = WaitForSingleObject(h, 0) != 0;
			CloseHandle(h);
		}
		return ret;
	#else
		for (;;) {
			int rc;
			struct flock lock_info;
			memset(&lock_info, 0, sizeof(lock_info));
			lock_info.l_type = F_WRLCK;
			lock_info.l_whence = SEEK_SET;
			lock_info.l_start = pid;
			lock_info.l_len = 1;
			if ((rc = fcntl(me_lfd, op, &lock_info)) == 0) {
				if (op == F_GETLK && lock_info.l_type != F_UNLCK)
					rc = -1;
			} else if ((rc = ErrCode()) == EINTR) {
				continue;
			}
			return rc;
		}
	#endif
	}


	/** Insert pid into list if not already present.
	 * return -1 if already present.
	 */
	int DMibDatabaseInfrequentSection NPrivate::mdb_pid_insert(CProcessID *ids, CProcessID pid)
	{
		/* binary search of pid in list */
		unsigned base = 0;
		unsigned cursor = 1;
		int val = 0;
		unsigned n = ids[0];

		while( 0 < n ) {
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = pid - ids[cursor];

			if( val < 0 ) {
				n = pivot;

			} else if ( val > 0 ) {
				base = cursor;
				n -= pivot + 1;

			} else {
				/* found, so it's a duplicate */
				return -1;
			}
		}

		if( val > 0 ) {
			++cursor;
		}
		ids[0]++;
		for (n = ids[0]; n > cursor; n--)
			ids[n] = ids[n-1];
		ids[n] = pid;
		return 0;
	}

#ifdef MDB_ROBUST_SUPPORTED
	/** As #mdb_reader_check(). bReadLock = <caller locked the reader mutex>. */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_reader_check0(bool _bReadLock, umint *_pDead)
	{
		using namespace NPrivate;
		mdb_mutexref_t rmutex = _bReadLock ? nullptr : me_rmutex;
		uint32 i, j, rdrs;
		MDB_reader *mr;
		CProcessID *pids, pid;
		umint nDead = 0;

		rdrs = m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		pids = (CProcessID *)NMemory::fg_Alloc((rdrs+1) * sizeof(CProcessID));
		auto Cleanup = g_OnScopeExit / [&]
			{
				NMemory::fg_Free(pids, (rdrs+1) * sizeof(CProcessID));
			}
		;
		pids[0] = 0;
		mr = m_pLocksMemoryMap->m_Readers;
		for (i=0; i<rdrs; i++) {
			pid = mr[i].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			if (pid && pid != me_pid) {
				if (mdb_pid_insert(pids, pid) == 0) {
					if (!mdb_reader_pid(Pidcheck, pid)) {
						/* Stale reader found */
						j = i;
						if (rmutex) {
							int rc;
							if ((rc = LOCK_MUTEX0(rmutex)) != 0) {
								if ((rc = mdb_mutex_failed(rmutex, rc)))
									DMibError(NStr::fg_Format("Failed to lock mutex: {}", mdb_strerror(rc)));
								rdrs = 0; /* the above checked all readers */
							} else {
								/* Recheck, a new process may have reused pid */
								if (mdb_reader_pid(Pidcheck, pid))
									j = rdrs;
							}
						}
						for (; j<rdrs; j++)
								if (mr[j].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Acquire) == pid) {
									DMibDatabaseTrace("clear stale reader pid {} txn {}", (unsigned) pid, fg_RemoveQualifiers(mr[j].m_BeginReadTransactionID.f_Load()));
									mr[j].m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
									nDead++;
								}
						if (rmutex)
							UNLOCK_MUTEX(rmutex);
					}
				}
			}
		}

		if (_pDead)
			*_pDead = nDead;
	}
#endif

	#ifdef MDB_ROBUST_SUPPORTED
	/** Handle #LOCK_MUTEX0() failure.
	 * Try to repair the lock file if the mutex owner died.
	 * @param[in] mutex	LOCK_MUTEX0() mutex
	 * @param[in] rc	LOCK_MUTEX0() error (nonzero)
	 * @return 0 on success with the mutex locked, or an error code on failure.
	 */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_mutex_failed(NPrivate::mdb_mutexref_t mutex, int rc)
	{
#ifdef DPlatformFamily_macOS
		if (rc == EINVAL)
		{
			NMib::NThread::CThreadSpinWaiter SpinWaiter;
			while (rc == EINVAL)
			{
				SpinWaiter.f_Wait();
				rc = LOCK_MUTEX0(mutex);
			}

			if (rc == MDB_SUCCESS)
				return 0;
		}
#endif

		bool bReadLock;
		MDB_meta *meta;
		if (rc == MDB_OWNERDEAD)
		{
			/* We own the mutex. Clean up after dead previous owner. */
			rc = MDB_SUCCESS;
			bReadLock = (mutex == me_rmutex);
			if (!bReadLock)
			{
				/* Keep m_TransactionBody.m_TransactionID updated, otherwise next writer can
				 * overwrite data which latest meta page refers to.
				 */
				meta = mdb_env_pick_meta();
				m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(meta->mm_txnid.f_Load(NAtomic::gc_MemoryOrder_Relaxed), NAtomic::gc_MemoryOrder_Release);
				/* env is hosed if the dead thread was ours */
				if (me_txn)
				{
					me_flags |= MDB_FATAL_ERROR;
					me_txn = nullptr;
					rc = MDB_PANIC;
				}
			}
			DMibDatabaseTrace("{}mutex owner died, {}", (bReadLock ? "r" : "w"), (rc ? "this process' env is hosed" : "recovering"));
			try
			{
				mdb_reader_check0(bReadLock, nullptr);
			}
			catch (NException::CException const &)
			{
				rc = MDB_OWNERDEAD;
			}
			mdb_mutex_consistent(mutex);
			if (rc)
			{
				DMibDatabaseTrace("LOCK_MUTEX recovery failed, {}", mdb_strerror(rc));
				UNLOCK_MUTEX(mutex);
			}
		}
		else
		{
	#ifdef DPlatformFamily_Windows
			rc = ErrCode();
	#endif
			DMibDatabaseTrace("LOCK_MUTEX failed, {}", mdb_strerror(rc));
		}
		return rc;
	}
	#endif	/* MDB_ROBUST_SUPPORTED */

	/** Open and/or initialize the lock region for the environment.
	 * @param[in] _LockPath The pathname of the file used for the lock region.
	 * @param[in] mode The Unix permissions for the file, if we create it.
	 * @param[in,out] excl In -1, out lock type: -1 none, 0 shared, 1 exclusive
	 * @return 0 on success, non-zero on failure.
	 */
	#ifdef DPlatformFamily_Windows
		SECURITY_DESCRIPTOR NPrivate::mdb_null_sd;
		SECURITY_ATTRIBUTES NPrivate::mdb_all_sa;
		int NPrivate::mdb_sec_inited;
	#endif

	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_setup_locks(NStr::CStr const &_LockPath, int mode, int *excl)
	{
	#ifdef DPlatformFamily_Windows
	#	define MDB_ERRCODE_ROFS	ERROR_WRITE_PROTECT
	#else
	#	define MDB_ERRCODE_ROFS	EROFS
	#endif
	#ifdef MDB_USE_SYSV_SEM
		int semid;
		union semun semu;
	#endif
		off_t size, rsize;
		int error_location = 0;

		NStr::CStr LockPath = mdb_getfilename(_LockPath, MDB_O_LOCKS);
		{
			int rc = mdb_fopen(_LockPath, MDB_O_LOCKS, mode, &me_lfd);
			if (rc) {
				/* Omit lockfile if read-only env on read-only filesystem */
				if (rc == MDB_ERRCODE_ROFS && (me_flags & EDatabaseEnvironmentFlag_ReadOnly))
					return;

				DMibError(NStr::fg_Format("Error opening lock file: {}", mdb_strerror(rc)));
			}
		}

		if (!(me_flags & EDatabaseEnvironmentFlag_NoTls)) {
			int rc = pthread_key_create(&me_txkey, mdb_env_reader_dest);
			if (rc)
				DMibError(NStr::fg_Format("Error creating pthread key: {}", mdb_strerror(rc)));
			me_flags |= MDB_ENV_TXKEY;
	#ifdef DPlatformFamily_Windows
			/* Windows TLS callbacks need help finding their TLS info. */
			if (mdb_tls_nkeys >= MAX_TLS_KEYS)
				DMibError(NStr::fg_Format("Out of tls keys: {}", mdb_strerror(MDB_TLS_FULL)));
			mdb_tls_keys[mdb_tls_nkeys++] = me_txkey;
	#endif
		}

		/* Try to get exclusive lock. If we succeed, then
		 * nobody is using the lock region and we should initialize it.
		 */
		{
			int rc = mdb_env_excl_lock(excl);
			if (rc)
				DMibError(NStr::fg_Format("Failed to get exclusive lock: {}", mdb_strerror(rc)));
		}

	#ifdef DPlatformFamily_Windows
		size = GetFileSize(me_lfd, nullptr);
	#else
		size = lseek(me_lfd, 0, SEEK_END);
		if (size == -1)
		{
			error_location = 1;
			goto fail_errno;
		}
	#endif
		rsize = (m_MaxReaders-1) * sizeof(MDB_reader) + sizeof(CLocksMemoryMap);
		if (size < rsize && *excl > 0) {
	#ifdef DPlatformFamily_Windows
			if (SetFilePointer(me_lfd, rsize, nullptr, FILE_BEGIN) != (DWORD)rsize
				|| !SetEndOfFile(me_lfd))
			{
				error_location = 2;
				goto fail_errno;
			}
	#else
			if (ftruncate(me_lfd, rsize) != 0)
			{
				error_location = 3;
				goto fail_errno;
			}
	#endif
		} else {
			rsize = size;
			size = rsize - sizeof(CLocksMemoryMap);
			m_MaxReaders = size/sizeof(MDB_reader) + 1;
		}
		{
	#ifdef DPlatformFamily_Windows
			HANDLE mh;
			mh = CreateFileMapping(me_lfd, nullptr, PAGE_READWRITE,
				0, 0, nullptr);
			if (!mh)
			{
				error_location = 4;
				goto fail_errno;
			}
			m_pLocksMemoryMap = (NPrivate::CLocksMemoryMap *)MapViewOfFileEx(mh, FILE_MAP_WRITE, 0, 0, rsize, nullptr);
			CloseHandle(mh);
			if (!m_pLocksMemoryMap)
			{
				error_location = 5;
				goto fail_errno;
			}
	#else
			void *m = mmap(nullptr, rsize, PROT_READ|PROT_WRITE, MAP_SHARED,
				me_lfd, 0);
			if (m == MAP_FAILED)
			{
				error_location = 6;
				goto fail_errno;
			}
			m_pLocksMemoryMap = (CLocksMemoryMap	*)m;
	#endif
		}

		if (*excl > 0)
		{
	#ifdef DPlatformFamily_Windows
			BY_HANDLE_FILE_INFORMATION stbuf;
			struct {
				DWORD volume;
				DWORD nhigh;
				DWORD nlow;
			} idbuf;

			if (!mdb_sec_inited) {
				InitializeSecurityDescriptor(&mdb_null_sd,
					SECURITY_DESCRIPTOR_REVISION);
				SetSecurityDescriptorDacl(&mdb_null_sd, TRUE, 0, FALSE);
				mdb_all_sa.nLength = sizeof(SECURITY_ATTRIBUTES);
				mdb_all_sa.bInheritHandle = FALSE;
				mdb_all_sa.lpSecurityDescriptor = &mdb_null_sd;
				mdb_sec_inited = 1;
			}
			if (!GetFileInformationByHandle(me_lfd, &stbuf))
			{
				error_location = 7;
				goto fail_errno;
			}
			idbuf.volume = stbuf.dwVolumeSerialNumber;
			idbuf.nhigh  = stbuf.nFileIndexHigh;
			idbuf.nlow   = stbuf.nFileIndexLow;
			m_pLocksMemoryMap->m_TransactionBody.mtb_mutexid = mdb_hash(&idbuf, sizeof(idbuf));
			mdb_env_mname_init(this);
			me_rmutex = CreateMutexExA(&mdb_all_sa, MUTEXNAME(this, 'r'), 0, SYNCHRONIZE);
			if (!me_rmutex)
			{
				if (!!(me_flags & EDatabaseEnvironmentFlag_AllowOneProcessLocking) && GetLastError() == ERROR_ACCESS_DENIED)
					me_rmutex = CreateMutexExA(&mdb_all_sa, nullptr, 0, SYNCHRONIZE);

				if (!me_rmutex)
				{
					error_location = 8;
					goto fail_errno;
				}
			}
			me_wmutex = CreateMutexExA(&mdb_all_sa, MUTEXNAME(this, 'w'), 0, SYNCHRONIZE);
			if (!me_wmutex)
			{
				if (!!(me_flags & EDatabaseEnvironmentFlag_AllowOneProcessLocking) && GetLastError() == ERROR_ACCESS_DENIED)
					me_wmutex = CreateMutexExA(&mdb_all_sa, nullptr, 0, SYNCHRONIZE);

				if (!me_wmutex)
				{
					error_location = 9;
					goto fail_errno;
				}
			}
	#elif defined(MDB_USE_POSIX_SEM)
			struct stat stbuf;
			struct {
				dev_t dev;
				ino_t ino;
			} idbuf;

	#if defined(__NetBSD__)
	#define	MDB_SHORT_SEMNAMES	1	/* limited to 14 chars */
	#endif
			if (fstat(me_lfd, &stbuf))
			{
				error_location = 10;
				goto fail_errno;
			}
			memset(&idbuf, 0, sizeof(idbuf));
			idbuf.dev = stbuf.st_dev;
			idbuf.ino = stbuf.st_ino;
			m_pLocksMemoryMap->m_TransactionBody.mtb_mutexid = mdb_hash(&idbuf, sizeof(idbuf))
	#ifdef MDB_SHORT_SEMNAMES
				/* Max 9 base85-digits.  We truncate here instead of in
				 * mdb_env_mname_init() to keep the latter portable.
				 */
				% ((mdb_hash_t)85*85*85*85*85*85*85*85*85)
	#endif
				;
			mdb_env_mname_init(this);
			/* Clean up after a previous run, if needed:  Try to
			 * remove both semaphores before doing anything else.
			 */
			sem_unlink(MUTEXNAME(this, 'r'));
			sem_unlink(MUTEXNAME(this, 'w'));
			me_rmutex = sem_open(MUTEXNAME(this, 'r'), O_CREAT|O_EXCL, mode, 1);
			if (me_rmutex == SEM_FAILED)
			{
				error_location = 11;
				goto fail_errno;
			}
			me_wmutex = sem_open(MUTEXNAME(this, 'w'), O_CREAT|O_EXCL, mode, 1);
			if (me_wmutex == SEM_FAILED)
			{
				error_location = 12;
				goto fail_errno;
			}
	#elif defined(MDB_USE_SYSV_SEM)
			unsigned short vals[2] = {1, 1};
			// ftok has big chance of collision, use hash of same data instead to create key.
			// (key_t) (id << 24 | (st.st_dev & 0xff) << 16 | (st.st_ino & 0xffff));
			/*
			key_t key = ftok(LockPath.f_GetStr(), 'M');
			if (key == -1)
			{
				error_location = 13;
				goto fail_errno;
			}
			*/

			struct stat stbuf;
			if (fstat(me_lfd, &stbuf))
			{
				error_location = 22;
				goto fail_errno;
			}

			NCryptography::CHash_SHA256 Hash;
			Hash.f_AddData("MalterlibDB", 11);
			Hash.f_AddData(&stbuf.st_dev, sizeof(stbuf.st_dev));
			Hash.f_AddData(&stbuf.st_ino, sizeof(stbuf.st_ino));

			key_t key = Hash.f_GetDigest().f_FoldToInt<key_t>();

			if (key == IPC_PRIVATE) // Collision with IPC private
			{
				error_location = 23;
				errno = EINVAL;
				goto fail_errno;
			}

			semid = semget(key, 2, (mode & 0777) | IPC_CREAT);
			if (semid < 0)
			{
				error_location = 14;
				goto fail_errno;
			}
			semu.array = vals;
			if (semctl(semid, 0, SETALL, semu) < 0)
			{
				error_location = 15;
				goto fail_errno;
			}
			m_pLocksMemoryMap->m_TransactionBody.mtb_semid = semid;
			m_pLocksMemoryMap->m_TransactionBody.mtb_rlocked = 0;
			m_pLocksMemoryMap->m_WriteLock = 0;
	#else	/* MDB_USE_POSIX_MUTEX: */
			pthread_mutexattr_t mattr;

			/* Solaris needs this before initing a robust mutex.  Otherwise
			 * it may skip the init and return EBUSY "seems someone already
			 * inited" or EINVAL "it was inited differently".
			 */
			memset(m_pLocksMemoryMap->m_TransactionBody.mtb_rmutex, 0, sizeof(*m_pLocksMemoryMap->m_TransactionBody.mtb_rmutex));
			memset(m_pLocksMemoryMap->m_WriteLock, 0, sizeof(*m_pLocksMemoryMap->m_WriteLock));

			int rc;
			if ((rc = pthread_mutexattr_init(&mattr)) != 0)
				DMibError(NStr::fg_Format("Error initializing mutex attributes: {}", mdb_strerror(rc)));
			rc = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	#ifdef MDB_ROBUST_SUPPORTED
			if (!rc) rc = pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
	#endif
			if (!rc) rc = pthread_mutex_init(m_pLocksMemoryMap->m_TransactionBody.mtb_rmutex, &mattr);
			if (!rc) rc = pthread_mutex_init(m_pLocksMemoryMap->m_WriteLock, &mattr);
			pthread_mutexattr_destroy(&mattr);
			if (rc)
				DMibError(NStr::fg_Format("Error initializing mutex: {}", mdb_strerror(rc)));
	#endif	/* DPlatformFamily_Windows || ... */

			m_pLocksMemoryMap->m_TransactionBody.mtb_magic = MDB_MAGIC;
			m_pLocksMemoryMap->m_TransactionBody.mtb_format = MDB_LOCK_FORMAT;
			m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
			m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Store(0, NAtomic::gc_MemoryOrder_Release);
		}
		else
		{
	#ifdef MDB_USE_SYSV_SEM
			struct semid_ds buf;
	#endif
			if (m_pLocksMemoryMap->m_TransactionBody.mtb_magic != MDB_MAGIC)
				DMibError("Lock region has invalid magic");

			if (m_pLocksMemoryMap->m_TransactionBody.mtb_format != MDB_LOCK_FORMAT)
			{
				DMibError
					(
						NStr::fg_Format
						(
							"Lock region has format+version 0x{nfh,sf0,sj8}, expected 0x{nfh,sf0,sj8}", m_pLocksMemoryMap->m_TransactionBody.mtb_format, uint32(MDB_LOCK_FORMAT)
						)
					)
				;
			}

			{
				int rc = ErrCode();
				if (rc && rc != EACCES && rc != EAGAIN)
					DMibError(NStr::fg_Format("Error setting up locks (non-exclusive): {}", mdb_strerror(rc)));
			}
	#ifdef DPlatformFamily_Windows
			mdb_env_mname_init(this);
			me_rmutex = OpenMutexA(SYNCHRONIZE, FALSE, MUTEXNAME(this, 'r'));
			if (!me_rmutex)
			{
				error_location = 16;
				goto fail_errno;
			}
			me_wmutex = OpenMutexA(SYNCHRONIZE, FALSE, MUTEXNAME(this, 'w'));
			if (!me_wmutex)
			{
				error_location = 17;
				goto fail_errno;
			}
	#elif defined(MDB_USE_POSIX_SEM)
			mdb_env_mname_init(this);
			me_rmutex = sem_open(MUTEXNAME(this, 'r'), 0);
			if (me_rmutex == SEM_FAILED)
			{
				error_location = 18;
				goto fail_errno;
			}
			me_wmutex = sem_open(MUTEXNAME(this, 'w'), 0);
			if (me_wmutex == SEM_FAILED)
			{
				error_location = 19;
				goto fail_errno;
			}
	#elif defined(MDB_USE_SYSV_SEM)
			semid = m_pLocksMemoryMap->m_TransactionBody.mtb_semid;
			semu.buf = &buf;
			/* check for read access */
			if (semctl(semid, 0, IPC_STAT, semu) < 0)
			{
				error_location = 20;
				goto fail_errno;
			}
			/* check for write access */
			if (semctl(semid, 0, IPC_SET, semu) < 0)
			{
				error_location = 21;
				goto fail_errno;
			}
	#endif
		}
	#ifdef MDB_USE_SYSV_SEM
		me_rmutex->semid = semid;
		me_wmutex->semid = semid;
		me_rmutex->semnum = 0;
		me_wmutex->semnum = 1;
		me_rmutex->locked = &m_pLocksMemoryMap->m_TransactionBody.mtb_rlocked;
		me_wmutex->locked = &m_pLocksMemoryMap->m_WriteLock;
	#endif

		return;

	fail_errno:
		{
			int rc = ErrCode();
			DMibError(NStr::fg_Format("Error setting up locks ({}): {}", error_location, mdb_strerror_system(rc)));
		}
	}

	/** Downgrade the exclusive lock on the region back to shared */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_share_locks(int *excl)
	{
		int rc = 0;

	#ifdef DPlatformFamily_Windows
		{
			OVERLAPPED ov;
			/* First acquire a shared lock. The Unlock will
			 * then release the existing exclusive lock.
			 */
			memset(&ov, 0, sizeof(ov));
			if (!LockFileEx(me_lfd, 0, 0, 1, 0, &ov)) {
				rc = ErrCode();
			} else {
				UnlockFile(me_lfd, 0, 0, 1, 0);
				*excl = 0;
			}
		}
	#else
		{
			struct flock lock_info;
			/* The shared lock replaces the existing lock */
			memset((void *)&lock_info, 0, sizeof(lock_info));
			lock_info.l_type = F_RDLCK;
			lock_info.l_whence = SEEK_SET;
			lock_info.l_start = 0;
			lock_info.l_len = 1;
			while ((rc = fcntl(me_lfd, F_SETLK, &lock_info)) &&
					(rc = ErrCode()) == EINTR) ;
			*excl = rc ? -1 : 0;	/* error may mean we lost the lock */
		}
	#endif

		return rc;
	}

	/** Try to get exclusive lock, otherwise shared.
	 *	Maintain *excl = -1: no/unknown lock, 0: shared, 1: exclusive.
	 */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::mdb_env_excl_lock(int *excl)
	{
		int rc = 0;
	#ifdef DPlatformFamily_Windows
		if (LockFile(me_lfd, 0, 0, 1, 0)) {
			*excl = 1;
		} else {
			OVERLAPPED ov;
			memset(&ov, 0, sizeof(ov));
			if (LockFileEx(me_lfd, 0, 0, 1, 0, &ov)) {
				*excl = 0;
			} else {
				rc = ErrCode();
			}
		}
	#else
		struct flock lock_info;
		memset((void *)&lock_info, 0, sizeof(lock_info));
		lock_info.l_type = F_WRLCK;
		lock_info.l_whence = SEEK_SET;
		lock_info.l_start = 0;
		lock_info.l_len = 1;
		while ((rc = fcntl(me_lfd, F_SETLK, &lock_info)) &&
				(rc = ErrCode()) == EINTR) ;
		if (!rc) {
			*excl = 1;
		} else
	# ifndef MDB_USE_POSIX_MUTEX
		if (*excl < 0) /* always true when MDB_USE_POSIX_MUTEX */
	# endif
		{
			lock_info.l_type = F_RDLCK;
			while ((rc = fcntl(me_lfd, F_SETLKW, &lock_info)) &&
					(rc = ErrCode()) == EINTR) ;
			if (rc == 0)
				*excl = 0;
		}
	#endif
		return rc;
	}
}
