// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDatabase::NPrivate
{
#ifndef DPlatformFamily_Windows
#	ifdef MDB_USE_POSIX_SEM
	inline_always static int fg_SemWait(sem_t *_pSemaphore)
	{
		int Result;
		while ((Result = sem_wait(_pSemaphore)) && (Result = errno) == EINTR);
		return Result;
	}
#	elif defined DMdbUseSysvSem
	inline_always static int fg_SemWait(CMdbMutexRef _pSemaphore)
	{
		int Result, *pLocked = _pSemaphore->m_pLocked;
		struct sembuf SemaphoreOperation = {0, -1, SEM_UNDO};
		SemaphoreOperation.sem_num = _pSemaphore->m_SemaphoreNumber;
		do
		{
			if (!semop(_pSemaphore->m_SemaphoreID, &SemaphoreOperation, 1))
			{
				Result = *pLocked ? DMdbOwnerdead : DMdbSuccess;
				*pLocked = 1;
				break;
			}
		}
		while ((Result = errno) == EINTR);
		return Result;
	}
#	endif
#endif
}
