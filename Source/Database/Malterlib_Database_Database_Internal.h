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

#pragma once

#ifdef DMibDebug
#	define DMibDatabaseDebug 3
#endif

#include <Mib/Core/Core>
#include "Malterlib_Database_Database_IDList.h"

#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>

#ifndef _GNU_SOURCE
#	define _GNU_SOURCE 1 // NOLINT(malterlib-macro-naming)
#endif
#if defined(DMibDatabase_Always64BitSupport) || defined(__WIN64__)
#	define _FILE_OFFSET_BITS	64 // NOLINT(malterlib-macro-naming)
#endif
#ifdef DPlatformFamily_Windows
#	include <windows.h>
#	include <wchar.h>

/* We use native NT APIs to setup the memory map, so that we can
 * let the DB file grow incrementally instead of always preallocating
 * the full size. These APIs are defined in <wdm.h> and <ntifs.h>
 * but those headers are meant for driver-level development and
 * conflict with the regular user-level headers, so we explicitly
 * declare them here. We get pointers to these functions from
 * NTDLL.DLL at runtime, to avoid buildtime dependencies on any
 * NTDLL import libraries.
 */
// NOLINTBEGIN(malterlib-type-naming): the names in the Windows headers
typedef NTSTATUS (WINAPI NtCreateSectionFunc)
	(
		OUT PHANDLE o_pSection
		, IN ACCESS_MASK _DesiredAccess
		, IN void * _pObjectAttributes OPTIONAL
		, IN PLARGE_INTEGER _pMaximumSize OPTIONAL
		, IN ULONG _SectionPageProtection
		, IN ULONG _AllocationAttributes
		, IN HANDLE _pFile OPTIONAL
	)
;

typedef enum _SECTION_INHERIT
{
	ViewShare   = 1
	, ViewUnmap = 2
} SECTION_INHERIT;

typedef NTSTATUS (WINAPI NtMapViewOfSectionFunc)
	(
		IN HANDLE _pSection
		, IN HANDLE _pProcess
		, IN OUT PVOID *o_pBaseAddress
		, IN ULONG_PTR _ZeroBits
		, IN SIZE_T _CommitSize
		, IN OUT PLARGE_INTEGER _pSectionOffset OPTIONAL
		, IN OUT PSIZE_T _pViewSize
		, IN SECTION_INHERIT _InheritDisposition
		, IN ULONG _AllocationType
		, IN ULONG _Win32Protect
	)
;

typedef NTSTATUS (WINAPI NtCloseFunc)(HANDLE _pHandle);
// NOLINTEND(malterlib-type-naming)

/** getpid() returns int; MinGW defines pid_t but MinGW64 typedefs it
 *  as int64 which is wrong. MSVC doesn't define it at all, so just
 *  don't use it.
 */
using CProcessID = int;
using CThreadID = DWORD;
#	include <sys/types.h>
#	include <sys/stat.h>
#	ifdef __GNUC__
#		include <sys/param.h>
#	else
#		define LITTLE_ENDIAN	1234 // NOLINT(malterlib-macro-naming)
#		define BIG_ENDIAN	4321 // NOLINT(malterlib-macro-naming)
#		define BYTE_ORDER	LITTLE_ENDIAN // NOLINT(malterlib-macro-naming)
#		ifndef SSIZE_MAX
#			define SSIZE_MAX	INT_MAX // NOLINT(malterlib-macro-naming)
#		endif
#	endif
#else
#	include <sys/types.h>
#	include <sys/stat.h>
using CProcessID = pid_t;
using CThreadID = pthread_t;
#	include <sys/param.h>
#	include <sys/uio.h>
#	include <sys/mman.h>
#	ifdef HAVE_SYS_FILE_H
#		include <sys/file.h>
#	endif
#	include <fcntl.h>
#endif

#if defined(__mips) && defined(__linux)
/* MIPS has cache coherency issues, requires explicit cache control */
#	include <asm/cachectl.h>
extern int cacheflush(char *addr, int nbytes, int cache);
#	define DCacheFlush(d_Addr, d_Bytes, d_Cache)	cacheflush(d_Addr, d_Bytes, d_Cache)
#else
#	define DCacheFlush(d_Addr, d_Bytes, d_Cache)
#endif

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#	include <io.h>
#else
#	include <unistd.h>
#endif

#if defined (__sun)
#	define _POSIX_PTHREAD_SEMANTICS	1 // NOLINT(malterlib-macro-naming)
#endif

#if !(defined(BYTE_ORDER) || defined(__BYTE_ORDER))
#	include <netinet/in.h>
#	include <resolv.h>	/* defines BYTE_ORDER on HPUX and Solaris */
#endif

#if defined(__APPLE__) || defined (BSD) || defined(__FreeBSD_kernel__)
#	if !(defined(DMdbUsePosixMutex) || defined(MDB_USE_POSIX_SEM))
#		define DMdbUseSysvSem	1
#	endif
#	define DMdbFDataSync		fsync
#elif defined(__ANDROID__)
#	define DMdbFDataSync		fsync
#endif

#ifndef DPlatformFamily_Windows
#	include <pthread.h>
#	include <signal.h>
#	ifdef MDB_USE_POSIX_SEM
#		define DMdbUseHash		1
#		include <semaphore.h>
#	elif defined(DMdbUseSysvSem)
#		include <sys/ipc.h>
#		include <sys/sem.h>
#		ifdef _SEM_SEMUN_UNDEFINED
union semun
{
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};
#		endif /* _SEM_SEMUN_UNDEFINED */
#	else
#		define DMdbUsePosixMutex	1
#	endif /* MDB_USE_POSIX_SEM */
#endif /* !DPlatformFamily_Windows */

#if defined(DPlatformFamily_Windows) + defined(MDB_USE_POSIX_SEM) + defined(DMdbUseSysvSem) \
	+ defined(DMdbUsePosixMutex) != 1
#	error "Ambiguous shared-lock implementation"
#endif

#ifndef BYTE_ORDER
#	if (defined(_LITTLE_ENDIAN) || defined(_BIG_ENDIAN)) && !(defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN))
/* Solaris just defines one or the other */
#		define LITTLE_ENDIAN	1234 // NOLINT(malterlib-macro-naming)
#		define BIG_ENDIAN	4321 // NOLINT(malterlib-macro-naming)
#		ifdef _LITTLE_ENDIAN
#			define BYTE_ORDER  LITTLE_ENDIAN // NOLINT(malterlib-macro-naming)
#		else
#			define BYTE_ORDER  BIG_ENDIAN // NOLINT(malterlib-macro-naming)
#		endif
#	else
#		define BYTE_ORDER   __BYTE_ORDER // NOLINT(malterlib-macro-naming)
#	endif
#endif

#ifndef LITTLE_ENDIAN
#	define LITTLE_ENDIAN	__LITTLE_ENDIAN // NOLINT(malterlib-macro-naming)
#endif
#ifndef BIG_ENDIAN
#	define BIG_ENDIAN	__BIG_ENDIAN // NOLINT(malterlib-macro-naming)
#endif

#if defined(__i386) || defined(__x86_64) || defined(_M_IX86)
#	define DMisalignedOk	1
#endif

namespace NMib::NDatabase
{
	///	\enum Write Flags
	enum EDatabaseWriteFlag : uint32
	{
		EDatabaseWriteFlag_None                          = 0
		, EDatabaseWriteFlag_NoOverwrite                 = 0x10    ///< For put: Don't write if the key already exists.
		, EDatabaseWriteFlag_OverwriteAtCurrentCursorPos = 0x40    /// For fg_CursorPut: overwrite the current key/data pair
		, EDatabaseWriteFlag_ReserveSpace                = 0x10000 ///< For put: Just reserve space for data, don't copy it. Return a
		                                                           ///<  pointer to the reserved space.
		, EDatabaseWriteFlag_Append                      = 0x20000 ///< Data is being appended, don't split full pages.

		// Dup data options
		, EDatabaseWriteFlag_NoDuplicateData     = 0x20    ///< Only for #EDatabaseSubFlag_SortedDuplicates<br>
		                                                   ///< For put: Don't write if the key and data pair already exist.<br>
		                                                   ///< For fg_CursorDel: Remove all duplicate data items.
		, EDatabaseWriteFlag_AppendDuplicateData = 0x40000 ///< Duplicate data is being appended, don't split full pages.
		, EDatabaseWriteFlag_BatchDuplicateData  = 0x80000 ///< Store multiple data items in one call. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize.
	};

	namespace NPrivate
	{
#ifdef DPlatformFamily_Windows
		extern NtCreateSectionFunc *g_fDatabase_NtCreateSection;
		extern NtMapViewOfSectionFunc *g_fDatabase_NtMapViewOfSection;
		extern NtCloseFunc *g_fDatabase_NtClose;
#endif

		/** @defgroup  errors	Return Codes
		 *
		 *	BerkeleyDB uses -30800 to -30999, we'll go under them
		 *	@{
		 */
		/**	Successful result */
#define DMdbSuccess	 0
		/** key/data pair already exists */
#define DMdbKeyExist	(-30799)
		/** key/data pair not found (EOF) */
#define DMdbNotFound	(-30798)
		/** Requested page not found - this usually indicates corruption */
#define DMdbPageNotfound	(-30797)
		/** Located page was wrong type */
#define DMdbCorrupted	(-30796)
		/** Update of meta page failed or environment had fatal error */
#define DMdbPanic		(-30795)
		/** Environment version mismatch */
#define DMdbVersionMismatch	(-30794)
		/** File is not a valid LMDB file */
#define DMdbInvalid	(-30793)
		/** Environment mapsize reached */
#define DMdbMapFull	(-30792)
		/** Environment maxdbs reached */
#define DMdbDbsFull	(-30791)
		/** Environment maxreaders reached */
#define DMdbReadersFull	(-30790)
		/** Too many TLS keys in use - Windows only */
#define DMdbTlsFull	(-30789)
		/** Txn has too many dirty pages */
#define DMdbTxnFull	(-30788)
		/** Cursor stack too deep - internal error */
#define DMdbCursorFull	(-30787)
		/** Page has not enough space - internal error */
#define DMdbPageFull	(-30786)
		/** Database contents grew beyond environment mapsize */
#define DMdbMapResized	(-30785)
		/** Operation and DB incompatible, or DB type changed. This can mean:
		 *	<ul>
		 *	<li>The operation expects an #EDatabaseSubFlag_SortedDuplicates / #EDatabaseSubFlag_SortedDuplicatesFixedSize database.
		 *	<li>Opening a named DB when the unnamed DB has #EDatabaseSubFlag_SortedDuplicates / #EDatabaseSubFlag_IntegerKeys.
		 *	<li>Accessing a data record as a database, or vice versa.
		 *	<li>The database was dropped and recreated with different flags.
		 *	</ul>
		 */
#define DMdbIncompatible	(-30784)
		/** Invalid reuse of reader locktable slot */
#define DMdbBadRslot		(-30783)
		/** Transaction must abort, has a child, or is invalid */
#define DMdbBadTxn			(-30782)
		/** Unsupported size of key/DB name/data, or wrong DUPFIXED size */
#define DMdbBadValsize		(-30781)
		/** The specified DBI was changed unexpectedly */
#define DMdbBadDbi		(-30780)
		/** Unexpected problem - txn should abort */
#define DMdbProblem		(-30779)
		/** The last defined error code */
#define DMdbLastErrcode	DMdbProblem
		/** @} */

		using CMdbEnv = CDatabaseEnvironment_Internal;
		using CMdbTxn = CDatabaseTransactionCommon::CInternal;
		using CMdbCursor = CDatabaseCursorCommon::CInternal;

#if (BYTE_ORDER == LITTLE_ENDIAN) == (BYTE_ORDER == BIG_ENDIAN)
#	error "Unknown or unsupported endianness (BYTE_ORDER)"
#elif (-6 & 5) || CHAR_BIT!=8 || UINT_MAX!=0xffffffff
#	error "Two's complement, reasonably sized integer types, please"
#endif

