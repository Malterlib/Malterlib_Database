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
	/** Set the default comparison functions for a database.
	 * Called immediately after a database is opened to set the defaults.
	 * The user can then override them with #f_SetDatabaseKeyCompare() or
	 * #f_SetDatabaseDataCompare().
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 */
	void mdb_default_cmp(MDB_txn *txn, CDatabaseSubID dbi)
	{
		auto &Database = txn->mt_dbs[dbi];
		auto &LocalDatabase = txn->mt_dbxs[dbi];

		uint16 f = Database.md_flags;

		FDatabaseCompare fKeyCompare;

		if (f & EDatabaseSubFlag_ReverseKeys)
			fKeyCompare = g_mdb_cmp_memnr;
		else
		{
			if (f & EDatabaseSubFlag_IntegerKeys)
				fKeyCompare = g_mdb_cmp_cint;
			else
				fKeyCompare = g_mdb_cmp_memn;
		}

		LocalDatabase.m_fKeyCompare = fg_Move(fKeyCompare);

		FDatabaseCompare fDataCompare;

		if (!(f & EDatabaseSubFlag_SortedDuplicates))
			;
		else
		{
			if (f & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
			{
				if (f & EDatabaseSubFlag_SortedDuplicatesFixedSize)
					fDataCompare = g_mdb_cmp_int;
				else
					fDataCompare = g_mdb_cmp_cint;
			}
			else
			{
				if (f & EDatabaseSubFlag_SortedDuplicatesReverseKeys)
					fDataCompare = g_mdb_cmp_memnr;
				else
					fDataCompare = g_mdb_cmp_memn;
			}
		}

		LocalDatabase.m_fDataCompare = fg_Move(fDataCompare);
	}

	/** Compare two items pointing at aligned #CDatabaseOffset's */
	int mdb_cmp_long(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		return (*(CDatabaseOffset *)_Left.m_pData < *(CDatabaseOffset *)_Right.m_pData) ? -1 :
			*(CDatabaseOffset *)_Left.m_pData > *(CDatabaseOffset *)_Right.m_pData;
	}

	/** Compare two items pointing at aligned unsigned int's.
	 *
	 *	This is also set as #EDatabaseSubFlag_SortedDuplicatesIntegerKeys|#EDatabaseSubFlag_SortedDuplicatesFixedSize's #MDB_dbx.%m_fDataCompare,
	 *	but #mdb_cmp_clong() is called instead if the data type is #CDatabaseOffset.
	 */
	int mdb_cmp_int(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		return (*(uint32 *)_Left.m_pData < *(uint32 *)_Right.m_pData) ? -1 :
			*(uint32 *)_Left.m_pData > *(uint32 *)_Right.m_pData;
	}

	/** Compare two items pointing at unsigned ints of unknown alignment.
	 *	Nodes and keys are guaranteed to be 2-byte aligned.
	 */
	int mdb_cmp_cint(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
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
	int mdb_cmp_memn(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
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
	int mdb_cmp_memnr(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
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

	FDatabaseCompare g_mdb_cmp_memn = mdb_cmp_memn;
	FDatabaseCompare g_mdb_cmp_memnr = mdb_cmp_memnr;
	FDatabaseCompare g_mdb_cmp_int = mdb_cmp_int;
	FDatabaseCompare g_mdb_cmp_cint = mdb_cmp_cint;
	FDatabaseCompare g_mdb_cmp_long = mdb_cmp_long;
}
