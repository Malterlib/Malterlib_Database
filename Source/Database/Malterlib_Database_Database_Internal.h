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
#define _GNU_SOURCE 1
#endif
#if defined(DMibDatabase_Always64BitSupport) || defined(__WIN64__)
#define _FILE_OFFSET_BITS	64
#endif
#ifdef DPlatformFamily_Windows
#include <windows.h>
#include <wchar.h>

/* We use native NT APIs to setup the memory map, so that we can
 * let the DB file grow incrementally instead of always preallocating
 * the full size. These APIs are defined in <wdm.h> and <ntifs.h>
 * but those headers are meant for driver-level development and
 * conflict with the regular user-level headers, so we explicitly
 * declare them here. We get pointers to these functions from
 * NTDLL.DLL at runtime, to avoid buildtime dependencies on any
 * NTDLL import libraries.
 */
typedef NTSTATUS (WINAPI NtCreateSectionFunc)
  (OUT PHANDLE sh, IN ACCESS_MASK acc,
  IN void * oa OPTIONAL,
  IN PLARGE_INTEGER ms OPTIONAL,
  IN ULONG pp, IN ULONG aa, IN HANDLE fh OPTIONAL);

typedef enum _SECTION_INHERIT {
	ViewShare = 1,
	ViewUnmap = 2
} SECTION_INHERIT;

typedef NTSTATUS (WINAPI NtMapViewOfSectionFunc)
  (IN HANDLE sh, IN HANDLE ph,
  IN OUT PVOID *addr, IN ULONG_PTR zbits,
  IN SIZE_T cs, IN OUT PLARGE_INTEGER off OPTIONAL,
  IN OUT PSIZE_T vs, IN SECTION_INHERIT ih,
  IN ULONG at, IN ULONG pp);

typedef NTSTATUS (WINAPI NtCloseFunc)(HANDLE h);

/** getpid() returns int; MinGW defines pid_t but MinGW64 typedefs it
 *  as int64 which is wrong. MSVC doesn't define it at all, so just
 *  don't use it.
 */
using CProcessID = int;
using CThreadID = DWORD;
#include <sys/types.h>
#include <sys/stat.h>
#ifdef __GNUC__
# include <sys/param.h>
#else
# define LITTLE_ENDIAN	1234
# define BIG_ENDIAN	4321
# define BYTE_ORDER	LITTLE_ENDIAN
# ifndef SSIZE_MAX
#  define SSIZE_MAX	INT_MAX
# endif
#endif
#else
#include <sys/types.h>
#include <sys/stat.h>
using CProcessID = pid_t;
using CThreadID = pthread_t;
#include <sys/param.h>
#include <sys/uio.h>
#include <sys/mman.h>
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#include <fcntl.h>
#endif

#if defined(__mips) && defined(__linux)
/* MIPS has cache coherency issues, requires explicit cache control */
#include <asm/cachectl.h>
extern int cacheflush(char *addr, int nbytes, int cache);
#define CACHEFLUSH(addr, bytes, cache)	cacheflush(addr, bytes, cache)
#else
#define CACHEFLUSH(addr, bytes, cache)
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
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined (__sun)
# define _POSIX_PTHREAD_SEMANTICS	1
#endif

#if !(defined(BYTE_ORDER) || defined(__BYTE_ORDER))
#include <netinet/in.h>
#include <resolv.h>	/* defines BYTE_ORDER on HPUX and Solaris */
#endif

#if defined(__APPLE__) || defined (BSD) || defined(__FreeBSD_kernel__)
# if !(defined(MDB_USE_POSIX_MUTEX) || defined(MDB_USE_POSIX_SEM))
# define MDB_USE_SYSV_SEM	1
# endif
# define MDB_FDATASYNC		fsync
#elif defined(__ANDROID__)
# define MDB_FDATASYNC		fsync
#endif

#ifndef DPlatformFamily_Windows
#include <pthread.h>
#include <signal.h>
#ifdef MDB_USE_POSIX_SEM
# define MDB_USE_HASH		1
#include <semaphore.h>
#elif defined(MDB_USE_SYSV_SEM)
#include <sys/ipc.h>
#include <sys/sem.h>
#ifdef _SEM_SEMUN_UNDEFINED
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};
#endif /* _SEM_SEMUN_UNDEFINED */
#else
#define MDB_USE_POSIX_MUTEX	1
#endif /* MDB_USE_POSIX_SEM */
#endif /* !DPlatformFamily_Windows */

#if defined(DPlatformFamily_Windows) + defined(MDB_USE_POSIX_SEM) + defined(MDB_USE_SYSV_SEM) \
	+ defined(MDB_USE_POSIX_MUTEX) != 1
# error "Ambiguous shared-lock implementation"
#endif

#ifndef BYTE_ORDER
# if (defined(_LITTLE_ENDIAN) || defined(_BIG_ENDIAN)) && !(defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN))
/* Solaris just defines one or the other */
#  define LITTLE_ENDIAN	1234
#  define BIG_ENDIAN	4321
#  ifdef _LITTLE_ENDIAN
#   define BYTE_ORDER  LITTLE_ENDIAN
#  else
#   define BYTE_ORDER  BIG_ENDIAN
#  endif
# else
#  define BYTE_ORDER   __BYTE_ORDER
# endif
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN	__LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN	__BIG_ENDIAN
#endif

#if defined(__i386) || defined(__x86_64) || defined(_M_IX86)
#define MISALIGNED_OK	1
#endif

namespace NMib::NDatabase
{
	///	\enum Write Flags
	enum EDatabaseWriteFlag : uint32
	{
		EDatabaseWriteFlag_None = 0
		, EDatabaseWriteFlag_NoOverwrite = 0x10					///< For put: Don't write if the key already exists.
		, EDatabaseWriteFlag_OverwriteAtCurrentCursorPos = 0x40	/// For mdb_cursor_put: overwrite the current key/data pair
		, EDatabaseWriteFlag_ReserveSpace = 0x10000				/**< For put: Just reserve space for data, don't copy it. Return a
																	pointer to the reserved space.
																	*/
		, EDatabaseWriteFlag_Append = 0x20000					///< Data is being appended, don't split full pages.

		// Dup data options
		, EDatabaseWriteFlag_NoDuplicateData = 0x20				/**< Only for #EDatabaseSubFlag_SortedDuplicates<br>
																	For put: Don't write if the key and data pair already exist.<br>
																	For mdb_cursor_del: Remove all duplicate data items.
																	*/
		, EDatabaseWriteFlag_AppendDuplicateData = 0x40000		///< Duplicate data is being appended, don't split full pages.
		, EDatabaseWriteFlag_BatchDuplicateData = 0x80000		///< Store multiple data items in one call. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize.
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
		#define MDB_SUCCESS	 0
			/** key/data pair already exists */
		#define MDB_KEYEXIST	(-30799)
			/** key/data pair not found (EOF) */
		#define MDB_NOTFOUND	(-30798)
			/** Requested page not found - this usually indicates corruption */
		#define MDB_PAGE_NOTFOUND	(-30797)
			/** Located page was wrong type */
		#define MDB_CORRUPTED	(-30796)
			/** Update of meta page failed or environment had fatal error */
		#define MDB_PANIC		(-30795)
			/** Environment version mismatch */
		#define MDB_VERSION_MISMATCH	(-30794)
			/** File is not a valid LMDB file */
		#define MDB_INVALID	(-30793)
			/** Environment mapsize reached */
		#define MDB_MAP_FULL	(-30792)
			/** Environment maxdbs reached */
		#define MDB_DBS_FULL	(-30791)
			/** Environment maxreaders reached */
		#define MDB_READERS_FULL	(-30790)
			/** Too many TLS keys in use - Windows only */
		#define MDB_TLS_FULL	(-30789)
			/** Txn has too many dirty pages */
		#define MDB_TXN_FULL	(-30788)
			/** Cursor stack too deep - internal error */
		#define MDB_CURSOR_FULL	(-30787)
			/** Page has not enough space - internal error */
		#define MDB_PAGE_FULL	(-30786)
			/** Database contents grew beyond environment mapsize */
		#define MDB_MAP_RESIZED	(-30785)
			/** Operation and DB incompatible, or DB type changed. This can mean:
			 *	<ul>
			 *	<li>The operation expects an #EDatabaseSubFlag_SortedDuplicates / #EDatabaseSubFlag_SortedDuplicatesFixedSize database.
			 *	<li>Opening a named DB when the unnamed DB has #EDatabaseSubFlag_SortedDuplicates / #EDatabaseSubFlag_IntegerKeys.
			 *	<li>Accessing a data record as a database, or vice versa.
			 *	<li>The database was dropped and recreated with different flags.
			 *	</ul>
			 */
		#define MDB_INCOMPATIBLE	(-30784)
			/** Invalid reuse of reader locktable slot */
		#define MDB_BAD_RSLOT		(-30783)
			/** Transaction must abort, has a child, or is invalid */
		#define MDB_BAD_TXN			(-30782)
			/** Unsupported size of key/DB name/data, or wrong DUPFIXED size */
		#define MDB_BAD_VALSIZE		(-30781)
			/** The specified DBI was changed unexpectedly */
		#define MDB_BAD_DBI		(-30780)
			/** Unexpected problem - txn should abort */
		#define MDB_PROBLEM		(-30779)
			/** The last defined error code */
		#define MDB_LAST_ERRCODE	MDB_PROBLEM
		/** @} */

		using MDB_env = CDatabaseEnvironment_Internal;
		using MDB_txn = CDatabaseTransactionCommon::CInternal;
		using MDB_cursor = CDatabaseCursorCommon::CInternal;

		#if (BYTE_ORDER == LITTLE_ENDIAN) == (BYTE_ORDER == BIG_ENDIAN)
		# error "Unknown or unsupported endianness (BYTE_ORDER)"
		#elif (-6 & 5) || CHAR_BIT!=8 || UINT_MAX!=0xffffffff
		# error "Two's complement, reasonably sized integer types, please"
		#endif

#			if defined(DMibDebug) || defined(DArchitecture_arm64) || defined(DArchitecture_arm64e)
#				define DMibDatabaseInfrequentSection
#			else
			#ifdef __GNUC__
			/** Put infrequently used env functions in separate section */
			# ifdef __APPLE__
			#  define	DMibDatabaseInfrequentSection	__attribute__ ((section("__TEXT,text_env")))
			# else
			#  define	DMibDatabaseInfrequentSection	__attribute__ ((section("text_env")))
			# endif
			#else
			#	define DMibDatabaseInfrequentSection
			#endif
#			endif