		// Disable for now, because llvm generated incorrect exception headers
#if defined(DMibDebug) || defined(DArchitecture_arm64) || defined(DArchitecture_arm64e) || 1
#	define DMibDatabaseInfrequentSection
#else
#	ifdef __GNUC__
			/** Put infrequently used env functions in separate section */
#		ifdef __APPLE__
#			define	DMibDatabaseInfrequentSection	__attribute__ ((section("__TEXT,text_env")))
#		else
#			define	DMibDatabaseInfrequentSection	__attribute__ ((section("text_env")))
#		endif
#	else
#		define DMibDatabaseInfrequentSection
#	endif
#endif

#ifdef DPlatformFamily_Windows
#	define DCallConv WINAPI
#else
#	define DCallConv
#endif

		/** @defgroup internal	LMDB Internals
		 *	@{
		 */
		/** @defgroup compat	Compatibility Macros
		 *	A bunch of macros to minimize the amount of platform-specific ifdefs
		 *	needed throughout the rest of the code. When the features this library
		 *	needs are similar enough to POSIX to be hidden in a one-or-two line
		 *	replacement, this macro approach is used.
		 *	@{
		 */

			/** Features under development */
#ifndef DMdbDevel
#	define DMdbDevel 0
#endif

			/** Wrapper around __func__, which is a C99 feature */
#if __STDC_VERSION__ >= 199901L
#	define mdb_func_	__func__
#elif __GNUC__ >= 2 || _MSC_VER >= 1300
#	define mdb_func_	__FUNCTION__
#else
		/* If a debug message says <mdb_unknown>(), update the #if statements above */
#	define mdb_func_	"<mdb_unknown>"
#endif

		/* Internal error codes, not exposed outside liblmdb */
#define	DMdbNoRoot		(DMdbLastErrcode + 10)
#ifdef DPlatformFamily_Windows
#	define DMdbOwnerdead	((int) WAIT_ABANDONED)
#elif defined DMdbUseSysvSem
#	define DMdbOwnerdead	(DMdbLastErrcode + 11)
#elif defined(DMdbUsePosixMutex) && defined(EOWNERDEAD)
#	define DMdbOwnerdead	EOWNERDEAD	/**< #DLockMutex0() result if dead owner */
#endif

#ifdef __GLIBC__
#	define	DGlibcVer	((__GLIBC__ << 16 )| __GLIBC_MINOR__)
#endif
		/** Some platforms define the EOWNERDEAD error code
		 * even though they don't support Robust Mutexes.
		 * Compile with -DMDB_USE_ROBUST=0, or use some other
		 * mechanism like -DMDB_USE_SYSV_SEM instead of
		 * -DMDB_USE_POSIX_MUTEX. (SysV semaphores are
		 * also Robust, but some systems don't support them
		 * either.)
		 */
#ifndef DMdbUseRobust
		/* Android currently lacks Robust Mutex support. So does glibc < 2.4. */
#	if defined(DMdbUsePosixMutex) && (defined(__ANDROID__) || \
		(defined(__GLIBC__) && DGlibcVer < 0x020004))
#		define DMdbUseRobust	0
#	else
#		define DMdbUseRobust	1
#	endif
#endif /* !DMdbUseRobust */

#if defined(DMdbUsePosixMutex) && (DMdbUseRobust)
		/* glibc < 2.12 only provided _np API */
#	if (defined(__GLIBC__) && DGlibcVer < 0x02000c) || \
		(defined(PTHREAD_MUTEX_ROBUST_NP) && !defined(PTHREAD_MUTEX_ROBUST))
#		define PTHREAD_MUTEX_ROBUST	PTHREAD_MUTEX_ROBUST_NP // NOLINT(malterlib-macro-naming)
#		define pthread_mutexattr_setrobust(d_Attr, d_Flag)	pthread_mutexattr_setrobust_np(d_Attr, d_Flag)
#		define pthread_mutex_consistent(d_Mutex)	pthread_mutex_consistent_np(d_Mutex)
#	endif
#endif /* DMdbUsePosixMutex && DMdbUseRobust */

#if defined(DMdbOwnerdead) && (DMdbUseRobust)
#	define DMdbRobustSupported	1
#endif

#ifdef DPlatformFamily_Windows
#	define DMdbUseHash	1
#	define DMdbPidLock	0
#	define pthread_t	HANDLE
#	define pthread_mutex_t	HANDLE
#	define pthread_cond_t	HANDLE
		typedef HANDLE CMdbMutexType, CMdbMutexRef;
#	define pthread_key_t	DWORD
#	define pthread_self()	GetCurrentThreadId()
#	define pthread_key_create(d_Key,d_Destructor)	\
		((*(d_Key) = TlsAlloc()) == TLS_OUT_OF_INDEXES ? DErrCode() : 0)
#	define pthread_key_delete(d_Key)	TlsFree(d_Key)
#	define pthread_getspecific(d_Key)	TlsGetValue(d_Key)
#	define pthread_setspecific(d_Key,d_Value)	(TlsSetValue(d_Key,d_Value) ? 0 : DErrCode())
#	define pthread_mutex_unlock(d_Mutex)	ReleaseMutex(*d_Mutex)
#	define pthread_mutex_lock(d_Mutex)	WaitForSingleObject(*d_Mutex, INFINITE)
#	define pthread_cond_signal(d_Condition)	SetEvent(*d_Condition)
#	define pthread_cond_wait(d_Cond,d_Mutex)	do{SignalObjectAndWait(*d_Mutex, *d_Cond, INFINITE, FALSE); WaitForSingleObject(*d_Mutex, INFINITE);}while(0)
#	define DLockMutex0(d_Mutex)		WaitForSingleObject(d_Mutex, INFINITE)
#	define DUnlockMutex(d_Mutex)		ReleaseMutex(d_Mutex)
#	define mdb_mutex_consistent(d_Mutex)	0
#	define getpid()	GetCurrentProcessId()
#	define	DMdbFDataSync(d_Fd)	(!FlushFileBuffers(d_Fd))
#	define	DMdbMsync(d_Addr,d_Len,d_Flags)	(!FlushViewOfFile(d_Addr,d_Len))
#	define	DErrCode()	GetLastError()
#	define	close(d_Fd)	(CloseHandle(d_Fd) ? 0 : -1)
#	define	munmap(d_Ptr,d_Len)	UnmapViewOfFile(d_Ptr)
#	ifdef PROCESS_QUERY_LIMITED_INFORMATION
#		define DMdbProcessQueryLimitedInformation PROCESS_QUERY_LIMITED_INFORMATION
#	else
#		define DMdbProcessQueryLimitedInformation 0x1000
#	endif
#else

		/** For DMdbLockFormat: True if readers take a pid lock in the lockfile */
#	define DMdbPidLock			1

#	ifdef MDB_USE_POSIX_SEM

		typedef sem_t *CMdbMutexType, *CMdbMutexRef;
#		define DLockMutex0(d_Mutex)		fg_SemWait(d_Mutex)
#		define DUnlockMutex(d_Mutex)		sem_post(d_Mutex)

		inline_always static int fg_SemWait(sem_t *_pSemaphore)
		{
			int Result;
			while ((Result = sem_wait(_pSemaphore)) && (Result = errno) == EINTR);
			return Result;
		}

#	elif defined DMdbUseSysvSem

		struct CMdbMutex
		{
			int m_SemaphoreID;
			int m_SemaphoreNumber;
			int *m_pLocked;
		};

		using CMdbMutexRef = CMdbMutex *;
		using CMdbMutexType = CMdbMutex[1];

#		define DLockMutex0(d_Mutex)		fg_SemWait(d_Mutex)
#		define DUnlockMutex(d_Mutex)		do { \
				struct sembuf SemaphoreOperation = { 0, 1, SEM_UNDO }; \
				SemaphoreOperation.sem_num = (d_Mutex)->m_SemaphoreNumber; \
				*(d_Mutex)->m_pLocked = 0; \
				semop((d_Mutex)->m_SemaphoreID, &SemaphoreOperation, 1); \
			} while(0)

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

#		define mdb_mutex_consistent(d_Mutex)	0

#	else	/* DMdbUsePosixMutex: */
		/** Shared mutex/semaphore as the original is stored.
		 *
		 *	Not for copies.  Instead it can be assigned to an #CMdbMutexRef.
		 *	When CMdbMutexRef is a pointer and CMdbMutexType is not, then it
		 *	is array[size 1] so it can be assigned to the pointer.
		 */
		typedef pthread_mutex_t CMdbMutexType[1];
		/** Reference to an #CMdbMutexType */
		typedef pthread_mutex_t *CMdbMutexRef;
		/** Lock the reader or writer mutex.
		 *	Returns 0 or a code to give #f_MutexFailed(), as in #DLockMutex().
		 */
#		define DLockMutex0(d_Mutex)	pthread_mutex_lock(d_Mutex)
		/** Unlock the reader or writer mutex.
		 */
#		define DUnlockMutex(d_Mutex)	pthread_mutex_unlock(d_Mutex)
		/** Mark mutex-protected data as repaired, after death of previous owner.
		 */
#		define mdb_mutex_consistent(d_Mutex)	pthread_mutex_consistent(d_Mutex)
#	endif	/* MDB_USE_POSIX_SEM || DMdbUseSysvSem */

		/** Get the error code for the last failed system function.
		 */
#	define	DErrCode()	errno

		/** An abstraction for a file handle.
		 *	On POSIX systems file handles are small integers. On Windows
		 *	they're opaque pointers.
		 */
#	define	HANDLE	int // NOLINT(malterlib-macro-naming)

		/**	A value for an invalid file handle.
		 *	Mainly used to initialize file variables and signify that they are
		 *	unused.
		 */
#	define INVALID_HANDLE_VALUE	(-1) // NOLINT(malterlib-macro-naming)

#endif

#ifdef DMdbUseSysvSem
#	define DMnameLen	(sizeof(int))
#else
#	define DMnameLen	(sizeof(pthread_mutex_t))
#endif

		/** Initial part of #CMdbEnv.m_MutexName[].
		 *	Changes to this code must be reflected in #DMdbLockFormat.
		 */
#ifdef DPlatformFamily_Windows
#	define DMutexNamePrefix		"Global\\MDB"
#elif defined MDB_USE_POSIX_SEM
#	define DMutexNamePrefix		"/MDB"
#endif

		/** @} */

#ifdef DMdbRobustSupported
		/** Lock mutex, handle any error, set rc = result.
		 *	Return 0 on success, nonzero (not rc) on error.
		 */
#	define DLockMutex(d_Rc, d_Env, d_Mutex) \
		(((d_Rc) = DLockMutex0(d_Mutex)) && \
		 ((d_Rc) = d_Env->f_MutexFailed(d_Mutex, d_Rc)))
#else
#	define DLockMutex(d_Rc, d_Env, d_Mutex) ((d_Rc) = DLockMutex0(d_Mutex))
#endif

#ifndef DPlatformFamily_Windows
		/**	A flag for opening a file and requesting synchronous data writes.
		 *	This is only used when writing a meta page. It's not strictly needed;
		 *	we could just do a normal write and then immediately perform a flush.
		 *	But if this flag is available it saves us an extra system call.
		 *
		 *	@note If O_DSYNC is undefined but exists in /usr/include,
		 * preferably set some compiler flag to get the definition.
		 */
#	ifndef DMdbDSync
#		ifdef O_DSYNC
#			define DMdbDSync	O_DSYNC
#		else
#			define DMdbDSync	O_SYNC
#		endif
#	endif
#endif

		/** Function for flushing the data of a file. Define this to fsync
		 *	if fdatasync() is not supported.
		 */
#ifndef DMdbFDataSync
#	define DMdbFDataSync	fdatasync
#endif

#ifndef DMdbMsync
#	define DMdbMsync(d_Addr,d_Len,d_Flags)	msync(d_Addr,d_Len,d_Flags)
#endif

#ifndef MS_SYNC
#	define	MS_SYNC	1 // NOLINT(malterlib-macro-naming)
#endif

