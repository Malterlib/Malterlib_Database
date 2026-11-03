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

namespace NMib::NDatabase
{
	using namespace NPrivate;
	/** Set or check a pid lock. Set returns 0 on success.
	 * Check returns 0 if the process is certainly dead, nonzero if it may
	 * be alive (the lock exists or an error happened so we do not know).
	 *
	 * On Windows EPidLockOp_Set is a no-op, we merely check for the existence
	 * of the process with the given pid. On POSIX we use a single byte
	 * lock on the lockfile, set at an offset equal to the pid.
	 */
	int CDatabaseEnvironment_Internal::f_ReaderPid(EPidLockOp _Operation, CProcessID _ProcessID)
	{
#if !(DMdbPidLock)		/* Currently the same as defined(DPlatformFamily_Windows) */
		int ret = 0;
		HANDLE h;
		if (_Operation == EPidLockOp_Check)
		{
			h = OpenProcess(me_pidquery, FALSE, _ProcessID);
			/* No documented "no such process" code, but other program use this: */
			if (!h)
				return DErrCode() != ERROR_INVALID_PARAMETER;
			/* A process exists until all handles to it close. Has it exited? */
			ret = WaitForSingleObject(h, 0) != 0;
			CloseHandle(h);
		}
		return ret;
#else
		for (;;)
		{
			int rc;
			struct flock lock_info;
			memset(&lock_info, 0, sizeof(lock_info));
			lock_info.l_type = F_WRLCK;
			lock_info.l_whence = SEEK_SET;
			lock_info.l_start = _ProcessID;
			lock_info.l_len = 1;
			if ((rc = fcntl(me_lfd, _Operation, &lock_info)) == 0)
			{
				if (_Operation == F_GETLK && lock_info.l_type != F_UNLCK)
					rc = -1;
			}
			else if ((rc = DErrCode()) == EINTR)
				continue;
			return rc;
		}
#endif
	}

	/** Insert _ProcessID into list if not already present.
	 * return -1 if already present.
	 */
	int DMibDatabaseInfrequentSection NPrivate::fg_PidInsert(CProcessID *_pProcessIDs, CProcessID _ProcessID)
	{
		/* binary search of _ProcessID in list */
		unsigned base = 0;
		unsigned cursor = 1;
		int val = 0;
		unsigned n = _pProcessIDs[0];

		while (0 < n)
		{
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = _ProcessID - _pProcessIDs[cursor];

			if (val < 0)
				n = pivot;
			else if (val > 0)
			{
				base = cursor;
				n -= pivot + 1;
			}
			else
			{
				/* found, so it's a duplicate */
				return -1;
			}
		}

		if (val > 0)
			++cursor;
		_pProcessIDs[0]++;
		for (n = _pProcessIDs[0]; n > cursor; n--)
			_pProcessIDs[n] = _pProcessIDs[n - 1];
		_pProcessIDs[n] = _ProcessID;
		return 0;
	}

#ifdef DMdbRobustSupported
	/** As #mdb_reader_check(). bReadLock = <caller locked the reader mutex>. */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_ReaderCheck0(bool _bReadLock, umint *_pDead)
	{
		using namespace NPrivate;
		CMdbMutexRef rmutex = _bReadLock ? nullptr : me_rmutex;
		uint32 i, j, rdrs;
		CMdbReader *mr;
		CProcessID *pids, pid;
		umint nDead = 0;

		rdrs = m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		pids = (CProcessID *)NMemory::fg_Alloc((rdrs + 1) * sizeof(CProcessID));
		auto Cleanup = g_OnScopeExit / [&]
			{
				NMemory::fg_Free(pids, (rdrs + 1) * sizeof(CProcessID));
			}
		;
		pids[0] = 0;
		mr = m_pLocksMemoryMap->m_Readers;
		for (i = 0; i < rdrs; i++)
		{
			pid = mr[i].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			if (pid && pid != me_pid)
			{
				if (fg_PidInsert(pids, pid) == 0)
				{
					if (!f_ReaderPid(EPidLockOp_Check, pid))
					{
						/* Stale reader found */
						j = i;
						if (rmutex)
						{
							int rc;
							if ((rc = DLockMutex0(rmutex)) != 0)
							{
								if ((rc = f_MutexFailed(rmutex, rc)))
									DMibError(NStr::fg_Format("Failed to lock mutex: {}", fg_StrError(rc)));
								rdrs = 0; // the above checked all readers
							}
							else
							{
								/* Recheck, a new process may have reused pid */
								if (f_ReaderPid(EPidLockOp_Check, pid))
									j = rdrs;
							}
						}
						for (; j < rdrs; j++)
						{
							if (mr[j].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Acquire) == pid)
							{
								DMibDatabaseTrace("clear stale reader pid {} txn {}", (unsigned)pid, fg_RemoveQualifiers(mr[j].m_BeginReadTransactionID.f_Load()));
								mr[j].m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
								nDead++;
							}
						}
						if (rmutex)
							DUnlockMutex(rmutex);
					}
				}
			}
		}