		#ifdef DPlatformFamily_Windows
		#define CALL_CONV WINAPI
		#else
		#define CALL_CONV
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
		#ifndef MDB_DEVEL
		#define MDB_DEVEL 0
		#endif

			/** Wrapper around __func__, which is a C99 feature */
		#if __STDC_VERSION__ >= 199901L
		# define mdb_func_	__func__
		#elif __GNUC__ >= 2 || _MSC_VER >= 1300
		# define mdb_func_	__FUNCTION__
		#else
		/* If a debug message says <mdb_unknown>(), update the #if statements above */
		# define mdb_func_	"<mdb_unknown>"
		#endif

		/* Internal error codes, not exposed outside liblmdb */
		#define	MDB_NO_ROOT		(MDB_LAST_ERRCODE + 10)
		#ifdef DPlatformFamily_Windows
		#define MDB_OWNERDEAD	((int) WAIT_ABANDONED)
		#elif defined MDB_USE_SYSV_SEM
		#define MDB_OWNERDEAD	(MDB_LAST_ERRCODE + 11)
		#elif defined(MDB_USE_POSIX_MUTEX) && defined(EOWNERDEAD)
		#define MDB_OWNERDEAD	EOWNERDEAD	/**< #LOCK_MUTEX0() result if dead owner */
		#endif

		#ifdef __GLIBC__
		#define	GLIBC_VER	((__GLIBC__ << 16 )| __GLIBC_MINOR__)
		#endif
		/** Some platforms define the EOWNERDEAD error code
		 * even though they don't support Robust Mutexes.
		 * Compile with -DMDB_USE_ROBUST=0, or use some other
		 * mechanism like -DMDB_USE_SYSV_SEM instead of
		 * -DMDB_USE_POSIX_MUTEX. (SysV semaphores are
		 * also Robust, but some systems don't support them
		 * either.)
		 */
		#ifndef MDB_USE_ROBUST
		/* Android currently lacks Robust Mutex support. So does glibc < 2.4. */
		# if defined(MDB_USE_POSIX_MUTEX) && (defined(__ANDROID__) || \
			(defined(__GLIBC__) && GLIBC_VER < 0x020004))
		#  define MDB_USE_ROBUST	0
		# else
		#  define MDB_USE_ROBUST	1
		# endif
		#endif /* !MDB_USE_ROBUST */

		#if defined(MDB_USE_POSIX_MUTEX) && (MDB_USE_ROBUST)
		/* glibc < 2.12 only provided _np API */
		#  if (defined(__GLIBC__) && GLIBC_VER < 0x02000c) || \
			(defined(PTHREAD_MUTEX_ROBUST_NP) && !defined(PTHREAD_MUTEX_ROBUST))
		#   define PTHREAD_MUTEX_ROBUST	PTHREAD_MUTEX_ROBUST_NP
		#   define pthread_mutexattr_setrobust(attr, flag)	pthread_mutexattr_setrobust_np(attr, flag)
		#   define pthread_mutex_consistent(mutex)	pthread_mutex_consistent_np(mutex)
		#  endif
		#endif /* MDB_USE_POSIX_MUTEX && MDB_USE_ROBUST */

		#if defined(MDB_OWNERDEAD) && (MDB_USE_ROBUST)
		#define MDB_ROBUST_SUPPORTED	1
		#endif

		#ifdef DPlatformFamily_Windows
		#define MDB_USE_HASH	1
		#define MDB_PIDLOCK	0
		#define pthread_t	HANDLE
		#define pthread_mutex_t	HANDLE
		#define pthread_cond_t	HANDLE
		typedef HANDLE mdb_mutex_t, mdb_mutexref_t;
		#define pthread_key_t	DWORD
		#define pthread_self()	GetCurrentThreadId()
		#define pthread_key_create(x,y)	\
			((*(x) = TlsAlloc()) == TLS_OUT_OF_INDEXES ? ErrCode() : 0)
		#define pthread_key_delete(x)	TlsFree(x)
		#define pthread_getspecific(x)	TlsGetValue(x)
		#define pthread_setspecific(x,y)	(TlsSetValue(x,y) ? 0 : ErrCode())
		#define pthread_mutex_unlock(x)	ReleaseMutex(*x)
		#define pthread_mutex_lock(x)	WaitForSingleObject(*x, INFINITE)
		#define pthread_cond_signal(x)	SetEvent(*x)
		#define pthread_cond_wait(cond,mutex)	do{SignalObjectAndWait(*mutex, *cond, INFINITE, FALSE); WaitForSingleObject(*mutex, INFINITE);}while(0)
		#define LOCK_MUTEX0(mutex)		WaitForSingleObject(mutex, INFINITE)
		#define UNLOCK_MUTEX(mutex)		ReleaseMutex(mutex)
		#define mdb_mutex_consistent(mutex)	0
		#define getpid()	GetCurrentProcessId()
		#define	MDB_FDATASYNC(fd)	(!FlushFileBuffers(fd))
		#define	MDB_MSYNC(addr,len,flags)	(!FlushViewOfFile(addr,len))
		#define	ErrCode()	GetLastError()
		#define	close(fd)	(CloseHandle(fd) ? 0 : -1)
		#define	munmap(ptr,len)	UnmapViewOfFile(ptr)
		#ifdef PROCESS_QUERY_LIMITED_INFORMATION
		#define MDB_PROCESS_QUERY_LIMITED_INFORMATION PROCESS_QUERY_LIMITED_INFORMATION
		#else
		#define MDB_PROCESS_QUERY_LIMITED_INFORMATION 0x1000
		#endif
		#else

			/** For MDB_LOCK_FORMAT: True if readers take a pid lock in the lockfile */
		#define MDB_PIDLOCK			1

		#ifdef MDB_USE_POSIX_SEM

		typedef sem_t *mdb_mutex_t, *mdb_mutexref_t;
		#define LOCK_MUTEX0(mutex)		mdb_sem_wait(mutex)
		#define UNLOCK_MUTEX(mutex)		sem_post(mutex)

		inline_always static int mdb_sem_wait(sem_t *sem)
		{
		   int rc;
		   while ((rc = sem_wait(sem)) && (rc = errno) == EINTR) ;
		   return rc;
		}

		#elif defined MDB_USE_SYSV_SEM

		struct mdb_mutex {
			int semid;
			int semnum;
			int *locked;
		};
		using mdb_mutexref_t = mdb_mutex *;
		using mdb_mutex_t = mdb_mutex[1];