#ifndef MS_ASYNC
#	define	MS_ASYNC	0 // NOLINT(malterlib-macro-naming)
#endif

		/** A page number in the database.
		 *	Note that 64 bit page numbers are overkill, since pages themselves
		 *	already represent 12-13 bits of addressable memory, and the OS will
		 *	always limit applications to a maximum of 63 bits of address space.
		 *
		 *	@note In the #CMdbNode structure, we only store 48 bits of this value,
		 *	which thus limits us to only 60 bits of addressable data.
		 */
		typedef CMdbId CPgno;

		/** A transaction ID.
		 *	See struct CMdbTxn.m_TransactionID for details.
		 */
		using CTransactionID = CMdbId;

		/** @defgroup debug	Debug Macros
		 *	@{
		 */
#ifndef DMibDatabaseDebug
		/**	Enable debug output.  Needs variable argument macros (a C99 feature).
		 *	Set this to 1 for copious tracing. Set to 2 to add dumps of all IDLs
		 *	read from and written to the database (used for free space management).
		 */
#	define DMibDatabaseDebug 0
#endif

#if DMibDatabaseDebug
		extern int g_bDebug;
		extern CTransactionID g_DebugStartTransactionID;

		/**	Print a debug message with printf formatting.
		 *	Requires double parenthesis around 2 or more args.
		 */
#	define DMibDatabaseTraceMSVC(d_Expand) d_Expand
#	define DMibDatabaseTrace(...) do { if (NPrivate::g_bDebug) DMibDatabaseTraceMSVC( DMibDatabaseTrace0(__VA_ARGS__) ); } while (false)
#	define DMibDatabaseTrace0(d_Format, ...) DMibTrace(DMibPFileLineFormat d_Format "{\n}", DMibPFile, DMibPLine, __VA_ARGS__)
#else
#	define DMibDatabaseTrace(...)	((void) 0)
#endif
		/**	Print a debug string.
		 *	The string is printed literally, with no format processing.
		 */
#define DPuts(d_Arg)	DMibDatabaseTrace("{}", d_Arg)
		/** Debuging output value of a cursor DBI: Negative in a sub-cursor. */
#define DDbi(d_Mc) \
	(((d_Mc)->m_Flags & DCSub) ? -(int)(d_Mc)->m_DatabaseID : (int)(d_Mc)->m_DatabaseID)
		/** @} */

		/**	@brief The maximum size of a database page.
		 *
		 *	It is 32k or 64k, since value-DPageBase must fit in
		 *	#CMdbPage.%m_FreeSpace.m_Upper.
		 *
		 *	LMDB will use database pages < OS pages if needed.
		 *	That causes more I/O in write transactions: The OS must
		 *	know (read) the whole page before writing a partial page.
		 *
		 *	Note that we don't currently support Huge pages. On Linux,
		 *	regular data files cannot use Huge pages, and in general
		 *	Huge pages aren't actually pageable. We rely on the OS
		 *	demand-pager to read our data and page it out when memory
		 *	pressure from other processes is high. So until OSs have
		 *	actual paging support for Huge pages, they're not viable.
		 */
#define DMaxPageSize	 (DPageBase ? 0x10000 : 0x8000)

		/** The minimum number of keys required in a database page.
		 *	Setting this to a larger value will place a smaller bound on the
		 *	maximum size of a data item. Data items larger than this size will
		 *	be pushed into overflow pages instead of being stored directly in
		 *	the B-tree node. This value used to default to 4. With a page size
		 *	of 4096 bytes that meant that any item larger than 1024 bytes would
		 *	go into an overflow page. That also meant that on average 2-3KB of
		 *	each overflow page was wasted space. The value cannot be lower than
		 *	2 because then there would no longer be a tree structure. With this
		 *	value, items larger than 2KB will go into overflow pages, and on
		 *	average only 1KB will be wasted.
		 */
#define DMdbMinKeys	 2

		/**	A stamp that identifies a file as an LMDB file.
		 *	There's nothing special about this value other than that it is easily
		 *	recognizable, and it will reflect any byte order mismatches.
		 */
#define DMdbMagic	 0xBEEFC0DE

		/**	The version number for a database's datafile format. */
#define DMdbDataVersion	 ((DMdbDevel) ? 999 : 1)
		/**	The version number for a database's lockfile format. */
#define DMdbLockVersion	 ((DMdbDevel) ? 999 : 2)
		/** Number of bits representing #DMdbLockVersion in #DMdbLockFormat.
		 *	The remaining bits must leave room for #gc_MdbLockDesc.
		 */
#define DMdbLockVersionBits 12

			/**	@brief The max size of a key we can write, or 0 for computed max.
			 *
			 *	This macro should normally be left alone or set to 0.
			 *	Note that a database with big keys or dupsort data cannot be
			 *	reliably modified by a liblmdb which uses a smaller max.
			 *	The default is 511 for backwards compat, or 0 when #DMdbDevel.
			 *
			 *	Other values are allowed, for backwards compat.  However:
			 *	A value bigger than the computed max can break if you do not
			 *	know what you are doing, and liblmdb <= 0.9.10 can break when
			 *	modifying a DB with keys/dupsort data bigger than its max.
			 *
			 *	Data items in an #EDatabaseSubFlag_SortedDuplicates database are also limited to
			 *	this size, since they're actually keys of a sub-DB.  Keys and
			 *	#EDatabaseSubFlag_SortedDuplicates data items must fit on a node in a regular page.
			 */
#ifndef DMdbMaxKeySize
//#define DMdbMaxKeySize	 ((DMdbDevel) ? 0 : 511)
#	define DMdbMaxKeySize	 0
#endif

			/**	The maximum size of a key we can write to the environment. */
#if DMdbMaxKeySize
#	define DEnvMaxkey(d_Env)	(DMdbMaxKeySize)
#else
#	define DEnvMaxkey(d_Env)	((d_Env)->m_MaxKeySize)
#endif

		/**	@brief The maximum size of a data item.
		 *
		 *	We only store a 32 bit value for node sizes.
		 */
#define DMaxDataSize	0xffffffffUL

#if DMibDatabaseDebug
		/**	Key size which fits in a #DKBuf.
		 *	@ingroup debug
		 */
#	define DKBufMaxKeySize ((DMdbMaxKeySize) > 0 ? (DMdbMaxKeySize) : 511)
		/**	A key buffer.
		 *	@ingroup debug
		 *	This is used for printing a hex dump of a key's contents.
		 */
#	define DKBuf	char KeyBuffer[DKBufMaxKeySize*2+1]
		/**	Display a key in hex.
		 *	@ingroup debug
		 *	Invoke a function to display a key in hex.
		 */
#	define	DKey(d_Key)	fg_DKey(d_Key, KeyBuffer)
#else
#	define	DKBuf
#	define DKey(d_Key)	0
#endif

		/** An invalid page number.
		 *	Mainly used to denote an empty tree.
		 */
#define DPInvalid	 (~(CPgno)0)

		/** Test if the flags \b f are set in a flag word \b w. */
#define DFIsSet(d_Word, d_Flag)	 (((d_Word) & (d_Flag)) == (d_Flag))

		/** Round \b n up to an even number. */
#define DEven(d_Number)		(((d_Number) + 1U) & -2) /* sign-extending -2 to match n+1U */

		/** Least significant 1-bit of \b n.  n must be of an unsigned type. */
#define DLowBit(d_Number)		((d_Number) & (-(d_Number)))

		/** (log2(\b p2) % \b n), for p2 = power of 2 and 0 < n < 8. */
#define DLog2Mod(d_P2, d_Number)	(7 - 86 / ((d_P2) % ((1U<<(d_Number))-1) + 11))
		/* Explanation: Let p2 = 2**(n*y + x), x<n and M = (1U<<n)-1. Now p2 =
		 * (M+1)**y * 2**x = 2**x (mod M). Finally "/" "happens" to return 7-x.
		 */

		/**	Used for offsets within a single page.
		 *	Since memory pages are typically 4 or 8KB in size, 12-13 bits,
		 *	this is plenty.
		 */
		typedef uint16 CIndx;

		typedef unsigned long long CMdbHash;

		CMdbHash fg_Hash(void const *_pData, size_t _nBytes);
		void fg_EnvMNameInit(CMdbEnv *_pEnvironment);
#define DMutexName(d_Env, d_Ch) ( \
	(void) ((d_Env)->m_MutexName[sizeof(DMutexNamePrefix)-1] = (d_Ch)), \
	(d_Env)->m_MutexName)

#ifdef DPlatformFamily_Windows
		/** @brief Map a result from an NTAPI call to WIN32. */
		DWORD fg_Nt2Win32(NTSTATUS _Status);
#endif

		/**	Default size of memory map.
		 *	This is certainly too small for any actual applications. Apps should always set
		 *	the size explicitly using #f_SetMapSize().
		 */
#define DDefaultMapSize	1048576

		/**	@defgroup readers	Reader Lock Table
		 *	Readers don't acquire any locks for their data access. Instead, they
		 *	simply record their transaction ID in the reader table. The reader
		 *	mutex is needed just to find an empty slot in the reader table. The
		 *	slot's address is saved in thread-specific data so that subsequent read
		 *	transactions started by the same thread need no further locking to proceed.
		 *
		 *	If #EDatabaseEnvironmentFlag_NoTls is set, the slot address is not saved in thread-specific data.
		 *
		 *	No reader table is used if the database is on a read-only filesystem, or
		 *	if #EDatabaseEnvironmentFlag_NoLock is set.
		 *
		 *	Since the database uses multi-version concurrency control, readers don't
		 *	actually need any locking. This table is used to keep track of which
		 *	readers are using data from which old transactions, so that we'll know
		 *	when a particular old transaction is no longer in use. Old transactions
		 *	that have discarded any data pages can then have those pages reclaimed
		 *	for use by a later write transaction.
		 *
		 *	The lock table is constructed such that reader slots are aligned with the
		 *	processor's cache line size. Any slot is only ever used by one thread.
		 *	This alignment guarantees that there will be no contention or cache
		 *	thrashing as threads update their own slot info, and also eliminates
		 *	any need for locking when accessing a slot.
		 *
		 *	A writer thread will scan every slot in the table to determine the oldest
		 *	outstanding reader transaction. Any freed pages older than this will be
		 *	reclaimed by the writer. The writer doesn't use any locks when scanning
		 *	this table. This means that there's no guarantee that the writer will
		 *	see the most up-to-date reader info, but that's not required for correct
		 *	operation - all we need is to know the upper bound on the oldest reader,
		 *	we don't care at all about the newest reader. So the only consequence of
		 *	reading stale information here is that old pages might hang around a
		 *	while longer before being reclaimed. That's actually good anyway, because
		 *	the longer we delay reclaiming old pages, the more likely it is that a
		 *	string of contiguous pages can be found after coalescing old pages from
		 *	many old transactions together.
		 *	@{
		 */
		/**	Number of slots in the reader table.
		 *	This value was chosen somewhat arbitrarily. 126 readers plus a
		 *	couple mutexes fit exactly into 8KB on my development machine.
		 *	Applications should set the table size using #f_SetMaxReaders().
		 */
