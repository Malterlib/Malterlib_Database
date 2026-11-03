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
		int Result = 0;
		HANDLE Process;
		if (_Operation == EPidLockOp_Check)
		{
			Process = OpenProcess(m_ProcessIDQuery, FALSE, _ProcessID);
			/* No documented "no such process" code, but other program use this: */
			if (!Process)
				return DErrCode() != ERROR_INVALID_PARAMETER;
			/* A process exists until all handles to it close. Has it exited? */
			Result = WaitForSingleObject(Process, 0) != 0;
			CloseHandle(Process);
		}
		return Result;
#else
		for (;;)
		{
			int Result;
			struct flock LockInfo;
			memset(&LockInfo, 0, sizeof(LockInfo));
			LockInfo.l_type = F_WRLCK;
			LockInfo.l_whence = SEEK_SET;
			LockInfo.l_start = _ProcessID;
			LockInfo.l_len = 1;
			if ((Result = fcntl(m_LockFileHandle, _Operation, &LockInfo)) == 0)
			{
				if (_Operation == F_GETLK && LockInfo.l_type != F_UNLCK)
					Result = -1;
			}
			else if ((Result = DErrCode()) == EINTR)
				continue;
			return Result;
		}
#endif
	}

	/** Insert _ProcessID into list if not already present.
	 * return -1 if already present.
	 */
	int DMibDatabaseInfrequentSection NPrivate::fg_PidInsert(CProcessID *_pProcessIDs, CProcessID _ProcessID)
	{
		/* binary search of _ProcessID in list */
		unsigned iBase = 0;
		unsigned iCursor = 1;
		int Compare = 0;
		unsigned nRemaining = _pProcessIDs[0];

		while (0 < nRemaining)
		{
			unsigned nHalf = nRemaining >> 1;
			iCursor = iBase + nHalf + 1;
			Compare = _ProcessID - _pProcessIDs[iCursor];

			if (Compare < 0)
				nRemaining = nHalf;
			else if (Compare > 0)
			{
				iBase = iCursor;
				nRemaining -= nHalf + 1;
			}
			else
			{
				/* found, so it's a duplicate */
				return -1;
			}
		}

		if (Compare > 0)
			++iCursor;
		_pProcessIDs[0]++;
		for (nRemaining = _pProcessIDs[0]; nRemaining > iCursor; nRemaining--)
			_pProcessIDs[nRemaining] = _pProcessIDs[nRemaining - 1];
		_pProcessIDs[nRemaining] = _ProcessID;
		return 0;
	}

