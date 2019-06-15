// Copyright © 2016 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2016 Howard Chu, Symas Corp. All rights reserved.

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

#include "Malterlib_Database_LowLevel.h"
#include "Malterlib_Database_LowLevel_Internal.h"

namespace NMib
{
	/** Release a reader thread's slot in the reader lock table.
	 *	This function is called automatically when a thread exits.
	 * @param[in] ptr This points to the slot in the reader lock table.
	 */
	void NDatabase::NPrivate::mdb_env_reader_dest(void *ptr)
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
	#ifndef MAX_TLS_KEYS
	#define MAX_TLS_KEYS	64
	#endif
	namespace
	{
		pthread_key_t mdb_tls_keys[MAX_TLS_KEYS];
		int mdb_tls_nkeys;

		void NTAPI mdb_tls_callback(PVOID module, DWORD reason, PVOID ptr)
		{
			int i;
			switch(reason) {
			case DLL_PROCESS_ATTACH: break;
			case DLL_THREAD_ATTACH: break;
			case DLL_THREAD_DETACH:
				for (i=0; i<mdb_tls_nkeys; i++) {
					MDB_reader *r = pthread_getspecific(mdb_tls_keys[i]);
					if (r) {
						mdb_env_reader_dest(r);
					}
				}
				break;
			case DLL_PROCESS_DETACH: break;
			}
		}
	}
	#ifdef __GNUC__
	#ifdef _WIN64
	const PIMAGE_TLS_CALLBACK mdb_tls_cbp __attribute__((section (".CRT$XLB"))) = mdb_tls_callback;
	#else
	PIMAGE_TLS_CALLBACK mdb_tls_cbp __attribute__((section (".CRT$XLB"))) = mdb_tls_callback;
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
	extern const PIMAGE_TLS_CALLBACK mdb_tls_cbp;
	const PIMAGE_TLS_CALLBACK mdb_tls_cbp = mdb_tls_callback;
	#pragma const_seg()
	#else	/* DPlatformFamily_Windows */
	#pragma comment(linker, "/INCLUDE:__tls_used")
	#pragma comment(linker, "/INCLUDE:_mdb_tls_cbp")
	#pragma data_seg(".CRT$XLB")
	PIMAGE_TLS_CALLBACK mdb_tls_cbp = mdb_tls_callback;
	#pragma data_seg()
	#endif	/* WIN 32/64 */
	#endif	/* !__GNUC__ */
	#endif
}