#define DDefaultReaders	126

		/**	The information we store in a single slot of the reader table.
		 *	In addition to a transaction ID, we also record the process and
		 *	thread ID that owns a slot, so that we can detect stale information,
		 *	e.g. threads or processes that went away without cleaning up.
		 *	@note We currently don't check for stale records. We simply re-init
		 *	the table when we know that we're the only process opening the
		 *	lock file.
		 */
		struct align_cacheline CMdbReader
		{
			/**	Current Transaction ID when this transaction began, or (CTransactionID)-1.
			 *	Multiple readers that start at the same time will probably have the
			 *	same ID here. Again, it's not important to exclude them from
			 *	anything; all we need to know is which version of the DB they
			 *	started from so we can avoid overwriting any data used in that
			 *	particular version.
			 */
			NAtomic::TCAtomic<CTransactionID> m_BeginReadTransactionID;
			/** The process ID of the process owning this reader txn. */
			NAtomic::TCAtomic<CProcessID> m_OwningProcessID;
			/** The thread ID of the thread owning this txn. */
			NAtomic::TCAtomic<CThreadID> m_OwningThreadID; // NOLINT(malterlib-conceptual-prefix): pthread_t is a pointer on macOS and an integer on Linux
		};

		/** The header for the reader table.
		 *	The table resides in a memory-mapped file. (This is a different file
		 *	than is used for the main database.)
		 *
		 *	For POSIX the actual mutexes reside in the shared memory of this
		 *	mapped file. On Windows, mutexes are named objects allocated by the
		 *	kernel; we store the mutex names in this mapped file so that other
		 *	processes can grab them. This same approach is also used on
		 *	macOS/Darwin (using named semaphores) since macOS doesn't support
		 *	process-shared POSIX mutexes. For these cases where a named object
		 *	is used, the object name is derived from a 64 bit FNV hash of the
		 *	environment pathname. As such, naming collisions are extremely
		 *	unlikely. If a collision occurs, the results are unpredictable.
		 */
		struct CMdbTxBody
		{
			/** Stamp identifying this as an LMDB file. It must be set
			 *	to #DMdbMagic. */
			uint32 m_Magic;
			/** Format of this lock file. Must be set to #DMdbLockFormat. */
			uint32 m_Format;
			/**	The ID of the last transaction committed to the database.
			 *	This is recorded here only for convenience; the value can always
			 *	be determined by reading the main database meta pages.
			 */
			NAtomic::TCAtomic<CTransactionID> m_TransactionID;
			/** The number of slots that have been used in the reader table.
			 *	This always records the maximum count, it is not decremented
			 *	when readers release their slots.
			 */
			NAtomic::TCAtomic<uint32> m_NumReaders;
#if defined(DPlatformFamily_Windows) || defined(MDB_USE_POSIX_SEM)
			/** Binary form of names of the reader/writer locks */
			CMdbHash m_MutexID;
#elif defined(DMdbUseSysvSem)
			int m_SemaphoreID;
			int m_ReaderLock;
#else
			/** Mutex protecting access to this table.
			 *	This is the reader table lock used with DLockMutex().
			 */
			CMdbMutexType m_ReaderTableMutex;
#endif
		};

		/** The actual reader table definition. */
		struct CLocksMemoryMap
		{
			align_cacheline CMdbTxBody m_TransactionBody;

#if !(defined(DPlatformFamily_Windows) || defined(MDB_USE_POSIX_SEM))
#	ifdef DMdbUseSysvSem
			align_cacheline int m_WriteLock;
#	else
			align_cacheline CMdbMutexType m_WriteLock;
#	endif
#endif
			CMdbReader m_Readers[1];
		};

		/** Lockfile format signature: version, features and field layout */
#define DMdbLockFormat \
	((uint32_t)         \
	 (((DMdbLockVersion) % (1U << DMdbLockVersionBits)) \
	  + gc_MdbLockDesc     * (1U << DMdbLockVersionBits)))

			/** Lock type and layout. Values 0-119. DPlatformFamily_Windows implies #DMdbPidLock.
			 *	Some low values are reserved for future tweaks.
			 */
#ifdef DPlatformFamily_Windows
#	define DMdbLockType	(0 + alignof(CMdbHash)/8 % 2)
#elif defined MDB_USE_POSIX_SEM
#	define DMdbLockType	(4 + alignof(CMdbHash)/8 % 2)
#elif defined DMdbUseSysvSem
#	define DMdbLockType	(8)
#elif defined DMdbUsePosixMutex
		/* We do not know the inside of a POSIX mutex and how to check if mutexes
		 * used by two executables are compatible. Just check alignment and size.
		 */
#	define DMdbLockType	(10 + \
		DLog2Mod(alignof(pthread_mutex_t), 5) + \
		sizeof(pthread_mutex_t) / 4U % 22 * 5)
#endif

		/** Magic number for lockfile layout and features.
	 *
	 *  This *attempts* to stop liblmdb variants compiled with conflicting
	 *	options from using the lockfile at the same time and thus breaking
	 *	it.  It describes locking types, and sizes and sometimes alignment
	 *	of the various lockfile items.
	 *
	 *	The detected ranges are mostly guesswork, or based simply on how
	 *	big they could be without using more bits.  So we can tweak them
	 *	in good conscience when updating #DMdbLockVersion.
	 */
		static constexpr aint gc_MdbLockDesc =
		/* Default CACHELINE=64 vs. other values (have seen mention of 32-256) */
		(DMibPMemoryCacheLineSize == 64 ? 0 : 1 + DLog2Mod(DMibPMemoryCacheLineSize >> int(DMibPMemoryCacheLineSize > 64), 5))
		+ 6 * (sizeof(CProcessID) / 4 % 3) // legacy(2) to word(4/8)?
		+ 18 * (sizeof(pthread_t) / 4 % 5) // can be struct{id, active data}
		+ 90 * (sizeof(CMdbTxBody) / DMibPMemoryCacheLineSize % 3)
		+ 270 * (DMdbLockType % 120)
		/* The above is < 270*120 < 2**15 */
		+ ((sizeof(CTransactionID) == 8) << 15) // 32bit/64bit
		+ ((sizeof(CMdbReader) > DMibPMemoryCacheLineSize) << 16)
		/* Not really needed - implied by DMdbLockType != (DPlatformFamily_Windows locking) */
		+ (((DMdbPidLock) != 0) << 17)
		/* 18 bits total: Must be <= (32 - DMdbLockVersionBits). */;

		/** @} */

		/** Common header for all page types. The page type depends on #m_Flags.
		 *
		 * #DPBranch and #DPLeaf pages have unsorted '#CMdbNode's at the end, with
		 * sorted #m_NodeOffsets[] entries referring to them. Exception: #DPLeaf2 pages
		 * omit m_NodeOffsets and pack sorted #MDB_DUPFIXED values after the page header.
		 *
		 * #DPOverflow records occupy one or more contiguous pages where only the
		 * first has a page header. They hold the real data of #DFBigData nodes.
		 *
		 * #DPSubp sub-pages are small leaf "pages" with duplicate data.
		 * A node with flag #DFDupData but not #DFSubData contains a sub-page.
		 * (Duplicate data can also go in sub-databases, which use normal pages.)
		 *
		 * #DPMeta pages contain #CMdbMeta, the start point of an LMDB snapshot.
		 *
		 * Each non-metapage up to #CMdbMeta.%m_LastPageNumber is reachable exactly once
		 * in the snapshot: Either used by a database or listed in a freeDB record.
		 */
		struct CMdbPage
		{
			struct CFreeSpace
			{
				CIndx m_Lower; ///< lower bound of free space
				CIndx m_Upper; ///< upper bound of free space
			};

			union
			{
				CPgno m_PageNumber;       ///< page number
				struct CMdbPage *m_pNext; ///< for in-memory list of freed pages
			};

			uint16 m_Pad; ///< key size if this is a LEAF2 page
			/**	@defgroup mdb_page	Page Flags
			 *	@ingroup internal
			 *	Flags for the page headers.
			 *	@{
			 */
#define	DPBranch	 0x01		/**< branch page */
#define	DPLeaf		 0x02		/**< leaf page */
#define	DPOverflow	 0x04		/**< overflow page */
#define	DPMeta		 0x08		/**< meta page */
#define	DPDirty		 0x10		/**< dirty page, also set for #DPSubp pages */
#define	DPLeaf2		 0x20		/**< for #EDatabaseSubFlag_SortedDuplicatesFixedSize records */
#define	DPSubp		 0x40		/**< for #EDatabaseSubFlag_SortedDuplicates sub-pages */
#define	DPLoose		 0x4000		/**< page was dirtied then freed, can be reused */
#define	DPKeep		 0x8000		/**< leave this page alone during spill */
			/** @} */
			uint16 m_Flags; ///< @ref mdb_page
			union
			{
				CFreeSpace m_FreeSpace;  ///< bounds of free space
				uint32 m_nOverflowPages; ///< number of overflow pages
			};

			CIndx m_NodeOffsets[1]; ///< dynamic size
		};

		/** Size of the page header, excluding dynamic data at the end */
#define DPageHeaderSize	 ((unsigned) offsetof(CMdbPage, m_NodeOffsets))

		/** Address of first usable data byte in a page, after the header */
#define DMetaData(d_Page)	 ((uint8 *)(d_Page) + DPageHeaderSize)

			/** ITS#7713, change DPageBase to handle 65536 byte pages */
//#define	DPageBase	((DMdbDevel) ? DPageHeaderSize : 0)
#define	DPageBase	DPageHeaderSize

		/** Number of nodes on a page */
#define DNumKeys(d_Page)	 (((d_Page)->m_FreeSpace.m_Lower - (DPageHeaderSize-DPageBase)) >> 1)

		/** The amount of space remaining in the page */
#define DSizeLeft(d_Page)	 (CIndx)((d_Page)->m_FreeSpace.m_Upper - (d_Page)->m_FreeSpace.m_Lower)

		/** The percentage of space used in the page, in tenths of a percent. */
#define DPageFill(d_Env, d_Page) (1000L * ((d_Env)->m_PageSize - DPageHeaderSize - DSizeLeft(d_Page)) / \
	((d_Env)->m_PageSize - DPageHeaderSize))
		/** The minimum page fill factor, in tenths of a percent.
		 *	Pages emptier than this are candidates for merging.
		 */
#define DFillThreshold	 250

		/** Test if a page is a leaf page */
#define DIsLeaf(d_Page)	 DFIsSet((d_Page)->m_Flags, DPLeaf)
		/** Test if a page is a LEAF2 page */
#define DIsLeaf2(d_Page)	 DFIsSet((d_Page)->m_Flags, DPLeaf2)
		/** Test if a page is a branch page */
#define DIsBranch(d_Page)	 DFIsSet((d_Page)->m_Flags, DPBranch)
		/** Test if a page is an overflow page */
#define DIsOverflow(d_Page)	 DFIsSet((d_Page)->m_Flags, DPOverflow)
		/** Test if a page is a sub page */
#define DIsSubp(d_Page)	 DFIsSet((d_Page)->m_Flags, DPSubp)

		/** The number of overflow pages needed to store the given size. */
#define DOverflowPages(d_Size, d_Psize)	((DPageHeaderSize-1 + (d_Size)) / (d_Psize) + 1)

		/** Link in #CMdbTxn.%m_pLoosePages list.
		 *  Kept outside the page header, which is needed when reusing the page.
		 */
#define DNextLoosePage(d_Page)		(*(CMdbPage **)((d_Page) + 2))

		/** Header for a single key/data pair within a page.
		 * Used in pages of type #DPBranch and #DPLeaf without #DPLeaf2.
		 * We guarantee 2-byte alignment for 'CMdbNode's.
		 *
		 * #m_Low and #m_High are used for data size on leaf nodes, and for child
		 * pgno on branch nodes.  On 64 bit platforms, #m_Flags is also used
		 * for pgno.  (Branch nodes have no flags).  Lo and hi are in host byte
		 * order in case some accesses can be optimized to 32-bit word access.
		 *
		 * Leaf node flags describe node contents.  #DFBigData says the node's
		 * data part is the page number of an overflow page with actual data.
		 * #DFDupData and #DFSubData can be combined giving duplicate data in
		 * a sub-page/sub-database, and named databases (just #DFSubData).
		 */
		struct CMdbNode
		{
			/** part of data size or pgno
			 *	@{ */
#if BYTE_ORDER == LITTLE_ENDIAN
			unsigned short m_Low, m_High;
#else
			unsigned short m_High, m_Low;
#endif
			/** @} */
			/** @defgroup mdb_node Node Flags
			 *	@ingroup internal
			 *	Flags for node headers.
			 *	@{
			 */
#define DFBigData	 0x01			/**< data put on overflow page */
#define DFSubData	 0x02			/**< data is a sub-database */
#define DFDupData	 0x04			/**< data has duplicates */

			/** valid flags for #fg_NodeAdd() */
#define	DNodeAddFlags	(DFDupData|DFSubData|EDatabaseWriteFlag_ReserveSpace|EDatabaseWriteFlag_Append)

			/** @} */
			unsigned short m_Flags;   ///< @ref mdb_node
			unsigned short m_KeySize; ///< key size
			uint8 m_Data[1];          ///< key and data are appended here
		};

		/** Size of the node header, excluding dynamic data at the end */
