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
	/** Release a reader thread's slot in the reader lock table.
	 *	This function is called automatically when a thread exits.
	 * @param[in] ptr This points to the slot in the reader lock table.
	 */
	void NPrivate::mdb_env_reader_dest(void *ptr)
	{
		MDB_reader *reader = (MDB_reader *)ptr;

#ifndef DPlatformFamily_Windows
		if (reader->m_OwningProcessID == getpid()) /* catch pthread_exit() in child process */
#endif
			/* We omit the mutex, so do this atomically (i.e. skip mr_txnid) */
			reader->m_OwningProcessID = 0;
	}

	#ifdef DPlatformFamily_Windows
	/** Junk for arranging thread-specific callbacks on Windows. This is
	 *	necessarily platform and compiler-specific. Windows supports up
	 *	to 1088 keys. Let's assume nobody opens more than 64 environments
	 *	in a single process, for now. They can override this if needed.
	 */
	pthread_key_t NPrivate::mdb_tls_keys[MAX_TLS_KEYS];
	int NPrivate::mdb_tls_nkeys = 0;

	namespace
	{
		void NTAPI mdb_tls_callback(PVOID module, DWORD reason, PVOID ptr)
		{
			int i;
			switch(reason) {
			case DLL_PROCESS_ATTACH: break;
			case DLL_THREAD_ATTACH: break;
			case DLL_THREAD_DETACH:
				for (i=0; i<NPrivate::mdb_tls_nkeys; i++) {
					NPrivate::MDB_reader *r = (NPrivate::MDB_reader *)pthread_getspecific(NPrivate::mdb_tls_keys[i]);
					if (r) {
						mdb_env_reader_dest(r);
					}
				}
				break;
			case DLL_PROCESS_DETACH: break;
			}
		}
	}
	#endif
}

#ifdef DPlatformFamily_Windows
#ifdef __GNUC__
#ifdef _WIN64
extern "C" const PIMAGE_TLS_CALLBACK mdb_tls_cbp __attribute__((section (".CRT$XLB"))) = mdb_tls_callback;
#else
extern "C" PIMAGE_TLS_CALLBACK mdb_tls_cbp __attribute__((section (".CRT$XLB"))) = mdb_tls_callback;
#endif
#else
#ifdef _WIN64
/* Force some symbol references.
	*	_tls_used forces the linker to create the TLS directory if not already done
	*	mdb_tls_cbp prevents whole-program-optimizer from dropping the symbol.
	*/
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:mdb_tls_cbp")
#pragma const_seg(".CRT$XLB")
extern "C" const PIMAGE_TLS_CALLBACK mdb_tls_cbp;
extern "C" const PIMAGE_TLS_CALLBACK mdb_tls_cbp = NMib::NDatabase::mdb_tls_callback;
#pragma const_seg()
#else	/* DPlatformFamily_Windows */
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_mdb_tls_cbp")
#pragma data_seg(".CRT$XLB")
extern "C" PIMAGE_TLS_CALLBACK mdb_tls_cbp = NMib::NDatabase::mdb_tls_callback;
#pragma data_seg()
#endif	/* WIN 32/64 */
#endif	/* !__GNUC__ */
#endif
