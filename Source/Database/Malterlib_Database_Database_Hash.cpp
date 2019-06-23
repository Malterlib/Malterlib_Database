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

namespace NMib::NDatabase::NPrivate
{
	#ifdef MDB_USE_HASH
	/*
	 * hash_64 - 64 bit Fowler/Noll/Vo-0 FNV-1a hash code
	 *
	 * @(#) $Revision: 5.1 $
	 * @(#) $Id: hash_64a.c,v 5.1 2009/06/30 09:01:38 chongo Exp $
	 * @(#) $Source: /usr/local/src/cmd/fnv/RCS/hash_64a.c,v $
	 *
	 *	  http://www.isthe.com/chongo/tech/comp/fnv/index.html
	 *
	 ***
	 *
	 * Please do not copyright this code.  This code is in the public domain.
	 *
	 * LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
	 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
	 * EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
	 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
	 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
	 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
	 * PERFORMANCE OF THIS SOFTWARE.
	 *
	 * By:
	 *	chongo <Landon Curt Noll> /\oo/\
	 *	  http://www.isthe.com/chongo/
	 *
	 * Share and Enjoy!	:-)
	 */

	/** perform a 64 bit Fowler/Noll/Vo FNV-1a hash on a buffer
	 * @param[in] val	value to hash
	 * @param[in] len	length of value
	 * @return 64 bit hash
	 */
	mdb_hash_t mdb_hash(const void *val, size_t len)
	{
		const unsigned char *s = (const unsigned char *) val, *end = s + len;
		mdb_hash_t hval = 0xcbf29ce484222325ULL;
		/*
		 * FNV-1a hash each octet of the buffer
		 */
		while (s < end) {
			hval = (hval ^ *s++) * 0x100000001b3ULL;
		}
		/* return our new hash value */
		return hval;
	}

	/** Hash the string and output the encoded hash.
	 * This uses modified RFC1924 Ascii85 encoding to accommodate systems with
	 * very short name limits. We don't care about the encoding being reversible,
	 * we just want to preserve as many bits of the input as possible in a
	 * small printable string.
	 * @param[in] str string to hash
	 * @param[out] encbuf an array of 11 chars to hold the hash
	 */
	namespace
	{
		const char mdb_a85[]= "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~";
	}

	void DMibDatabaseInfrequentSection
	mdb_pack85(unsigned long long l, char *out)
	{
		int i;

		for (i=0; i<10 && l; i++) {
			*out++ = mdb_a85[l % 85];
			l /= 85;
		}
		*out = '\0';
	}

	/** Init #MDB_env.me_mutexname[] except the char which #MUTEXNAME() will set.
	 *	Changes to this code must be reflected in #MDB_LOCK_FORMAT.
	 */
	void DMibDatabaseInfrequentSection
	mdb_env_mname_init(MDB_env *env)
	{
		char *nm = env->me_mutexname;
		strcpy(nm, MUTEXNAME_PREFIX);
		mdb_pack85(env->m_pLocksMemoryMap->mti_mutexid, nm + sizeof(MUTEXNAME_PREFIX));
	}

	/** Return env->me_mutexname after filling in ch ('r'/'w') for convenience */
	#endif
}