#define DNodeSize	 offsetof(CMdbNode, m_Data)

		/** Bit position of top word in page number, for shifting m_Flags */
#define DPgnoTopword ((CPgno)-1 > 0xffffffffu ? 32 : 0)

		/** Size of a node in a branch page with a given key.
		 *	This is just the node header plus the key, there is no data.
		 */
#define DIndexSize(d_Key)	 (DNodeSize + ((d_Key) == nullptr ? 0 : (d_Key)->m_Size))

		/** Size of a node in a leaf page with a given key and data.
		 *	This is node header plus key plus data size.
		 */
#define DLeafSize(d_Key, d_Data)	 (DNodeSize + (d_Key)->m_Size + (d_Data)->m_Size)

		/** Address of node \b i in page \b p */
#define DNodePtr(d_Page, d_Index)	 ((CMdbNode *)((char *)(d_Page) + (d_Page)->m_NodeOffsets[d_Index] + DPageBase))

		/** Address of the key for the node */
#define DNodeKey(d_Node)	 (uint8 *)((d_Node)->m_Data)

		/** Address of the data for a node */
#define DNodeData(d_Node)	 ((uint8 *)(d_Node)->m_Data + (d_Node)->m_KeySize)

		/** Get the page number pointed to by a branch node */
#define DNodePgno(d_Node) \
	((d_Node)->m_Low | ((CPgno) (d_Node)->m_High << 16) | \
	 (DPgnoTopword ? ((CPgno) (d_Node)->m_Flags << DPgnoTopword) : 0))
		/** Set the page number in a branch node */
#define DSetPgno(d_Node,d_Pgno)	do { \
	(d_Node)->m_Low = (d_Pgno) & 0xffff; (d_Node)->m_High = (d_Pgno) >> 16; \
	if (DPgnoTopword) (d_Node)->m_Flags = (d_Pgno) >> DPgnoTopword; } while(0)

		/** Get the size of the data in a leaf node */
#define DNodeDataSize(d_Node)	 ((d_Node)->m_Low | ((unsigned)(d_Node)->m_High << 16))
		/** Set the size of the data for a leaf node */
#define DSetDataSize(d_Node,d_Size)	do { \
	(d_Node)->m_Low = (d_Size) & 0xffff; (d_Node)->m_High = (d_Size) >> 16;} while(0)
		/** The size of a key in a node */
#define DNodeKeySize(d_Node)	 ((d_Node)->m_KeySize)

			/** Copy a page number from src to dst */
#ifdef DMisalignedOk
#	define DCopyPgno(d_Dst,d_Src)	d_Dst = d_Src
#else
#	if DMibDatabase_IDBits > 32
#		define DCopyPgno(d_Dst,d_Src)	do { \
				unsigned short *pCopySource, *pCopyDestination;	\
				pCopySource = (unsigned short *)&(d_Src);	\
				pCopyDestination = (unsigned short *)&(d_Dst);	\
				*pCopyDestination++ = *pCopySource++;	\
				*pCopyDestination++ = *pCopySource++;	\
				*pCopyDestination++ = *pCopySource++;	\
				*pCopyDestination = *pCopySource;	\
			} while (0)
#	else
#		define DCopyPgno(d_Dst,d_Src)	do { \
				unsigned short *pCopySource, *pCopyDestination;	\
				pCopySource = (unsigned short *)&(d_Src);	\
				pCopyDestination = (unsigned short *)&(d_Dst);	\
				*pCopyDestination++ = *pCopySource++;	\
				*pCopyDestination = *pCopySource;	\
			} while (0)
#	endif
#endif
		/** The address of a key in a LEAF2 page.
		 *	LEAF2 pages are used for #EDatabaseSubFlag_SortedDuplicatesFixedSize sorted-duplicate sub-DBs.
		 *	There are no node headers, keys are stored contiguously.
		 */
#define DLeaf2Key(d_Page, d_Index, d_Ks)	((uint8 *)(d_Page) + DPageHeaderSize + ((d_Index)*(d_Ks)))

		/** Set the \b node's key into \b keyptr, if requested. */
#define DMdbGetKey(d_Node, d_Keyptr)	{ if ((d_Keyptr) != nullptr) { \
	(d_Keyptr)->m_Size = DNodeKeySize(d_Node); (d_Keyptr)->m_pData = (uint8 *)DNodeKey(d_Node); } }

		/** Set the \b node's key into \b key. */
#define DMdbGetKey2(d_Node, d_Key)	{ d_Key.m_Size = DNodeKeySize(d_Node); d_Key.m_pData = DNodeKey(d_Node); }

		/** Information about a single database in the environment. */
		struct CMdbDb
		{
			uint32 m_Pad;               ///< also ksize for LEAF2 pages
			uint16 m_Flags;             ///< @ref f_OpenDatabase
			uint16 m_Depth;             ///< depth of this tree
			CPgno m_nBranchPages;       ///< number of internal pages
			CPgno m_nLeafPages;         ///< number of leaf pages
			CPgno m_nOverflowPages;     ///< number of overflow pages
			CDatabaseOffset m_nEntries; ///< number of data items
			CPgno m_RootPage;           ///< the root page of this tree
		};

#define DMdbValid	0x8000		/**< DB handle is valid, for m_pDatabaseFlags */
#define DPersistentFlags	(0xffff & ~(DMdbValid))
		/** f_OpenDatabase flags */
#define DValidFlags	(EDatabaseSubFlag_ReverseKeys|EDatabaseSubFlag_SortedDuplicates|EDatabaseSubFlag_IntegerKeys|EDatabaseSubFlag_SortedDuplicatesFixedSize|\
	EDatabaseSubFlag_SortedDuplicatesIntegerKeys|EDatabaseSubFlag_SortedDuplicatesReverseKeys|EDatabaseSubFlag_Create)

		/** Handle for the DB used to track free pages. */
#define	DFreeDbi	0
		/** Handle for the default DB. */
#define	DMainDbi	1
		/** Number of DBs in metapage (free and main) - also hardcoded elsewhere */
#define DCoreDbs	2

		/** Number of meta pages - also hardcoded elsewhere */
