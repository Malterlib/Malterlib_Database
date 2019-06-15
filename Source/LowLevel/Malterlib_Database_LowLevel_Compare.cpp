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
	/** Set the default comparison functions for a database.
	 * Called immediately after a database is opened to set the defaults.
	 * The user can then override them with #f_SetDatabaseKeyCompare() or
	 * #f_SetDatabaseDataCompare().
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 */
	void NDatabase::NPrivate::mdb_default_cmp(MDB_txn *txn, CLowLevelDatabaseID dbi)
	{
		auto &Database = txn->mt_dbs[dbi];
		auto &LocalDatabase = txn->mt_dbxs[dbi];

		uint16 f = Database.md_flags;

		FLowLevelCompare fKeyCompare;

		if (f & ELowLevelDatabaseFlag_ReverseKeys)
			fKeyCompare = g_mdb_cmp_memnr;
		else
		{
			if (f & ELowLevelDatabaseFlag_IntegerKeys)
				fKeyCompare = g_mdb_cmp_cint;
			else
				fKeyCompare = g_mdb_cmp_memn;
		}

		LocalDatabase.m_fKeyCompare = fg_Move(fKeyCompare);

		FLowLevelCompare fDataCompare;

		if (!(f & ELowLevelDatabaseFlag_SortedDuplicates))
			;
		else
		{
			if (f & ELowLevelDatabaseFlag_SortedDuplicatesIntegerKeys)
			{
				if (f & ELowLevelDatabaseFlag_SortedDuplicatesFixedSize)
					fDataCompare = g_mdb_cmp_int;
				else
					fDataCompare = g_mdb_cmp_cint;
			}
			else
			{
				if (f & ELowLevelDatabaseFlag_SortedDuplicatesReverseKeys)
					fDataCompare = g_mdb_cmp_memnr;
				else
					fDataCompare = g_mdb_cmp_memn;
			}
		}

		LocalDatabase.m_fDataCompare = fg_Move(fDataCompare);
	}

	/** Compare two items pointing at aligned #CLowLevelSize's */
	int NDatabase::NPrivate::mdb_cmp_long(CLowLevelValue const &_Left, CLowLevelValue const &_Right)
	{
		return (*(CLowLevelSize *)_Left.m_pData < *(CLowLevelSize *)_Right.m_pData) ? -1 :
			*(CLowLevelSize *)_Left.m_pData > *(CLowLevelSize *)_Right.m_pData;
	}

	/** Compare two items pointing at aligned unsigned int's.
	 *
	 *	This is also set as #ELowLevelDatabaseFlag_SortedDuplicatesIntegerKeys|#ELowLevelDatabaseFlag_SortedDuplicatesFixedSize's #MDB_dbx.%m_fDataCompare,
	 *	but #mdb_cmp_clong() is called instead if the data type is #CLowLevelSize.
	 */
	int NDatabase::NPrivate::mdb_cmp_int(CLowLevelValue const &_Left, CLowLevelValue const &_Right)
	{
		return (*(uint32 *)_Left.m_pData < *(uint32 *)_Right.m_pData) ? -1 :
			*(uint32 *)_Left.m_pData > *(uint32 *)_Right.m_pData;
	}

	/** Compare two items pointing at unsigned ints of unknown alignment.
	 *	Nodes and keys are guaranteed to be 2-byte aligned.
	 */
	int NDatabase::NPrivate::mdb_cmp_cint(CLowLevelValue const &_Left, CLowLevelValue const &_Right)
	{
	#if BYTE_ORDER == LITTLE_ENDIAN
		unsigned short *u, *c;
		int x;

		u = (unsigned short *) ((char *) _Left.m_pData + _Left.m_Size);
		c = (unsigned short *) ((char *) _Right.m_pData + _Left.m_Size);
		do {
			x = *--u - *--c;
		} while(!x && u > (unsigned short *)_Left.m_pData);
		return x;
	#else
		unsigned short *u, *c, *end;
		int x;

		end = (unsigned short *) ((char *) _Left.m_pData + _Left.m_Size);
		u = (unsigned short *)_Left.m_pData;
		c = (unsigned short *)_Right.m_pData;
		do {
			x = *u++ - *c++;
		} while(!x && u < end);
		return x;
	#endif
	}

	/** Compare two items lexically */
	int NDatabase::NPrivate::mdb_cmp_memn(CLowLevelValue const &_Left, CLowLevelValue const &_Right)
	{
		int diff;
		smint len_diff;
		uint32 len;

		len = _Left.m_Size;
		len_diff = (smint) _Left.m_Size - (smint) _Right.m_Size;
		if (len_diff > 0) {
			len = _Right.m_Size;
			len_diff = 1;
		}

		diff = memcmp(_Left.m_pData, _Right.m_pData, len);
		return diff ? diff : len_diff<0 ? -1 : len_diff;
	}

	/** Compare two items in reverse byte order */
	int NDatabase::NPrivate::mdb_cmp_memnr(CLowLevelValue const &_Left, CLowLevelValue const &_Right)
	{
		const unsigned char	*p1, *p2, *p1_lim;
		smint len_diff;
		int diff;

		p1_lim = (const unsigned char *)_Left.m_pData;
		p1 = (const unsigned char *)_Left.m_pData + _Left.m_Size;
		p2 = (const unsigned char *)_Right.m_pData + _Right.m_Size;

		len_diff = (smint) _Left.m_Size - (smint) _Right.m_Size;
		if (len_diff > 0) {
			p1_lim += len_diff;
			len_diff = 1;
		}

		while (p1 > p1_lim) {
			diff = *--p1 - *--p2;
			if (diff)
				return diff;
		}
		return len_diff<0 ? -1 : len_diff;
	}

	NDatabase::FLowLevelCompare NDatabase::NPrivate::g_mdb_cmp_memn = NPrivate::mdb_cmp_memn;
	NDatabase::FLowLevelCompare NDatabase::NPrivate::g_mdb_cmp_memnr = NPrivate::mdb_cmp_memnr;
	NDatabase::FLowLevelCompare NDatabase::NPrivate::g_mdb_cmp_int = NPrivate::mdb_cmp_int;
	NDatabase::FLowLevelCompare NDatabase::NPrivate::g_mdb_cmp_cint = NPrivate::mdb_cmp_cint;
	NDatabase::FLowLevelCompare NDatabase::NPrivate::g_mdb_cmp_long = NPrivate::mdb_cmp_long;
}