		#define LOCK_MUTEX0(mutex)		mdb_sem_wait(mutex)
		#define UNLOCK_MUTEX(mutex)		do { \
			struct sembuf sb = { 0, 1, SEM_UNDO }; \
			sb.sem_num = (mutex)->semnum; \
			*(mutex)->locked = 0; \
			semop((mutex)->semid, &sb, 1); \
		} while(0)

		inline_always static int mdb_sem_wait(mdb_mutexref_t sem)
		{
			int rc, *locked = sem->locked;
			struct sembuf sb = { 0, -1, SEM_UNDO };
			sb.sem_num = sem->semnum;
			do {
				if (!semop(sem->semid, &sb, 1)) {
					rc = *locked ? MDB_OWNERDEAD : MDB_SUCCESS;
					*locked = 1;
					break;
				}
			} while ((rc = errno) == EINTR);
			return rc;
		}

		#define mdb_mutex_consistent(mutex)	0

		#else	/* MDB_USE_POSIX_MUTEX: */
			/** Shared mutex/semaphore as the original is stored.
			 *
			 *	Not for copies.  Instead it can be assigned to an #mdb_mutexref_t.
			 *	When mdb_mutexref_t is a pointer and mdb_mutex_t is not, then it
			 *	is array[size 1] so it can be assigned to the pointer.
			 */
		typedef pthread_mutex_t mdb_mutex_t[1];
			/** Reference to an #mdb_mutex_t */
		typedef pthread_mutex_t *mdb_mutexref_t;
			/** Lock the reader or writer mutex.
			 *	Returns 0 or a code to give #mdb_mutex_failed(), as in #LOCK_MUTEX().
			 */
		#define LOCK_MUTEX0(mutex)	pthread_mutex_lock(mutex)
			/** Unlock the reader or writer mutex.
			 */
		#define UNLOCK_MUTEX(mutex)	pthread_mutex_unlock(mutex)
			/** Mark mutex-protected data as repaired, after death of previous owner.
			 */
		#define mdb_mutex_consistent(mutex)	pthread_mutex_consistent(mutex)
		#endif	/* MDB_USE_POSIX_SEM || MDB_USE_SYSV_SEM */

			/** Get the error code for the last failed system function.
			 */
		#define	ErrCode()	errno

			/** An abstraction for a file handle.
			 *	On POSIX systems file handles are small integers. On Windows
			 *	they're opaque pointers.
			 */
		#define	HANDLE	int

			/**	A value for an invalid file handle.
			 *	Mainly used to initialize file variables and signify that they are
			 *	unused.
			 */
		#define INVALID_HANDLE_VALUE	(-1)

		#endif

		#ifdef MDB_USE_SYSV_SEM
		#define MNAME_LEN	(sizeof(int))
		#else
		#define MNAME_LEN	(sizeof(pthread_mutex_t))
		#endif

		/** Initial part of #MDB_env.me_mutexname[].
		 *	Changes to this code must be reflected in #MDB_LOCK_FORMAT.
		 */
		#ifdef DPlatformFamily_Windows
		#define MUTEXNAME_PREFIX		"Global\\MDB"
		#elif defined MDB_USE_POSIX_SEM
		#define MUTEXNAME_PREFIX		"/MDB"
		#endif

		/** @} */

		#ifdef MDB_ROBUST_SUPPORTED
			/** Lock mutex, handle any error, set rc = result.
			 *	Return 0 on success, nonzero (not rc) on error.
			 */
		#define LOCK_MUTEX(rc, env, mutex) \
			(((rc) = LOCK_MUTEX0(mutex)) && \
			 ((rc) = env->mdb_mutex_failed(mutex, rc)))
		#else
		#define LOCK_MUTEX(rc, env, mutex) ((rc) = LOCK_MUTEX0(mutex))
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
		#ifndef MDB_DSYNC
		# ifdef O_DSYNC
		# define MDB_DSYNC	O_DSYNC
		# else
		# define MDB_DSYNC	O_SYNC
		# endif
		#endif
		#endif

		/** Function for flushing the data of a file. Define this to fsync
		 *	if fdatasync() is not supported.
		 */
		#ifndef MDB_FDATASYNC
		# define MDB_FDATASYNC	fdatasync
		#endif

		#ifndef MDB_MSYNC
		# define MDB_MSYNC(addr,len,flags)	msync(addr,len,flags)
		#endif

		#ifndef MS_SYNC
		#define	MS_SYNC	1
		#endif

		#ifndef MS_ASYNC
		#define	MS_ASYNC	0
		#endif

			/** A page number in the database.
			 *	Note that 64 bit page numbers are overkill, since pages themselves
			 *	already represent 12-13 bits of addressable memory, and the OS will
			 *	always limit applications to a maximum of 63 bits of address space.
			 *
			 *	@note In the #MDB_node structure, we only store 48 bits of this value,
			 *	which thus limits us to only 60 bits of addressable data.
			 */
		typedef MDB_ID	pgno_t;

			/** A transaction ID.
			 *	See struct MDB_txn.mt_txnid for details.
			 */
		using CTransactionID = MDB_ID;

		/** @defgroup debug	Debug Macros
		 *	@{
		 */
		#ifndef DMibDatabaseDebug
			/**	Enable debug output.  Needs variable argument macros (a C99 feature).
			 *	Set this to 1 for copious tracing. Set to 2 to add dumps of all IDLs
			 *	read from and written to the database (used for free space management).
			 */
		#define DMibDatabaseDebug 0
		#endif

		#if DMibDatabaseDebug
		extern int mdb_debug;
		extern CTransactionID mdb_debug_start;

			/**	Print a debug message with printf formatting.
			 *	Requires double parenthesis around 2 or more args.
			 */
		#	define DMibDatabaseTraceMSVC(d_Expand) d_Expand
		#	define DMibDatabaseTrace(...) { if (NPrivate::mdb_debug) DMibDatabaseTraceMSVC( DMibDatabaseTrace0(__VA_ARGS__) ); }
		#	define DMibDatabaseTrace0(d_Format, ...) DMibTrace2(DMibPFileLineFormat d_Format "{\n}", DMibPFile, DMibPLine, __VA_ARGS__)
		#else
		# define DMibDatabaseTrace(...)	((void) 0)
		#endif
			/**	Print a debug string.
			 *	The string is printed literally, with no format processing.
			 */
		#define DPUTS(arg)	DMibDatabaseTrace("{}", arg)
			/** Debuging output value of a cursor DBI: Negative in a sub-cursor. */
		#define DDBI(mc) \
			(((mc)->mc_flags & C_SUB) ? -(int)(mc)->mc_dbi : (int)(mc)->mc_dbi)
		/** @} */

			/**	@brief The maximum size of a database page.
			 *
			 *	It is 32k or 64k, since value-PAGEBASE must fit in
			 *	#MDB_page.%mp_upper.
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
		#define MAX_PAGESIZE	 (PAGEBASE ? 0x10000 : 0x8000)

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
		#define MDB_MINKEYS	 2

			/**	A stamp that identifies a file as an LMDB file.
			 *	There's nothing special about this value other than that it is easily
			 *	recognizable, and it will reflect any byte order mismatches.
			 */
		#define MDB_MAGIC	 0xBEEFC0DE

			/**	The version number for a database's datafile format. */
		#define MDB_DATA_VERSION	 ((MDB_DEVEL) ? 999 : 1)
			/**	The version number for a database's lockfile format. */
		#define MDB_LOCK_VERSION	 ((MDB_DEVEL) ? 999 : 2)
			/** Number of bits representing #MDB_LOCK_VERSION in #MDB_LOCK_FORMAT.
			 *	The remaining bits must leave room for #MDB_lock_desc.
			 */
		#define MDB_LOCK_VERSION_BITS 12

			/**	@brief The max size of a key we can write, or 0 for computed max.
			 *
			 *	This macro should normally be left alone or set to 0.
			 *	Note that a database with big keys or dupsort data cannot be
			 *	reliably modified by a liblmdb which uses a smaller max.
			 *	The default is 511 for backwards compat, or 0 when #MDB_DEVEL.
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
		#ifndef MDB_MAXKEYSIZE
		//#define MDB_MAXKEYSIZE	 ((MDB_DEVEL) ? 0 : 511)
		#define MDB_MAXKEYSIZE	 0
		#endif

			/**	The maximum size of a key we can write to the environment. */
		#if MDB_MAXKEYSIZE
		#define ENV_MAXKEY(env)	(MDB_MAXKEYSIZE)
		#else
		#define ENV_MAXKEY(env)	((env)->me_maxkey)
		#endif

			/**	@brief The maximum size of a data item.
			 *
			 *	We only store a 32 bit value for node sizes.
			 */
		#define MAXDATASIZE	0xffffffffUL

		#if DMibDatabaseDebug
			/**	Key size which fits in a #DKBUF.
			 *	@ingroup debug
			 */
		#define DKBUF_MAXKEYSIZE ((MDB_MAXKEYSIZE) > 0 ? (MDB_MAXKEYSIZE) : 511)
			/**	A key buffer.
			 *	@ingroup debug
			 *	This is used for printing a hex dump of a key's contents.
			 */
		#define DKBUF	char kbuf[DKBUF_MAXKEYSIZE*2+1]
			/**	Display a key in hex.
			 *	@ingroup debug
			 *	Invoke a function to display a key in hex.
			 */
		#define	DKEY(x)	mdb_dkey(x, kbuf)
		#else
		#define	DKBUF
		#define DKEY(x)	0
		#endif

			/** An invalid page number.
			 *	Mainly used to denote an empty tree.
			 */
		#define P_INVALID	 (~(pgno_t)0)

			/** Test if the flags \b f are set in a flag word \b w. */
		#define F_ISSET(w, f)	 (((w) & (f)) == (f))

			/** Round \b n up to an even number. */
		#define EVEN(n)		(((n) + 1U) & -2) /* sign-extending -2 to match n+1U */

			/** Least significant 1-bit of \b n.  n must be of an unsigned type. */
		#define LOW_BIT(n)		((n) & (-(n)))

			/** (log2(\b p2) % \b n), for p2 = power of 2 and 0 < n < 8. */
		#define LOG2_MOD(p2, n)	(7 - 86 / ((p2) % ((1U<<(n))-1) + 11))
			/* Explanation: Let p2 = 2**(n*y + x), x<n and M = (1U<<n)-1. Now p2 =
			 * (M+1)**y * 2**x = 2**x (mod M). Finally "/" "happens" to return 7-x.
			 */

			/**	Used for offsets within a single page.
			 *	Since memory pages are typically 4 or 8KB in size, 12-13 bits,
			 *	this is plenty.
			 */
		typedef uint16	 indx_t;

		typedef unsigned long long	mdb_hash_t;

		mdb_hash_t mdb_hash(const void *val, size_t len);
		void mdb_env_mname_init(MDB_env *env);
		#define MUTEXNAME(env, ch) ( \
			(void) ((env)->me_mutexname[sizeof(MUTEXNAME_PREFIX)-1] = (ch)), \
			(env)->me_mutexname)

		#ifdef DPlatformFamily_Windows
		/** @brief Map a result from an NTAPI call to WIN32. */
		DWORD mdb_nt2win32(NTSTATUS st);
		#endif

			/**	Default size of memory map.
			 *	This is certainly too small for any actual applications. Apps should always set
			 *	the size explicitly using #f_SetMapSize().
			 */
		#define DEFAULT_MAPSIZE	1048576

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
		#define DEFAULT_READERS	126

		/**	The information we store in a single slot of the reader table.
		 *	In addition to a transaction ID, we also record the process and
		 *	thread ID that owns a slot, so that we can detect stale information,
		 *	e.g. threads or processes that went away without cleaning up.
		 *	@note We currently don't check for stale records. We simply re-init
		 *	the table when we know that we're the only process opening the
		 *	lock file.
		 */
		struct align_cacheline MDB_reader
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
			NAtomic::TCAtomic<CThreadID> m_OwningThreadID;
		};

			/** The header for the reader table.
			 *	The table resides in a memory-mapped file. (This is a different file
			 *	than is used for the main database.)
			 *
			 *	For POSIX the actual mutexes reside in the shared memory of this
			 *	mapped file. On Windows, mutexes are named objects allocated by the
			 *	kernel; we store the mutex names in this mapped file so that other
			 *	processes can grab them. This same approach is also used on
			 *	MacOSX/Darwin (using named semaphores) since MacOSX doesn't support
			 *	process-shared POSIX mutexes. For these cases where a named object
			 *	is used, the object name is derived from a 64 bit FNV hash of the
			 *	environment pathname. As such, naming collisions are extremely
			 *	unlikely. If a collision occurs, the results are unpredictable.
			 */
		struct MDB_txbody {
				/** Stamp identifying this as an LMDB file. It must be set
				 *	to #MDB_MAGIC. */
			uint32	mtb_magic;
				/** Format of this lock file. Must be set to #MDB_LOCK_FORMAT. */
			uint32	mtb_format;
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
			mdb_hash_t			mtb_mutexid;
		#elif defined(MDB_USE_SYSV_SEM)
			int 	mtb_semid;
			int		mtb_rlocked;
		#else
				/** Mutex protecting access to this table.
				 *	This is the reader table lock used with LOCK_MUTEX().
				 */
			mdb_mutex_t	mtb_rmutex;
		#endif
		};

		/** The actual reader table definition. */
		struct CLocksMemoryMap
		{
			align_cacheline MDB_txbody m_TransactionBody;

#if !(defined(DPlatformFamily_Windows) || defined(MDB_USE_POSIX_SEM))
#ifdef MDB_USE_SYSV_SEM
			align_cacheline int m_WriteLock;
#else
			align_cacheline mdb_mutex_t	m_WriteLock;
#endif
#endif
			MDB_reader	m_Readers[1];
		};

			/** Lockfile format signature: version, features and field layout */
		#define MDB_LOCK_FORMAT \
			((uint32_t)         \
			 (((MDB_LOCK_VERSION) % (1U << MDB_LOCK_VERSION_BITS)) \
			  + MDB_lock_desc     * (1U << MDB_LOCK_VERSION_BITS)))

			/** Lock type and layout. Values 0-119. DPlatformFamily_Windows implies #MDB_PIDLOCK.
			 *	Some low values are reserved for future tweaks.
			 */
		#ifdef DPlatformFamily_Windows
		# define MDB_LOCK_TYPE	(0 + alignof(mdb_hash_t)/8 % 2)
		#elif defined MDB_USE_POSIX_SEM
		# define MDB_LOCK_TYPE	(4 + alignof(mdb_hash_t)/8 % 2)
		#elif defined MDB_USE_SYSV_SEM
		# define MDB_LOCK_TYPE	(8)
		#elif defined MDB_USE_POSIX_MUTEX
		/* We do not know the inside of a POSIX mutex and how to check if mutexes
		 * used by two executables are compatible. Just check alignment and size.
		 */
		# define MDB_LOCK_TYPE	(10 + \
				LOG2_MOD(alignof(pthread_mutex_t), 5) + \
				sizeof(pthread_mutex_t) / 4U % 22 * 5)
		#endif

		enum {
			/** Magic number for lockfile layout and features.
			 *
			 *  This *attempts* to stop liblmdb variants compiled with conflicting
			 *	options from using the lockfile at the same time and thus breaking
			 *	it.  It describes locking types, and sizes and sometimes alignment
			 *	of the various lockfile items.
			 *
			 *	The detected ranges are mostly guesswork, or based simply on how
			 *	big they could be without using more bits.  So we can tweak them
			 *	in good conscience when updating #MDB_LOCK_VERSION.
			 */
			MDB_lock_desc =
			/* Default CACHELINE=64 vs. other values (have seen mention of 32-256) */
			(DMibPMemoryCacheLineSize==64 ? 0 : 1 + LOG2_MOD(DMibPMemoryCacheLineSize >> int(DMibPMemoryCacheLineSize>64), 5))
			+ 6  * (sizeof(CProcessID)/4 % 3)    /* legacy(2) to word(4/8)? */
			+ 18 * (sizeof(pthread_t)/4 % 5)    /* can be struct{id, active data} */
			+ 90 * (sizeof(MDB_txbody) / DMibPMemoryCacheLineSize % 3)
			+ 270 * (MDB_LOCK_TYPE % 120)
			/* The above is < 270*120 < 2**15 */
			+ ((sizeof(CTransactionID) == 8) << 15)    /* 32bit/64bit */
			+ ((sizeof(MDB_reader) > DMibPMemoryCacheLineSize) << 16)
			/* Not really needed - implied by MDB_LOCK_TYPE != (DPlatformFamily_Windows locking) */
			+ (((MDB_PIDLOCK) != 0)   << 17)
			/* 18 bits total: Must be <= (32 - MDB_LOCK_VERSION_BITS). */
		};
		/** @} */

		/** Common header for all page types. The page type depends on #mp_flags.
		 *
		 * #P_BRANCH and #P_LEAF pages have unsorted '#MDB_node's at the end, with
		 * sorted #mp_ptrs[] entries referring to them. Exception: #P_LEAF2 pages
		 * omit mp_ptrs and pack sorted #MDB_DUPFIXED values after the page header.
		 *
		 * #P_OVERFLOW records occupy one or more contiguous pages where only the
		 * first has a page header. They hold the real data of #F_BIGDATA nodes.
		 *
		 * #P_SUBP sub-pages are small leaf "pages" with duplicate data.
		 * A node with flag #F_DUPDATA but not #F_SUBDATA contains a sub-page.
		 * (Duplicate data can also go in sub-databases, which use normal pages.)
		 *
		 * #P_META pages contain #MDB_meta, the start point of an LMDB snapshot.
		 *
		 * Each non-metapage up to #MDB_meta.%mm_last_pg is reachable exactly once
		 * in the snapshot: Either used by a database or listed in a freeDB record.
		 */
		struct MDB_page {
			union {
				pgno_t		p_pgno;	/**< page number */
				struct MDB_page *p_next; /**< for in-memory list of freed pages */
			} mp_p;
			uint16	mp_pad;			/**< key size if this is a LEAF2 page */
		/**	@defgroup mdb_page	Page Flags
		 *	@ingroup internal
		 *	Flags for the page headers.
		 *	@{
		 */
		#define	P_BRANCH	 0x01		/**< branch page */
		#define	P_LEAF		 0x02		/**< leaf page */
		#define	P_OVERFLOW	 0x04		/**< overflow page */
		#define	P_META		 0x08		/**< meta page */
		#define	P_DIRTY		 0x10		/**< dirty page, also set for #P_SUBP pages */
		#define	P_LEAF2		 0x20		/**< for #EDatabaseSubFlag_SortedDuplicatesFixedSize records */
		#define	P_SUBP		 0x40		/**< for #EDatabaseSubFlag_SortedDuplicates sub-pages */
		#define	P_LOOSE		 0x4000		/**< page was dirtied then freed, can be reused */
		#define	P_KEEP		 0x8000		/**< leave this page alone during spill */
		/** @} */
			uint16	mp_flags;		/**< @ref mdb_page */
		#define mp_lower	mp_pb.pb.pb_lower
		#define mp_upper	mp_pb.pb.pb_upper
		#define mp_pages	mp_pb.pb_pages
			union {
				struct {
					indx_t		pb_lower;		/**< lower bound of free space */
					indx_t		pb_upper;		/**< upper bound of free space */
				} pb;
				uint32	pb_pages;	/**< number of overflow pages */
			} mp_pb;
			indx_t		mp_ptrs[1];		/**< dynamic size */
		};

			/** Size of the page header, excluding dynamic data at the end */
		#define PAGEHDRSZ	 ((unsigned) offsetof(MDB_page, mp_ptrs))

			/** Address of first usable data byte in a page, after the header */
		#define METADATA(p)	 ((uint8 *)(p) + PAGEHDRSZ)

			/** ITS#7713, change PAGEBASE to handle 65536 byte pages */
		//#define	PAGEBASE	((MDB_DEVEL) ? PAGEHDRSZ : 0)
		#define	PAGEBASE	PAGEHDRSZ

			/** Number of nodes on a page */
		#define NUMKEYS(p)	 (((p)->mp_lower - (PAGEHDRSZ-PAGEBASE)) >> 1)

			/** The amount of space remaining in the page */
		#define SIZELEFT(p)	 (indx_t)((p)->mp_upper - (p)->mp_lower)

			/** The percentage of space used in the page, in tenths of a percent. */
		#define PAGEFILL(env, p) (1000L * ((env)->me_psize - PAGEHDRSZ - SIZELEFT(p)) / \
						((env)->me_psize - PAGEHDRSZ))
			/** The minimum page fill factor, in tenths of a percent.
			 *	Pages emptier than this are candidates for merging.
			 */
		#define FILL_THRESHOLD	 250

			/** Test if a page is a leaf page */
		#define IS_LEAF(p)	 F_ISSET((p)->mp_flags, P_LEAF)
			/** Test if a page is a LEAF2 page */
		#define IS_LEAF2(p)	 F_ISSET((p)->mp_flags, P_LEAF2)
			/** Test if a page is a branch page */
		#define IS_BRANCH(p)	 F_ISSET((p)->mp_flags, P_BRANCH)
			/** Test if a page is an overflow page */
		#define IS_OVERFLOW(p)	 F_ISSET((p)->mp_flags, P_OVERFLOW)
			/** Test if a page is a sub page */
		#define IS_SUBP(p)	 F_ISSET((p)->mp_flags, P_SUBP)

			/** The number of overflow pages needed to store the given size. */
		#define OVPAGES(size, psize)	((PAGEHDRSZ-1 + (size)) / (psize) + 1)

			/** Link in #MDB_txn.%mt_loose_pgs list.
			 *  Kept outside the page header, which is needed when reusing the page.
			 */
		#define NEXT_LOOSE_PAGE(p)		(*(MDB_page **)((p) + 2))

			/** Header for a single key/data pair within a page.
			 * Used in pages of type #P_BRANCH and #P_LEAF without #P_LEAF2.
			 * We guarantee 2-byte alignment for 'MDB_node's.
			 *
			 * #mn_lo and #mn_hi are used for data size on leaf nodes, and for child
			 * pgno on branch nodes.  On 64 bit platforms, #mn_flags is also used
			 * for pgno.  (Branch nodes have no flags).  Lo and hi are in host byte
			 * order in case some accesses can be optimized to 32-bit word access.
			 *
			 * Leaf node flags describe node contents.  #F_BIGDATA says the node's
			 * data part is the page number of an overflow page with actual data.
			 * #F_DUPDATA and #F_SUBDATA can be combined giving duplicate data in
			 * a sub-page/sub-database, and named databases (just #F_SUBDATA).
			 */
		struct MDB_node {
			/** part of data size or pgno
			 *	@{ */
		#if BYTE_ORDER == LITTLE_ENDIAN
			unsigned short	mn_lo, mn_hi;
		#else
			unsigned short	mn_hi, mn_lo;
		#endif
			/** @} */
		/** @defgroup mdb_node Node Flags
		 *	@ingroup internal
		 *	Flags for node headers.
		 *	@{
		 */
		#define F_BIGDATA	 0x01			/**< data put on overflow page */
		#define F_SUBDATA	 0x02			/**< data is a sub-database */
		#define F_DUPDATA	 0x04			/**< data has duplicates */

		/** valid flags for #mdb_node_add() */
		#define	NODE_ADD_FLAGS	(F_DUPDATA|F_SUBDATA|EDatabaseWriteFlag_ReserveSpace|EDatabaseWriteFlag_Append)

		/** @} */
			unsigned short	mn_flags;		/**< @ref mdb_node */
			unsigned short	mn_ksize;		/**< key size */
			uint8		mn_data[1];			/**< key and data are appended here */
		};

			/** Size of the node header, excluding dynamic data at the end */
		#define NODESIZE	 offsetof(MDB_node, mn_data)

			/** Bit position of top word in page number, for shifting mn_flags */
		#define PGNO_TOPWORD ((pgno_t)-1 > 0xffffffffu ? 32 : 0)

			/** Size of a node in a branch page with a given key.
			 *	This is just the node header plus the key, there is no data.
			 */
		#define INDXSIZE(k)	 (NODESIZE + ((k) == nullptr ? 0 : (k)->m_Size))

			/** Size of a node in a leaf page with a given key and data.
			 *	This is node header plus key plus data size.
			 */
		#define LEAFSIZE(k, d)	 (NODESIZE + (k)->m_Size + (d)->m_Size)

			/** Address of node \b i in page \b p */
		#define NODEPTR(p, i)	 ((MDB_node *)((char *)(p) + (p)->mp_ptrs[i] + PAGEBASE))

			/** Address of the key for the node */
		#define NODEKEY(node)	 (uint8 *)((node)->mn_data)

			/** Address of the data for a node */
		#define NODEDATA(node)	 ((uint8 *)(node)->mn_data + (node)->mn_ksize)

			/** Get the page number pointed to by a branch node */
		#define NODEPGNO(node) \
			((node)->mn_lo | ((pgno_t) (node)->mn_hi << 16) | \
			 (PGNO_TOPWORD ? ((pgno_t) (node)->mn_flags << PGNO_TOPWORD) : 0))
			/** Set the page number in a branch node */
		#define SETPGNO(node,pgno)	do { \
			(node)->mn_lo = (pgno) & 0xffff; (node)->mn_hi = (pgno) >> 16; \
			if (PGNO_TOPWORD) (node)->mn_flags = (pgno) >> PGNO_TOPWORD; } while(0)

			/** Get the size of the data in a leaf node */
		#define NODEDSZ(node)	 ((node)->mn_lo | ((unsigned)(node)->mn_hi << 16))
			/** Set the size of the data for a leaf node */
		#define SETDSZ(node,size)	do { \
			(node)->mn_lo = (size) & 0xffff; (node)->mn_hi = (size) >> 16;} while(0)
			/** The size of a key in a node */
		#define NODEKSZ(node)	 ((node)->mn_ksize)

			/** Copy a page number from src to dst */
		#ifdef MISALIGNED_OK
		#define COPY_PGNO(dst,src)	dst = src
		#else
		#if DMibDatabase_IDBits > 32
		#define COPY_PGNO(dst,src)	do { \
			unsigned short *s, *d;	\
			s = (unsigned short *)&(src);	\
			d = (unsigned short *)&(dst);	\
			*d++ = *s++;	\
			*d++ = *s++;	\
			*d++ = *s++;	\
			*d = *s;	\
		} while (0)
		#else
		#define COPY_PGNO(dst,src)	do { \
			unsigned short *s, *d;	\
			s = (unsigned short *)&(src);	\
			d = (unsigned short *)&(dst);	\
			*d++ = *s++;	\
			*d = *s;	\
		} while (0)
		#endif
		#endif
			/** The address of a key in a LEAF2 page.
			 *	LEAF2 pages are used for #EDatabaseSubFlag_SortedDuplicatesFixedSize sorted-duplicate sub-DBs.
			 *	There are no node headers, keys are stored contiguously.
			 */
		#define LEAF2KEY(p, i, ks)	((uint8 *)(p) + PAGEHDRSZ + ((i)*(ks)))

			/** Set the \b node's key into \b keyptr, if requested. */
		#define MDB_GET_KEY(node, keyptr)	{ if ((keyptr) != nullptr) { \
			(keyptr)->m_Size = NODEKSZ(node); (keyptr)->m_pData = (uint8 *)NODEKEY(node); } }

			/** Set the \b node's key into \b key. */
		#define MDB_GET_KEY2(node, key)	{ key.m_Size = NODEKSZ(node); key.m_pData = NODEKEY(node); }

			/** Information about a single database in the environment. */
		struct MDB_db {
			uint32	md_pad;		/**< also ksize for LEAF2 pages */
			uint16	md_flags;	/**< @ref f_OpenDatabase */
			uint16	md_depth;	/**< depth of this tree */
			pgno_t		md_branch_pages;	/**< number of internal pages */
			pgno_t		md_leaf_pages;		/**< number of leaf pages */
			pgno_t		md_overflow_pages;	/**< number of overflow pages */
			CDatabaseOffset	md_entries;		/**< number of data items */
			pgno_t		md_root;		/**< the root page of this tree */
		};

		#define MDB_VALID	0x8000		/**< DB handle is valid, for me_dbflags */
		#define PERSISTENT_FLAGS	(0xffff & ~(MDB_VALID))
			/** f_OpenDatabase flags */
		#define VALID_FLAGS	(EDatabaseSubFlag_ReverseKeys|EDatabaseSubFlag_SortedDuplicates|EDatabaseSubFlag_IntegerKeys|EDatabaseSubFlag_SortedDuplicatesFixedSize|\
			EDatabaseSubFlag_SortedDuplicatesIntegerKeys|EDatabaseSubFlag_SortedDuplicatesReverseKeys|EDatabaseSubFlag_Create)

			/** Handle for the DB used to track free pages. */
		#define	FREE_DBI	0
			/** Handle for the default DB. */
		#define	MAIN_DBI	1
			/** Number of DBs in metapage (free and main) - also hardcoded elsewhere */
		#define CORE_DBS	2

			/** Number of meta pages - also hardcoded elsewhere */
		#define NUM_METAS	2

			/** Meta page content.
			 *	A meta page is the start point for accessing a database snapshot.
			 *	Pages 0-1 are meta pages. Transaction N writes meta page #(N % 2).
			 */
		struct MDB_meta {
				/** Stamp identifying this as an LMDB file. It must be set
				 *	to #MDB_MAGIC. */
			uint32	mm_magic;
				/** Version number of this file. Must be set to #MDB_DATA_VERSION. */
			uint32	mm_version;
		#ifdef DMibDatabase_Always64BitSupport
			union {		/* always zero since we don't support fixed mapping in DMibDatabase_Always64BitSupport */
				MDB_ID	mmun_ull;
				void *mmun_address;
			} mm_un;
		#define	mm_address mm_un.mmun_address
		#else
			void		*mm_address;		/**< address for fixed mapping */
		#endif
			CDatabaseOffset	mm_mapsize;			/**< size of mmap region */
			MDB_db		mm_dbs[CORE_DBS];	/**< first is free space, 2nd is main db */
			/** Last used page in the datafile.
			 *	Actually the file may be shorter if the freeDB lists the final pages.
			 */
			pgno_t		mm_last_pg;
			NAtomic::TCAtomicAggregate<CTransactionID> mm_txnid;	/**< txnid that committed this page */
		};

			/** Buffer for a stack-allocated meta page.
			 *	The members define size and alignment, and silence type
			 *	aliasing warnings.  They are not used directly; that could
			 *	mean incorrectly using several union members in parallel.
			 */
		typedef union MDB_metabuf {
			MDB_page	mb_page;
			struct {
				char		mm_pad[PAGEHDRSZ];
				MDB_meta	mm_meta;
			} mb_metabuf;
		} MDB_metabuf;

			/** Auxiliary DB info.
			 *	The information here is mostly static/read-only. There is
			 *	only a single copy of this record in the environment.
			 */
		struct MDB_dbx
		{
			CDatabaseValue m_DatabaseName;				///< name of the database
			FDatabaseCompare m_fKeyCompare;		///< function for comparing keys
			FDatabaseCompare m_fDataCompare;	///< function for comparing data items
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

		bool f_Get(CDatabaseSubID _DatabaseID, CDatabaseValue *key, CDatabaseValue *data);
		void f_Put(CDatabaseSubID _DatabaseID, CDatabaseValue *key, CDatabaseValue *data, EDatabaseWriteFlag _WriteFlags);
		void f_Delete(CDatabaseSubID _DatabaseID, CDatabaseValue const *key, CDatabaseValue const *data);
		int f_KeyCompare(CDatabaseSubID _DatabaseID, const CDatabaseValue *a, const CDatabaseValue *b);
		int f_DataCompare(CDatabaseSubID _DatabaseID, const CDatabaseValue *a, const CDatabaseValue *b);

		NPrivate::MDB_cursor *f_Cursor(CDatabaseSubID _DatabaseID);

		NPrivate::MDB_txn		*mt_parent;		/**< parent of a nested txn */
		/** Nested txn under this txn, set together with flag #MDB_TXN_HAS_CHILD */
		NPrivate::MDB_txn		*mt_child;
		NPrivate::pgno_t		mt_next_pgno;	/**< next unallocated page */
	#ifdef DMibDatabase_Always64BitSupport
		NPrivate::pgno_t		mt_last_pgno;	/**< last written page */
	#endif
		/** The ID of this transaction. IDs are integers incrementing from 1.
		 *	Only committed write transactions increment the ID. If a transaction
		 *	aborts, the ID may be re-used by the next writer.
		 */
		NPrivate::CTransactionID		mt_txnid;
		NPrivate::MDB_env		*mt_env;		/**< the DB environment */
		/** The list of pages that became unused during this transaction.
		 */
		NPrivate::MDB_IDL		mt_free_pgs;
		/** The list of loose pages that became unused and may be reused
		 *	in this transaction, linked through #NEXT_LOOSE_PAGE(page).
		 */
		NPrivate::MDB_page	*mt_loose_pgs;
		/** Number of loose pages (#mt_loose_pgs) */
		int			mt_loose_count;
		/** The sorted list of dirty pages we temporarily wrote to disk
		 *	because the dirty list was full. page numbers in here are
		 *	shifted left by 1, deleted slots have the LSB set.
		 */
		NPrivate::MDB_IDL		mt_spill_pgs;
		union {
			/** For write txns: Modified pages. Sorted when not EDatabaseEnvironmentFlag_WriteMap. */
			NPrivate::MDB_ID2L	dirty_list;
			/** For read txns: This thread/txn's reader table slot, or nullptr. */
			NPrivate::MDB_reader	*reader;
		} mt_u;
		/** Array of records for each DB known in the environment. */
		NPrivate::MDB_dbx		*mt_dbxs;
		/** Array of MDB_db records for each known DB */
		NPrivate::MDB_db		*mt_dbs;
		/** Array of sequence numbers for each DB handle */
		uint32	*mt_dbiseqs;
	/** @defgroup mt_dbflag	Transaction DB Flags
	 *	@ingroup internal
	 * @{
	 */
	#define DB_DIRTY	0x01		/**< DB was written in this txn */
	#define DB_STALE	0x02		/**< Named-DB record is older than txnID */
	#define DB_NEW		0x04		/**< Named-DB handle opened in this txn */
	#define DB_VALID	0x08		/**< DB handle is valid, see also #MDB_VALID */
	#define DB_USRVALID	0x10		/**< As #DB_VALID, but not set for #FREE_DBI */
	#define DB_DUPDATA	0x20		/**< DB is #MDB_DUPSORT data */
	/** @} */
		/** In write txns, array of cursors for each DB */
		NPrivate::MDB_cursor	**mt_cursors;
		/** Array of flags for each DB */
		unsigned char	*mt_dbflags;
	#ifdef DMibDatabase_Always64BitSupport
		/** List of read-only pages (actually chunks) */
		NPrivate::MDB_ID3L	mt_rpages;
		/** We map chunks of 16 pages. Even though Windows uses 4KB pages, all
		 * mappings must begin on 64KB boundaries. So we round off all pgnos to
		 * a chunk boundary. We do the same on Linux for symmetry, and also to
		 * reduce the frequency of mmap/munmap calls.
		 */
	#define MDB_RPAGE_CHUNK	16
	#define MDB_TRPAGE_SIZE	4096	/**< size of #mt_rpages array of chunks */
	#define MDB_TRPAGE_MAX	(MDB_TRPAGE_SIZE-1)	/**< maximum chunk index */
		uint32 mt_rpcheck;	/**< threshold for reclaiming unref'd chunks */
	#endif
		/**	Number of DB records in use, or 0 when the txn is finished.
		 *	This number only ever increments until the txn finishes; we
		 *	don't decrement it when individual DB handles are closed.
		 */
		CDatabaseSubID		mt_numdbs;

	/** @defgroup mdb_txn	Transaction Flags
	 *	@ingroup internal
	 *	@{
	 */
		/** #mdb_txn_begin() flags */
	#define MDB_TXN_BEGIN_FLAGS	(EDatabaseEnvironmentFlag_NoMetaSync|EDatabaseEnvironmentFlag_NoSync|EDatabaseEnvironmentFlag_ReadOnly)
	#define MDB_TXN_NOMETASYNC	EDatabaseEnvironmentFlag_NoMetaSync	/**< don't sync meta for this txn on commit */
	#define MDB_TXN_NOSYNC		EDatabaseEnvironmentFlag_NoSync	/**< don't sync this txn on commit */
	#define MDB_TXN_RDONLY		EDatabaseEnvironmentFlag_ReadOnly	/**< read-only transaction */
		/* internal txn flags */
	#define MDB_TXN_WRITEMAP	EDatabaseEnvironmentFlag_WriteMap	/**< copy of #MDB_env flag in writers */
	#define MDB_TXN_FINISHED	0x01		/**< txn is finished or never began */
	#define MDB_TXN_ERROR		0x02		/**< txn is unusable after an error */
	#define MDB_TXN_DIRTY		0x04		/**< must write, even if dirty list is empty */
	#define MDB_TXN_SPILLS		0x08		/**< txn or a parent has spilled pages */
	#define MDB_TXN_HAS_CHILD	0x10		/**< txn has an #MDB_txn.%mt_child */
		/** most operations on the txn are currently illegal */
	#define MDB_TXN_BLOCKED		(MDB_TXN_FINISHED|MDB_TXN_ERROR|MDB_TXN_HAS_CHILD)
	/** @} */
		uint32	mt_flags;		/**< @ref mdb_txn */
		/** #dirty_list room: Array size - \#dirty pages visible to this txn.
		 *	Includes ancestor txns' dirty pages not hidden by other txns'
		 *	dirty/spilled pages. Thus commit(nested txn) has room to merge
		 *	dirty_list into mt_parent after freeing hidden mt_parent pages.
		 */
		uint32	mt_dirty_room;
	};

	namespace NPrivate
	{
		/** Enough space for 2^32 nodes with minimum of 2 keys per node. I.e., plenty.
		 * At 4 keys per node, enough for 2^64 nodes, so there's probably no need to
		 * raise this on a 64 bit machine.
		 */
		#define CURSOR_STACK		 32

		struct MDB_xcursor;

			/** Cursors are used for all DB operations.
			 *	A cursor holds a path of (page pointer, key index) from the DB
			 *	root to a position in the DB, plus other state. #EDatabaseSubFlag_SortedDuplicates
			 *	cursors include an xcursor to the current data item. Write txns
			 *	track their cursors and keep them up to date when data moves.
			 *	Exception: An xcursor's pointer to a #P_SUBP page can be stale.
			 *	(A node with #F_DUPDATA but no #F_SUBDATA contains a subpage).
			 */

	}

	struct CDatabaseCursorCommon::CInternal
	{
		/** Next cursor on this DB in this txn */
		CInternal	*mc_next;
		/** Backup of the original cursor if this cursor is a shadow */
		CInternal	*mc_backup;
		/** Context used for databases with #EDatabaseSubFlag_SortedDuplicates, otherwise nullptr */
		NPrivate::MDB_xcursor	*mc_xcursor;
		/** The transaction that owns this cursor */
		NPrivate::MDB_txn		*mc_txn;
		/** The database handle this cursor operates on */
		CDatabaseSubID		mc_dbi;
		/** The database record for this cursor */
		NPrivate::MDB_db		*mc_db;
		/** The database auxiliary record for this cursor */
		NPrivate::MDB_dbx		*mc_dbx;
		/** The @ref mt_dbflag for this database */
		unsigned char	*mc_dbflag;
		unsigned short 	mc_snum;	/**< number of pushed pages */
		unsigned short	mc_top;		/**< index of top page, normally mc_snum-1 */
	/** @defgroup CInternal	Cursor Flags
	 *	@ingroup internal
	 *	Cursor state flags.
	 *	@{
	 */
	#define C_INITIALIZED	0x01	/**< cursor has been initialized and is valid */
	#define C_EOF	0x02			/**< No more data */
	#define C_SUB	0x04			/**< Cursor is a sub-cursor */
	#define C_DEL	0x08			/**< last op was a cursor_del */
	#define C_UNTRACK	0x40		/**< Un-track cursor when closing */
	#define C_WRITEMAP	MDB_TXN_WRITEMAP /**< Copy of txn flag */
	/** Read-only cursor into the txn's original snapshot in the map.
	 *	Set for read-only txns, and in #mdb_page_alloc() for #FREE_DBI when
	 *	#MDB_DEVEL & 2. Only implements code which is necessary for this.
	 */
	#define C_ORIG_RDONLY	MDB_TXN_RDONLY
	/** @} */
		uint32	mc_flags;	/**< @ref mdb_cursor */
		NPrivate::MDB_page	*mc_pg[CURSOR_STACK];	/**< stack of pushed pages */
		NPrivate::indx_t		mc_ki[CURSOR_STACK];	/**< stack of page indices */
	#ifdef DMibDatabase_Always64BitSupport
		NPrivate::MDB_page	*mc_ovpg;		/**< a referenced overflow page */
	#	define MC_OVPG(mc)			((mc)->mc_ovpg)
	#	define MC_SET_OVPG(mc, pg)	((mc)->mc_ovpg = (pg))
	#else
	#	define MC_OVPG(mc)			((MDB_page *)0)
	#	define MC_SET_OVPG(mc, pg)	((void)0)
	#endif
		void f_Put(CDatabaseValue *key, CDatabaseValue *data, EDatabaseWriteFlag _WriteFlags);
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
		struct MDB_xcursor {
			/** A sub-cursor for traversing the Dup DB */
			MDB_cursor mx_cursor;
			/** The database record for this Dup DB */
			MDB_db	mx_db;
			/**	The auxiliary DB record for this Dup DB */
			MDB_dbx	mx_dbx;
			/** The @ref mt_dbflag for this Dup DB */
			unsigned char mx_dbflag;
		};

			/** Check if there is an inited xcursor */
		#define XCURSOR_INITED(mc) \
			((mc)->mc_xcursor && ((mc)->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED))

			/** Update the xcursor's sub-page pointer, if any, in \b mc.  Needed
			 *	when the node which contains the sub-page may have moved.  Called
			 *	with leaf page \b mp = mc->mc_pg[\b top].
			 */
		#define XCURSOR_REFRESH(mc, top, mp) do { \
			MDB_page *xr_pg = (mp); \
			MDB_node *xr_node; \
			if (!XCURSOR_INITED(mc) || (mc)->mc_ki[top] >= NUMKEYS(xr_pg)) break; \
			xr_node = NODEPTR(xr_pg, (mc)->mc_ki[top]); \
			if ((xr_node->mn_flags & (F_DUPDATA|F_SUBDATA)) == F_DUPDATA) \
				(mc)->mc_xcursor->mx_cursor.mc_pg[0] = (MDB_page *)NODEDATA(xr_node); \
		} while (0)

			/** State of FreeDB old pages, stored in the MDB_env */
		struct MDB_pgstate {
			pgno_t		*mf_pghead;	/**< Reclaimed freeDB pages, or nullptr before use */
			CTransactionID		mf_pglast;	/**< ID of last used record, or 0 if !mf_pghead */
		};

		/** Nested transaction */
		struct MDB_ntxn {
			MDB_txn		mnt_txn;		/**< the transaction */
			MDB_pgstate	mnt_pgstate;	/**< parent transaction's saved freestate */
		};

		#if !(MDB_PIDLOCK)		/* Currently the same as defined(DPlatformFamily_Windows) */
		enum Pidlock_op {
			Pidset, Pidcheck
		};
		#else
		enum Pidlock_op {
			Pidset = F_SETLK, Pidcheck = F_GETLK
		};
		#endif

		#ifdef O_CLOEXEC /* POSIX.1-2008: Set FD_CLOEXEC atomically at open() */
		# define MDB_CLOEXEC		O_CLOEXEC
		#else
		# define MDB_CLOEXEC		0
		#endif

		/** File type, access mode etc. for #mdb_fopen() */
		enum mdb_fopen_type {
		#ifdef DPlatformFamily_Windows
			MDB_O_RDONLY, MDB_O_RDWR, MDB_O_META, MDB_O_COPY, MDB_O_LOCKS
		#else
			/* A comment in mdb_fopen() explains some O_* flag choices. */
			MDB_O_RDONLY= O_RDONLY,                            /**< for RDONLY me_fd */
			MDB_O_RDWR  = O_RDWR  |O_CREAT,                    /**< for me_fd */
			MDB_O_META  = O_WRONLY|MDB_DSYNC     |MDB_CLOEXEC, /**< for me_mfd */
			MDB_O_COPY  = O_WRONLY|O_CREAT|O_EXCL|MDB_CLOEXEC, /**< for #mdb_env_copy() */
			/** Bitmask for open() flags in enum #mdb_fopen_type.  The other bits
			 * distinguish otherwise-equal MDB_O_* constants from each other.
			 */
			MDB_O_MASK  = MDB_O_RDWR|MDB_CLOEXEC | MDB_O_RDONLY|MDB_O_META|MDB_O_COPY,
			MDB_O_LOCKS = MDB_O_RDWR|MDB_CLOEXEC | ((MDB_O_MASK+1) & ~MDB_O_MASK) /**< for me_lfd */
		#endif
		};

		#ifdef _MSC_VER
		typedef	int	mdb_mode_t;
		#else
		typedef	mode_t	mdb_mode_t;
		#endif
	}
		/** The database environment. */
	struct CDatabaseEnvironment_Internal
	{
		HANDLE		me_fd;		/**< The main data file */
		HANDLE		me_lfd;		/**< The lock file */
		HANDLE		me_mfd;		/**< For writing and syncing the meta pages */
	#if defined(DMibDatabase_Always64BitSupport) && defined(DPlatformFamily_Windows)
		HANDLE		me_fmh;		/**< File Mapping handle */
	#endif
		/** Failed to update the meta page. Probably an I/O error. */
	#define	MDB_FATAL_ERROR	0x80000000U
		/** Some fields are initialized. */
	#define	MDB_ENV_ACTIVE	0x20000000U
		/** me_txkey is set */
	#define	MDB_ENV_TXKEY	0x10000000U
		/** fdatasync is unreliable */
	#define	MDB_FSYNCONLY	0x08000000U
		uint32 	me_flags;		/**< @ref mdb_env */
		uint32	me_psize;	/**< DB page size, inited from me_os_psize */
		uint32	me_os_psize;	/**< OS page size */
		uint32	m_MaxReaders;	/**< size of the reader table */
		/** Max #CLocksMemoryMap.%m_TransactionBody.m_NumReaders of interest to #f_Close() */
		NAtomic::TCAtomic<int32> me_close_readers;
		CDatabaseSubID		me_numdbs;		/**< number of DBs opened */
		CDatabaseSubID		me_maxdbs;		/**< size of the DB table */
		CProcessID	me_pid;		/**< process ID of this env */
		NStr::CStr	me_path;		/**< path to the DB files */
		char		*me_map;		/**< the memory map of the data file */
		NPrivate::CLocksMemoryMap	*m_pLocksMemoryMap;		/**< the memory map of the lock file or nullptr */
		NPrivate::MDB_meta	*me_metas[NUM_METAS];	/**< pointers to the two meta pages */
		void		*me_pbuf;		/**< scratch area for DUPSORT put() */
		NPrivate::MDB_txn		*me_txn;		/**< current write transaction */
		NPrivate::MDB_txn		*me_txn0;		/**< prealloc'd write transaction */
		CDatabaseOffset	m_MapSize;		/**< size of the data memory map */
		off_t		me_size;		/**< current file size */
		NPrivate::pgno_t		me_maxpg;		/**< m_MapSize / me_psize */
		NContainer::TCVector<NPrivate::MDB_dbx>	m_StaticDasabaseInfo;		/**< array of static DB info */
		uint16	*me_dbflags;	/**< array of flags from MDB_db.md_flags */
		uint32	*me_dbiseqs;	/**< array of dbi sequence numbers */
		pthread_key_t	me_txkey;	/**< thread-key for readers */
		NPrivate::CTransactionID		me_pgoldest;	/**< ID of oldest reader last time we looked */
		NPrivate::MDB_pgstate	me_pgstate;		/**< state of old pages from freeDB */
		NPrivate::MDB_page	*me_dpages;		/**< list of malloc'd blocks for re-use */
		/** IDL of pages that became unused in a write txn */
		NPrivate::MDB_IDL		me_free_pgs;
		/** ID2L of pages written during a write txn. Length MDB_IDL_UM_SIZE. */
		NPrivate::MDB_ID2L	me_dirty_list;
		/** Max number of freelist items that can fit in a single overflow page */
		int			me_maxfree_1pg;
		/** Max size of a node on a page */
		uint32	me_nodemax;
	#if !(MDB_MAXKEYSIZE)
		uint32	me_maxkey;	/**< max size of a key */
	#endif
		int		me_live_reader;		/**< have liveness lock in reader table */
	#ifdef DPlatformFamily_Windows
		int		me_pidquery;		/**< Used in OpenProcess */
	#endif
	#ifdef MDB_USE_POSIX_MUTEX	/* Posix mutexes reside in shared mem */
	#	define		me_rmutex	m_pLocksMemoryMap->m_TransactionBody.mtb_rmutex /**< Shared reader lock */
	#	define		me_wmutex	m_pLocksMemoryMap->m_WriteLock /**< Shared writer lock */
	#else
		NPrivate::mdb_mutex_t	me_rmutex;
		NPrivate::mdb_mutex_t	me_wmutex;
	# if defined(DPlatformFamily_Windows) || defined(MDB_USE_POSIX_SEM)
		/** Half-initialized name of mutexes, to be completed by #MUTEXNAME() */
		char		me_mutexname[sizeof(MUTEXNAME_PREFIX) + 11];
	# endif
	#endif
	#ifdef DMibDatabase_Always64BitSupport
		NPrivate::MDB_ID3L	me_rpages;	/**< like #mt_rpages, but global to env */
		pthread_mutex_t	me_rpmutex;	/**< control access to #me_rpages */
	#define MDB_ERPAGE_SIZE	16384
	#define MDB_ERPAGE_MAX	(MDB_ERPAGE_SIZE-1)
		uint32 me_rpcheck;
	#endif
		mint m_LockedThreadID = 0;
		mint m_nReferences = 0;
		// Publics
		NDatabase::CDatabaseTransactionCommon mdb_txn_begin(NPrivate::MDB_txn *parent, uint32 flags);
		void mdb_dbi_close(CDatabaseSubID dbi);
		void mdb_txn_free(NPrivate::MDB_txn *_pTransaction);

		// Privates
		void f_CopyCompact(HANDLE fd);
		void f_Copy(HANDLE fd);
		CDatabaseStatistics f_DatabaseStatistics(NPrivate::MDB_db *db);
		CDatabaseSizeStatistics f_SizeStatistics(NPrivate::MDB_db *db);
		CDatabaseEnvironmentInfo f_EnvironmentInfo();
		bool mdb_env_read_header(int prev, NPrivate::MDB_meta *meta);
		NPrivate::MDB_meta *mdb_env_pick_meta() const;
		void mdb_env_close0(int excl);
		mint mdb_leaf_size(CDatabaseValue *key, CDatabaseValue *data);
		mint mdb_branch_size(CDatabaseValue *key);
		void mdb_reader_check0(bool _bReadLock, mint *_pDead);
		void mdb_page_free(NPrivate::MDB_page *mp);
		void mdb_dpage_free(NPrivate::MDB_page *dp);
		void mdb_dpage_free_memory(NPrivate::MDB_page *dp);
		int mdb_reader_pid(NPrivate::Pidlock_op op, CProcessID pid);
		void mdb_env_sync0(bool _bForce, NPrivate::pgno_t numpgs);
		void mdb_env_init_meta0(NPrivate::MDB_meta *meta);
		void mdb_env_init_meta(NPrivate::MDB_meta *meta);
		int mdb_env_map(void *addr);
		void mdb_env_open2(int prev, uint32 _PageSize);
		int mdb_env_share_locks(int *excl);
		int mdb_env_excl_lock(int *excl);
		void mdb_env_setup_locks(NStr::CStr const &_LockPath, int mode, int *excl);
		int mdb_mutex_failed(NPrivate::mdb_mutexref_t mutex, int rc);
		int mdb_fopen(NStr::CStr const &_FileName, NPrivate::mdb_fopen_type which, NPrivate::mdb_mode_t mode, HANDLE *res);
		NStr::CStr mdb_getfilename(NStr::CStr const &_FileName, NPrivate::mdb_fopen_type which);
		bool f_IsInsideMemoryMap(uint8 const *_pData);
	};

	namespace NPrivate
	{
			/** max number of pages to commit in one writev() call */
		#define MDB_COMMIT_PAGES	 64
		#if defined(IOV_MAX) && IOV_MAX < MDB_COMMIT_PAGES
		#undef MDB_COMMIT_PAGES
		#define MDB_COMMIT_PAGES	IOV_MAX
		#endif

			/** max bytes to write in one call */
		#define MAX_WRITE		(0x40000000U >> int(sizeof(smint) == 4))

			/** Check \b txn and \b dbi arguments to a function */
		#define TXN_DBI_EXIST(txn, dbi, validity) \
			((txn) && (dbi)<(txn)->mt_numdbs && ((txn)->mt_dbflags[dbi] & (validity)))

			/** Check for misused \b dbi handles */
		#define TXN_DBI_CHANGED(txn, dbi) \
			((txn)->mt_dbiseqs[dbi] != (txn)->mt_env->me_dbiseqs[dbi])

		int  mdb_page_alloc(MDB_cursor *mc, int num, MDB_page **mp);
		int  mdb_page_new(MDB_cursor *mc, uint32 flags, int num, MDB_page **mp);
		int  mdb_page_touch(MDB_cursor *mc);

		MDB_page *mdb_page_malloc(MDB_txn *txn, unsigned num);
		void mdb_dlist_free(MDB_txn *txn);
		int mdb_page_loose(MDB_cursor *mc, MDB_page *mp);
		int mdb_pages_xkeep(MDB_cursor *mc, unsigned pflags, int all);
		int mdb_page_spill(MDB_cursor *m0, CDatabaseValue *key, CDatabaseValue *data);

		CTransactionID mdb_find_oldest(MDB_txn *txn);
		void mdb_page_dirty(MDB_txn *txn, MDB_page *mp);
		void mdb_page_copy(MDB_page *dst, MDB_page *src, uint32 psize);
		int mdb_page_unspill(MDB_txn *txn, MDB_page *mp, MDB_page **ret);

		void mdb_cursor_shadow(MDB_txn *src, MDB_txn *dst);
		void mdb_cursors_close(MDB_txn *txn, unsigned merge);
		void mdb_txn_renew0(MDB_txn *txn);
		void mdb_txn_commit(MDB_txn *txn);
		void mdb_dbis_update(MDB_txn *txn, int keep);
		int mdb_freelist_save(MDB_txn *txn);

		CDatabaseOffset mdb_fsize(HANDLE fd);
		void mdb_env_reader_dest(void *ptr);
		int mdb_ovpage_free(MDB_cursor *mc, MDB_page *mp);
		int mdb_page_search_lowest(MDB_cursor *mc);
		int mdb_cursor_touch(MDB_cursor *mc);
		void mdb_cursor_copy(const MDB_cursor *csrc, MDB_cursor *cdst);

		/** State needed for a compacting copy. */
		struct CCompactingCopyState;

		int mdb_env_cthr_toggle(CCompactingCopyState &_CopyState);
		int mdb_env_cwalk(CCompactingCopyState &_CopyState, pgno_t *pg, int flags);
		int mdb_pid_insert(CProcessID *ids, CProcessID pid);

		#define MDB_END_NAMES {"committed", "empty-commit", "abort", "reset", \
			"reset-tmp", "fail-begin", "fail-beginchild"}
		enum {
			/* mdb_txn_end operation number, for logging */
			MDB_END_COMMITTED, MDB_END_EMPTY_COMMIT, MDB_END_ABORT, MDB_END_RESET,
			MDB_END_RESET_TMP, MDB_END_FAIL_BEGIN, MDB_END_FAIL_BEGINCHILD
		};
		#define MDB_END_OPMASK	0x0F	/**< mask for #mdb_txn_end() operation number */
		#define MDB_END_UPDATE	0x10	/**< update env state (DBIs) */
		#define MDB_END_FREE	0x20	/**< free txn unless it is #MDB_env.%me_txn0 */
		#define MDB_END_SLOT EDatabaseEnvironmentFlag_NoTls	/**< release any reader slot if #EDatabaseEnvironmentFlag_NoTls */
		void mdb_txn_end(MDB_txn *txn, unsigned mode);
		void mdb_txn_abort(MDB_txn *txn);

		NStr::CStr mdb_strerror(int _Error);

		int  mdb_page_get(MDB_cursor *mc, pgno_t pgno, MDB_page **mp, int *lvl);
		int  mdb_page_search_root(MDB_cursor *mc, CDatabaseValue const *key, int modify);
		#define MDB_PS_MODIFY	1
		#define MDB_PS_ROOTONLY	2
		#define MDB_PS_FIRST	4
		#define MDB_PS_LAST		8
		int  mdb_page_search(MDB_cursor *mc,
						CDatabaseValue const *key, int flags);
		int	mdb_page_merge(MDB_cursor *csrc, MDB_cursor *cdst);

		#define MDB_SPLIT_REPLACE	EDatabaseWriteFlag_AppendDuplicateData	/**< newkey is not new */
		int	mdb_page_split(MDB_cursor *mc, CDatabaseValue *newkey, CDatabaseValue *newdata,
						pgno_t newpgno, uint32 nflags);

		int mdb_page_flush(MDB_txn *txn, int keep);

		int  mdb_env_write_meta(MDB_txn *txn);

		MDB_node *mdb_node_search(MDB_cursor *mc, CDatabaseValue const *key, int *exactp);
		int  mdb_node_add(MDB_cursor *mc, indx_t indx,
						CDatabaseValue *key, CDatabaseValue *data, pgno_t pgno, uint32 flags);
		void mdb_node_del(MDB_cursor *mc, int ksize);
		void mdb_node_shrink(MDB_page *mp, indx_t indx);
		int	mdb_node_move(MDB_cursor *csrc, MDB_cursor *cdst, int fromleft);
		int  mdb_node_read(MDB_cursor *mc, MDB_node *leaf, CDatabaseValue *data);

		int	mdb_rebalance(MDB_cursor *mc);
		int	mdb_update_key(MDB_cursor *mc, CDatabaseValue *key);

		void	mdb_cursor_pop(MDB_cursor *mc);
		int	mdb_cursor_push(MDB_cursor *mc, MDB_page *mp);

		int	mdb_cursor_del0(MDB_cursor *mc);
		int	mdb_del0(MDB_txn *txn, CDatabaseSubID dbi, CDatabaseValue const *key, CDatabaseValue const *data, EDatabaseWriteFlag _Flags);
		int	mdb_cursor_sibling(MDB_cursor *mc, int move_right);

		/** @brief Cursor Get operations.
		 *
		 *	This is the set of all operations for retrieving data
		 *	using a cursor.
		 */
		enum EDatabaseCursorGet
		{
			MDB_FIRST,				///< Position at first key/data item
			MDB_FIRST_DUP,			///< Position at first data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			MDB_GET_BOTH,			///< Position at key/data pair. Only for #EDatabaseSubFlag_SortedDuplicates
			MDB_GET_BOTH_RANGE,		///< position at key, nearest data. Only for #EDatabaseSubFlag_SortedDuplicates
			MDB_GET_CURRENT,		///< Return key/data at current cursor position
			MDB_GET_MULTIPLE,		///< Return up to a page of duplicate data items
									///	from current cursor position. Move cursor to prepare
									///	for #MDB_NEXT_MULTIPLE. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize
			MDB_LAST,				///< Position at last key/data item
			MDB_LAST_DUP,			///< Position at last data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			MDB_NEXT,				///< Position at next data item
			MDB_NEXT_DUP,			///< Position at next data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			MDB_NEXT_MULTIPLE,		///< Return up to a page of duplicate data items
									///	from next cursor position. Move cursor to prepare
									///	for #MDB_NEXT_MULTIPLE. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize
			MDB_NEXT_NODUP,			///< Position at first data item of next key
			MDB_PREV,				///< Position at previous data item
			MDB_PREV_DUP,			///< Position at previous data item of current key. Only for #EDatabaseSubFlag_SortedDuplicates
			MDB_PREV_NODUP,			///< Position at last data item of previous key
			MDB_SET,				///< Position at specified key
			MDB_SET_KEY,			///< Position at specified key, return key + data
			MDB_SET_RANGE,			///< Position at first key greater than or equal to specified key.
			MDB_PREV_MULTIPLE		///< Position at previous page and return up to a page of duplicate data items. Only for #EDatabaseSubFlag_SortedDuplicatesFixedSize
		};


		int	mdb_cursor_next(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op);
		int	mdb_cursor_prev(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op);
		int	mdb_cursor_set(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op, int *exactp);
		int mdb_cursor_first(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data);
		int	mdb_cursor_last(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data);

		void mdb_cursor_init(MDB_cursor *mc, MDB_txn *txn, CDatabaseSubID dbi, MDB_xcursor *mx);
		void mdb_xcursor_init0(MDB_cursor *mc);
		void mdb_xcursor_init1(MDB_cursor *mc, MDB_node *node);
		void mdb_xcursor_init2(MDB_cursor *mc, MDB_xcursor *src_mx, int force);

		int	mdb_drop0(MDB_cursor *mc, int subs);
		void mdb_default_cmp(MDB_txn *txn, CDatabaseSubID dbi);

		/** @cond */
		int mdb_cmp_memn(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int mdb_cmp_memnr(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int mdb_cmp_int(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int mdb_cmp_cint(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int mdb_cmp_long(CDatabaseValue const &_Left, CDatabaseValue const &_Right);

		extern FDatabaseCompare g_mdb_cmp_memn;
		extern FDatabaseCompare g_mdb_cmp_memnr;
		extern FDatabaseCompare g_mdb_cmp_int;
		extern FDatabaseCompare g_mdb_cmp_cint;
		extern FDatabaseCompare g_mdb_cmp_long;

#ifdef DPlatformFamily_Windows
#ifndef MAX_TLS_KEYS
#define MAX_TLS_KEYS	64
#endif
		extern int mdb_tls_nkeys;
		extern pthread_key_t mdb_tls_keys[MAX_TLS_KEYS];
#endif

		/** @endcond */

		/** Compare two items pointing at '#CDatabaseOffset's of unknown alignment. */
		#ifdef MISALIGNED_OK
		#	define g_mdb_cmp_clong g_mdb_cmp_long
		#else
		#	define g_mdb_cmp_clong g_mdb_cmp_cint
		#endif

		/** True if we need #mdb_cmp_clong() instead of \b cmp for #MDB_INTEGERDUP */
		#define NEED_CMP_CLONG(cmp, ksize) \
			(32 < DMibDatabase_IDBits && \
			 (cmp) == NPrivate::g_mdb_cmp_int && (ksize) == sizeof(CDatabaseOffset))

		#ifdef DPlatformFamily_Windows
			extern SECURITY_DESCRIPTOR mdb_null_sd;
			extern SECURITY_ATTRIBUTES mdb_all_sa;
			extern int mdb_sec_inited;
		#endif

		/** Do not spill pages to disk if txn is getting full, may fail instead */
		#define MDB_NOSPILL	0x8000

		#ifdef DMibDatabase_Always64BitSupport
			void mdb_page_unref(MDB_txn *txn, MDB_page *mp);
		#	define MDB_PAGE_UNREF(txn, mp)	mdb_page_unref(txn, mp)
		#else
		#	define MDB_PAGE_UNREF(txn, mp)
		#endif

		/** Only a subset of the @ref mdb_env flags can be changed
		 *	at runtime. Changing other flags requires closing the
		 *	environment and re-opening it with the new flags.
		 */
		static_assert(!(VALID_FLAGS & PERSISTENT_FLAGS & (EDatabaseEnvironmentFlag_Changable | EDatabaseEnvironmentFlag_Changeless)), "Persistent DB flags & env flags overlap, but both go in mm_flags");

		bool fg_BrokenFSync(MDB_env *_pEnvironment, HANDLE _FileDescriptor);

		/** Perform \b act while tracking temporary cursor \b mn */
		#define WITH_CURSOR_TRACKING(mn, act) do { \
			MDB_cursor dummy, *tracked, **tp = &(mn).mc_txn->mt_cursors[mn.mc_dbi]; \
			if ((mn).mc_flags & C_SUB) { \
				dummy.mc_flags =  C_INITIALIZED; \
				dummy.mc_xcursor = (MDB_xcursor *)&(mn);	\
				tracked = &dummy; \
			} else { \
				tracked = &(mn); \
			} \
			tracked->mc_next = *tp; \
			*tp = tracked; \
			{ act; } \
			*tp = tracked->mc_next; \
		} while (0)

		void mdb_cursor_close(MDB_cursor *cursor);
		int  mdb_cursor_get(MDB_cursor *cursor, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op);
		int  mdb_cursor_put(MDB_cursor *cursor, CDatabaseValue *key, CDatabaseValue *data, EDatabaseWriteFlag _WriteFlags = EDatabaseWriteFlag_None);
		int  mdb_cursor_del(MDB_cursor *cursor, EDatabaseWriteFlag _WriteFlags = EDatabaseWriteFlag_None);
		int  mdb_cursor_count(MDB_cursor *cursor, CDatabaseOffset *countp);

		#ifdef DMibDatabase_Always64BitSupport
		void mdb_cursor_unref(MDB_cursor *mc);
		#define MDB_CURSOR_UNREF(mc, force) \
			(((force) || ((mc)->mc_flags & C_INITIALIZED)) \
			 ? mdb_cursor_unref(mc) \
			 : (void)0)

		#else
		#define MDB_CURSOR_UNREF(mc, force) ((void)0)
		#endif /* DMibDatabase_Always64BitSupport */

	}
}

#include "Malterlib_Database_Database_Debug.h"