		if (_pDead)
			*_pDead = nDead;
	}
#endif

#ifdef DMdbRobustSupported
	/** Handle #DLockMutex0() failure.
	 * Try to repair the lock file if the mutex owner died.
	 * @param[in] mutex	DLockMutex0() mutex
	 * @param[in] rc	DLockMutex0() error (nonzero)
	 * @return 0 on success with the mutex locked, or an error code on failure.
	 */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_MutexFailed(NPrivate::CMdbMutexRef _pMutex, int _Result)
	{
#	ifdef DPlatformFamily_macOS
		if (_Result == EINVAL)
		{
			NMib::NThread::CThreadSpinWaiter SpinWaiter;
			while (_Result == EINVAL)
			{
				SpinWaiter.f_Wait();
				_Result = DLockMutex0(_pMutex);
			}

			if (_Result == DMdbSuccess)
				return 0;
		}
#	endif

		bool bReadLock;
		CMdbMeta *meta;
		if (_Result == DMdbOwnerdead)
		{
			/* We own the mutex. Clean up after dead previous owner. */
			_Result = DMdbSuccess;
			bReadLock = (_pMutex == me_rmutex);
			if (!bReadLock)
			{
				/* Keep m_TransactionBody.m_TransactionID updated, otherwise next writer can
				 * overwrite data which latest meta page refers to.
				 */
				meta = f_PickMeta();
				m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(meta->mm_txnid.f_Load(NAtomic::gc_MemoryOrder_Relaxed), NAtomic::gc_MemoryOrder_Release);
				/* env is hosed if the dead thread was ours */
				if (me_txn)
				{
					me_flags |= DMdbFatalError;
					me_txn = nullptr;
					_Result = DMdbPanic;
				}
			}
			DMibDatabaseTrace("{}mutex owner died, {}", (bReadLock ? "r" : "w"), (_Result ? "this process' env is hosed" : "recovering"));
			try
			{
				f_ReaderCheck0(bReadLock, nullptr);
			}
			catch (NException::CException const &)
			{
				_Result = DMdbOwnerdead;
			}
			mdb_mutex_consistent(_pMutex);
			if (_Result)
			{
				DMibDatabaseTrace("LOCK_MUTEX recovery failed, {}", fg_StrError(_Result));
				DUnlockMutex(_pMutex);
			}
		}
		else
		{
#	ifdef DPlatformFamily_Windows
			_Result = DErrCode();
#	endif
			DMibDatabaseTrace("LOCK_MUTEX failed, {}", fg_StrError(_Result));
		}
		return _Result;
	}
