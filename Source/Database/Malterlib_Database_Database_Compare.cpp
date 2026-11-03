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

namespace NMib::NDatabase::NPrivate
{
	/** Set the default comparison functions for a database.
	 * Called immediately after a database is opened to set the defaults.
	 * The user can then override them with #f_SetDatabaseKeyCompare() or
	 * #f_SetDatabaseDataCompare().
	 * @param[in] _pTransaction A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 */
	void fg_DefaultCmp(CMdbTxn *_pTransaction, CDatabaseSubID _DatabaseID)
	{
		auto &Database = _pTransaction->m_pDatabases[_DatabaseID];
		auto &LocalDatabase = _pTransaction->m_pDatabaseInfos[_DatabaseID];

		uint16 Flags = Database.m_Flags;

		FDatabaseCompare fKeyCompare;

		if (Flags & EDatabaseSubFlag_ReverseKeys)
			fKeyCompare = g_fCompareMemNR;
		else
		{
			if (Flags & EDatabaseSubFlag_IntegerKeys)
				fKeyCompare = g_fCompareCInt;
			else
				fKeyCompare = g_fCompareMemN;
		}

		LocalDatabase.m_fKeyCompare = fg_Move(fKeyCompare);

		FDatabaseCompare fDataCompare;

		if (!(Flags & EDatabaseSubFlag_SortedDuplicates))
			;
		else
		{
			if (Flags & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
			{
				if (Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)
					fDataCompare = g_fCompareInt;
				else
					fDataCompare = g_fCompareCInt;
			}
			else
			{
				if (Flags & EDatabaseSubFlag_SortedDuplicatesReverseKeys)
					fDataCompare = g_fCompareMemNR;
				else
					fDataCompare = g_fCompareMemN;
			}
		}

		LocalDatabase.m_fDataCompare = fg_Move(fDataCompare);
	}

	/** Compare two items pointing at aligned #CDatabaseOffset's */
	int fg_CmpLong(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		return (*(CDatabaseOffset *)_Left.m_pData < *(CDatabaseOffset *)_Right.m_pData) ? -1 : *(CDatabaseOffset *)_Left.m_pData > *(CDatabaseOffset *)_Right.m_pData;
	}

	/** Compare two items pointing at aligned unsigned int's.
	 *
	 *	This is also set as #EDatabaseSubFlag_SortedDuplicatesIntegerKeys|#EDatabaseSubFlag_SortedDuplicatesFixedSize's #CMdbDbx.%m_fDataCompare,
	 *	but #mdb_cmp_clong() is called instead if the data type is #CDatabaseOffset.
	 */
	int fg_CmpInt(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		return (*(uint32 *)_Left.m_pData < *(uint32 *)_Right.m_pData) ? -1 : *(uint32 *)_Left.m_pData > *(uint32 *)_Right.m_pData;
	}

	/** Compare two items pointing at unsigned ints of unknown alignment.
	 *	Nodes and keys are guaranteed to be 2-byte aligned.
	 */
	int fg_CmpCInt(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
#if BYTE_ORDER == LITTLE_ENDIAN
		unsigned short *pLeft, *pRight;
		int Difference;

		pLeft = (unsigned short *)((char *)_Left.m_pData + _Left.m_Size);
		pRight = (unsigned short *)((char *)_Right.m_pData + _Left.m_Size);
		do
		{
			Difference = *--pLeft - *--pRight;
		}
		while (!Difference && pLeft > (unsigned short *)_Left.m_pData);
		return Difference;
#else
		unsigned short *pLeft, *pRight, *end;
		int Difference;

		end = (unsigned short *)((char *)_Left.m_pData + _Left.m_Size);
		pLeft = (unsigned short *)_Left.m_pData;
		pRight = (unsigned short *)_Right.m_pData;
		do
		{
			Difference = *pLeft++ - *pRight++;
		}
		while (!Difference && pLeft < end);
		return Difference;
#endif
	}

	/** Compare two items lexically */
	int fg_CmpMemN(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		int Difference;
		smint SizeDifference;
		uint32 Size;

		Size = _Left.m_Size;
		SizeDifference = (smint)_Left.m_Size - (smint)_Right.m_Size;
		if (SizeDifference > 0)
		{
			Size = _Right.m_Size;
			SizeDifference = 1;
		}

		Difference = memcmp(_Left.m_pData, _Right.m_pData, Size);
		return Difference ? Difference : SizeDifference < 0 ? -1 : SizeDifference;
	}

	/** Compare two items in reverse byte order */
	int fg_CmpMemNR(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		unsigned char const *pLeft, *pRight, *pLeftLimit;
		smint SizeDifference;
		int Difference;

		pLeftLimit = (unsigned char const *)_Left.m_pData;
		pLeft = (unsigned char const *)_Left.m_pData + _Left.m_Size;
		pRight = (unsigned char const *)_Right.m_pData + _Right.m_Size;

		SizeDifference = (smint)_Left.m_Size - (smint)_Right.m_Size;
		if (SizeDifference > 0)
		{
			pLeftLimit += SizeDifference;
			SizeDifference = 1;
		}

		while (pLeft > pLeftLimit)
		{
			Difference = *--pLeft - *--pRight;
			if (Difference)
				return Difference;
		}
		return SizeDifference < 0 ? -1 : SizeDifference;
	}

	FDatabaseCompare g_fCompareMemN = fg_CmpMemN;
	FDatabaseCompare g_fCompareMemNR = fg_CmpMemNR;
	FDatabaseCompare g_fCompareInt = fg_CmpInt;
	FDatabaseCompare g_fCompareCInt = fg_CmpCInt;
	FDatabaseCompare g_fCompareLong = fg_CmpLong;
}
