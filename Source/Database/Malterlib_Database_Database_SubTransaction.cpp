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
#include "Malterlib_Database_Database_IDList.h"

namespace NMib::NDatabase
{
	void CDatabaseSubReadTransaction::f_Clear()
	{
		mp_pTransaction = nullptr;
	}

	CDatabaseStatistics CDatabaseSubReadTransaction::f_DatabaseStatistics()
	{
		return mp_pTransaction->f_DatabaseStatistics(mp_DatabaseID);
	}

	CDatabaseSizeStatistics CDatabaseSubReadTransaction::f_SizeStatistics()
	{
		return mp_pTransaction->f_SizeStatistics(mp_DatabaseID);
	}

	CDatabaseEnvironmentInfo CDatabaseSubReadTransaction::f_EnvironmentInfo()
	{
		return mp_pTransaction->f_EnvironmentInfo();
	}

	uint32 CDatabaseSubReadTransaction::f_DatabaseFlags()
	{
		return mp_pTransaction->f_DatabaseFlags(mp_DatabaseID);
	}

	bool CDatabaseSubReadTransaction::f_Get(CDatabaseValue const &_Key, CDatabaseValue &o_Value)
	{
		return mp_pTransaction->f_Get(mp_DatabaseID, const_cast<CDatabaseValue *>(&_Key), &o_Value);
	}

	bool CDatabaseSubReadTransaction::f_Exists(CDatabaseValue const &_Key)
	{
		CDatabaseValue TempValue;
		return mp_pTransaction->f_Get(mp_DatabaseID, const_cast<CDatabaseValue *>(&_Key), &TempValue);
	}

	int CDatabaseSubReadTransaction::f_KeyCompare(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		return mp_pTransaction->f_KeyCompare(mp_DatabaseID, &_Left, &_Right);
	}

	int CDatabaseSubReadTransaction::f_DataCompare(CDatabaseValue const &_Left, CDatabaseValue const &_Right)
	{
		return mp_pTransaction->f_DataCompare(mp_DatabaseID, &_Left, &_Right);
	}

	CDatabaseCursor CDatabaseSubReadTransaction::f_ReadCursor()
	{
		NStorage::TCUniquePointer<CDatabaseCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
		return fg_Move(pCursor);
	}

	CDatabaseCursorDuplicateData CDatabaseSubReadTransaction::f_ReadCursorDuplicateData()
	{
		NStorage::TCUniquePointer<CDatabaseCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
		return fg_Move(pCursor);
	}

	CDatabaseCursor CDatabaseSubReadTransaction::f_ReadCursor(CDatabaseValue const &_Prefix)
	{
		auto Cursor = f_ReadCursor();
		Cursor.f_SetPrefix(_Prefix);
		return Cursor;
	}

	CDatabaseCursorDuplicateData CDatabaseSubReadTransaction::f_ReadCursorDuplicateData(CDatabaseValue const &_Prefix)
	{
		auto Cursor = f_ReadCursorDuplicateData();
		Cursor.f_SetPrefix(_Prefix);
		return Cursor;
	}

	CDatabaseSubReadTransaction::CDatabaseSubReadTransaction(CDatabaseTransactionCommon::CInternal *_pTransaction, CDatabaseSubID _DatabaseID)
		: mp_pTransaction(_pTransaction)
		, mp_DatabaseID(_DatabaseID)
	{
	}

	void CDatabaseSubWriteTransaction::f_DropDatabase(bool _bDelete)
	{
		return mp_pTransaction->f_DropDatabase(mp_DatabaseID, _bDelete);
	}

	void CDatabaseSubWriteTransaction::f_Insert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Transaction = *mp_pTransaction;
		CDatabaseValue Key = _Key;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_NoOverwrite;
		if (Transaction.mt_dbs[mp_DatabaseID].md_flags & EDatabaseSubFlag_SortedDuplicates)
			Flags |= EDatabaseWriteFlag_NoDuplicateData;
		Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
	}

	CDatabaseValue CDatabaseSubWriteTransaction::f_InsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend)
	{
		auto &Transaction = *mp_pTransaction;
		CDatabaseValue Key = _Key;
		CDatabaseValue Value;
		Value.m_Size = _DataSize;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_NoOverwrite | EDatabaseWriteFlag_ReserveSpace;
		Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
		return Value;
	}

	void CDatabaseSubWriteTransaction::f_Upsert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Transaction = *mp_pTransaction;
		CDatabaseValue Key = _Key;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None);
		Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
	}

	CDatabaseValue CDatabaseSubWriteTransaction::f_UpsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend)
	{
		auto &Transaction = *mp_pTransaction;
		CDatabaseValue Key = _Key;
		CDatabaseValue Value;
		Value.m_Size = _DataSize;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_ReserveSpace;
		Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
		return Value;
	}

	void CDatabaseSubWriteTransaction::f_Delete(CDatabaseValue const &_Key)
	{
		return mp_pTransaction->f_Delete(mp_DatabaseID, &_Key, nullptr);
	}

	void CDatabaseSubWriteTransaction::f_DeleteData(CDatabaseValue const &_Key, CDatabaseValue const &_Data)
	{
		return mp_pTransaction->f_Delete(mp_DatabaseID, &_Key, &_Data);
	}

	CDatabaseSubWriteTransaction::CDatabaseSubWriteTransaction(CDatabaseTransactionCommon::CInternal *_pTransaction, CDatabaseSubID _DatabaseID)
		: CDatabaseSubReadTransaction(_pTransaction, _DatabaseID)
	{
	}

	CDatabaseCursorWrite CDatabaseSubWriteTransaction::f_WriteCursor()
	{
		NStorage::TCUniquePointer<CDatabaseCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
		return fg_Move(pCursor);
	}

	CDatabaseCursorDuplicateDataWrite CDatabaseSubWriteTransaction::f_WriteCursorDuplicateData()
	{
		NStorage::TCUniquePointer<CDatabaseCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
		return fg_Move(pCursor);
	}

	CDatabaseCursorWrite CDatabaseSubWriteTransaction::f_WriteCursor(CDatabaseValue const &_Prefix)
	{
		auto Cursor = f_WriteCursor();
		Cursor.f_SetPrefix(_Prefix);
		return Cursor;
	}

	CDatabaseCursorDuplicateDataWrite CDatabaseSubWriteTransaction::f_WriteCursorDuplicateData(CDatabaseValue const &_Prefix)
	{
		auto Cursor = f_WriteCursorDuplicateData();
		Cursor.f_SetPrefix(_Prefix);
		return Cursor;
	}
}