#define DNumMetas	2

		/** Meta page content.
		 *	A meta page is the start point for accessing a database snapshot.
		 *	Pages 0-1 are meta pages. Transaction N writes meta page #(N % 2).
		 */
		struct CMdbMeta
		{
			/** Stamp identifying this as an LMDB file. It must be set
			 *	to #DMdbMagic. */
			uint32 m_Magic;
			/** Version number of this file. Must be set to #DMdbDataVersion. */
			uint32 m_Version;
#ifdef DMibDatabase_Always64BitSupport
			union
			{ // always zero since we don't support fixed mapping in DMibDatabase_Always64BitSupport
				CMdbId m_AddressStorage;
				void *m_pAddress;
			};
#else
			void *m_pAddress; ///< address for fixed mapping
#endif
			CDatabaseOffset m_MapSize;    ///< size of mmap region
			CMdbDb m_Databases[DCoreDbs]; ///< first is free space, 2nd is main db
			/** Last used page in the datafile.
			 *	Actually the file may be shorter if the freeDB lists the final pages.
			 */
			CPgno m_LastPageNumber;
			NAtomic::TCAtomicIndeterminate<CTransactionID> m_TransactionID; ///< txnid that committed this page
		};

		/** Buffer for a stack-allocated meta page.
		 *	The members define size and alignment, and silence type
		 *	aliasing warnings.  They are not used directly; that could
		 *	mean incorrectly using several union members in parallel.
		 */
		typedef union CMdbMetaBuf
		{
			CMdbPage m_Page;
			struct
			{
				char m_Pad[DPageHeaderSize];
				CMdbMeta m_Meta;
			} m_MetaBuffer;
		} CMdbMetaBuf;

		/** Auxiliary DB info.
		 *	The information here is mostly static/read-only. There is
		 *	only a single copy of this record in the environment.
		 */
		struct CMdbDbx
		{
			CDatabaseValue m_DatabaseName;   ///< name of the database
			FDatabaseCompare m_fKeyCompare;  ///< function for comparing keys
			FDatabaseCompare m_fDataCompare; ///< function for comparing data items
		};
	}
	/** A database transaction.
	 *	Every operation requires a transaction handle.
	 */
	struct CDatabaseTransactionCommon::CInternal
	{
		static void fs_CheckBlocked(uint32 _Flags);
		void f_CheckBlocked() const;

		void f_PrepareStatisticsQuery(CDatabaseSubID _DatabaseID);
		CDatabaseEnvironmentInfo f_EnvironmentInfo();
		CDatabaseStatistics f_DatabaseStatistics(CDatabaseSubID _DatabaseID);
		CDatabaseSizeStatistics f_SizeStatistics(CDatabaseSubID _DatabaseID);
		uint32 f_DatabaseFlags(CDatabaseSubID _DatabaseID);
		void f_DropDatabase(CDatabaseSubID _DatabaseID, bool _bDelete);
		void f_SetDatabaseKeyCompare(CDatabaseSubID _DatabaseID, FDatabaseCompare &&_fCompare);
		void f_SetDatabaseDataCompare(CDatabaseSubID _DatabaseID, FDatabaseCompare &&_fCompare);

		bool f_Get(CDatabaseSubID _DatabaseID, CDatabaseValue *_pKey, CDatabaseValue *_pData);
		void f_Put(CDatabaseSubID _DatabaseID, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseWriteFlag _WriteFlags);
		void f_Delete(CDatabaseSubID _DatabaseID, CDatabaseValue const *_pKey, CDatabaseValue const *_pData);
		int f_KeyCompare(CDatabaseSubID _DatabaseID, CDatabaseValue const *_pLeft, CDatabaseValue const *_pRight);
		int f_DataCompare(CDatabaseSubID _DatabaseID, CDatabaseValue const *_pLeft, CDatabaseValue const *_pRight);

		NPrivate::CMdbCursor *f_Cursor(CDatabaseSubID _DatabaseID);

		NPrivate::CMdbTxn *m_pParent; ///< parent of a nested txn
		/** Nested txn under this txn, set together with flag #DMdbTxnHasChild */
		NPrivate::CMdbTxn *m_pChild;
		NPrivate::CPgno m_NextPageNumber; ///< next unallocated page
#ifdef DMibDatabase_Always64BitSupport
		NPrivate::CPgno m_LastPageNumber; ///< last written page
#endif
		/** The ID of this transaction. IDs are integers incrementing from 1.
		 *	Only committed write transactions increment the ID. If a transaction
		 *	aborts, the ID may be re-used by the next writer.
		 */
		NPrivate::CTransactionID m_TransactionID;
		NPrivate::CMdbEnv *m_pEnvironment; ///< the DB environment
		/** The list of pages that became unused during this transaction.
		 */
		NPrivate::CMdbIdl m_pFreePages;
		/** The list of loose pages that became unused and may be reused
		 *	in this transaction, linked through #DNextLoosePage(page).
		 */
		NPrivate::CMdbPage *m_pLoosePages;
		/** Number of loose pages (#m_pLoosePages) */
		int m_nLoosePages;
		/** The sorted list of dirty pages we temporarily wrote to disk
		 *	because the dirty list was full. page numbers in here are
		 *	shifted left by 1, deleted slots have the LSB set.
		 */
		NPrivate::CMdbIdl m_pSpillPages;
		union
		{
			/** For write txns: Modified pages. Sorted when not EDatabaseEnvironmentFlag_WriteMap. */
			NPrivate::CMdbId2L m_pDirtyList;
			/** For read txns: This thread/txn's reader table slot, or nullptr. */
			NPrivate::CMdbReader *m_pReader;
		};

		/** Array of records for each DB known in the environment. */
		NPrivate::CMdbDbx *m_pDatabaseInfos;
		/** Array of CMdbDb records for each known DB */
		NPrivate::CMdbDb *m_pDatabases;
		/** Array of sequence numbers for each DB handle */
		uint32 *m_pDatabaseSequences;
		/** @defgroup mt_dbflag	Transaction DB Flags
		 *	@ingroup internal
		 * @{
		 */
#define DDbDirty	0x01		/**< DB was written in this txn */
#define DDbStale	0x02		/**< Named-DB record is older than txnID */
#define DDbNew		0x04		/**< Named-DB handle opened in this txn */
#define DDbValid	0x08		/**< DB handle is valid, see also #DMdbValid */
#define DDbUserValid	0x10		/**< As #DDbValid, but not set for #DFreeDbi */
#define DDbDupData	0x20		/**< DB is #MDB_DUPSORT data */
		/** @} */
		/** In write txns, array of cursors for each DB */
		NPrivate::CMdbCursor **m_pCursors;
		/** Array of flags for each DB */
		unsigned char *m_pDatabaseFlags;
#ifdef DMibDatabase_Always64BitSupport
		/** List of read-only pages (actually chunks) */
		NPrivate::MDB_ID3L m_pReadOnlyPages;
		/** We map chunks of 16 pages. Even though Windows uses 4KB pages, all
		 * mappings must begin on 64KB boundaries. So we round off all pgnos to
		 * a chunk boundary. We do the same on Linux for symmetry, and also to
		 * reduce the frequency of mmap/munmap calls.
		 */
#	define DMdbRpageChunk	16
#	define DMdbTrpageSize	4096	/**< size of #m_pReadOnlyPages array of chunks */
#	define DMdbTrpageMax	(DMdbTrpageSize-1)	/**< maximum chunk index */
		uint32 m_ReadOnlyPageCheck; ///< threshold for reclaiming unref'd chunks
#endif
		/**	Number of DB records in use, or 0 when the txn is finished.
		 *	This number only ever increments until the txn finishes; we
		 *	don't decrement it when individual DB handles are closed.
		 */
		CDatabaseSubID m_nDatabases;

		/** @defgroup mdb_txn	Transaction Flags
		 *	@ingroup internal
		 *	@{
		 */
		/** #f_TxnBegin() flags */
#define DMdbTxnBeginFlags	(EDatabaseEnvironmentFlag_NoMetaSync|EDatabaseEnvironmentFlag_NoSync|EDatabaseEnvironmentFlag_ReadOnly)
#define DMdbTxnNoMetaSync	EDatabaseEnvironmentFlag_NoMetaSync	/**< don't sync meta for this txn on commit */
#define DMdbTxnNoSync		EDatabaseEnvironmentFlag_NoSync	/**< don't sync this txn on commit */
#define DMdbTxnReadOnly		EDatabaseEnvironmentFlag_ReadOnly	/**< read-only transaction */
		/* internal txn flags */
#define DMdbTxnWriteMap	EDatabaseEnvironmentFlag_WriteMap	/**< copy of #CMdbEnv flag in writers */
#define DMdbTxnFinished	0x01		/**< txn is finished or never began */
#define DMdbTxnError		0x02		/**< txn is unusable after an error */
#define DMdbTxnDirty		0x04		/**< must write, even if dirty list is empty */
#define DMdbTxnSpills		0x08		/**< txn or a parent has spilled pages */
#define DMdbTxnHasChild	0x10		/**< txn has an #CMdbTxn.%m_pChild */
		/** most operations on the txn are currently illegal */
#define DMdbTxnBlocked		(DMdbTxnFinished|DMdbTxnError|DMdbTxnHasChild)
		/** @} */
		uint32 m_Flags; ///< @ref mdb_txn
		/** #m_pDirtyList room: Array size - \#dirty pages visible to this txn.
		 *	Includes ancestor txns' dirty pages not hidden by other txns'
		 *	dirty/spilled pages. Thus commit(nested txn) has room to merge
		 *	m_pDirtyList into m_pParent after freeing hidden m_pParent pages.
		 */
		uint32 m_DirtyRoom;
	};

	namespace NPrivate
	{
		/** Enough space for 2^32 nodes with minimum of 2 keys per node. I.e., plenty.
		 * At 4 keys per node, enough for 2^64 nodes, so there's probably no need to
		 * raise this on a 64 bit machine.
		 */
#define DCursorStack		 32

		struct CMdbXCursor;

		/** Cursors are used for all DB operations.
		 *	A cursor holds a path of (page pointer, key index) from the DB
		 *	root to a position in the DB, plus other state. #EDatabaseSubFlag_SortedDuplicates
		 *	cursors include an xcursor to the current data item. Write txns
		 *	track their cursors and keep them up to date when data moves.
		 *	Exception: An xcursor's pointer to a #DPSubp page can be stale.
		 *	(A node with #DFDupData but no #DFSubData contains a subpage).
		 */
	}

	struct CDatabaseCursorCommon::CInternal
	{
		/** Next cursor on this DB in this txn */
		CInternal *m_pNext;
		/** Backup of the original cursor if this cursor is a shadow */
		CInternal *m_pBackup;
		/** Context used for databases with #EDatabaseSubFlag_SortedDuplicates, otherwise nullptr */
		NPrivate::CMdbXCursor *m_pXCursor;
		/** The transaction that owns this cursor */
		NPrivate::CMdbTxn *m_pTransaction;
		/** The database handle this cursor operates on */
		CDatabaseSubID m_DatabaseID;
		/** The database record for this cursor */
		NPrivate::CMdbDb *m_pDatabase;
		/** The database auxiliary record for this cursor */
		NPrivate::CMdbDbx *m_pDatabaseInfo;
		/** The @ref mt_dbflag for this database */
		unsigned char *m_pDatabaseFlag;
		unsigned short m_nPages; ///< number of pushed pages
		unsigned short m_iTop;   ///< index of top page, normally m_nPages-1
		/** @defgroup CInternal	Cursor Flags
		 *	@ingroup internal
		 *	Cursor state flags.
		 *	@{
		 */
#define DCInitialized	0x01	/**< cursor has been initialized and is valid */
#define DCEof	0x02			/**< No more data */
#define DCSub	0x04			/**< Cursor is a sub-cursor */
#define DCDel	0x08			/**< last op was a cursor_del */
#define DCUntrack	0x40		/**< Un-track cursor when closing */
#define DCWriteMap	DMdbTxnWriteMap /**< Copy of txn flag */
		/** Read-only cursor into the txn's original snapshot in the map.
		 *	Set for read-only txns, and in #fg_PageAlloc() for #DFreeDbi when
		 *	#DMdbDevel & 2. Only implements code which is necessary for this.
		 */
#define DCOrigReadOnly	DMdbTxnReadOnly
		/** @} */
		uint32 m_Flags;                              ///< @ref mdb_cursor
		NPrivate::CMdbPage *m_Pages[DCursorStack];   ///< stack of pushed pages
		NPrivate::CIndx m_NodeIndices[DCursorStack]; ///< stack of page indices
#ifdef DMibDatabase_Always64BitSupport
		NPrivate::CMdbPage *m_pOverflowPage; ///< a referenced overflow page
#	define DMcOvpg(d_Mc)			((d_Mc)->m_pOverflowPage)
#	define DMcSetOvpg(d_Mc, d_Pg)	((d_Mc)->m_pOverflowPage = (d_Pg))
#else
#	define DMcOvpg(d_Mc)			((CMdbPage *)0)
#	define DMcSetOvpg(d_Mc, d_Pg)	((void)0)
#endif
		void f_Put(CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseWriteFlag _WriteFlags);
		void f_Delete(EDatabaseWriteFlag _WriteFlags);
		CDatabaseOffset f_CountDuplicates();
	};

	namespace NPrivate
	{
		/** Context for sorted-dup records.
		 *	We could have gone to a fully recursive design, with arbitrarily
		 *	deep nesting of sub-databases. But for now we only handle these
		 *	levels - main DB, optional sub-DB, sorted-duplicate DB.
		 */
		struct CMdbXCursor
		{
			/** A sub-cursor for traversing the Dup DB */
			CMdbCursor m_Cursor;
			/** The database record for this Dup DB */
			CMdbDb m_Database;
			/**	The auxiliary DB record for this Dup DB */
			CMdbDbx m_DatabaseInfo;
			/** The @ref mt_dbflag for this Dup DB */
			unsigned char m_DatabaseFlag;
		};

		/** Check if there is an inited xcursor */
#define DXCursorInited(d_Mc) \
	((d_Mc)->m_pXCursor && ((d_Mc)->m_pXCursor->m_Cursor.m_Flags & DCInitialized))

		/** Update the xcursor's sub-page pointer, if any, in \b mc.  Needed
		 *	when the node which contains the sub-page may have moved.  Called
		 *	with leaf page \b mp = mc->m_Pages[\b top].
		 */
#define DXCursorRefresh(d_Mc, d_Top, d_Mp) do { \
		CMdbPage *pRefreshPage = (d_Mp); \
		CMdbNode *pRefreshNode; \
		if (!DXCursorInited(d_Mc) || (d_Mc)->m_NodeIndices[d_Top] >= DNumKeys(pRefreshPage)) break; \
		pRefreshNode = DNodePtr(pRefreshPage, (d_Mc)->m_NodeIndices[d_Top]); \
		if ((pRefreshNode->m_Flags & (DFDupData|DFSubData)) == DFDupData) \
			(d_Mc)->m_pXCursor->m_Cursor.m_Pages[0] = (CMdbPage *)DNodeData(pRefreshNode); \
	} while (0)

		/** State of FreeDB old pages, stored in the CMdbEnv */
		struct CMdbPgState
		{
			CPgno *m_pReclaimedPages;      ///< Reclaimed freeDB pages, or nullptr before use
			CTransactionID m_LastRecordID; ///< ID of last used record, or 0 if !m_pReclaimedPages
		};

		/** Nested transaction */
		struct CMdbNTxn
		{
			CMdbTxn m_Transaction;   ///< the transaction
			CMdbPgState m_PageState; ///< parent transaction's saved freestate
		};

#if !(DMdbPidLock)		/* Currently the same as defined(DPlatformFamily_Windows) */
		enum EPidLockOp
		{
			EPidLockOp_Set
			, EPidLockOp_Check
		};
#else
		enum EPidLockOp
		{
			EPidLockOp_Set     = F_SETLK
			, EPidLockOp_Check = F_GETLK
		};
#endif

#ifdef O_CLOEXEC /* POSIX.1-2008: Set FD_CLOEXEC atomically at open() */
#	define DMdbCloExec		O_CLOEXEC
#else
#	define DMdbCloExec		0
#endif

		/** File type, access mode etc. for #f_FOpen() */
		enum EMdbFOpenType
		{
#ifdef DPlatformFamily_Windows
			EMdbFOpenType_ReadOnly
			, EMdbFOpenType_ReadWrite
			, EMdbFOpenType_Meta
			, EMdbFOpenType_Copy
			, EMdbFOpenType_Locks
#else
			/* A comment in f_FOpen() explains some O_* flag choices. */
			EMdbFOpenType_ReadOnly = O_RDONLY ///< for RDONLY m_FileHandle
			, EMdbFOpenType_ReadWrite = O_RDWR | O_CREAT                          ///< for m_FileHandle
			, EMdbFOpenType_Meta      = O_WRONLY | DMdbDSync | DMdbCloExec        ///< for m_MetaFileHandle
			, EMdbFOpenType_Copy      = O_WRONLY | O_CREAT | O_EXCL | DMdbCloExec ///< for #mdb_env_copy()
			/** Bitmask for open() flags in enum #EMdbFOpenType.  The other bits
			 * distinguish otherwise-equal MDB_O_* constants from each other.
			 */
			, EMdbFOpenType_Mask  = EMdbFOpenType_ReadWrite | DMdbCloExec | EMdbFOpenType_ReadOnly | EMdbFOpenType_Meta | EMdbFOpenType_Copy
			, EMdbFOpenType_Locks = EMdbFOpenType_ReadWrite | DMdbCloExec | ((EMdbFOpenType_Mask + 1) & ~EMdbFOpenType_Mask) ///< for m_LockFileHandle
#endif
		};