#ifdef DMdbRobustSupported
	/** As #mdb_reader_check(). bReadLock = <caller locked the reader mutex>. */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_ReaderCheck0(bool _bReadLock, umint *_pDead)
	{
		using namespace NPrivate;
		CMdbMutexRef pReaderMutex = _bReadLock ? nullptr : m_ReaderMutex;
		uint32 iReader, iSameProcess, nReaders;
		CMdbReader *pReaders;
		CProcessID *pProcessIDs, ProcessID;
		umint nDead = 0;

		nReaders = m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		pProcessIDs = (CProcessID *)NMemory::fg_Alloc((nReaders + 1) * sizeof(CProcessID));
		auto Cleanup = g_OnScopeExit / [&]
			{
				NMemory::fg_Free(pProcessIDs, (nReaders + 1) * sizeof(CProcessID));
			}
		;
		pProcessIDs[0] = 0;
		pReaders = m_pLocksMemoryMap->m_Readers;
		for (iReader = 0; iReader < nReaders; iReader++)
		{
			ProcessID = pReaders[iReader].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			if (ProcessID && ProcessID != m_ProcessID)
			{
				if (fg_PidInsert(pProcessIDs, ProcessID) == 0)
				{
					if (!f_ReaderPid(EPidLockOp_Check, ProcessID))
					{
						/* Stale reader found */
						iSameProcess = iReader;
						if (pReaderMutex)
						{
							int Result;
							if ((Result = DLockMutex0(pReaderMutex)) != 0)
							{
								if ((Result = f_MutexFailed(pReaderMutex, Result)))
									DMibError(NStr::fg_Format("Failed to lock mutex: {}", fg_StrError(Result)));
								nReaders = 0; // the above checked all readers
							}
							else
							{
								/* Recheck, a new process may have reused pid */
								if (f_ReaderPid(EPidLockOp_Check, ProcessID))
									iSameProcess = nReaders;
							}
						}
						for (; iSameProcess < nReaders; iSameProcess++)
						{
							if (pReaders[iSameProcess].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Acquire) == ProcessID)
							{
								DMibDatabaseTrace("clear stale reader pid {} txn {}", (unsigned)ProcessID, fg_RemoveQualifiers(pReaders[iSameProcess].m_BeginReadTransactionID.f_Load()));
								pReaders[iSameProcess].m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
								nDead++;
							}
						}
						if (pReaderMutex)
							DUnlockMutex(pReaderMutex);
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
		CMdbMeta *pMeta;
		if (_Result == DMdbOwnerdead)
		{
			/* We own the mutex. Clean up after dead previous owner. */
			_Result = DMdbSuccess;
			bReadLock = (_pMutex == m_ReaderMutex);
			if (!bReadLock)
			{
				/* Keep m_TransactionBody.m_TransactionID updated, otherwise next writer can
				 * overwrite data which latest meta page refers to.
				 */
				pMeta = f_PickMeta();
				m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed), NAtomic::gc_MemoryOrder_Release);
				/* env is hosed if the dead thread was ours */
				if (m_pTransaction)
				{
					m_Flags |= DMdbFatalError;
					m_pTransaction = nullptr;
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
	SECURITY_DESCRIPTOR NPrivate::g_NullSecurityDescriptor;
	SECURITY_ATTRIBUTES NPrivate::g_AllSecurityAttributes;
	int NPrivate::g_bSecurityInitialized;
#endif

	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_SetupLocks(NStr::CStr const &_LockPath, int _Mode, int *_pExclusive)
	{
#ifdef DPlatformFamily_Windows
#	define DMdbErrcodeRofs	ERROR_WRITE_PROTECT
#else
#	define DMdbErrcodeRofs	EROFS
#endif
#ifdef DMdbUseSysvSem
		int SemaphoreID;
		union semun SemaphoreUnion;
#endif
		off_t Size, ReaderTableSize;
		int ErrorLocation = 0;

		NStr::CStr LockPath = f_GetFileName(_LockPath, EMdbFOpenType_Locks);
		{
			int Result = f_FOpen(_LockPath, EMdbFOpenType_Locks, _Mode, &m_LockFileHandle);
			if (Result)
			{
				/* Omit lockfile if read-only env on read-only filesystem */
				if (Result == DMdbErrcodeRofs && (m_Flags & EDatabaseEnvironmentFlag_ReadOnly))
					return;

				DMibError(NStr::fg_Format("Error opening lock file: {}", fg_StrError(Result)));
			}
		}

		if (!(m_Flags & EDatabaseEnvironmentFlag_NoTls))
		{
			int Result = pthread_key_create(&m_ThreadKey, fg_EnvReaderDest);
			if (Result)
				DMibError(NStr::fg_Format("Error creating pthread key: {}", fg_StrError(Result)));
			m_Flags |= DMdbEnvTxkey;
#ifdef DPlatformFamily_Windows
			/* Windows TLS callbacks need help finding their TLS info. */
			if (g_nTlsKeys >= DMaxTlsKeys)
				DMibError(NStr::fg_Format("Out of tls keys: {}", fg_StrError(DMdbTlsFull)));
			g_TlsKeys[g_nTlsKeys++] = m_ThreadKey;
#endif
		}

		/* Try to get exclusive lock. If we succeed, then
		 * nobody is using the lock region and we should initialize it.
		 */
		{
			int Result = f_ExclLock(_pExclusive);
			if (Result)
				DMibError(NStr::fg_Format("Failed to get exclusive lock: {}", fg_StrError(Result)));
		}

#ifdef DPlatformFamily_Windows
		Size = GetFileSize(m_LockFileHandle, nullptr);
#else
		Size = lseek(m_LockFileHandle, 0, SEEK_END);
		if (Size == -1)
		{
			ErrorLocation = 1;
			goto fail_errno;
		}
#endif
		ReaderTableSize = (m_MaxReaders - 1) * sizeof(CMdbReader) + sizeof(CLocksMemoryMap);
		if (Size < ReaderTableSize && *_pExclusive > 0)
		{
#ifdef DPlatformFamily_Windows
			if (SetFilePointer(m_LockFileHandle, ReaderTableSize, nullptr, FILE_BEGIN) != (DWORD)ReaderTableSize || !SetEndOfFile(m_LockFileHandle))
			{
				ErrorLocation = 2;
				goto fail_errno;
			}
#else
			if (ftruncate(m_LockFileHandle, ReaderTableSize) != 0)
			{
				ErrorLocation = 3;
				goto fail_errno;
			}
#endif
		}
		else
		{
			ReaderTableSize = Size;
			Size = ReaderTableSize - sizeof(CLocksMemoryMap);
			m_MaxReaders = Size/sizeof(CMdbReader) + 1;
		}
		{
#ifdef DPlatformFamily_Windows
			HANDLE FileMapping;
			FileMapping = CreateFileMapping(m_LockFileHandle, nullptr, PAGE_READWRITE, 0, 0, nullptr);
			if (!FileMapping)
			{
				ErrorLocation = 4;
				goto fail_errno;
			}
			m_pLocksMemoryMap = (NPrivate::CLocksMemoryMap *)MapViewOfFileEx(FileMapping, FILE_MAP_WRITE, 0, 0, ReaderTableSize, nullptr);
			CloseHandle(FileMapping);
			if (!m_pLocksMemoryMap)
			{
				ErrorLocation = 5;
				goto fail_errno;
			}
#else
			void *pMap = mmap(nullptr, ReaderTableSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_LockFileHandle, 0);
			if (pMap == MAP_FAILED)
			{
				ErrorLocation = 6;
				goto fail_errno;
			}
			m_pLocksMemoryMap = (CLocksMemoryMap *)pMap;
#endif
		}

		if (*_pExclusive > 0)
		{
#ifdef DPlatformFamily_Windows
			BY_HANDLE_FILE_INFORMATION Stat;
			struct
			{
				DWORD m_Volume;
				DWORD m_IndexHigh;
				DWORD m_IndexLow;
			} FileID;

			if (!g_bSecurityInitialized)
			{
				InitializeSecurityDescriptor(&g_NullSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
				SetSecurityDescriptorDacl(&g_NullSecurityDescriptor, TRUE, 0, FALSE);
				g_AllSecurityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
				g_AllSecurityAttributes.bInheritHandle = FALSE;
				g_AllSecurityAttributes.lpSecurityDescriptor = &g_NullSecurityDescriptor;
				g_bSecurityInitialized = 1;
			}
			if (!GetFileInformationByHandle(m_LockFileHandle, &Stat))
			{
				ErrorLocation = 7;
				goto fail_errno;
			}
			FileID.m_Volume = Stat.dwVolumeSerialNumber;
			FileID.m_IndexHigh = Stat.nFileIndexHigh;
			FileID.m_IndexLow = Stat.nFileIndexLow;
			m_pLocksMemoryMap->m_TransactionBody.m_MutexID = fg_Hash(&FileID, sizeof(FileID));
			fg_EnvMNameInit(this);
			m_ReaderMutex = CreateMutexExA(&g_AllSecurityAttributes, DMutexName(this, 'r'), 0, SYNCHRONIZE);
			if (!m_ReaderMutex)
			{
				if (!!(m_Flags & EDatabaseEnvironmentFlag_AllowOneProcessLocking) && GetLastError() == ERROR_ACCESS_DENIED)
					m_ReaderMutex = CreateMutexExA(&g_AllSecurityAttributes, nullptr, 0, SYNCHRONIZE);

				if (!m_ReaderMutex)
				{
					ErrorLocation = 8;
					goto fail_errno;
				}
			}
			m_WriterMutex = CreateMutexExA(&g_AllSecurityAttributes, DMutexName(this, 'w'), 0, SYNCHRONIZE);
			if (!m_WriterMutex)
			{
				if (!!(m_Flags & EDatabaseEnvironmentFlag_AllowOneProcessLocking) && GetLastError() == ERROR_ACCESS_DENIED)
					m_WriterMutex = CreateMutexExA(&g_AllSecurityAttributes, nullptr, 0, SYNCHRONIZE);

				if (!m_WriterMutex)
				{
					ErrorLocation = 9;
					goto fail_errno;
				}
			}
#elif defined(MDB_USE_POSIX_SEM)
			struct stat Stat;
			struct
			{
				dev_t dev;
				ino_t ino;
			} idbuf;

#	if defined(__NetBSD__)
#		define	DMdbShortSemnames	1	/* limited to 14 chars */
#	endif
			if (fstat(m_LockFileHandle, &Stat))
			{
				ErrorLocation = 10;
				goto fail_errno;
			}
			memset(&idbuf, 0, sizeof(idbuf));
			idbuf.dev = Stat.st_dev;
			idbuf.ino = Stat.st_ino;
			m_pLocksMemoryMap->m_TransactionBody.m_MutexID = fg_Hash(&idbuf, sizeof(idbuf))
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
			m_ReaderMutex = sem_open(DMutexName(this, 'r'), O_CREAT | O_EXCL, _Mode, 1);
			if (m_ReaderMutex == SEM_FAILED)
			{
				ErrorLocation = 11;
				goto fail_errno;
			}
			m_WriterMutex = sem_open(DMutexName(this, 'w'), O_CREAT | O_EXCL, _Mode, 1);
			if (m_WriterMutex == SEM_FAILED)
			{
				ErrorLocation = 12;
				goto fail_errno;
			}
#elif defined(DMdbUseSysvSem)
			unsigned short Values[2] = {1, 1};
			// ftok has big chance of collision, use hash of same data instead to create key.
			// (key_t) (id << 24 | (st.st_dev & 0xff) << 16 | (st.st_ino & 0xffff));
			/*
			key_t key = ftok(LockPath.f_GetStr(), 'M');
			if (key == -1)
			{
				ErrorLocation = 13;
				goto fail_errno;
			}
			*/

			struct stat Stat;
			if (fstat(m_LockFileHandle, &Stat))
			{
				ErrorLocation = 22;
				goto fail_errno;
			}

			NCryptography::CHash_SHA256 Hash;
			Hash.f_AddData("MalterlibDB", 11);
			Hash.f_AddData(&Stat.st_dev, sizeof(Stat.st_dev));
			Hash.f_AddData(&Stat.st_ino, sizeof(Stat.st_ino));

			key_t Key = Hash.f_GetDigest().f_FoldToInt<key_t>();

			if (Key == IPC_PRIVATE) // Collision with IPC private
			{
				ErrorLocation = 23;
				errno = EINVAL;
				goto fail_errno;
			}

			SemaphoreID = semget(Key, 2, (_Mode & 0777) | IPC_CREAT);
			if (SemaphoreID < 0)
			{
				ErrorLocation = 14;
				goto fail_errno;
			}
			SemaphoreUnion.array = Values;
			if (semctl(SemaphoreID, 0, SETALL, SemaphoreUnion) < 0)
			{
				ErrorLocation = 15;
				goto fail_errno;
			}
			m_pLocksMemoryMap->m_TransactionBody.m_SemaphoreID = SemaphoreID;
			m_pLocksMemoryMap->m_TransactionBody.m_ReaderLock = 0;
			m_pLocksMemoryMap->m_WriteLock = 0;
#else	/* DMdbUsePosixMutex: */
			pthread_mutexattr_t MutexAttributes;

			/* Solaris needs this before initing a robust mutex.  Otherwise
			 * it may skip the init and return EBUSY "seems someone already
			 * inited" or EINVAL "it was inited differently".
			 */
			memset(m_pLocksMemoryMap->m_TransactionBody.m_ReaderTableMutex, 0, sizeof(*m_pLocksMemoryMap->m_TransactionBody.m_ReaderTableMutex));
			memset(m_pLocksMemoryMap->m_WriteLock, 0, sizeof(*m_pLocksMemoryMap->m_WriteLock));

			int Result;
			if ((Result = pthread_mutexattr_init(&MutexAttributes)) != 0)
				DMibError(NStr::fg_Format("Error initializing mutex attributes: {}", fg_StrError(Result)));
			Result = pthread_mutexattr_setpshared(&MutexAttributes, PTHREAD_PROCESS_SHARED);
#	ifdef DMdbRobustSupported
			if (!Result)
				Result = pthread_mutexattr_setrobust(&MutexAttributes, PTHREAD_MUTEX_ROBUST);
#	endif
			if (!Result)
				Result = pthread_mutex_init(m_pLocksMemoryMap->m_TransactionBody.m_ReaderTableMutex, &MutexAttributes);
			if (!Result)
				Result = pthread_mutex_init(m_pLocksMemoryMap->m_WriteLock, &MutexAttributes);
			pthread_mutexattr_destroy(&MutexAttributes);
			if (Result)
				DMibError(NStr::fg_Format("Error initializing mutex: {}", fg_StrError(Result)));
#endif	/* DPlatformFamily_Windows || ... */

			m_pLocksMemoryMap->m_TransactionBody.m_Magic = DMdbMagic;
			m_pLocksMemoryMap->m_TransactionBody.m_Format = DMdbLockFormat;
			m_pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
			m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Store(0, NAtomic::gc_MemoryOrder_Release);
		}
		else
		{
#ifdef DMdbUseSysvSem
			struct semid_ds SemaphoreInfo;
#endif
			if (m_pLocksMemoryMap->m_TransactionBody.m_Magic != DMdbMagic)
				DMibError("Lock region has invalid magic");

			if (m_pLocksMemoryMap->m_TransactionBody.m_Format != DMdbLockFormat)
				DMibError(NStr::fg_Format("Lock region has format+version 0x{nfh,sf0,sj8}, expected 0x{nfh,sf0,sj8}", m_pLocksMemoryMap->m_TransactionBody.m_Format, uint32(DMdbLockFormat)));

			{
				int Result = DErrCode();
				if (Result && Result != EACCES && Result != EAGAIN)
					DMibError(NStr::fg_Format("Error setting up locks (non-exclusive): {}", fg_StrError(Result)));
			}
#ifdef DPlatformFamily_Windows
			fg_EnvMNameInit(this);
			m_ReaderMutex = OpenMutexA(SYNCHRONIZE, FALSE, DMutexName(this, 'r'));
			if (!m_ReaderMutex)
			{
				ErrorLocation = 16;
				goto fail_errno;
			}
			m_WriterMutex = OpenMutexA(SYNCHRONIZE, FALSE, DMutexName(this, 'w'));
			if (!m_WriterMutex)
			{
				ErrorLocation = 17;
				goto fail_errno;
			}
#elif defined(MDB_USE_POSIX_SEM)
			fg_EnvMNameInit(this);
			m_ReaderMutex = sem_open(DMutexName(this, 'r'), 0);
			if (m_ReaderMutex == SEM_FAILED)
			{
				ErrorLocation = 18;
				goto fail_errno;
			}
			m_WriterMutex = sem_open(DMutexName(this, 'w'), 0);
			if (m_WriterMutex == SEM_FAILED)
			{
				ErrorLocation = 19;
				goto fail_errno;
			}
#elif defined(DMdbUseSysvSem)
			SemaphoreID = m_pLocksMemoryMap->m_TransactionBody.m_SemaphoreID;
			SemaphoreUnion.buf = &SemaphoreInfo;
			/* check for read access */
			if (semctl(SemaphoreID, 0, IPC_STAT, SemaphoreUnion) < 0)
			{
				ErrorLocation = 20;
				goto fail_errno;
			}
			/* check for write access */
			if (semctl(SemaphoreID, 0, IPC_SET, SemaphoreUnion) < 0)
			{
				ErrorLocation = 21;
				goto fail_errno;
			}
#endif
		}
#ifdef DMdbUseSysvSem
		m_ReaderMutex->m_SemaphoreID = SemaphoreID;
		m_WriterMutex->m_SemaphoreID = SemaphoreID;
		m_ReaderMutex->m_SemaphoreNumber = 0;
		m_WriterMutex->m_SemaphoreNumber = 1;
		m_ReaderMutex->m_pLocked = &m_pLocksMemoryMap->m_TransactionBody.m_ReaderLock;
		m_WriterMutex->m_pLocked = &m_pLocksMemoryMap->m_WriteLock;
#endif

		return;

	fail_errno:
		{
			int Result = DErrCode();
			DMibError(NStr::fg_Format("Error setting up locks ({}): {}", ErrorLocation, fg_StrErrorSystem(Result)));
		}
	}

	/** Downgrade the exclusive lock on the region back to shared */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_ShareLocks(int *_pExclusive)
	{
		int Result = 0;

#ifdef DPlatformFamily_Windows
		{
			OVERLAPPED Overlapped;
			/* First acquire a shared lock. The Unlock will
			 * then release the existing exclusive lock.
			 */
			memset(&Overlapped, 0, sizeof(Overlapped));
			if (!LockFileEx(m_LockFileHandle, 0, 0, 1, 0, &Overlapped))
				Result = DErrCode();
			else
			{
				UnlockFile(m_LockFileHandle, 0, 0, 1, 0);
				*_pExclusive = 0;
			}
		}
#else
		{
			struct flock LockInfo;
			/* The shared lock replaces the existing lock */
			memset((void *)&LockInfo, 0, sizeof(LockInfo));
			LockInfo.l_type = F_RDLCK;
			LockInfo.l_whence = SEEK_SET;
			LockInfo.l_start = 0;
			LockInfo.l_len = 1;
			while ((Result = fcntl(m_LockFileHandle, F_SETLK, &LockInfo)) && (Result = DErrCode()) == EINTR);
			*_pExclusive = Result ? -1 : 0; // error may mean we lost the lock
		}
#endif

		return Result;
	}

	/** Try to get exclusive lock, otherwise shared.
	 *	Maintain *_pExclusive = -1: no/unknown lock, 0: shared, 1: exclusive.
	 */
	int DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_ExclLock(int *_pExclusive)
	{
		int Result = 0;
#ifdef DPlatformFamily_Windows
		if (LockFile(m_LockFileHandle, 0, 0, 1, 0))
			*_pExclusive = 1;
		else
		{
			OVERLAPPED Overlapped;
			memset(&Overlapped, 0, sizeof(Overlapped));
			if (LockFileEx(m_LockFileHandle, 0, 0, 1, 0, &Overlapped))
				*_pExclusive = 0;
			else
				Result = DErrCode();
		}
#else
		struct flock LockInfo;
		memset((void *)&LockInfo, 0, sizeof(LockInfo));
		LockInfo.l_type = F_WRLCK;
		LockInfo.l_whence = SEEK_SET;
		LockInfo.l_start = 0;
		LockInfo.l_len = 1;
		while ((Result = fcntl(m_LockFileHandle, F_SETLK, &LockInfo)) && (Result = DErrCode()) == EINTR);
		if (!Result)
			*_pExclusive = 1;
		else
#	ifndef DMdbUsePosixMutex
		if (*_pExclusive < 0) // always true when DMdbUsePosixMutex
#	endif
		{
			LockInfo.l_type = F_RDLCK;
			while ((Result = fcntl(m_LockFileHandle, F_SETLKW, &LockInfo)) && (Result = DErrCode()) == EINTR);
			if (Result == 0)
				*_pExclusive = 0;
		}
#endif
		return Result;
	}
}