#endif	/* DMdbRobustSupported */

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

	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_SetupLocks(NStr::CStr const &_LockPath, int _Mode, int *_pExclusive)
	{
#ifdef DPlatformFamily_Windows
#	define DMdbErrcodeRofs	ERROR_WRITE_PROTECT
#else
#	define DMdbErrcodeRofs	EROFS
#endif
#ifdef DMdbUseSysvSem
		int semid;
		union semun semu;
#endif
		off_t size, rsize;
		int error_location = 0;

		NStr::CStr LockPath = f_GetFileName(_LockPath, EMdbFOpenType_Locks);
		{
			int rc = f_FOpen(_LockPath, EMdbFOpenType_Locks, _Mode, &me_lfd);
			if (rc)
			{
				/* Omit lockfile if read-only env on read-only filesystem */
				if (rc == DMdbErrcodeRofs && (me_flags & EDatabaseEnvironmentFlag_ReadOnly))
					return;

				DMibError(NStr::fg_Format("Error opening lock file: {}", fg_StrError(rc)));
			}
		}

		if (!(me_flags & EDatabaseEnvironmentFlag_NoTls))
		{
			int rc = pthread_key_create(&me_txkey, fg_EnvReaderDest);
			if (rc)
				DMibError(NStr::fg_Format("Error creating pthread key: {}", fg_StrError(rc)));
			me_flags |= DMdbEnvTxkey;
#ifdef DPlatformFamily_Windows
			/* Windows TLS callbacks need help finding their TLS info. */
			if (mdb_tls_nkeys >= DMaxTlsKeys)
				DMibError(NStr::fg_Format("Out of tls keys: {}", fg_StrError(DMdbTlsFull)));
			mdb_tls_keys[mdb_tls_nkeys++] = me_txkey;
#endif
		}

		/* Try to get exclusive lock. If we succeed, then
		 * nobody is using the lock region and we should initialize it.
		 */
		{
			int rc = f_ExclLock(_pExclusive);
			if (rc)
				DMibError(NStr::fg_Format("Failed to get exclusive lock: {}", fg_StrError(rc)));
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
		rsize = (m_MaxReaders - 1) * sizeof(CMdbReader) + sizeof(CLocksMemoryMap);
		if (size < rsize && *_pExclusive > 0)
		{
#ifdef DPlatformFamily_Windows
			if (SetFilePointer(me_lfd, rsize, nullptr, FILE_BEGIN) != (DWORD)rsize || !SetEndOfFile(me_lfd))
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
		}
		else
		{
			rsize = size;
			size = rsize - sizeof(CLocksMemoryMap);
			m_MaxReaders = size/sizeof(CMdbReader) + 1;
		}
		{
#ifdef DPlatformFamily_Windows
			HANDLE mh;
			mh = CreateFileMapping(me_lfd, nullptr, PAGE_READWRITE, 0, 0, nullptr);
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
			void *m = mmap(nullptr, rsize, PROT_READ | PROT_WRITE, MAP_SHARED, me_lfd, 0);
			if (m == MAP_FAILED)
			{
				error_location = 6;
				goto fail_errno;
			}
			m_pLocksMemoryMap = (CLocksMemoryMap *)m;
#endif
		}

		if (*_pExclusive > 0)
		{
#ifdef DPlatformFamily_Windows
			BY_HANDLE_FILE_INFORMATION stbuf;
			struct
			{
				DWORD volume;
				DWORD nhigh;
				DWORD nlow;
			} idbuf;

			if (!mdb_sec_inited)
			{
				InitializeSecurityDescriptor(&mdb_null_sd, SECURITY_DESCRIPTOR_REVISION);
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
			idbuf.nhigh = stbuf.nFileIndexHigh;
			idbuf.nlow = stbuf.nFileIndexLow;
			m_pLocksMemoryMap->m_TransactionBody.mtb_mutexid = fg_Hash(&idbuf, sizeof(idbuf));
			fg_EnvMNameInit(this);
			me_rmutex = CreateMutexExA(&mdb_all_sa, DMutexName(this, 'r'), 0, SYNCHRONIZE);
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
			me_wmutex = CreateMutexExA(&mdb_all_sa, DMutexName(this, 'w'), 0, SYNCHRONIZE);
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
			struct
			{
				dev_t dev;
				ino_t ino;
			} idbuf;

#	if defined(__NetBSD__)
#		define	DMdbShortSemnames	1	/* limited to 14 chars */
#	endif
			if (fstat(me_lfd, &stbuf))
			{
				error_location = 10;
				goto fail_errno;
			}
			memset(&idbuf, 0, sizeof(idbuf));
			idbuf.dev = stbuf.st_dev;
			idbuf.ino = stbuf.st_ino;
			m_pLocksMemoryMap->m_TransactionBody.mtb_mutexid = fg_Hash(&idbuf, sizeof(idbuf))
#	ifdef DMdbShortSemnames
				/* Max 9 base85-digits.  We truncate here instead of in
				 * fg_EnvMNameInit() to keep the latter portable.
				 */
				% ((CMdbHash)85 * 85 * 85 * 85 * 85 * 85 * 85 * 85 * 85)
#	endif
				;
			fg_EnvMNameInit(this);
			/* Clean up after a previous run, if needed:  Try to
			 * remove both semaphores before doing anything else.
			 */
			sem_unlink(DMutexName(this, 'r'));
			sem_unlink(DMutexName(this, 'w'));
			me_rmutex = sem_open(DMutexName(this, 'r'), O_CREAT | O_EXCL, _Mode, 1);
			if (me_rmutex == SEM_FAILED)
			{
				error_location = 11;
				goto fail_errno;
			}
			me_wmutex = sem_open(DMutexName(this, 'w'), O_CREAT | O_EXCL, _Mode, 1);
			if (me_wmutex == SEM_FAILED)
			{
				error_location = 12;
				goto fail_errno;
			}
#elif defined(DMdbUseSysvSem)
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

			semid = semget(key, 2, (_Mode & 0777) | IPC_CREAT);
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
#else	/* DMdbUsePosixMutex: */
			pthread_mutexattr_t mattr;

			/* Solaris needs this before initing a robust mutex.  Otherwise
			 * it may skip the init and return EBUSY "seems someone already
			 * inited" or EINVAL "it was inited differently".
			 */
			memset(m_pLocksMemoryMap->m_TransactionBody.mtb_rmutex, 0, sizeof(*m_pLocksMemoryMap->m_TransactionBody.mtb_rmutex));
			memset(m_pLocksMemoryMap->m_WriteLock, 0, sizeof(*m_pLocksMemoryMap->m_WriteLock));

			int rc;
			if ((rc = pthread_mutexattr_init(&mattr)) != 0)
				DMibError(NStr::fg_Format("Error initializing mutex attributes: {}", fg_StrError(rc)));
			rc = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
#	ifdef DMdbRobustSupported
			if (!rc)
				rc = pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
#	endif
			if (!rc)
				rc = pthread_mutex_init(m_pLocksMemoryMap->m_TransactionBody.mtb_rmutex, &mattr);
			if (!rc)
				rc = pthread_mutex_init(m_pLocksMemoryMap->m_WriteLock, &mattr);
			pthread_mutexattr_destroy(&mattr);
			if (rc)
				DMibError(NStr::fg_Format("Error initializing mutex: {}", fg_StrError(rc)));
#endif	/* DPlatformFamily_Windows || ... */

			m_pLocksMemoryMap->m_TransactionBody.mtb_magic = DMdbMagic;
			m_pLocksMemoryMap->m_TransactionBody.mtb_format = DMdbLockFormat;
			m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
			m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Store(0, NAtomic::gc_MemoryOrder_Release);
		}
		else
		{
#ifdef DMdbUseSysvSem
			struct semid_ds buf;
#endif
			if (m_pLocksMemoryMap->m_TransactionBody.mtb_magic != DMdbMagic)
				DMibError("Lock region has invalid magic");

			if (m_pLocksMemoryMap->m_TransactionBody.mtb_format != DMdbLockFormat)
			{
				DMibError
					(
						NStr::fg_Format("Lock region has format+version 0x{nfh,sf0,sj8}, expected 0x{nfh,sf0,sj8}", m_pLocksMemoryMap->m_TransactionBody.mtb_format, uint32(DMdbLockFormat))
					)
				;
			}

			{
				int rc = DErrCode();
				if (rc && rc != EACCES && rc != EAGAIN)
					DMibError(NStr::fg_Format("Error setting up locks (non-exclusive): {}", fg_StrError(rc)));
			}
#ifdef DPlatformFamily_Windows
			fg_EnvMNameInit(this);
			me_rmutex = OpenMutexA(SYNCHRONIZE, FALSE, DMutexName(this, 'r'));
			if (!me_rmutex)
			{
				error_location = 16;
				goto fail_errno;
			}
			me_wmutex = OpenMutexA(SYNCHRONIZE, FALSE, DMutexName(this, 'w'));
			if (!me_wmutex)
			{
				error_location = 17;
				goto fail_errno;
			}
#elif defined(MDB_USE_POSIX_SEM)
			fg_EnvMNameInit(this);
			me_rmutex = sem_open(DMutexName(this, 'r'), 0);
			if (me_rmutex == SEM_FAILED)
			{
				error_location = 18;
				goto fail_errno;
			}
			me_wmutex = sem_open(DMutexName(this, 'w'), 0);
			if (me_wmutex == SEM_FAILED)
			{
				error_location = 19;
				goto fail_errno;
			}
#elif defined(DMdbUseSysvSem)
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
#ifdef DMdbUseSysvSem
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
			int rc = DErrCode();
			DMibError(NStr::fg_Format("Error setting up locks ({}): {}", error_location, fg_StrErrorSystem(rc)));
		}
	}

	/** Downgrade the exclusive lock on the region back to shared */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_ShareLocks(int *_pExclusive)
	{
		int rc = 0;

#ifdef DPlatformFamily_Windows
		{
			OVERLAPPED ov;
			/* First acquire a shared lock. The Unlock will
			 * then release the existing exclusive lock.
			 */
			memset(&ov, 0, sizeof(ov));
			if (!LockFileEx(me_lfd, 0, 0, 1, 0, &ov))
				rc = DErrCode();
			else
			{
				UnlockFile(me_lfd, 0, 0, 1, 0);
				*_pExclusive = 0;
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
			while ((rc = fcntl(me_lfd, F_SETLK, &lock_info)) && (rc = DErrCode()) == EINTR);
			*_pExclusive = rc ? -1 : 0; // error may mean we lost the lock
		}
#endif

		return rc;
	}

	/** Try to get exclusive lock, otherwise shared.
	 *	Maintain *_pExclusive = -1: no/unknown lock, 0: shared, 1: exclusive.
	 */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_ExclLock(int *_pExclusive)
	{
		int rc = 0;
#ifdef DPlatformFamily_Windows
		if (LockFile(me_lfd, 0, 0, 1, 0))
			*_pExclusive = 1;
		else
		{
			OVERLAPPED ov;
			memset(&ov, 0, sizeof(ov));
			if (LockFileEx(me_lfd, 0, 0, 1, 0, &ov))
				*_pExclusive = 0;
			else
				rc = DErrCode();
		}
#else
		struct flock lock_info;
		memset((void *)&lock_info, 0, sizeof(lock_info));
		lock_info.l_type = F_WRLCK;
		lock_info.l_whence = SEEK_SET;
		lock_info.l_start = 0;
		lock_info.l_len = 1;
		while ((rc = fcntl(me_lfd, F_SETLK, &lock_info)) && (rc = DErrCode()) == EINTR);
		if (!rc)
			*_pExclusive = 1;
		else
#	ifndef DMdbUsePosixMutex
		if (*_pExclusive < 0) // always true when DMdbUsePosixMutex
#	endif
		{
			lock_info.l_type = F_RDLCK;
			while ((rc = fcntl(me_lfd, F_SETLKW, &lock_info)) && (rc = DErrCode()) == EINTR);
			if (rc == 0)
				*_pExclusive = 0;
		}
#endif
		return rc;
	}
}