#ifdef _MSC_VER
		typedef int CMdbMode;
#else
		typedef mode_t CMdbMode;
#endif
	}
	/** The database environment. */
	struct CDatabaseEnvironment_Internal
	{
		// NOLINTNEXTLINE(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		HANDLE m_FileHandle; ///< The main data file
		// NOLINTNEXTLINE(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		HANDLE m_LockFileHandle; ///< The lock file
		// NOLINTNEXTLINE(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		HANDLE m_MetaFileHandle; ///< For writing and syncing the meta pages
#if defined(DMibDatabase_Always64BitSupport) && defined(DPlatformFamily_Windows)
		HANDLE m_FileMappingHandle; ///< File Mapping handle
#endif
		/** Failed to update the meta page. Probably an I/O error. */
#define	DMdbFatalError	0x80000000U
		/** Some fields are initialized. */
#define	DMdbEnvActive	0x20000000U
		/** m_ThreadKey is set */
#define	DMdbEnvTxkey	0x10000000U
		/** fdatasync is unreliable */
#define	DMdbFSyncOnly	0x08000000U
		uint32 m_Flags;      ///< @ref mdb_env
		uint32 m_PageSize;   ///< DB page size, inited from m_OSPageSize
		uint32 m_OSPageSize; ///< OS page size
		uint32 m_MaxReaders; ///< size of the reader table
		/** Max #CLocksMemoryMap.%m_TransactionBody.m_NumReaders of interest to #f_Close() */
		NAtomic::TCAtomic<int32> m_nCloseReaders;
		CDatabaseSubID m_nDatabases;                                  ///< number of DBs opened
		CDatabaseSubID m_MaxDatabases;                                ///< size of the DB table
		CProcessID m_ProcessID;                                       ///< process ID of this env
		NStr::CStr m_Path;                                            ///< path to the DB files
		char *m_pMap;                                                 ///< the memory map of the data file
		NPrivate::CLocksMemoryMap *m_pLocksMemoryMap;                 ///< the memory map of the lock file or nullptr
		NPrivate::CMdbMeta *m_Metas[DNumMetas];                       ///< pointers to the two meta pages
		void *m_pScratchBuffer;                                       ///< scratch area for DUPSORT put()
		NPrivate::CMdbTxn *m_pTransaction;                            ///< current write transaction
		NPrivate::CMdbTxn *m_pPreallocatedTransaction;                ///< prealloc'd write transaction
		CDatabaseOffset m_MapSize;                                    ///< size of the data memory map
		off_t m_FileSize;                                             ///< current file size
		NPrivate::CPgno m_nMaxPages;                                  ///< m_MapSize / m_PageSize
		NContainer::TCVector<NPrivate::CMdbDbx> m_StaticDasabaseInfo; ///< array of static DB info
		uint16 *m_pDatabaseFlags;                                     ///< array of flags from CMdbDb.m_Flags
		uint32 *m_pDatabaseSequences;                                 ///< array of dbi sequence numbers
		pthread_key_t m_ThreadKey;                                    ///< thread-key for readers
		NPrivate::CTransactionID m_OldestTransactionID;               ///< ID of oldest reader last time we looked
		NPrivate::CMdbPgState m_PageState;                            ///< state of old pages from freeDB
		NPrivate::CMdbPage *m_pReusablePages;                         ///< list of malloc'd blocks for re-use
		/** IDL of pages that became unused in a write txn */
		NPrivate::CMdbIdl m_pFreePages;
		/** ID2L of pages written during a write txn. Length DMdbIdlUmSize. */
		NPrivate::CMdbId2L m_pDirtyList;
		/** Max number of freelist items that can fit in a single overflow page */
		int m_nMaxFreePerPage;
		/** Max size of a node on a page */
		uint32 m_MaxNodeSize;
#if !(DMdbMaxKeySize)
		uint32 m_MaxKeySize; ///< max size of a key
#endif
		int m_bLiveReader; ///< have liveness lock in reader table
#ifdef DPlatformFamily_Windows
		int m_ProcessIDQuery; ///< Used in OpenProcess
#endif
#ifdef DMdbUsePosixMutex	/* Posix mutexes reside in shared mem */
		// NOLINTNEXTLINE(malterlib-macro-naming): stands in for the member that holds the mutex on the other platforms
#	define		m_ReaderMutex	m_pLocksMemoryMap->m_TransactionBody.m_ReaderTableMutex /**< Shared reader lock */
		// NOLINTNEXTLINE(malterlib-macro-naming): stands in for the member that holds the mutex on the other platforms
#	define		m_WriterMutex	m_pLocksMemoryMap->m_WriteLock /**< Shared writer lock */
#else
		NPrivate::CMdbMutexType m_ReaderMutex; // NOLINT(malterlib-conceptual-prefix): a HANDLE on Windows, and a pointer, an array or a macro with the other mutex setups
		NPrivate::CMdbMutexType m_WriterMutex; // NOLINT(malterlib-conceptual-prefix): a HANDLE on Windows, and a pointer, an array or a macro with the other mutex setups
#	if defined(DPlatformFamily_Windows) || defined(MDB_USE_POSIX_SEM)
		/** Half-initialized name of mutexes, to be completed by #DMutexName() */
		char m_MutexName[sizeof(DMutexNamePrefix) + 11];
#	endif
#endif
#ifdef DMibDatabase_Always64BitSupport
		NPrivate::MDB_ID3L m_pReadOnlyPages;  ///< like CMdbTxn's #m_pReadOnlyPages, but global to env
		pthread_mutex_t m_ReadOnlyPagesMutex; ///< control access to #m_pReadOnlyPages
#	define DMdbErpageSize	16384
#	define DMdbErpageMax	(DMdbErpageSize-1)
		uint32 m_ReadOnlyPageCheck;
#endif
		umint m_LockedThreadID = 0;
		NAtomic::TCAtomic<umint> m_nReferences = 0;
		// Publics
		NDatabase::CDatabaseTransactionCommon f_TxnBegin(NPrivate::CMdbTxn *_pParent, uint32 _Flags);
		void f_DbiClose(CDatabaseSubID _DatabaseID);
		void f_TxnFree(NPrivate::CMdbTxn *_pTransaction);

		// Privates
		void f_CopyCompact(HANDLE _FileHandle); // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		void f_Copy(HANDLE _FileHandle); // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		CDatabaseStatistics f_DatabaseStatistics(NPrivate::CMdbDb *_pDatabase);
		CDatabaseSizeStatistics f_SizeStatistics(NPrivate::CMdbDb *_pDatabase);
		CDatabaseEnvironmentInfo f_EnvironmentInfo();
		bool f_ReadHeader(int _bPrevious, NPrivate::CMdbMeta *_pMeta);
		NPrivate::CMdbMeta *f_PickMeta() const;
		void f_Close0(int _Exclusive);
		umint f_LeafSize(CDatabaseValue *_pKey, CDatabaseValue *_pData);
		umint f_BranchSize(CDatabaseValue *_pKey);
#ifdef DMdbRobustSupported
		void f_ReaderCheck0(bool _bReadLock, umint *_pDead);
#endif
		void f_PageFree(NPrivate::CMdbPage *_pPage);
		void f_DPageFree(NPrivate::CMdbPage *_pPage);
		void f_DPageFreeMemory(NPrivate::CMdbPage *_pPage);
		int f_ReaderPid(NPrivate::EPidLockOp _Operation, CProcessID _ProcessID);
		void f_Sync0(bool _bForce, NPrivate::CPgno _nPages);
		void f_InitMeta0(NPrivate::CMdbMeta *_pMeta);
		void f_InitMeta(NPrivate::CMdbMeta *_pMeta);
		int f_Map(void *_pAddress);
		void f_Open2(int _bPrevious, uint32 _PageSize);
		int f_ShareLocks(int *_pExclusive);
		int f_ExclLock(int *_pExclusive);
		void f_SetupLocks(NStr::CStr const &_LockPath, int _Mode, int *_pExclusive);
#ifdef DMdbRobustSupported
		int f_MutexFailed(NPrivate::CMdbMutexRef _pMutex, int _Result);
#endif
		int f_FOpen(NStr::CStr const &_FileName, NPrivate::EMdbFOpenType _FileType, NPrivate::CMdbMode _Mode, HANDLE *o_pFileHandle);
		NStr::CStr f_GetFileName(NStr::CStr const &_FileName, NPrivate::EMdbFOpenType _FileType);
		bool f_IsInsideMemoryMap(uint8 const *_pData);
	};

	namespace NPrivate
	{
			/** max number of pages to commit in one writev() call */
#define DMdbCommitPages	 64
#if defined(IOV_MAX) && IOV_MAX < DMdbCommitPages
#	undef DMdbCommitPages
#	define DMdbCommitPages	IOV_MAX
#endif

			/** max bytes to write in one call */
#define DMaxWrite		(0x40000000U >> int(sizeof(smint) == 4))

			/** Check \b txn and \b dbi arguments to a function */
#define DTxnDbiExist(d_Txn, d_Dbi, d_Validity) \
	((d_Txn) && (d_Dbi)<(d_Txn)->m_nDatabases && ((d_Txn)->m_pDatabaseFlags[d_Dbi] & (d_Validity)))

			/** Check for misused \b dbi handles */
#define DTxnDbiChanged(d_Txn, d_Dbi) \
	((d_Txn)->m_pDatabaseSequences[d_Dbi] != (d_Txn)->m_pEnvironment->m_pDatabaseSequences[d_Dbi])

		int fg_PageAlloc(CMdbCursor *_pCursor, int _nPages, CMdbPage **o_pPage);
		int fg_PageNew(CMdbCursor *_pCursor, uint32 _Flags, int _nPages, CMdbPage **o_pPage);
		int fg_PageTouch(CMdbCursor *_pCursor);

		CMdbPage *fg_PageMalloc(CMdbTxn *_pTransaction, unsigned _nPages);
		void fg_DListFree(CMdbTxn *_pTransaction);
		int fg_PageLoose(CMdbCursor *_pCursor, CMdbPage *_pPage);
		int fg_PagesXKeep(CMdbCursor *_pCursor, unsigned _PageFlags, int _bAll);
		int fg_PageSpill(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData);

		CTransactionID fg_FindOldest(CMdbTxn *_pTransaction);
		void fg_PageDirty(CMdbTxn *_pTransaction, CMdbPage *_pPage);
		void fg_PageCopy(CMdbPage *_pDestination, CMdbPage *_pSource, uint32 _PageSize);
		int fg_PageUnspill(CMdbTxn *_pTransaction, CMdbPage *_pPage, CMdbPage **o_pDirtyPage);

		void fg_CursorShadow(CMdbTxn *_pSource, CMdbTxn *_pDestination);
		void fg_CursorsClose(CMdbTxn *_pTransaction, unsigned _bMerge);
		void fg_TxnRenew0(CMdbTxn *_pTransaction);
		void fg_TxnCommit(CMdbTxn *_pTransaction);
		void fg_DbisUpdate(CMdbTxn *_pTransaction, int _bKeep);
		int fg_FreelistSave(CMdbTxn *_pTransaction);

		CDatabaseOffset fg_FSize(HANDLE _FileHandle); // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		void fg_EnvReaderDest(void *_pReader);
		int fg_OvPageFree(CMdbCursor *_pCursor, CMdbPage *_pPage);
		int fg_PageSearchLowest(CMdbCursor *_pCursor);
		int fg_CursorTouch(CMdbCursor *_pCursor);
		void fg_CursorCopy(CMdbCursor const *_pSourceCursor, CMdbCursor *_pDestinationCursor);

		/** State needed for a compacting copy. */
		struct CCompactingCopyState;

		int fg_EnvCThrToggle(CCompactingCopyState &_CopyState);
		int fg_EnvCWalk(CCompactingCopyState &_CopyState, CPgno *_pPageNumber, int _Flags);
		int fg_PidInsert(CProcessID *_pProcessIDs, CProcessID _ProcessID);

#define DMdbEndNames {"committed", "empty-commit", "abort", "reset", \
	"reset-tmp", "fail-begin", "fail-beginchild"}
		constexpr aint gc_MDB_END_COMMITTED = 0;
		constexpr aint gc_MDB_END_EMPTY_COMMIT = gc_MDB_END_COMMITTED + 1;
		constexpr aint gc_MDB_END_ABORT = gc_MDB_END_EMPTY_COMMIT + 1;
		constexpr aint gc_MDB_END_RESET = gc_MDB_END_ABORT + 1;
		constexpr aint gc_MDB_END_RESET_TMP = gc_MDB_END_RESET + 1;
		constexpr aint gc_MDB_END_FAIL_BEGIN = gc_MDB_END_RESET_TMP + 1;
		constexpr aint gc_MDB_END_FAIL_BEGINCHILD = gc_MDB_END_FAIL_BEGIN + 1;

