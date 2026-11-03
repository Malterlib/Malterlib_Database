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
	/** @brief Get items from a database.
	 *
	 * This function retrieves key/data pairs from the database. The address
	 * and length of the data associated with the specified \b _pKey are returned
	 * in the structure to which \b _pData refers.
	 * If the database supports duplicate keys (#EDatabaseSubFlag_SortedDuplicates) then the
	 * first data item for the key will be returned. Retrieval of other
	 * items requires the use of #fg_CursorGet().
	 *
	 * @note The memory pointed to by the returned values is owned by the
	 * database. The caller need not dispose of the memory, and may not
	 * modify it in any way. For values returned in a read-only transaction
	 * any modification attempts will cause a SIGSEGV.
	 * @note Values returned from the database are valid only until a
	 * subsequent update operation, or the end of the transaction.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] _pKey The key to search for in the database
	 * @param[out] _pData The data corresponding to the key
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#DMdbNotFound - the key was not in the database.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	bool CDatabaseTransactionCommon::CInternal::f_Get(CDatabaseSubID _DatabaseID, CDatabaseValue *_pKey, CDatabaseValue *_pData)
	{
		DMibRequire(_pKey);
		DMibRequire(_pData);

		using namespace NPrivate;
		CMdbCursor Cursor;
		CMdbXCursor XCursor;
		int bExact = 0;
		DKBuf;

		DMibDatabaseTrace("===> get db {} key [{}]", _DatabaseID, DKey(_pKey));

		if (!DTxnDbiExist(this, _DatabaseID, DDbUserValid))
			DMibError("Database does not exists");

		f_CheckBlocked();

		fg_CursorInit(&Cursor, this, _DatabaseID, &XCursor);
		{
			int Result = fg_CursorSet(&Cursor, _pKey, _pData, EDatabaseCursorGet_Set, &bExact);
			if (Result)
			{
				if (Result == DMdbNotFound)
					return false;
				DMibError(NStr::fg_Format("Error setting data: {}", fg_StrError(Result)));
			}
		}
		/* unref all the pages when DMibDatabase_Always64BitSupport - caller must copy the data
		 * before doing anything else
		 */
		DMdbCursorUnref(&Cursor, 1);
		return true;
	}

	/** @brief Store items into a database.
	 *
	 * This function stores key/data pairs in the database. The default behavior
	 * is to enter the new key/data pair, replacing any previously existing key
	 * if duplicates are disallowed, or adding a duplicate data item if
	 * duplicates are allowed (#EDatabaseSubFlag_SortedDuplicates).
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] _pKey The key to store in the database
	 * @param[in,out] _pData The data to store
	 * @param[in] flags Special options for this operation. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * <ul>
	 *	<li>#EDatabaseWriteFlag_NoDuplicateData - enter the new key/data pair only if it does not
	 *		already appear in the database. This flag may only be specified
	 *		if the database was opened with #EDatabaseSubFlag_SortedDuplicates. The function will
	 *		return #DMdbKeyExist if the key/data pair already appears in the
	 *		database.
	 *	<li>#EDatabaseWriteFlag_NoOverwrite - enter the new key/data pair only if the key
	 *		does not already appear in the database. The function will return
	 *		#DMdbKeyExist if the key already appears in the database, even if
	 *		the database supports duplicates (#EDatabaseSubFlag_SortedDuplicates). The \b _pData
	 *		parameter will be set to point to the existing item.
	 *	<li>#EDatabaseWriteFlag_ReserveSpace - reserve space for data of the given size, but
	 *		don't copy the given data. Instead, return a pointer to the
	 *		reserved space, which the caller can fill in later - before
	 *		the next update operation or the transaction ends. This saves
	 *		an extra memcpy if the data is being generated later.
	 *		LMDB does nothing else with this memory, the caller is expected
	 *		to modify all of the space requested. This flag must not be
	 *		specified if the database was opened with #EDatabaseSubFlag_SortedDuplicates.
	 *	<li>#EDatabaseWriteFlag_Append - append the given key/data pair to the end of the
	 *		database. This option allows fast bulk loading when keys are
	 *		already known to be in the correct order. Loading unsorted keys
	 *		with this flag will cause a #DMdbKeyExist error.
	 *	<li>#EDatabaseWriteFlag_AppendDuplicateData - as above, but for sorted dup data.
	 * </ul>
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#DMdbMapFull - the database is full, see #f_SetMapSize().
	 *	<li>#DMdbTxnFull - the transaction has too many dirty pages.
	 *	<li>EACCES - an attempt was made to write in a read-only transaction.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void CDatabaseTransactionCommon::CInternal::f_Put(CDatabaseSubID _DatabaseID, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseWriteFlag _WriteFlags)
	{
		DMibRequire(_pKey);
		DMibRequire(_pData);
		DMibRequire(!m_pEnvironment->f_IsInsideMemoryMap(_pKey->m_pData) && !m_pEnvironment->f_IsInsideMemoryMap(_pData->m_pData))
			("You cannot use a value from the database in a database write. The database can change the value while writing, corrupting the database.")
		;
		using namespace NPrivate;

		if (!DTxnDbiExist(this, _DatabaseID, DDbUserValid))
			DMibError("Database does not exists");

		if
		(
			_WriteFlags
			& ~
			(
				EDatabaseWriteFlag_NoOverwrite
				| EDatabaseWriteFlag_NoDuplicateData
				| EDatabaseWriteFlag_ReserveSpace
				| EDatabaseWriteFlag_Append
				| EDatabaseWriteFlag_AppendDuplicateData
				| EDatabaseWriteFlag_OverwriteAtCurrentCursorPos
			)
		)
		{
			DMibError("Invalid flags");
		}

		f_CheckBlocked();

		if (m_Flags & (DMdbTxnReadOnly))
			DMibError("Trying to write to read only transaction");

		CMdbCursor Cursor;
		CMdbXCursor XCursor;

		fg_CursorInit(&Cursor, this, _DatabaseID, &XCursor);
		Cursor.m_pNext = m_pCursors[_DatabaseID];
		m_pCursors[_DatabaseID] = &Cursor;
		int Result = fg_CursorPut(&Cursor, _pKey, _pData, _WriteFlags);
		m_pCursors[_DatabaseID] = Cursor.m_pNext;
		if (Result)
			DMibError(NStr::fg_Format("Error putting data: {}", fg_StrError(Result)));
	}

	/** @brief Delete items from a database.
	 *
	 * This function removes key/data pairs from the database.
	 * If the database does not support sorted duplicate data items
	 * (#EDatabaseSubFlag_SortedDuplicates) the data parameter is ignored.
	 * If the database supports sorted duplicates and the data parameter
	 * is NULL, all of the duplicate data items for the key will be
	 * deleted. Otherwise, if the data parameter is non-NULL
	 * only the matching data item will be deleted.
	 * This function will return #DMdbNotFound if the specified key/data
	 * pair is not in the database.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] _pKey The key to delete from the database
	 * @param[in] _pData The data to delete
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EACCES - an attempt was made to write in a read-only transaction.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void CDatabaseTransactionCommon::CInternal::f_Delete(CDatabaseSubID _DatabaseID, CDatabaseValue const *_pKey, CDatabaseValue const *_pData)
	{
		DMibRequire(_pKey);
		using namespace NPrivate;

		if (!DTxnDbiExist(this, _DatabaseID, DDbUserValid))
			DMibError("Database does not exists");

		f_CheckBlocked();

		if (m_Flags & (DMdbTxnReadOnly))
			DMibError("Trying to write to read only transaction");

		if (!DFIsSet(m_pDatabases[_DatabaseID].m_Flags, EDatabaseSubFlag_SortedDuplicates))
		{
			/* must ignore any data */
			_pData = nullptr;
		}

		int Result = fg_Del0(this, _DatabaseID, _pKey, _pData, EDatabaseWriteFlag_None);
		if (Result)
			DMibError(NStr::fg_Format("Error deleting: {}", fg_StrError(Result)));
	}

	/** @brief Compare two data items according to a particular database.
	 *
	 * This returns a comparison as if the two data items were keys in the
	 * specified database.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] _pLeft The first item to compare
	 * @param[in] _pRight The second item to compare
	 * @return < 0 if _pLeft < _pRight, 0 if _pLeft == _pRight, > 0 if _pLeft > _pRight
	 */
	int CDatabaseTransactionCommon::CInternal::f_KeyCompare(CDatabaseSubID _DatabaseID, CDatabaseValue const *_pLeft, CDatabaseValue const *_pRight)
	{
		return m_pDatabaseInfos[_DatabaseID].m_fKeyCompare(*_pLeft, *_pRight);
	}

	/** @brief Compare two data items according to a particular database.
	 *
	 * This returns a comparison as if the two items were data items of
	 * the specified database. The database must have the #EDatabaseSubFlag_SortedDuplicates flag.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] _pLeft The first item to compare
	 * @param[in] _pRight The second item to compare
	 * @return < 0 if _pLeft < _pRight, 0 if _pLeft == _pRight, > 0 if _pLeft > _pRight
	 */
	int CDatabaseTransactionCommon::CInternal::f_DataCompare(CDatabaseSubID _DatabaseID, CDatabaseValue const *_pLeft, CDatabaseValue const *_pRight)
	{
		FDatabaseCompare const &DataCompare = m_pDatabaseInfos[_DatabaseID].m_fDataCompare;
#if DMibPPtrBits > 32 || defined(DMibDatabase_Always64BitSupport)
		if (_pLeft->m_Size == sizeof(CDatabaseOffset) && DataCompare == NPrivate::g_fCompareInt)
			return NPrivate::g_mdb_cmp_clong(*_pLeft, *_pRight);
#endif
		return DataCompare(*_pLeft, *_pRight);
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

	int NPrivate::fg_Del0(CMdbTxn *_pTransaction, CDatabaseSubID _DatabaseID, CDatabaseValue const *_pKey, CDatabaseValue const *_pData, EDatabaseWriteFlag _Flags)
	{
		CMdbCursor Cursor;
		CMdbXCursor XCursor;
		EDatabaseCursorGet Operation;
		CDatabaseValue DataCopy, *pMatchData;
		int Result, bExact = 0;
		DKBuf;

		DMibDatabaseTrace("====> delete db {} key [{}]", _DatabaseID, DKey(_pKey));

		fg_CursorInit(&Cursor, _pTransaction, _DatabaseID, &XCursor);

		if (_pData)
		{
			Operation = EDatabaseCursorGet_GetBoth;
			DataCopy = *_pData;
			pMatchData = &DataCopy;
		}
		else
		{
			Operation = EDatabaseCursorGet_Set;
			pMatchData = nullptr;
			_Flags |= EDatabaseWriteFlag_NoDuplicateData;
		}
		auto Key = *_pKey;
		Result = fg_CursorSet(&Cursor, &Key, pMatchData, Operation, &bExact);
		if (Result == 0)
		{
			/* let fg_PageSplit know about this cursor if needed:
			 * delete will trigger a rebalance; if it needs to move
			 * a node from one page to another, it will have to
			 * update the parent's separator key(s). If the new sepkey
			 * is larger than the current one, the parent page may
			 * run out of space, triggering a split. We need this
			 * cursor to be consistent until the end of the rebalance.
			 */
			Cursor.m_pNext = _pTransaction->m_pCursors[_DatabaseID];
			_pTransaction->m_pCursors[_DatabaseID] = &Cursor;
			Result = fg_CursorDel(&Cursor, _Flags);
			_pTransaction->m_pCursors[_DatabaseID] = Cursor.m_pNext;
		}
		return Result;
	}
}