#define DMdbEndOpmask	0x0F	/**< mask for #fg_TxnEnd() operation number */
#define DMdbEndUpdate	0x10	/**< update env state (DBIs) */
#define DMdbEndFree	0x20	/**< free txn unless it is #CMdbEnv.%m_pPreallocatedTransaction */
#define DMdbEndSlot EDatabaseEnvironmentFlag_NoTls	/**< release any reader slot if #EDatabaseEnvironmentFlag_NoTls */
		void fg_TxnEnd(CMdbTxn *_pTransaction, unsigned _Mode);
		void fg_TxnAbort(CMdbTxn *_pTransaction);

		NStr::CStr fg_StrError(int _Error);
		NStr::CStr fg_StrErrorSystem(int _Error);

		int fg_PageGet(CMdbCursor *_pCursor, CPgno _PageNumber, CMdbPage **o_pPage, int *o_pLevel);
		int fg_PageSearchRoot(CMdbCursor *_pCursor, CDatabaseValue const *_pKey, int _Flags);
#define DMdbPsModify	1
#define DMdbPsRootonly	2
#define DMdbPsFirst	4
#define DMdbPsLast		8
		int fg_PageSearch(CMdbCursor *_pCursor, CDatabaseValue const *_pKey, int _Flags);
		int fg_PageMerge(CMdbCursor *_pSourceCursor, CMdbCursor *_pDestinationCursor);

#define DMdbSplitReplace	EDatabaseWriteFlag_AppendDuplicateData	/**< _pNewKey is not new */
		int fg_PageSplit(CMdbCursor *_pCursor, CDatabaseValue *_pNewKey, CDatabaseValue *_pNewData, CPgno _NewPageNumber, uint32 _NodeFlags);

		int fg_PageFlush(CMdbTxn *_pTransaction, int _nKeep);

		int fg_EnvWriteMeta(CMdbTxn *_pTransaction);

		CMdbNode *fg_NodeSearch(CMdbCursor *_pCursor, CDatabaseValue const *_pKey, int *o_pExact);
		int fg_NodeAdd(CMdbCursor *_pCursor, CIndx _iNode, CDatabaseValue *_pKey, CDatabaseValue *_pData, CPgno _PageNumber, uint32 _Flags);
		void fg_NodeDel(CMdbCursor *_pCursor, int _KeySize);
		void fg_NodeShrink(CMdbPage *_pPage, CIndx _iNode);
		int fg_NodeMove(CMdbCursor *_pSourceCursor, CMdbCursor *_pDestinationCursor, int _bFromLeft);
		int fg_NodeRead(CMdbCursor *_pCursor, CMdbNode *_pLeaf, CDatabaseValue *_pData);

		int fg_Rebalance(CMdbCursor *_pCursor);
		int fg_UpdateKey(CMdbCursor *_pCursor, CDatabaseValue *_pKey);

		void fg_CursorPop(CMdbCursor *_pCursor);
		int fg_CursorPush(CMdbCursor *_pCursor, CMdbPage *_pPage);

		int fg_CursorDel0(CMdbCursor *_pCursor);
		int fg_Del0(CMdbTxn *_pTransaction, CDatabaseSubID _DatabaseID, CDatabaseValue const *_pKey, CDatabaseValue const *_pData, EDatabaseWriteFlag _Flags);
		int fg_CursorSibling(CMdbCursor *_pCursor, int _bMoveRight);

		/** @brief Cursor Get operations.
		 *
		 *	This is the set of all operations for retrieving data
		 *	using a cursor.
		 */
		enum EDatabaseCursorGet
		{
			EDatabaseCursorGet_First          ///< Position at first key/data item
			, EDatabaseCursorGet_FirstDup     ///< Position at first data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			, EDatabaseCursorGet_GetBoth      ///< Position at key/data pair. Only for #EDatabaseSubFlag_SortedDuplicates
			, EDatabaseCursorGet_GetBothRange ///< position at key, nearest data. Only for #EDatabaseSubFlag_SortedDuplicates
			, EDatabaseCursorGet_GetCurrent   ///< Return key/data at current cursor position
			, EDatabaseCursorGet_GetMultiple  ///< Return up to a page of duplicate data items
			                                  ///   from current cursor position. Move cursor to prepare
			                                  ///   for #EDatabaseCursorGet_NextMultiple. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize
			, EDatabaseCursorGet_Last         ///< Position at last key/data item
			, EDatabaseCursorGet_LastDup      ///< Position at last data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			, EDatabaseCursorGet_Next         ///< Position at next data item
			, EDatabaseCursorGet_NextDup      ///< Position at next data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			, EDatabaseCursorGet_NextMultiple ///< Return up to a page of duplicate data items
			                                  ///   from next cursor position. Move cursor to prepare
			                                  ///   for #EDatabaseCursorGet_NextMultiple. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize
			, EDatabaseCursorGet_NextNoDup    ///< Position at first data item of next key
			, EDatabaseCursorGet_Prev         ///< Position at previous data item
			, EDatabaseCursorGet_PrevDup      ///< Position at previous data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			, EDatabaseCursorGet_PrevNoDup    ///< Position at last data item of previous key
			, EDatabaseCursorGet_Set          ///< Position at specified key
			, EDatabaseCursorGet_SetKey       ///< Position at specified key, return key + data
			, EDatabaseCursorGet_SetRange     ///< Position at first key greater than or equal to specified key.
			, EDatabaseCursorGet_PrevMultiple ///< Position at previous page and return up to a page of duplicate data items. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize
		};

		int fg_CursorNext(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation);
		int fg_CursorPrev(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation);
		int fg_CursorSet(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation, int *o_pExact);
		int fg_CursorFirst(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData);
		int fg_CursorLast(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData);

		void fg_CursorInit(CMdbCursor *_pCursor, CMdbTxn *_pTransaction, CDatabaseSubID _DatabaseID, CMdbXCursor *_pXCursor);
		void fg_XCursorInit0(CMdbCursor *_pCursor);
		void fg_XCursorInit1(CMdbCursor *_pCursor, CMdbNode *_pNode);
		void fg_XCursorInit2(CMdbCursor *_pCursor, CMdbXCursor *_pSourceXCursor, int _bNewDuplicateData);

		int fg_Drop0(CMdbCursor *_pCursor, int _bSubDatabases);
		void fg_DefaultCmp(CMdbTxn *_pTransaction, CDatabaseSubID _DatabaseID);

		/** @cond */
		int fg_CmpMemN(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int fg_CmpMemNR(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int fg_CmpInt(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int fg_CmpCInt(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int fg_CmpLong(CDatabaseValue const &_Left, CDatabaseValue const &_Right);

		extern FDatabaseCompare g_fCompareMemN;
		extern FDatabaseCompare g_fCompareMemNR;
		extern FDatabaseCompare g_fCompareInt;
		extern FDatabaseCompare g_fCompareCInt;
		extern FDatabaseCompare g_fCompareLong;

#ifdef DPlatformFamily_Windows
#	ifndef DMaxTlsKeys
#		define DMaxTlsKeys	64
#	endif
		extern int g_nTlsKeys;
		extern pthread_key_t g_TlsKeys[DMaxTlsKeys];
#endif

		/** @endcond */

		/** Compare two items pointing at '#CDatabaseOffset's of unknown alignment. */
#ifdef DMisalignedOk
#	define g_mdb_cmp_clong g_fCompareLong
#else
#	define g_mdb_cmp_clong g_fCompareCInt
#endif

		/** True if we need #mdb_cmp_clong() instead of \b cmp for #MDB_INTEGERDUP */
#define DNeedCmpClong(d_Cmp, d_Ksize) \
	(32 < DMibDatabase_IDBits && \
	 (d_Cmp) == NPrivate::g_fCompareInt && (d_Ksize) == sizeof(CDatabaseOffset))

#ifdef DPlatformFamily_Windows
		extern SECURITY_DESCRIPTOR g_NullSecurityDescriptor;
		extern SECURITY_ATTRIBUTES g_AllSecurityAttributes;
		extern int g_bSecurityInitialized;
#endif

		/** Do not spill pages to disk if txn is getting full, may fail instead */
#define DMdbNoSpill	0x8000

#ifdef DMibDatabase_Always64BitSupport
		void fg_PageUnref(CMdbTxn *txn, CMdbPage *mp);
#	define DMdbPageUnref(d_Txn, d_Mp)	fg_PageUnref(d_Txn, d_Mp)
#else
#	define DMdbPageUnref(d_Txn, d_Mp)
#endif

		/** Only a subset of the @ref mdb_env flags can be changed
		 *	at runtime. Changing other flags requires closing the
		 *	environment and re-opening it with the new flags.
		 */
		static_assert
			(
				!(DValidFlags & DPersistentFlags & (EDatabaseEnvironmentFlag_Changable | EDatabaseEnvironmentFlag_Changeless))
				, "Persistent DB flags & env flags overlap, but both go in mm_flags"
			)
		;

		bool fg_BrokenFSync(CMdbEnv *_pEnvironment, HANDLE _FileDescriptor); // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere

		/** Perform \b act while tracking temporary cursor \b mn */
#define DWithCursorTracking(d_Mn, d_Act) do { \
		CMdbCursor TrackingDummy, *pTracked, **pTrackingList = &(d_Mn).m_pTransaction->m_pCursors[d_Mn.m_DatabaseID]; \
		if ((d_Mn).m_Flags & DCSub) { \
			TrackingDummy.m_Flags =  DCInitialized; \
			TrackingDummy.m_pXCursor = (CMdbXCursor *)&(d_Mn);	\
			pTracked = &TrackingDummy; \
		} else { \
			pTracked = &(d_Mn); \
		} \
		pTracked->m_pNext = *pTrackingList; \
		*pTrackingList = pTracked; \
		{ d_Act; } \
		*pTrackingList = pTracked->m_pNext; \
	} while (0)

		void fg_CursorClose(CMdbCursor *_pCursor);
		int fg_CursorGet(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation);
		int fg_CursorPut(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseWriteFlag _WriteFlags = EDatabaseWriteFlag_None);
		int fg_CursorDel(CMdbCursor *_pCursor, EDatabaseWriteFlag _WriteFlags = EDatabaseWriteFlag_None);
		int fg_CursorCount(CMdbCursor *_pCursor, CDatabaseOffset *o_pCount);

#ifdef DMibDatabase_Always64BitSupport
		void fg_CursorUnref(CMdbCursor *mc);
#	define DMdbCursorUnref(d_Mc, d_Force) \
		(((d_Force) || ((d_Mc)->m_Flags & DCInitialized)) \
		 ? fg_CursorUnref(d_Mc) \
		 : (void)0)

#else
#	define DMdbCursorUnref(d_Mc, d_Force) ((void)0)
#endif /* DMibDatabase_Always64BitSupport */
	}
}

#include "Malterlib_Database_Database_Debug.h"
