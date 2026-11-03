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
	CDatabaseCursorCommon::CDatabaseCursorCommon() = default;

	CDatabaseCursorCommon::~CDatabaseCursorCommon()
	{
		if (mp_pInternal)
			NPrivate::fg_CursorClose(mp_pInternal.f_Detach());
	}

	CDatabaseCursorCommon::CDatabaseCursorCommon(CDatabaseCursorCommon &&_Other) = default;

	CDatabaseCursorCommon &CDatabaseCursorCommon::operator = (CDatabaseCursorCommon &&_Other) = default;

	CDatabaseCursorCommon::CDatabaseCursorCommon(NStorage::TCUniquePointer<CInternal> &&_pInternal)
		: mp_pInternal(fg_Move(_pInternal))
	{
	}

	CDatabaseCursor::CDatabaseCursor(NStorage::TCUniquePointer<CInternal> &&_pInternal)
		: CDatabaseCursorCommon(fg_Move(_pInternal))
	{
	}

	CDatabaseCursorDuplicateData::CDatabaseCursorDuplicateData(NStorage::TCUniquePointer<CInternal> &&_pInternal)
		: CDatabaseCursorCommon(fg_Move(_pInternal))
	{
	}

	CDatabaseCursorWrite::CDatabaseCursorWrite(NStorage::TCUniquePointer<CInternal> &&_pInternal)
		: CDatabaseCursor(fg_Move(_pInternal))
	{
	}

	CDatabaseCursorWrite CDatabaseCursorWrite::fs_InitEmpty()
	{
		return CDatabaseCursorWrite(nullptr);
	}

	void CDatabaseCursorCommon::f_SetPrefix(CDatabaseValue const &_Prefix)
	{
		mp_Prefix = _Prefix;
		if (!mp_Prefix.f_IsValid())
		{
			f_First();
			return;
		}

		if (!fp_FindLowerBoundIgnoringPrefix(mp_Prefix))
			return;

		if (!mp_Key.f_HasPrefix(mp_Prefix))
			mp_bValid = false;
	}

	void CDatabaseCursorCommon::CInternal::f_Put(CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseWriteFlag _WriteFlags)
	{
		DMibRequire(_pKey);
		DMibRequire(_pData);
		DMibRequire(!m_pTransaction->m_pEnvironment->f_IsInsideMemoryMap(_pKey->m_pData) && !m_pTransaction->m_pEnvironment->f_IsInsideMemoryMap(_pData->m_pData))
			("You cannot use a value from the database in a database write. The database can change the value while writing, corrupting the database.")
		;

		using namespace NPrivate;

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

		if (m_pTransaction->m_Flags & (DMdbTxnReadOnly))
			DMibError("Trying to write to read only transaction");

		int Result = fg_CursorPut(this, _pKey, _pData, _WriteFlags);
		if (Result)
			DMibError(NStr::fg_Format("Error putting data: {}", fg_StrError(Result)));
	}

	void CDatabaseCursorCommon::CInternal::f_Delete(EDatabaseWriteFlag _WriteFlags)
	{
		using namespace NPrivate;

		if (_WriteFlags & ~(EDatabaseWriteFlag_NoDuplicateData))
			DMibError("Invalid flags");

		if (m_pTransaction->m_Flags & (DMdbTxnReadOnly))
			DMibError("Trying to delete in read only transaction");

		int Result = fg_CursorDel(this, _WriteFlags);
		if (Result)
			DMibError(NStr::fg_Format("Error deleting data: {}", fg_StrError(Result)));
	}

	CDatabaseOffset CDatabaseCursorCommon::CInternal::f_CountDuplicates()
	{
		using namespace NPrivate;

		CDatabaseOffset Ret;
		int Result = fg_CursorCount(this, &Ret);
		if (Result)
			DMibError(NStr::fg_Format("Error counting duplicates: {}", fg_StrError(Result)));
		return Ret;
	}

	void CDatabaseCursorWrite::f_SetValue(CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_OverwriteAtCurrentCursorPos;

		CDatabaseValue Key(mp_KeyCache, mp_Key);
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
	}

	CDatabaseValue CDatabaseCursorWrite::f_SetValueInplace(umint _DataSize, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value;
		Value.m_Size = _DataSize;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_OverwriteAtCurrentCursorPos | EDatabaseWriteFlag_ReserveSpace;
		CDatabaseValue Key(mp_KeyCache, mp_Key);
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
		return Value;
	}

	void CDatabaseCursorWrite::f_Insert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_NoOverwrite;
		auto Key = _Key;
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
	}

	CDatabaseValue CDatabaseCursorWrite::f_InsertInplace(CDatabaseValue const &_Key, umint _DataSize, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value;
		Value.m_Size = _DataSize;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_NoOverwrite | EDatabaseWriteFlag_ReserveSpace;
		auto Key = _Key;
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
		return Value;
	}

	void CDatabaseCursorWrite::f_Upsert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None);
		auto Key = _Key;
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
	}

	CDatabaseValue CDatabaseCursorWrite::f_UpsertInplace(CDatabaseValue const &_Key, umint _DataSize, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value;
		Value.m_Size = _DataSize;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_ReserveSpace;
		auto Key = _Key;
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
		return Value;
	}

	void CDatabaseCursorWrite::f_Delete()
	{
		auto &Cursor = *mp_pInternal;
		Cursor.f_Delete(EDatabaseWriteFlag_None);
		++(*this);
	}

	CDatabaseCursorDuplicateDataWrite::CDatabaseCursorDuplicateDataWrite(NStorage::TCUniquePointer<CInternal> &&_pInternal)
		: CDatabaseCursorDuplicateData(fg_Move(_pInternal))
	{
	}

	void CDatabaseCursorDuplicateDataWrite::f_SetValue(CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_OverwriteAtCurrentCursorPos;

		CDatabaseValue Key(mp_KeyCache, mp_Key);
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
	}

	void CDatabaseCursorDuplicateDataWrite::f_Insert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None) | EDatabaseWriteFlag_NoOverwrite | EDatabaseWriteFlag_NoDuplicateData;
		auto Key = _Key;
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
	}

	void CDatabaseCursorDuplicateDataWrite::f_Upsert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend)
	{
		auto &Cursor = *mp_pInternal;
		CDatabaseValue Value = _Value;
		auto Flags = (_bAppend ? EDatabaseWriteFlag_Append : EDatabaseWriteFlag_None);
		auto Key = _Key;
		Cursor.f_Put(&Key, &Value, Flags);
		mp_Key = Key;
		mp_Value = Value;
	}

	void CDatabaseCursorDuplicateDataWrite::f_DeleteKey()
	{
		auto &Cursor = *mp_pInternal;
		Cursor.f_Delete(EDatabaseWriteFlag_NoDuplicateData);
		fp_SetToCurrent();
	}

	void CDatabaseCursorDuplicateDataWrite::f_DeleteData()
	{
		auto &Cursor = *mp_pInternal;
		Cursor.f_Delete(EDatabaseWriteFlag_None);
		fp_SetToCurrent();
	}

	CDatabaseCursorCommon::operator bool () const
	{
		return mp_bValid;
	}

	CDatabaseValue const &CDatabaseCursorCommon::f_Key() const
	{
		return mp_Key;
	}

	void CDatabaseCursorCommon::fp_SetToCurrent()
	{
		fp_CheckValidityIgnoringPrefix(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_GetCurrent), "Error setting cursor to current: {}");
	}

	bool CDatabaseCursorCommon::fp_FindLowerBoundIgnoringPrefix(CDatabaseValue const &_Key)
	{
		mp_Key = _Key;

		return fp_CheckValidityIgnoringPrefix
			(
				NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_SetRange)
				, "Error setting cursor to lower bound: {}"
			)
		;
	}

	bool CDatabaseCursorCommon::fp_CheckValidityIgnoringPrefix(int _Result, ch8 const *_pErrorFormat)
	{
		if (_Result == DMdbNotFound)
			mp_bValid = false;
		else if (_Result)
			DMibError(NStr::fg_Format(_pErrorFormat, NPrivate::fg_StrError(_Result)));
		else
			mp_bValid = true;
		return mp_bValid;
	}

	bool CDatabaseCursorCommon::fp_CheckValidity(int _Result, ch8 const *_pErrorFormat)
	{
		if (_Result == DMdbNotFound)
			mp_bValid = false;
		else if (_Result)
			DMibError(NStr::fg_Format(_pErrorFormat, NPrivate::fg_StrError(_Result)));
		else if (mp_Prefix.f_IsValid())
			mp_bValid = mp_Key.f_HasPrefix(mp_Prefix);
		else
			mp_bValid = true;

		return mp_bValid;
	}

	bool CDatabaseCursorCommon::f_First()
	{
		if (mp_Prefix.f_IsValid())
		{
			if (!fp_FindLowerBoundIgnoringPrefix(mp_Prefix))
				return false;

			if (!mp_Key.f_HasPrefix(mp_Prefix))
				mp_bValid = false;

			return mp_bValid;
		}

		return fp_CheckValidityIgnoringPrefix(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_First), "Error setting cursor to first: {}");
	}

	bool CDatabaseCursorCommon::f_Last()
	{
		if (mp_Prefix.f_IsValid() && mp_Prefix.m_Size)
		{
			CDatabaseValue PrefixReverse;
			mp_PrefixReverseCache = mp_PrefixCache;
			auto &LastByte = mp_PrefixReverseCache[mp_Prefix.m_Size - 1];
			if (LastByte < 255)
				++LastByte;
			else
				mp_PrefixReverseCache.f_Insert(uint8(0));

			PrefixReverse = mp_PrefixReverseCache;
			if (!fp_FindLowerBoundIgnoringPrefix(PrefixReverse))
			{
				int Result = NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_Last);
				if (Result == DMdbNotFound)
				{
					mp_bValid = false;
					return false;
				}
				else if (Result)
					DMibError(NStr::fg_Format("Error setting cursor to last: {}", NPrivate::fg_StrError(Result)));
				else
					mp_bValid = true;
			}

			if (mp_Key.f_HasPrefix(mp_Prefix))
				return true;

			--(*this);
			if (!mp_bValid)
				return false;

			if (!mp_Key.f_HasPrefix(mp_Prefix))
				mp_bValid = false;

			return mp_bValid;
		}

		return fp_CheckValidityIgnoringPrefix(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_Last), "Error setting cursor to last: {}");
	}

	bool CDatabaseCursorCommon::f_FindEqual(CDatabaseValue const &_Key) // EDatabaseCursorGet_Set, EDatabaseCursorGet_SetKey
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;

		return fp_CheckValidity(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_SetKey), "Error setting cursor to key: {}");
	}

	bool CDatabaseCursorCommon::f_FindLowerBound(CDatabaseValue const &_Key) // EDatabaseCursorGet_SetRange
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;

		return fp_CheckValidity(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_SetRange), "Error setting cursor to lower bound: {}");
	}

	void CDatabaseCursorCommon::operator ++ ()
	{
		fp_CheckValidity(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_NextNoDup), "Error setting cursor to next: {}");
	}

	void CDatabaseCursorCommon::operator -- ()
	{
		fp_CheckValidity(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_PrevNoDup), "Error setting cursor to previous: {}");
	}

	CDatabaseOffset CDatabaseCursorDuplicateData::f_DataCount()
	{
		auto &Cursor = *mp_pInternal;
		return Cursor.f_CountDuplicates();
	}

	bool CDatabaseCursorDuplicateData::f_NextData()
	{
		return fp_CheckValidity(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_NextDup), "Error setting cursor to next data: {}");
	}

	bool CDatabaseCursorDuplicateData::f_PrevData()
	{
		return fp_CheckValidity(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_PrevDup), "Error setting cursor to prev data: {}");
	}

	bool CDatabaseCursorDuplicateData::fp_FindLowerBoundIgnoringPrefix(CDatabaseValue const &_Key, CDatabaseValue const &_Value)
	{
		mp_Key = _Key;
		mp_Value = _Value;

		return fp_CheckValidityIgnoringPrefix
			(
				NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_GetBothRange)
				, "Error setting cursor to lower bound key and data: {}"
			)
		;
	}

	bool CDatabaseCursorDuplicateData::f_FirstData()
	{
		if (mp_Prefix.f_IsValid())
		{
			if (!fp_FindLowerBoundIgnoringPrefix(mp_Prefix, CDatabaseValue()))
				return false;

			if (!mp_Key.f_HasPrefix(mp_Prefix))
				mp_bValid = false;

			return mp_bValid;
		}

		return fp_CheckValidityIgnoringPrefix
			(
				NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_FirstDup)
				, "Error setting cursor to first data: {}"
			)
		;
	}

	bool CDatabaseCursorDuplicateData::f_LastData()
	{
		if (mp_Prefix.f_IsValid() && mp_Prefix.m_Size)
		{
			CDatabaseValue PrefixReverse;
			mp_PrefixReverseCache = mp_PrefixCache;
			auto &LastByte = mp_PrefixReverseCache[mp_Prefix.m_Size - 1];
			if (LastByte < 255)
				++LastByte;
			else
				mp_PrefixReverseCache.f_Insert(uint8(0));

			PrefixReverse = mp_PrefixReverseCache;
			if (!fp_FindLowerBoundIgnoringPrefix(PrefixReverse, CDatabaseValue()))
			{
				int Result = NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_LastDup);
				if (Result == DMdbNotFound)
				{
					mp_bValid = false;
					return false;
				}
				else if (Result)
					DMibError(NStr::fg_Format("Error setting cursor to last: {}", NPrivate::fg_StrError(Result)));
				else
					mp_bValid = true;
			}

			if (mp_Key.f_HasPrefix(mp_Prefix))
				return true;

			f_PrevData();
			if (!mp_bValid)
				return false;

			if (!mp_Key.f_HasPrefix(mp_Prefix))
				mp_bValid = false;

			return mp_bValid;
		}

		return fp_CheckValidityIgnoringPrefix
			(
				NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_LastDup)
				, "Error setting cursor to last data: {}"
			)
		;
	}

	bool CDatabaseCursorDuplicateData::f_FindEqual(CDatabaseValue const &_Key, CDatabaseValue const &_Data)
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;
		mp_Value = _Data;

		return fp_CheckValidity(NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_GetBoth), "Error setting cursor to key and data: {}");
	}

	bool CDatabaseCursorDuplicateData::f_FindLowerBound(CDatabaseValue const &_Key, CDatabaseValue const &_Data)
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;
		mp_Value = _Data;

		return fp_CheckValidity
			(
				NPrivate::fg_CursorGet(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::EDatabaseCursorGet_GetBothRange)
				, "Error setting cursor to lower bound key and data: {}"
			)
		;
	}

	/** @brief Create a cursor handle.
	 *
	 * A cursor is associated with a specific transaction and database.
	 * A cursor cannot be used when its database handle is closed.  Nor
	 * when its transaction has ended.
	 * It can be discarded with #fg_CursorClose().
	 * A cursor in a write-transaction can be closed before its transaction
	 * ends, and will otherwise be closed when its transaction ends.
	 * A cursor in a read-only transaction must be closed explicitly, before
	 * or after its transaction ends.
	 * @note Earlier documentation said that cursors in every transaction
	 * were closed when the transaction committed or aborted.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[out] cursor Address where the new #CMdbCursor handle will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	NPrivate::CMdbCursor *CDatabaseTransactionCommon::CInternal::f_Cursor(CDatabaseSubID _DatabaseID)
	{
		using namespace NPrivate;

		CMdbCursor *pCursor;
		umint Size = sizeof(CMdbCursor);

		if (!DTxnDbiExist(this, _DatabaseID, DDbValid))
			DMibError("Database does not exists");

		f_CheckBlocked();

		if (_DatabaseID == DFreeDbi && !DFIsSet(m_Flags, DMdbTxnReadOnly))
			DMibError("Cannot open free database as read/write");

		if (m_pDatabases[_DatabaseID].m_Flags & EDatabaseSubFlag_SortedDuplicates)
			Size += sizeof(CMdbXCursor);

		pCursor = (CMdbCursor *)NMemory::fg_Alloc(Size);
		auto Cleanup = g_OnScopeExit / [&]()
			{
				fg_CursorClose(pCursor);
			}
		;
		fg_CursorInit(pCursor, this, _DatabaseID, (CMdbXCursor *)(pCursor + 1));
		if (m_pCursors)
		{
			pCursor->m_pNext = m_pCursors[_DatabaseID];
			m_pCursors[_DatabaseID] = pCursor;
			pCursor->m_Flags |= DCUntrack;
		}

		Cleanup.f_Clear();
		return pCursor;
	}
}

namespace NMib::NDatabase::NPrivate
{
	/** @brief Close a cursor handle.
	 *
	 * The cursor handle will be freed and must not be used again after this call.
	 * Its transaction must still be live if it is a write-transaction.
	 * @param[in] _pCursor A cursor handle returned by #f_Cursor()
	 */
	void fg_CursorClose(CMdbCursor *_pCursor)
	{
		if (_pCursor)
			DMdbCursorUnref(_pCursor, 0);
		if (_pCursor && !_pCursor->m_pBackup)
		{
			/* Remove from txn, if tracked.
			 * A read-only txn (!DCUntrack) may have been freed already,
			 * so do not peek inside it.  Only write txns track cursors.
			 */
			if ((_pCursor->m_Flags & DCUntrack) && _pCursor->m_pTransaction->m_pCursors)
			{
				CMdbCursor **pPrevious = &_pCursor->m_pTransaction->m_pCursors[_pCursor->m_DatabaseID];
				while (*pPrevious && *pPrevious != _pCursor)
					pPrevious = &(*pPrevious)->m_pNext;
				if (*pPrevious == _pCursor)
					*pPrevious = _pCursor->m_pNext;
			}
			umint Size = sizeof(CMdbCursor);
			if (_pCursor->m_pXCursor)
				Size += sizeof(CMdbXCursor);

			NMemory::fg_Free(_pCursor, Size);
		}
	}

	/** @brief Retrieve by cursor.
	 *
	 * This function retrieves key/data pairs from the database. The address and length
	 * of the key are returned in the object to which \b _pKey refers (except for the
	 * case of the #EDatabaseCursorGet_Set option, in which the \b _pKey object is unchanged), and
	 * the address and length of the data are returned in the object to which \b _pData
	 * refers.
	 * See #f_Get() for restrictions on using the output values.
	 * @param[in] _pCursor A cursor handle returned by #f_Cursor()
	 * @param[in,out] _pKey The key for a retrieved item
	 * @param[in,out] _pData The data of a retrieved item
	 * @param[in] _Operation A cursor operation #EDatabaseCursorGet
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#DMdbNotFound - no matching key found.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	int fg_CursorGet(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation)
	{
		using namespace NPrivate;

		int Result;
		int bExact = 0;
		int (*fMove)(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData);

		if (_pCursor == nullptr)
			return EINVAL;

		_pCursor->m_pTransaction->f_CheckBlocked();

		switch (_Operation)
		{
		case EDatabaseCursorGet_GetCurrent:
			if (!(_pCursor->m_Flags & DCInitialized))
				Result = EINVAL;
			else
			{
				CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop];
				int nKeys = DNumKeys(pPage);
				if (!nKeys || _pCursor->m_NodeIndices[_pCursor->m_iTop] >= nKeys)
				{
					_pCursor->m_NodeIndices[_pCursor->m_iTop] = nKeys;
					Result = DMdbNotFound;
					break;
				}
				Result = DMdbSuccess;
				if (DIsLeaf2(pPage))
				{
					_pKey->m_Size = _pCursor->m_pDatabase->m_Pad;
					_pKey->m_pData = DLeaf2Key(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop], _pKey->m_Size);
				}
				else
				{
					CMdbNode *pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
					DMdbGetKey(pLeaf, _pKey);
					if (_pData)
					{
						if (DFIsSet(pLeaf->m_Flags, DFDupData))
							Result = fg_CursorGet(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr, EDatabaseCursorGet_GetCurrent);
						else
							Result = fg_NodeRead(_pCursor, pLeaf, _pData);
					}
				}
			}
			break;
		case EDatabaseCursorGet_GetBoth:
		case EDatabaseCursorGet_GetBothRange:
			if (_pData == nullptr)
			{
				Result = EINVAL;
				break;
			}
			if (_pCursor->m_pXCursor == nullptr)
			{
				Result = DMdbIncompatible;
				break;
			}
			/* FALLTHRU */
		case EDatabaseCursorGet_Set:
		case EDatabaseCursorGet_SetKey:
		case EDatabaseCursorGet_SetRange:
			if (_pKey == nullptr)
				Result = EINVAL;
			else
				Result = fg_CursorSet(_pCursor, _pKey, _pData, _Operation, _Operation == EDatabaseCursorGet_SetRange ? nullptr : &bExact);
			break;
		case EDatabaseCursorGet_GetMultiple:
			if (_pData == nullptr || !(_pCursor->m_Flags & DCInitialized))
			{
				Result = EINVAL;
				break;
			}
			if (!(_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
			{
				Result = DMdbIncompatible;
				break;
			}
			Result = DMdbSuccess;
			if (!(_pCursor->m_pXCursor->m_Cursor.m_Flags & DCInitialized) || (_pCursor->m_pXCursor->m_Cursor.m_Flags & DCEof))
				break;
			goto fetchm;
		case EDatabaseCursorGet_NextMultiple:
			if (_pData == nullptr)
			{
				Result = EINVAL;
				break;
			}
			if (!(_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
			{
				Result = DMdbIncompatible;
				break;
			}
			Result = fg_CursorNext(_pCursor, _pKey, _pData, EDatabaseCursorGet_NextDup);
			if (Result == DMdbSuccess)
			{
				if (_pCursor->m_pXCursor->m_Cursor.m_Flags & DCInitialized)
				{
					CMdbCursor *pSubCursor;
				fetchm:
					pSubCursor = &_pCursor->m_pXCursor->m_Cursor;
					_pData->m_Size = DNumKeys(pSubCursor->m_Pages[pSubCursor->m_iTop]) * pSubCursor->m_pDatabase->m_Pad;
					_pData->m_pData = DMetaData(pSubCursor->m_Pages[pSubCursor->m_iTop]);
					pSubCursor->m_NodeIndices[pSubCursor->m_iTop] = DNumKeys(pSubCursor->m_Pages[pSubCursor->m_iTop]) - 1;
				}
				else
					Result = DMdbNotFound;
			}
			break;
		case EDatabaseCursorGet_PrevMultiple:
			if (_pData == nullptr)
			{
				Result = EINVAL;
				break;
			}
			if (!(_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
			{
				Result = DMdbIncompatible;
				break;
			}
			if (!(_pCursor->m_Flags & DCInitialized))
				Result = fg_CursorLast(_pCursor, _pKey, _pData);
			else
				Result = DMdbSuccess;
			if (Result == DMdbSuccess)
			{
				CMdbCursor *pSubCursor = &_pCursor->m_pXCursor->m_Cursor;
				if (pSubCursor->m_Flags & DCInitialized)
				{
					Result = fg_CursorSibling(pSubCursor, 0);
					if (Result == DMdbSuccess)
						goto fetchm;
				}
				else
					Result = DMdbNotFound;
			}
			break;
		case EDatabaseCursorGet_Next:
		case EDatabaseCursorGet_NextDup:
		case EDatabaseCursorGet_NextNoDup:
			Result = fg_CursorNext(_pCursor, _pKey, _pData, _Operation);
			break;
		case EDatabaseCursorGet_Prev:
		case EDatabaseCursorGet_PrevDup:
		case EDatabaseCursorGet_PrevNoDup:
			Result = fg_CursorPrev(_pCursor, _pKey, _pData, _Operation);
			break;
		case EDatabaseCursorGet_First: Result = fg_CursorFirst(_pCursor, _pKey, _pData); break;
		case EDatabaseCursorGet_FirstDup: fMove = fg_CursorFirst;
		mmove:
			if (_pData == nullptr || !(_pCursor->m_Flags & DCInitialized))
			{
				Result = EINVAL;
				break;
			}
			if (_pCursor->m_pXCursor == nullptr)
			{
				Result = DMdbIncompatible;
				break;
			}
			if (_pCursor->m_NodeIndices[_pCursor->m_iTop] >= DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop]))
			{
				_pCursor->m_NodeIndices[_pCursor->m_iTop] = DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop]);
				Result = DMdbNotFound;
				break;
			}
			{
				CMdbNode *pLeaf = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);
				if (!DFIsSet(pLeaf->m_Flags, DFDupData))
				{
					DMdbGetKey(pLeaf, _pKey);
					Result = fg_NodeRead(_pCursor, pLeaf, _pData);
					break;
				}
			}
			if (!(_pCursor->m_pXCursor->m_Cursor.m_Flags & DCInitialized))
			{
				Result = EINVAL;
				break;
			}
			Result = fMove(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr);
			break;
		case EDatabaseCursorGet_Last: Result = fg_CursorLast(_pCursor, _pKey, _pData); break;
		case EDatabaseCursorGet_LastDup:
			fMove = fg_CursorLast;
			goto mmove;
		default:
			DMibDatabaseTrace("Unhandled/unimplemented cursor operation {}", _Operation);
			Result = EINVAL;
			break;
		}

		if (_pCursor->m_Flags & DCDel)
			_pCursor->m_Flags ^= DCDel;

		return Result;
	}

	/** @brief Store by cursor.
	 *
	 * This function stores key/data pairs into the database.
	 * The cursor is positioned at the new item, or on failure usually near it.
	 * @note Earlier documentation incorrectly said errors would leave the
	 * state of the cursor unchanged.
	 * @param[in] _pCursor A cursor handle returned by #f_Cursor()
	 * @param[in] _pKey The key operated on.
	 * @param[in] _pData The data operated on.
	 * @param[in] flags Options for this operation. This parameter
	 * must be set to 0 or one of the values described here.
	 * <ul>
	 *	<li>#EDatabaseWriteFlag_OverwriteAtCurrentCursorPos - replace the item at the current cursor position.
	 *		The \b _pKey parameter must still be provided, and must match it.
	 *		If using sorted duplicates (#EDatabaseSubFlag_SortedDuplicates) the data item must still
	 *		sort into the same place. This is intended to be used when the
	 *		new data is the same size as the old. Otherwise it will simply
	 *		perform a delete of the old record followed by an insert.
	 *	<li>#EDatabaseWriteFlag_NoDuplicateData - enter the new key/data pair only if it does not
	 *		already appear in the database. This flag may only be specified
	 *		if the database was opened with #EDatabaseSubFlag_SortedDuplicates. The function will
	 *		return #DMdbKeyExist if the key/data pair already appears in the
	 *		database.
	 *	<li>#EDatabaseWriteFlag_NoOverwrite - enter the new key/data pair only if the key
	 *		does not already appear in the database. The function will return
	 *		#DMdbKeyExist if the key already appears in the database, even if
	 *		the database supports duplicates (#EDatabaseSubFlag_SortedDuplicates).
	 *	<li>#EDatabaseWriteFlag_ReserveSpace - reserve space for data of the given size, but
	 *		don't copy the given data. Instead, return a pointer to the
	 *		reserved space, which the caller can fill in later - before
	 *		the next update operation or the transaction ends. This saves
	 *		an extra memcpy if the data is being generated later. This flag
	 *		must not be specified if the database was opened with #EDatabaseSubFlag_SortedDuplicates.
	 *	<li>#EDatabaseWriteFlag_Append - append the given key/data pair to the end of the
	 *		database. No key comparisons are performed. This option allows
	 *		fast bulk loading when keys are already known to be in the
	 *		correct order. Loading unsorted keys with this flag will cause
	 *		a #DMdbKeyExist error.
	 *	<li>#EDatabaseWriteFlag_AppendDuplicateData - as above, but for sorted dup data.
	 *	<li>#EDatabaseWriteFlag_BatchDuplicateData - store multiple contiguous data elements in a
	 *		single request. This flag may only be specified if the database
	 *		was opened with #EDatabaseSubFlag_SortedDuplicatesFixedSize. The \b _pData argument must be an
	 *		array of two MDB_vals. The m_Size of the first CDatabaseValue must be
	 *		the size of a single data element. The m_pData of the first CDatabaseValue
	 *		must point to the beginning of the array of contiguous data elements.
	 *		The m_Size of the second CDatabaseValue must be the count of the number
	 *		of data elements to store. On return this field will be set to
	 *		the count of the number of elements actually written. The m_pData
	 *		of the second CDatabaseValue is unused.
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
	int fg_CursorPut(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseWriteFlag _Flags)
	{
		uint32 Flags = _Flags;
		using namespace NPrivate;
		CMdbEnv *pEnvironment;
		CMdbNode *pLeaf = nullptr;
		CMdbPage *pSubPage, *pPage, *pSubRoot = nullptr;
		uint16 SubPageFlags;
		CDatabaseValue NodeData, *pNodeData, DuplicateKey, OldData;
		CMdbDb DummyDatabase;
		int bDoSub = 0, bInsertKey, bInsertData;
		uint32 nPutItems = 0, nDataItems = 0, bNoSpill;
		umint NodeSize;
		int Result, Result2;
		uint32 NodeFlags;
		DKBuf;

		if (_pCursor == nullptr || _pKey == nullptr)
			return EINVAL;

		pEnvironment = _pCursor->m_pTransaction->m_pEnvironment;

		/* Check this first so counter will always be zero on any
		 * early failures.
		 */
		if (Flags & EDatabaseWriteFlag_BatchDuplicateData)
		{
			nDataItems = _pData[1].m_Size;
			_pData[1].m_Size = 0;
			if (!DFIsSet(_pCursor->m_pDatabase->m_Flags, EDatabaseSubFlag_SortedDuplicatesFixedSize))
				return DMdbIncompatible;
		}

		bNoSpill = Flags & DMdbNoSpill;
		Flags &= ~DMdbNoSpill;

		_pCursor->m_pTransaction->f_CheckBlocked();

		if (_pCursor->m_pTransaction->m_Flags & (DMdbTxnReadOnly))
			return EACCES;

		if (_pKey->m_Size - 1 >= DEnvMaxkey(pEnvironment))
			return DMdbBadValsize;

#if SIZE_MAX > DMaxDataSize
		if (_pData->m_Size > ((_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates) ? DEnvMaxkey(pEnvironment) : DMaxDataSize))
			return DMdbBadValsize;
#else
		if ((_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates) && _pData->m_Size > DEnvMaxkey(pEnvironment))
			return DMdbBadValsize;
#endif

		DMibDatabaseTrace("==> put db {} key [{}], size {}, data size {}", DDbi(_pCursor), DKey(_pKey), _pKey ? _pKey->m_Size : 0, _pData->m_Size);

		DuplicateKey.m_Size = 0;

		if (Flags & EDatabaseWriteFlag_OverwriteAtCurrentCursorPos)
		{
			if (!(_pCursor->m_Flags & DCInitialized))
				return EINVAL;
			Result = DMdbSuccess;
		}
		else if (_pCursor->m_pDatabase->m_RootPage == DPInvalid)
		{
			/* new database, cursor has nothing to point to */
			_pCursor->m_nPages = 0;
			_pCursor->m_iTop = 0;
			_pCursor->m_Flags &= ~DCInitialized;
			Result = DMdbNoRoot;
		}
		else
		{
			int bExact = 0;
			CDatabaseValue LastData;
			if (Flags & EDatabaseWriteFlag_Append)
			{
				CDatabaseValue LastKey;
				Result = fg_CursorLast(_pCursor, &LastKey, &LastData);
				if (Result == 0)
				{
					Result = _pCursor->m_pDatabaseInfo->m_fKeyCompare(*_pKey, LastKey);
					if (Result > 0)
					{
						Result = DMdbNotFound;
						_pCursor->m_NodeIndices[_pCursor->m_iTop]++;
					}
					else
					{
						/* new key is <= last key */
						Result = DMdbKeyExist;
					}
				}
			}
			else
				Result = fg_CursorSet(_pCursor, _pKey, &LastData, EDatabaseCursorGet_Set, &bExact);
			if ((Flags & EDatabaseWriteFlag_NoOverwrite) && Result == 0)
			{
				DMibDatabaseTrace("duplicate key [{}]", DKey(_pKey));
				*_pData = LastData;
				return DMdbKeyExist;
			}
			if (Result && Result != DMdbNotFound)
				return Result;
		}

		if (_pCursor->m_Flags & DCDel)
			_pCursor->m_Flags ^= DCDel;

		/* Cursor is positioned, check for room in the dirty list */
		if (!bNoSpill)
		{
			if (Flags & EDatabaseWriteFlag_BatchDuplicateData)
			{
				pNodeData = &NodeData;
				NodeData.m_Size = _pData->m_Size * nDataItems;
			}
			else
				pNodeData = _pData;
			if ((Result2 = fg_PageSpill(_pCursor, _pKey, pNodeData)))
				return Result2;
		}

		if (Result == DMdbNoRoot)
		{
			CMdbPage *pNewPage;
			/* new database, write a root leaf page */
			DPuts("allocating new root leaf page");
			if ((Result2 = fg_PageNew(_pCursor, DPLeaf, 1, &pNewPage)))
				return Result2;
			fg_CursorPush(_pCursor, pNewPage);
			_pCursor->m_pDatabase->m_RootPage = pNewPage->m_PageNumber;
			_pCursor->m_pDatabase->m_Depth++;
			*_pCursor->m_pDatabaseFlag |= DDbDirty;
			if ((_pCursor->m_pDatabase->m_Flags & (EDatabaseSubFlag_SortedDuplicates | EDatabaseSubFlag_SortedDuplicatesFixedSize)) == EDatabaseSubFlag_SortedDuplicatesFixedSize)
				pNewPage->m_Flags |= DPLeaf2;
			_pCursor->m_Flags |= DCInitialized;
		}
		else
		{
			/* make sure all cursor pages are writable */
			Result2 = fg_CursorTouch(_pCursor);
			if (Result2)
				return Result2;
		}

		unsigned Growth = 0;

		bInsertKey = bInsertData = Result;
		if (bInsertKey)
		{
			/* The key does not exist */
			DMibDatabaseTrace("inserting key at index {}", _pCursor->m_NodeIndices[_pCursor->m_iTop]);
			if ((_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates) && DLeafSize(_pKey, _pData) > pEnvironment->m_MaxNodeSize)
			{
				/* Too big for a node, insert in sub-DB.  Set up an empty
				 * "old sub-page" for prep_subDB to expand to a full page.
				 */
				SubPageFlags = DPLeaf | DPDirty;
				pSubPage = (CMdbPage *)pEnvironment->m_pScratchBuffer;
				pSubPage->m_Pad = _pData->m_Size; // used if EDatabaseSubFlag_SortedDuplicatesFixedSize
				pSubPage->m_FreeSpace.m_Lower = pSubPage->m_FreeSpace.m_Upper = (DPageHeaderSize - DPageBase);
				OldData.m_Size = DPageHeaderSize;
				goto prep_subDB;
			}
		}
		else
		{
			/* there's only a key anyway, so this is a no-op */
			if (DIsLeaf2(_pCursor->m_Pages[_pCursor->m_iTop]))
			{
				{
					uint8 *pLeaf2Key;
					uint32 KeySize = _pCursor->m_pDatabase->m_Pad;
					if (_pKey->m_Size != KeySize)
						return DMdbBadValsize;
					pLeaf2Key = DLeaf2Key(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop], KeySize);
					memcpy(pLeaf2Key, _pKey->m_pData, KeySize);
				}
			fix_parent:
				/* if overwriting slot 0 of leaf, need to
				 * update branch key if there is a parent page
				 */
				if (_pCursor->m_iTop && !_pCursor->m_NodeIndices[_pCursor->m_iTop])
				{
					unsigned short TopDelta = 1;
					_pCursor->m_iTop--;
					/* slot 0 is always an empty key, find real slot */
					while (_pCursor->m_iTop && !_pCursor->m_NodeIndices[_pCursor->m_iTop])
					{
						_pCursor->m_iTop--;
						TopDelta++;
					}
					if (_pCursor->m_NodeIndices[_pCursor->m_iTop])
						Result2 = fg_UpdateKey(_pCursor, _pKey);
					else
						Result2 = DMdbSuccess;
					_pCursor->m_iTop += TopDelta;
					if (Result2)
						return Result2;
				}
				return DMdbSuccess;
			}

		more:
			pLeaf = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);
			OldData.m_Size = DNodeDataSize(pLeaf);
			OldData.m_pData = DNodeData(pLeaf);

			/* DB has dups? */
			if (DFIsSet(_pCursor->m_pDatabase->m_Flags, EDatabaseSubFlag_SortedDuplicates))
			{
				/* Prepare (sub-)page/sub-DB to accept the new item,
				 * if needed.  pSubPage: old sub-page or a header faking
				 * it.  pPage: new (sub-)page.  offset: growth in page
				 * size.  NodeData: node data with new page or DB.
				 */
				unsigned iNode;
				pPage = pSubPage = (CMdbPage *)(NodeData.m_pData = (uint8 *)pEnvironment->m_pScratchBuffer);
				pPage->m_PageNumber = _pCursor->m_Pages[_pCursor->m_iTop]->m_PageNumber;

				/* Was a single item before, must convert now */
				if (!DFIsSet(pLeaf->m_Flags, DFDupData))
				{
					FDatabaseCompare *pDataCompare = nullptr;
					/* Just overwrite the current item */
					if (Flags == EDatabaseWriteFlag_OverwriteAtCurrentCursorPos)
						goto current;
					pDataCompare = &(_pCursor->m_pDatabaseInfo->m_fDataCompare);
					if (DNeedCmpClong(*pDataCompare, OldData.m_Size))
						pDataCompare = &g_mdb_cmp_clong;
					/* does data match? */
					if (!(*pDataCompare)(*_pData, OldData))
					{
						if (Flags & (EDatabaseWriteFlag_NoDuplicateData | EDatabaseWriteFlag_AppendDuplicateData))
							return DMdbKeyExist;
						/* overwrite it */
						goto current;
					}

					/* Back up original data item */
					DuplicateKey.m_Size = OldData.m_Size;
					DuplicateKey.m_pData = (uint8 *)memcpy(pSubPage + 1, OldData.m_pData, OldData.m_Size);

					/* Make sub-page header for the dup items, with dummy body */
					pSubPage->m_Flags = DPLeaf | DPDirty | DPSubp;
					pSubPage->m_FreeSpace.m_Lower = (DPageHeaderSize - DPageBase);
					NodeData.m_Size = DPageHeaderSize + DuplicateKey.m_Size + _pData->m_Size;
					if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)
					{
						pSubPage->m_Flags |= DPLeaf2;
						pSubPage->m_Pad = _pData->m_Size;
						NodeData.m_Size += 2 * _pData->m_Size; // leave space for 2 more
					}
					else
						NodeData.m_Size += 2 * (sizeof(CIndx) + DNodeSize) + (DuplicateKey.m_Size & 1) + (_pData->m_Size & 1);
					pSubPage->m_FreeSpace.m_Upper = NodeData.m_Size - DPageBase;
					OldData.m_Size = NodeData.m_Size; // pretend OldData is pSubPage
				}
				else if (pLeaf->m_Flags & DFSubData)
				{
					/* Data is on sub-DB, just store it */
					Flags |= DFDupData | DFSubData;
					goto put_sub;
				}
				else
				{
					/* Data is on sub-page */
					pSubPage = (CMdbPage *)OldData.m_pData;
					switch (Flags)
					{
					default:
						if (!(_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
						{
							Growth = DEven(DNodeSize + sizeof(CIndx) + _pData->m_Size);
							break;
						}
						Growth = pSubPage->m_Pad;
						if (DSizeLeft(pSubPage) < Growth)
						{
							Growth *= 4; // space for 4 more
							break;
						}
						/* FALLTHRU: Big enough EDatabaseSubFlag_SortedDuplicatesFixedSize sub-page */
					case EDatabaseWriteFlag_OverwriteAtCurrentCursorPos:
						pSubPage->m_Flags |= DPDirty;
						DCopyPgno(pSubPage->m_PageNumber, pPage->m_PageNumber);
						_pCursor->m_pXCursor->m_Cursor.m_Pages[0] = pSubPage;
						Flags |= DFDupData;
						goto put_sub;
					}
					NodeData.m_Size = OldData.m_Size + Growth;
				}

				SubPageFlags = pSubPage->m_Flags;
				if (DNodeSize + DNodeKeySize(pLeaf) + NodeData.m_Size > pEnvironment->m_MaxNodeSize)
				{
					/* Too big for a sub-page, convert to sub-DB */
					SubPageFlags &= ~DPSubp;
				prep_subDB:
					if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)
					{
						SubPageFlags |= DPLeaf2;
						DummyDatabase.m_Pad = pSubPage->m_Pad;
						DummyDatabase.m_Flags = EDatabaseSubFlag_SortedDuplicatesFixedSize;
						if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
							DummyDatabase.m_Flags |= EDatabaseSubFlag_IntegerKeys;
					}
					else
					{
						DummyDatabase.m_Pad = 0;
						DummyDatabase.m_Flags = 0;
					}
					DummyDatabase.m_Depth = 1;
					DummyDatabase.m_nBranchPages = 0;
					DummyDatabase.m_nLeafPages = 1;
					DummyDatabase.m_nOverflowPages = 0;
					DummyDatabase.m_nEntries = DNumKeys(pSubPage);
					NodeData.m_Size = sizeof(CMdbDb);
					NodeData.m_pData = (uint8 *)&DummyDatabase;
					if ((Result = fg_PageAlloc(_pCursor, 1, &pPage)))
						return Result;
					Growth = pEnvironment->m_PageSize - OldData.m_Size;
					Flags |= DFDupData | DFSubData;
					DummyDatabase.m_RootPage = pPage->m_PageNumber;
					pSubRoot = pPage;
				}
				if (pPage != pSubPage)
				{
					pPage->m_Flags = SubPageFlags | DPDirty;
					pPage->m_Pad = pSubPage->m_Pad;
					pPage->m_FreeSpace.m_Lower = pSubPage->m_FreeSpace.m_Lower;
					pPage->m_FreeSpace.m_Upper = pSubPage->m_FreeSpace.m_Upper + Growth;
					if (SubPageFlags & DPLeaf2)
						memcpy(DMetaData(pPage), DMetaData(pSubPage), DNumKeys(pSubPage) * pSubPage->m_Pad);
					else
					{
						memcpy
							(
								(char *)pPage + pPage->m_FreeSpace.m_Upper + DPageBase
								, (char *)pSubPage + pSubPage->m_FreeSpace.m_Upper + DPageBase
								, OldData.m_Size - pSubPage->m_FreeSpace.m_Upper - DPageBase
							)
						;
						memcpy((char *)(&pPage->m_NodeOffsets), (char *)(&pSubPage->m_NodeOffsets), DNumKeys(pSubPage) * sizeof(pPage->m_NodeOffsets[0]));
						for (iNode = 0; iNode < DNumKeys(pSubPage); iNode++)
							pPage->m_NodeOffsets[iNode] += Growth;
					}
				}

				pNodeData = &NodeData;
				Flags |= DFDupData;
				bDoSub = 1;
				if (!bInsertKey)
					fg_NodeDel(_pCursor, 0);
				goto new_sub;
			}
		current:
			/* LMDB passes DFSubData in 'flags' to write a DB record */
			if ((pLeaf->m_Flags ^ Flags) & DFSubData)
				return DMdbIncompatible;
			/* overflow page overwrites need special handling */
			if (DFIsSet(pLeaf->m_Flags, DFBigData))
			{
				CMdbPage *pOverflowPage;
				CPgno PageNumber;
				int Level, nOverflowPages, nDataPages = DOverflowPages(_pData->m_Size, pEnvironment->m_PageSize);

				memcpy(&PageNumber, OldData.m_pData, sizeof(PageNumber));
				if ((Result2 = fg_PageGet(_pCursor, PageNumber, &pOverflowPage, &Level)) != 0)
					return Result2;
				nOverflowPages = pOverflowPage->m_nOverflowPages;

				/* Is the ov page large enough? */
				if (nOverflowPages >= nDataPages)
				{
					if (!(pOverflowPage->m_Flags & DPDirty) && (Level || (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMap)))
					{
						Result = fg_PageUnspill(_pCursor->m_pTransaction, pOverflowPage, &pOverflowPage);
						if (Result)
							return Result;
						Level = 0; // dirty in this txn or clean
					}
					/* Is it dirty? */
					if (pOverflowPage->m_Flags & DPDirty)
					{
						/* yes, overwrite it. Note in this case we don't
						 * bother to try shrinking the page if the new data
						 * is smaller than the overflow threshold.
						 */
						if (Level > 1)
						{
							/* It is writable only in a parent txn */
							umint Size = (umint)pEnvironment->m_PageSize * nOverflowPages, CopyOffset;
							CMdbPage *pNewPage = fg_PageMalloc(_pCursor->m_pTransaction, nOverflowPages);
							CMdbId2 DirtyEntry;
							DirtyEntry.m_ID = PageNumber;
							DirtyEntry.m_pValue = pNewPage;
							/* Note - this page is already counted in parent's dirty_room */
							Result2 = fg_Mid2lInsert(_pCursor->m_pTransaction->m_pDirtyList, &DirtyEntry);
							DMibCheck(Result2 == 0);
							/* Currently we make the page look as with put() in the
							 * parent txn, in case the user peeks at EDatabaseWriteFlag_ReserveSpaced
							 * or unused parts. Some users treat nOverflowPages specially.
							 */
							if (!(Flags & EDatabaseWriteFlag_ReserveSpace))
							{
								/* Skip the part where LMDB will put *data.
								 * Copy end of page, adjusting alignment so
								 * compiler may copy words instead of bytes.
								 */
								CopyOffset = (DPageHeaderSize + _pData->m_Size) & -sizeof(umint);
								memcpy((umint *)((char *)pNewPage + CopyOffset), (umint *)((char *)pOverflowPage + CopyOffset), Size - CopyOffset);
								Size = DPageHeaderSize;
							}
							memcpy(pNewPage, pOverflowPage, Size); // Copy beginning of page
							pOverflowPage = pNewPage;
						}
						DSetDataSize(pLeaf, _pData->m_Size);
						if (DFIsSet(Flags, EDatabaseWriteFlag_ReserveSpace))
							_pData->m_pData = DMetaData(pOverflowPage);
						else
							memcpy(DMetaData(pOverflowPage), _pData->m_pData, _pData->m_Size);
						return DMdbSuccess;
					}
				}
				if ((Result2 = fg_OvPageFree(_pCursor, pOverflowPage)) != DMdbSuccess)
					return Result2;
			}
			else if (_pData->m_Size == OldData.m_Size)
			{
				/* same size, just replace it. Note that we could
				 * also reuse this node if the new data is smaller,
				 * but instead we opt to shrink the node in that case.
				 */
				if (DFIsSet(Flags, EDatabaseWriteFlag_ReserveSpace))
					_pData->m_pData = OldData.m_pData;
				else if (!(_pCursor->m_Flags & DCSub))
					memcpy((ch8 *)OldData.m_pData, _pData->m_pData, _pData->m_Size);
				else
				{
					memcpy(DNodeKey(pLeaf), _pKey->m_pData, _pKey->m_Size);
					goto fix_parent;
				}
				return DMdbSuccess;
			}
			fg_NodeDel(_pCursor, 0);
		}

		pNodeData = _pData;

	new_sub:
		NodeFlags = Flags & DNodeAddFlags;
		NodeSize = DIsLeaf2(_pCursor->m_Pages[_pCursor->m_iTop]) ? _pKey->m_Size : pEnvironment->f_LeafSize(_pKey, pNodeData);
		if (DSizeLeft(_pCursor->m_Pages[_pCursor->m_iTop]) < NodeSize)
		{
			if ((Flags & (DFDupData | DFSubData)) == DFDupData)
				NodeFlags &= ~EDatabaseWriteFlag_Append; // sub-page may need room to grow
			if (!bInsertKey)
				NodeFlags |= DMdbSplitReplace;
			Result = fg_PageSplit(_pCursor, _pKey, pNodeData, DPInvalid, NodeFlags);
		}
		else
		{
			/* There is room already in this leaf page. */
			Result = fg_NodeAdd(_pCursor, _pCursor->m_NodeIndices[_pCursor->m_iTop], _pKey, pNodeData, 0, NodeFlags);
			if (Result == 0)
			{
				/* Adjust other cursors pointing to pPage */
				CMdbCursor *pOther, *pOtherCursor;
				CDatabaseSubID DatabaseID = _pCursor->m_DatabaseID;
				unsigned iTop = _pCursor->m_iTop;
				CMdbPage *pPage = _pCursor->m_Pages[iTop];

				for (pOther = _pCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
				{
					if (_pCursor->m_Flags & DCSub)
						pOtherCursor = &pOther->m_pXCursor->m_Cursor;
					else
						pOtherCursor = pOther;
					if (pOtherCursor == _pCursor || pOtherCursor->m_nPages < _pCursor->m_nPages || pOtherCursor->m_Pages[iTop] != pPage)
						continue;
					if (pOtherCursor->m_NodeIndices[iTop] >= _pCursor->m_NodeIndices[iTop] && bInsertKey)
						pOtherCursor->m_NodeIndices[iTop]++;
					DXCursorRefresh(pOtherCursor, iTop, pPage);
				}
			}
		}

		if (Result == DMdbSuccess)
		{
			/* Now store the actual data in the child DB. Note that we're
			 * storing the user data in the keys field, so there are strict
			 * size limits on dupdata. The actual data fields of the child
			 * DB are all zero size.
			 */
			if (bDoSub)
			{
				int SubWriteFlags, bNewDuplicateData;
				CDatabaseOffset nOldEntries;
			put_sub:
				NodeData.m_Size = 0;
				NodeData.m_pData = (uint8 *)"";
				pLeaf = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);
				if (Flags == EDatabaseWriteFlag_OverwriteAtCurrentCursorPos)
					SubWriteFlags = EDatabaseWriteFlag_OverwriteAtCurrentCursorPos | DMdbNoSpill;
				else
				{
					fg_XCursorInit1(_pCursor, pLeaf);
					SubWriteFlags = (Flags & EDatabaseWriteFlag_NoDuplicateData) ? EDatabaseWriteFlag_NoOverwrite | DMdbNoSpill : DMdbNoSpill;
				}
				if (pSubRoot)
					_pCursor->m_pXCursor->m_Cursor.m_Pages[0] = pSubRoot;
				bNewDuplicateData = (int)DuplicateKey.m_Size;
				/* converted, write the original data first */
				if (DuplicateKey.m_Size)
				{
					Result = fg_CursorPut(&_pCursor->m_pXCursor->m_Cursor, &DuplicateKey, &NodeData, (EDatabaseWriteFlag)SubWriteFlags);
					if (Result)
						goto bad_sub;
					/* we've done our job */
					DuplicateKey.m_Size = 0;
				}
				if (!(pLeaf->m_Flags & DFSubData) || pSubRoot)
				{
					/* Adjust other cursors pointing to pPage */
					CMdbCursor *pOther;
					CMdbXCursor *pXCursor = _pCursor->m_pXCursor;
					unsigned iTop = _pCursor->m_iTop;
					CMdbPage *pPage = _pCursor->m_Pages[iTop];

					for (pOther = _pCursor->m_pTransaction->m_pCursors[_pCursor->m_DatabaseID]; pOther; pOther = pOther->m_pNext)
					{
						if (pOther == _pCursor || pOther->m_nPages < _pCursor->m_nPages)
							continue;
						if (!(pOther->m_Flags & DCInitialized))
							continue;
						if (pOther->m_Pages[iTop] == pPage)
						{
							if (pOther->m_NodeIndices[iTop] == _pCursor->m_NodeIndices[iTop])
								fg_XCursorInit2(pOther, pXCursor, bNewDuplicateData);
							else if (!bInsertKey)
								DXCursorRefresh(pOther, iTop, pPage);
						}
					}
				}
				nOldEntries = _pCursor->m_pXCursor->m_Database.m_nEntries;
				if (Flags & EDatabaseWriteFlag_AppendDuplicateData)
					SubWriteFlags |= EDatabaseWriteFlag_Append;
				Result = fg_CursorPut(&_pCursor->m_pXCursor->m_Cursor, _pData, &NodeData, (EDatabaseWriteFlag)SubWriteFlags);
				if (Flags & DFSubData)
				{
					void *pDatabase = DNodeData(pLeaf);
					memcpy(pDatabase, &_pCursor->m_pXCursor->m_Database, sizeof(CMdbDb));
				}
				bInsertData = _pCursor->m_pXCursor->m_Database.m_nEntries - nOldEntries;
			}
			/* Increment count unless we just replaced an existing item. */
			if (bInsertData)
				_pCursor->m_pDatabase->m_nEntries++;
			if (bInsertKey)
			{
				/* Invalidate txn if we created an empty sub-DB */
				if (Result)
					goto bad_sub;
				/* If we succeeded and the key didn't exist before,
				 * make sure the cursor is marked valid.
				 */
				_pCursor->m_Flags |= DCInitialized;
			}
			if (Flags & EDatabaseWriteFlag_BatchDuplicateData)
			{
				if (!Result)
				{
					nPutItems++;
					/* let caller know how many succeeded, if any */
					_pData[1].m_Size = nPutItems;
					if (nPutItems < nDataItems)
					{
						_pData[0].m_pData = (uint8 *)_pData[0].m_pData + _pData[0].m_Size;
						bInsertKey = bInsertData = 0;
						goto more;
					}
				}
			}
			return Result;
		bad_sub:
			if (Result == DMdbKeyExist) // should not happen, we deleted that item
				Result = DMdbProblem;
		}
		_pCursor->m_pTransaction->m_Flags |= DMdbTxnError;
		return Result;
	}

	/** @brief Delete current key/data pair
	 *
	 * This function deletes the key/data pair to which the cursor refers.
	 * This does not invalidate the cursor, so operations such as EDatabaseCursorGet_Next
	 * can still be used on it.
	 * Both EDatabaseCursorGet_Next and EDatabaseCursorGet_GetCurrent will return the same record after
	 * this operation.
	 * @param[in] _pCursor A cursor handle returned by #f_Cursor()
	 * @param[in] flags Options for this operation. This parameter
	 * must be set to 0 or one of the values described here.
	 * <ul>
	 *	<li>#EDatabaseWriteFlag_NoDuplicateData - delete all of the data items for the current key.
	 *		This flag may only be specified if the database was opened with #EDatabaseSubFlag_SortedDuplicates.
	 * </ul>
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EACCES - an attempt was made to write in a read-only transaction.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	int fg_CursorDel(CMdbCursor *_pCursor, EDatabaseWriteFlag _Flags)
	{
		uint32 Flags = _Flags;
		using namespace NPrivate;

		CMdbNode *pLeaf;
		CMdbPage *pPage;
		int Result;

		_pCursor->m_pTransaction->f_CheckBlocked();

		if (_pCursor->m_pTransaction->m_Flags & (DMdbTxnReadOnly))
			return EACCES;

		if (!(_pCursor->m_Flags & DCInitialized))
			return EINVAL;

		if (_pCursor->m_NodeIndices[_pCursor->m_iTop] >= DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop]))
			return DMdbNotFound;

		if (!(Flags & DMdbNoSpill) && (Result = fg_PageSpill(_pCursor, nullptr, nullptr)))
			return Result;

		Result = fg_CursorTouch(_pCursor);
		if (Result)
			return Result;

		pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		if (DIsLeaf2(pPage))
			goto del_key;
		pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);

		if (DFIsSet(pLeaf->m_Flags, DFDupData))
		{
			if (Flags & EDatabaseWriteFlag_NoDuplicateData)
			{
				/* fg_CursorDel0() will subtract the final entry */
				_pCursor->m_pDatabase->m_nEntries -= _pCursor->m_pXCursor->m_Database.m_nEntries - 1;
				_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~DCInitialized;
			}
			else
			{
				if (!DFIsSet(pLeaf->m_Flags, DFSubData))
					_pCursor->m_pXCursor->m_Cursor.m_Pages[0] = (CMdbPage *)DNodeData(pLeaf);
				Result = fg_CursorDel(&_pCursor->m_pXCursor->m_Cursor, (EDatabaseWriteFlag)DMdbNoSpill);
				if (Result)
					return Result;
				/* If sub-DB still has entries, we're done */
				if (_pCursor->m_pXCursor->m_Database.m_nEntries)
				{
					if (pLeaf->m_Flags & DFSubData)
					{
						/* update subDB info */
						void *pDatabase = (CMdbPage *)DNodeData(pLeaf);
						memcpy(pDatabase, &_pCursor->m_pXCursor->m_Database, sizeof(CMdbDb));
					}
					else
					{
						CMdbCursor *pOther;
						/* shrink fake page */
						fg_NodeShrink(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
						pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
						_pCursor->m_pXCursor->m_Cursor.m_Pages[0] = (CMdbPage *)DNodeData(pLeaf);
						/* fix other sub-DB cursors pointed at fake pages on this page */
						for (pOther = _pCursor->m_pTransaction->m_pCursors[_pCursor->m_DatabaseID]; pOther; pOther = pOther->m_pNext)
						{
							if (pOther == _pCursor || pOther->m_nPages < _pCursor->m_nPages)
								continue;
							if (!(pOther->m_Flags & DCInitialized))
								continue;
							if (pOther->m_Pages[_pCursor->m_iTop] == pPage)
								DXCursorRefresh(pOther, _pCursor->m_iTop, pPage);
						}
					}
					_pCursor->m_pDatabase->m_nEntries--;
					return Result;
				}
				else
					_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~DCInitialized;
				/* otherwise fall thru and delete the sub-DB */
			}

			if (pLeaf->m_Flags & DFSubData)
			{
				/* add all the child DB's pages to the free list */
				Result = fg_Drop0(&_pCursor->m_pXCursor->m_Cursor, 0);
				if (Result)
					goto fail;
			}
		}
		/* LMDB passes DFSubData in 'flags' to delete a DB record */
		else if ((pLeaf->m_Flags ^ Flags) & DFSubData)
		{
			Result = DMdbIncompatible;
			goto fail;
		}

		/* add overflow pages to free list */
		if (DFIsSet(pLeaf->m_Flags, DFBigData))
		{
			CMdbPage *pOverflowPage;
			CPgno PageNumber;

			memcpy(&PageNumber, DNodeData(pLeaf), sizeof(PageNumber));
			if ((Result = fg_PageGet(_pCursor, PageNumber, &pOverflowPage, nullptr)) || (Result = fg_OvPageFree(_pCursor, pOverflowPage)))
				goto fail;
		}

	del_key:
		return fg_CursorDel0(_pCursor);

	fail:
		_pCursor->m_pTransaction->m_Flags |= DMdbTxnError;
		return Result;
	}

	/** @brief Return count of duplicates for current key.
	 *
	 * This call is only valid on databases that support sorted duplicate
	 * data items #EDatabaseSubFlag_SortedDuplicates.
	 * @param[in] _pCursor A cursor handle returned by #f_Cursor()
	 * @param[out] o_pCount Address where the count will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - cursor is not initialized, or an invalid parameter was specified.
	 * </ul>
	 */
	int fg_CursorCount(CMdbCursor *_pCursor, CDatabaseOffset *o_pCount)
	{
		using namespace NPrivate;

		// Return the count of duplicate data items for the current key
		CMdbNode *pLeaf;

		if (_pCursor == nullptr || o_pCount == nullptr)
			return EINVAL;

		if (_pCursor->m_pXCursor == nullptr)
			return DMdbIncompatible;

		_pCursor->m_pTransaction->f_CheckBlocked();

		if (!(_pCursor->m_Flags & DCInitialized))
			return EINVAL;

		if (!_pCursor->m_nPages)
			return DMdbNotFound;

		if (_pCursor->m_Flags & DCEof)
		{
			if (_pCursor->m_NodeIndices[_pCursor->m_iTop] >= DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop]))
				return DMdbNotFound;
			_pCursor->m_Flags ^= DCEof;
		}

		pLeaf = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);
		if (!DFIsSet(pLeaf->m_Flags, DFDupData))
			*o_pCount = 1;
		else
		{
			if (!(_pCursor->m_pXCursor->m_Cursor.m_Flags & DCInitialized))
				return EINVAL;

			*o_pCount = _pCursor->m_pXCursor->m_Database.m_nEntries;
		}
		return DMdbSuccess;
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

#ifdef DMibDatabase_Always64BitSupport
	void fg_CursorUnref(CMdbCursor *mc)
	{
		int i;
		if (mc->m_pTransaction->m_pReadOnlyPages[0].m_ID)
		{
			if (!mc->m_nPages || !mc->m_Pages[0] || DIsSubp(mc->m_Pages[0]))
				return;
			for (i = 0; i < mc->m_nPages; i++)
				fg_PageUnref(mc->m_pTransaction, mc->m_Pages[i]);
			if (mc->m_pOverflowPage)
			{
				fg_PageUnref(mc->m_pTransaction, mc->m_pOverflowPage);
				mc->m_pOverflowPage = 0;
			}
		}
		mc->m_nPages = mc->m_iTop = 0;
		mc->m_Pages[0] = nullptr;
		mc->m_Flags &= ~DCInitialized;
	}
#endif /* DMibDatabase_Always64BitSupport */

	/** Back up parent txn's cursors, then grab the originals for tracking */
	void fg_CursorShadow(CMdbTxn *_pSource, CMdbTxn *_pDestination)
	{
		CMdbCursor *pCursor, *pBackup;
		CMdbXCursor *pXCursor;
		umint Size;
		int iDatabase;

		for (iDatabase = _pSource->m_nDatabases; --iDatabase >= 0;)
		{
			if ((pCursor = _pSource->m_pCursors[iDatabase]) != nullptr)
			{
				Size = sizeof(CMdbCursor);
				if (pCursor->m_pXCursor)
					Size += sizeof(CMdbXCursor);
				for (; pCursor; pCursor = pBackup->m_pNext)
				{
					pBackup = (CMdbCursor *)NMemory::fg_Alloc(Size);
					*pBackup = *pCursor;
					pCursor->m_pBackup = pBackup;
					pCursor->m_pDatabase = &_pDestination->m_pDatabases[iDatabase];
					/* Kill pointers into _pSource to reduce abuse: The
					 * user may not use pCursor until _pDestination ends. But we need a valid
					 * txn pointer here for cursor fixups to keep working.
					 */
					pCursor->m_pTransaction = _pDestination;
					pCursor->m_pDatabaseFlag = &_pDestination->m_pDatabaseFlags[iDatabase];
					if ((pXCursor = pCursor->m_pXCursor) != nullptr)
					{
						*(CMdbXCursor *)(pBackup + 1) = *pXCursor;
						pXCursor->m_Cursor.m_pTransaction = _pDestination;
					}
					pCursor->m_pNext = _pDestination->m_pCursors[iDatabase];
					_pDestination->m_pCursors[iDatabase] = pCursor;
				}
			}
		}
	}

	/** Close this write txn's cursors, give parent txn's cursors back to parent.
	 * @param[in] _pTransaction the transaction handle.
	 * @param[in] _bMerge true to keep changes to parent cursors, false to revert.
	 * @return 0 on success, non-zero on failure.
	 */
	void fg_CursorsClose(CMdbTxn *_pTransaction, unsigned _bMerge)
	{
		CMdbCursor **pCursors = _pTransaction->m_pCursors, *pCursor, *pNext, *pBackup;
		CMdbXCursor *pXCursor;
		int iDatabase;

		for (iDatabase = _pTransaction->m_nDatabases; --iDatabase >= 0;)
		{
			for (pCursor = pCursors[iDatabase]; pCursor; pCursor = pNext)
			{
				pNext = pCursor->m_pNext;
				if ((pBackup = pCursor->m_pBackup) != nullptr)
				{
					if (_bMerge)
					{
						/* Commit changes to parent txn */
						pCursor->m_pNext = pBackup->m_pNext;
						pCursor->m_pBackup = pBackup->m_pBackup;
						pCursor->m_pTransaction = pBackup->m_pTransaction;
						pCursor->m_pDatabase = pBackup->m_pDatabase;
						pCursor->m_pDatabaseFlag = pBackup->m_pDatabaseFlag;
						if ((pXCursor = pCursor->m_pXCursor) != nullptr)
							pXCursor->m_Cursor.m_pTransaction = pBackup->m_pTransaction;
					}
					else
					{
						/* Abort nested txn */
						*pCursor = *pBackup;
						if ((pXCursor = pCursor->m_pXCursor) != nullptr)
							*pXCursor = *(CMdbXCursor *)(pBackup + 1);
					}
					pCursor = pBackup;
				}
				/* Only malloced cursors are permanently tracked. */
				umint Size = sizeof(CMdbCursor);
				if (pCursor->m_pXCursor)
					Size += sizeof(CMdbXCursor);

				NMemory::fg_Free(pCursor, Size);
			}
			pCursors[iDatabase] = nullptr;
		}
	}

	/** Pop a page off the top of the cursor's stack. */
	void fg_CursorPop(CMdbCursor *_pCursor)
	{
		if (_pCursor->m_nPages)
		{
			DMibDatabaseTrace("popping page {} off db {} cursor {}", _pCursor->m_Pages[_pCursor->m_iTop]->m_PageNumber, DDbi(_pCursor), (void *)_pCursor);

			_pCursor->m_nPages--;
			if (_pCursor->m_nPages)
				_pCursor->m_iTop--;
			else
				_pCursor->m_Flags &= ~DCInitialized;
		}
	}

	/** Push a page onto the top of the cursor's stack.
	 * Set #DMdbTxnError on failure.
	 */
	int fg_CursorPush(CMdbCursor *_pCursor, CMdbPage *_pPage)
	{
		DMibDatabaseTrace("pushing page {} on db {} cursor {}", _pPage->m_PageNumber, DDbi(_pCursor), (void *)_pCursor);

		if (_pCursor->m_nPages >= DCursorStack)
		{
			_pCursor->m_pTransaction->m_Flags |= DMdbTxnError;
			return DMdbCursorFull;
		}

		_pCursor->m_iTop = _pCursor->m_nPages++;
		_pCursor->m_Pages[_pCursor->m_iTop] = _pPage;
		_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;

		return DMdbSuccess;
	}

	/** Find a sibling for a page.
	 * Replaces the page at the top of the cursor's stack with the
	 * specified sibling, if one exists.
	 * @param[in] _pCursor The cursor for this operation.
	 * @param[in] _bMoveRight Non-zero if the right sibling is requested,
	 * otherwise the left sibling.
	 * @return 0 on success, non-zero on failure.
	 */
	int fg_CursorSibling(CMdbCursor *_pCursor, int _bMoveRight)
	{
		int Result;
		CMdbNode *pNode;
		CMdbPage *pPage;
#ifdef DMibDatabase_Always64BitSupport
		CMdbPage *op;
#endif

		if (_pCursor->m_nPages < 2)
			return DMdbNotFound; // root has no siblings

#ifdef DMibDatabase_Always64BitSupport
		op = _pCursor->m_Pages[_pCursor->m_iTop];
#endif
		fg_CursorPop(_pCursor);
		DMibDatabaseTrace("parent page is page {}, index {}", _pCursor->m_Pages[_pCursor->m_iTop]->m_PageNumber, _pCursor->m_NodeIndices[_pCursor->m_iTop]);

		if (_bMoveRight ? (_pCursor->m_NodeIndices[_pCursor->m_iTop] + 1u >= DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop])) : (_pCursor->m_NodeIndices[_pCursor->m_iTop] == 0))
		{
			DMibDatabaseTrace("no more keys left, moving to {} sibling", _bMoveRight ? "right" : "left");
			if ((Result = fg_CursorSibling(_pCursor, _bMoveRight)) != DMdbSuccess)
			{
				/* undo cursor_pop before returning */
				_pCursor->m_iTop++;
				_pCursor->m_nPages++;
				return Result;
			}
		}
		else
		{
			if (_bMoveRight)
				_pCursor->m_NodeIndices[_pCursor->m_iTop]++;
			else
				_pCursor->m_NodeIndices[_pCursor->m_iTop]--;
			DMibDatabaseTrace("just moving to {} index key {}", _bMoveRight ? "right" : "left", _pCursor->m_NodeIndices[_pCursor->m_iTop]);
		}
		DMibCheck(DIsBranch(_pCursor->m_Pages[_pCursor->m_iTop]));

		DMdbPageUnref(_pCursor->m_pTransaction, op);

		pNode = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);
		if ((Result = fg_PageGet(_pCursor, DNodePgno(pNode), &pPage, nullptr)) != 0)
		{
			/* _pCursor will be inconsistent if caller does m_nPages++ as above */
			_pCursor->m_Flags &= ~(DCInitialized | DCEof);
			return Result;
		}

		fg_CursorPush(_pCursor, pPage);
		if (!_bMoveRight)
			_pCursor->m_NodeIndices[_pCursor->m_iTop] = DNumKeys(pPage) - 1;

		return DMdbSuccess;
	}

	/** Move the cursor to the next data item. */
	int fg_CursorNext(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation)
	{
		DMibFastCheck(_pKey);
		CMdbPage *pPage;
		CMdbNode *pLeaf;
		int Result;

		if ((_pCursor->m_Flags & DCDel && _Operation == EDatabaseCursorGet_NextDup))
			return DMdbNotFound;

		if (!(_pCursor->m_Flags & DCInitialized))
			return fg_CursorFirst(_pCursor, _pKey, _pData);

		pPage = _pCursor->m_Pages[_pCursor->m_iTop];

		if (_pCursor->m_Flags & DCEof)
		{
			if (_pCursor->m_NodeIndices[_pCursor->m_iTop] >= DNumKeys(pPage) - 1)
				return DMdbNotFound;
			_pCursor->m_Flags ^= DCEof;
		}

		if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates)
		{
			pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
			if (DFIsSet(pLeaf->m_Flags, DFDupData))
			{
				if (_Operation == EDatabaseCursorGet_Next || _Operation == EDatabaseCursorGet_NextDup)
				{
					Result = fg_CursorNext(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr, EDatabaseCursorGet_Next);
					if (_Operation != EDatabaseCursorGet_Next || Result != DMdbNotFound)
					{
						if (Result == DMdbSuccess)
							DMdbGetKey(pLeaf, _pKey);
						return Result;
					}
				}
				else
					DMdbCursorUnref(&_pCursor->m_pXCursor->m_Cursor, 0);
			}
			else
			{
				_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~(DCInitialized | DCEof);
				if (_Operation == EDatabaseCursorGet_NextDup)
					return DMdbNotFound;
			}
		}

		DMibDatabaseTrace("cursor_next: top page is {} in cursor {}", fg_DbgPgno(pPage), (void *)_pCursor);
		if (_pCursor->m_Flags & DCDel)
		{
			_pCursor->m_Flags ^= DCDel;
			goto skip;
		}

		if (_pCursor->m_NodeIndices[_pCursor->m_iTop] + 1u >= DNumKeys(pPage))
		{
			DPuts("=====> move to next sibling page");
			if ((Result = fg_CursorSibling(_pCursor, 1)) != DMdbSuccess)
			{
				_pCursor->m_Flags |= DCEof;
				return Result;
			}
			pPage = _pCursor->m_Pages[_pCursor->m_iTop];
			DMibDatabaseTrace("next page is {}, key index {}", pPage->m_PageNumber, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
		}
		else
			_pCursor->m_NodeIndices[_pCursor->m_iTop]++;

	skip:
		DMibDatabaseTrace("==> cursor points to page {} with {} keys, key index {}", fg_DbgPgno(pPage), DNumKeys(pPage), _pCursor->m_NodeIndices[_pCursor->m_iTop]);

		if (DIsLeaf2(pPage))
		{
			_pKey->m_Size = _pCursor->m_pDatabase->m_Pad;
			_pKey->m_pData = DLeaf2Key(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop], _pKey->m_Size);
			return DMdbSuccess;
		}

		DMibCheck(DIsLeaf(pPage));
		pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);

		if (DFIsSet(pLeaf->m_Flags, DFDupData))
			fg_XCursorInit1(_pCursor, pLeaf);
		if (_pData)
		{
			if ((Result = fg_NodeRead(_pCursor, pLeaf, _pData)) != DMdbSuccess)
				return Result;

			if (DFIsSet(pLeaf->m_Flags, DFDupData))
			{
				Result = fg_CursorFirst(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr);
				if (Result != DMdbSuccess)
					return Result;
			}
		}

		DMdbGetKey(pLeaf, _pKey);
		return DMdbSuccess;
	}

	/** Move the cursor to the previous data item. */
	int fg_CursorPrev(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation)
	{
		DMibFastCheck(_pKey);

		CMdbPage *pPage;
		CMdbNode *pLeaf;
		int Result;

		if (!(_pCursor->m_Flags & DCInitialized))
		{
			Result = fg_CursorLast(_pCursor, _pKey, _pData);
			if (Result)
				return Result;
			_pCursor->m_NodeIndices[_pCursor->m_iTop]++;
		}

		pPage = _pCursor->m_Pages[_pCursor->m_iTop];

		if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates)
		{
			pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
			if (DFIsSet(pLeaf->m_Flags, DFDupData))
			{
				if (_Operation == EDatabaseCursorGet_Prev || _Operation == EDatabaseCursorGet_PrevDup)
				{
					Result = fg_CursorPrev(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr, EDatabaseCursorGet_Prev);
					if (_Operation != EDatabaseCursorGet_Prev || Result != DMdbNotFound)
					{
						if (Result == DMdbSuccess)
						{
							DMdbGetKey(pLeaf, _pKey);
							_pCursor->m_Flags &= ~DCEof;
						}
						return Result;
					}
				}
				else
					DMdbCursorUnref(&_pCursor->m_pXCursor->m_Cursor, 0);
			}
			else
			{
				_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~(DCInitialized | DCEof);
				if (_Operation == EDatabaseCursorGet_PrevDup)
					return DMdbNotFound;
			}
		}

		DMibDatabaseTrace("cursor_prev: top page is {} in cursor {}", fg_DbgPgno(pPage), (void *)_pCursor);

		_pCursor->m_Flags &= ~(DCEof | DCDel);

		if (_pCursor->m_NodeIndices[_pCursor->m_iTop] == 0)
		{
			DPuts("=====> move to prev sibling page");
			if ((Result = fg_CursorSibling(_pCursor, 0)) != DMdbSuccess)
				return Result;
			pPage = _pCursor->m_Pages[_pCursor->m_iTop];
			_pCursor->m_NodeIndices[_pCursor->m_iTop] = DNumKeys(pPage) - 1;
			DMibDatabaseTrace("prev page is {}, key index {}", pPage->m_PageNumber, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
		}
		else
			_pCursor->m_NodeIndices[_pCursor->m_iTop]--;

		DMibDatabaseTrace("==> cursor points to page {} with {} keys, key index {}", fg_DbgPgno(pPage), DNumKeys(pPage), _pCursor->m_NodeIndices[_pCursor->m_iTop]);

		if (DIsLeaf2(pPage))
		{
			_pKey->m_Size = _pCursor->m_pDatabase->m_Pad;
			_pKey->m_pData = DLeaf2Key(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop], _pKey->m_Size);
			return DMdbSuccess;
		}

		DMibCheck(DIsLeaf(pPage));
		pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);

		if (DFIsSet(pLeaf->m_Flags, DFDupData))
			fg_XCursorInit1(_pCursor, pLeaf);

		if (_pData)
		{
			if ((Result = fg_NodeRead(_pCursor, pLeaf, _pData)) != DMdbSuccess)
				return Result;

			if (DFIsSet(pLeaf->m_Flags, DFDupData))
			{
				Result = fg_CursorLast(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr);
				if (Result != DMdbSuccess)
					return Result;
			}
		}

		DMdbGetKey(pLeaf, _pKey);
		return DMdbSuccess;
	}

	/** Set the cursor on a specific data item. */
	int fg_CursorSet(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation, int *o_pExact)
	{
		int Result;
		CMdbPage *pPage;
		CMdbNode *pLeaf = nullptr;
		DKBuf;

		if (_pKey->m_Size == 0)
			return DMdbBadValsize;

		if (_pCursor->m_pXCursor)
		{
			DMdbCursorUnref(&_pCursor->m_pXCursor->m_Cursor, 0);
			_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~(DCInitialized | DCEof);
		}

		/* See if we're already on the right page */
		if (_pCursor->m_Flags & DCInitialized)
		{
			CDatabaseValue NodeKey;

			pPage = _pCursor->m_Pages[_pCursor->m_iTop];
			if (!DNumKeys(pPage))
			{
				_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;
				return DMdbNotFound;
			}
			if (pPage->m_Flags & DPLeaf2)
			{
				NodeKey.m_Size = _pCursor->m_pDatabase->m_Pad;
				NodeKey.m_pData = DLeaf2Key(pPage, 0, NodeKey.m_Size);
			}
			else
			{
				pLeaf = DNodePtr(pPage, 0);
				DMdbGetKey2(pLeaf, NodeKey);
			}
			Result = _pCursor->m_pDatabaseInfo->m_fKeyCompare(*_pKey, NodeKey);
			if (Result == 0)
			{
				/* Probably happens rarely, but first node on the page
				 * was the one we wanted.
				 */
				_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;
				if (o_pExact)
					*o_pExact = 1;
				goto set1;
			}
			if (Result > 0)
			{
				uint32 iLevel;
				uint32 nKeys = DNumKeys(pPage);
				if (nKeys > 1)
				{
					if (pPage->m_Flags & DPLeaf2)
						NodeKey.m_pData = DLeaf2Key(pPage, nKeys - 1, NodeKey.m_Size);
					else
					{
						pLeaf = DNodePtr(pPage, nKeys - 1);
						DMdbGetKey2(pLeaf, NodeKey);
					}
					Result = _pCursor->m_pDatabaseInfo->m_fKeyCompare(*_pKey, NodeKey);
					if (Result == 0)
					{
						/* last node was the one we wanted */
						_pCursor->m_NodeIndices[_pCursor->m_iTop] = nKeys - 1;
						if (o_pExact)
							*o_pExact = 1;
						goto set1;
					}
					if (Result < 0)
					{
						if (_pCursor->m_NodeIndices[_pCursor->m_iTop] < DNumKeys(pPage))
						{
							/* This is definitely the right page, skip search_page */
							if (pPage->m_Flags & DPLeaf2)
								NodeKey.m_pData = DLeaf2Key(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop], NodeKey.m_Size);
							else
							{
								pLeaf = DNodePtr(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop]);
								DMdbGetKey2(pLeaf, NodeKey);
							}
							Result = _pCursor->m_pDatabaseInfo->m_fKeyCompare(*_pKey, NodeKey);
							if (Result == 0)
							{
								/* current node was the one we wanted */
								if (o_pExact)
									*o_pExact = 1;
								goto set1;
							}
						}
						Result = 0;
						_pCursor->m_Flags &= ~DCEof;
						goto set2;
					}
				}
				/* If any parents have right-sibs, search.
				 * Otherwise, there's nothing further.
				 */
				for (iLevel = 0; iLevel < _pCursor->m_iTop; iLevel++)
				{
					if (_pCursor->m_NodeIndices[iLevel] < DNumKeys(_pCursor->m_Pages[iLevel]) - 1)
						break;
				}
				if (iLevel == _pCursor->m_iTop)
				{
					/* There are no other pages */
					_pCursor->m_NodeIndices[_pCursor->m_iTop] = nKeys;
					return DMdbNotFound;
				}
			}
			if (!_pCursor->m_iTop)
			{
				/* There are no other pages */
				_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;
				if (_Operation == EDatabaseCursorGet_SetRange && !o_pExact)
				{
					Result = 0;
					goto set1;
				}
				else
					return DMdbNotFound;
			}
		}
		else
			_pCursor->m_Pages[0] = 0;

		Result = fg_PageSearch(_pCursor, _pKey, 0);
		if (Result != DMdbSuccess)
			return Result;

		pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		DMibCheck(DIsLeaf(pPage));

	set2:
		pLeaf = fg_NodeSearch(_pCursor, _pKey, o_pExact);
		if (o_pExact != nullptr && !*o_pExact)
		{
			/* EDatabaseCursorGet_Set specified and not an exact match. */
			return DMdbNotFound;
		}

		if (pLeaf == nullptr)
		{
			DPuts("===> inexact leaf not found, goto sibling");
			if ((Result = fg_CursorSibling(_pCursor, 1)) != DMdbSuccess)
			{
				_pCursor->m_Flags |= DCEof;
				return Result; // no entries matched
			}
			pPage = _pCursor->m_Pages[_pCursor->m_iTop];
			DMibCheck(DIsLeaf(pPage));
			pLeaf = DNodePtr(pPage, 0);
		}

	set1:
		_pCursor->m_Flags |= DCInitialized;
		_pCursor->m_Flags &= ~DCEof;

		if (DIsLeaf2(pPage))
		{
			if (_Operation == EDatabaseCursorGet_SetRange || _Operation == EDatabaseCursorGet_SetKey)
			{
				_pKey->m_Size = _pCursor->m_pDatabase->m_Pad;
				_pKey->m_pData = DLeaf2Key(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop], _pKey->m_Size);
			}
			return DMdbSuccess;
		}

		DMibFastCheck(pLeaf);
		if (DFIsSet(pLeaf->m_Flags, DFDupData))
			fg_XCursorInit1(_pCursor, pLeaf);
		if (_pData)
		{
			if (DFIsSet(pLeaf->m_Flags, DFDupData))
			{
				if (_Operation == EDatabaseCursorGet_Set || _Operation == EDatabaseCursorGet_SetKey || _Operation == EDatabaseCursorGet_SetRange)
					Result = fg_CursorFirst(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr);
				else
				{
					int SubExact, *pSubExact;
					if (_Operation == EDatabaseCursorGet_GetBoth)
					{
						pSubExact = &SubExact;
						SubExact = 0;
					}
					else
						pSubExact = nullptr;
					Result = fg_CursorSet(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr, EDatabaseCursorGet_SetRange, pSubExact);
					if (Result != DMdbSuccess)
						return Result;
				}
			}
			else if (_Operation == EDatabaseCursorGet_GetBoth || _Operation == EDatabaseCursorGet_GetBothRange)
			{
				CDatabaseValue OldData;
				FDatabaseCompare *pDataCompare = nullptr;
				if ((Result = fg_NodeRead(_pCursor, pLeaf, &OldData)) != DMdbSuccess)
					return Result;
				pDataCompare = &(_pCursor->m_pDatabaseInfo->m_fDataCompare);
				if (DNeedCmpClong(*pDataCompare, OldData.m_Size))
					pDataCompare = &g_mdb_cmp_clong;
				Result = (*pDataCompare)(*_pData, OldData);
				if (Result)
				{
					if (_Operation == EDatabaseCursorGet_GetBoth || Result > 0)
						return DMdbNotFound;
					Result = 0;
				}
				*_pData = OldData;
			}
			else
			{
				if (_pCursor->m_pXCursor)
					_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~(DCInitialized | DCEof);
				if ((Result = fg_NodeRead(_pCursor, pLeaf, _pData)) != DMdbSuccess)
					return Result;
			}
		}

		/* The key already matches in all other cases */
		if (_Operation == EDatabaseCursorGet_SetRange || _Operation == EDatabaseCursorGet_SetKey)
			DMdbGetKey(pLeaf, _pKey);
		DMibDatabaseTrace("==> cursor placed on key [{}]", DKey(_pKey));

		return Result;
	}

	/** Move the cursor to the first item in the database. */
	int fg_CursorFirst(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData)
	{
		int Result;
		CMdbNode *pLeaf;

		if (_pCursor->m_pXCursor)
		{
			DMdbCursorUnref(&_pCursor->m_pXCursor->m_Cursor, 0);
			_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~(DCInitialized | DCEof);
		}

		if (!(_pCursor->m_Flags & DCInitialized) || _pCursor->m_iTop)
		{
			Result = fg_PageSearch(_pCursor, nullptr, DMdbPsFirst);
			if (Result != DMdbSuccess)
				return Result;
		}
		DMibCheck(DIsLeaf(_pCursor->m_Pages[_pCursor->m_iTop]));

		pLeaf = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], 0);
		_pCursor->m_Flags |= DCInitialized;
		_pCursor->m_Flags &= ~DCEof;

		_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;

		if (DIsLeaf2(_pCursor->m_Pages[_pCursor->m_iTop]))
		{
			_pKey->m_Size = _pCursor->m_pDatabase->m_Pad;
			_pKey->m_pData = DLeaf2Key(_pCursor->m_Pages[_pCursor->m_iTop], 0, _pKey->m_Size);
			return DMdbSuccess;
		}

		if (_pData)
		{
			if (DFIsSet(pLeaf->m_Flags, DFDupData))
			{
				fg_XCursorInit1(_pCursor, pLeaf);
				Result = fg_CursorFirst(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr);
				if (Result)
					return Result;
			}
			else
			{
				if ((Result = fg_NodeRead(_pCursor, pLeaf, _pData)) != DMdbSuccess)
					return Result;
			}
		}
		DMdbGetKey(pLeaf, _pKey);
		return DMdbSuccess;
	}

	/** Move the cursor to the last item in the database. */
	int fg_CursorLast(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData)
	{
		int Result;
		CMdbNode *pLeaf;

		if (_pCursor->m_pXCursor)
		{
			DMdbCursorUnref(&_pCursor->m_pXCursor->m_Cursor, 0);
			_pCursor->m_pXCursor->m_Cursor.m_Flags &= ~(DCInitialized | DCEof);
		}

		if (!(_pCursor->m_Flags & DCInitialized) || _pCursor->m_iTop)
		{
			Result = fg_PageSearch(_pCursor, nullptr, DMdbPsLast);
			if (Result != DMdbSuccess)
				return Result;
		}
		DMibCheck(DIsLeaf(_pCursor->m_Pages[_pCursor->m_iTop]));

		_pCursor->m_NodeIndices[_pCursor->m_iTop] = DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop]) - 1;
		_pCursor->m_Flags |= DCInitialized | DCEof;
		pLeaf = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);

		if (DIsLeaf2(_pCursor->m_Pages[_pCursor->m_iTop]))
		{
			_pKey->m_Size = _pCursor->m_pDatabase->m_Pad;
			_pKey->m_pData = DLeaf2Key(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop], _pKey->m_Size);
			return DMdbSuccess;
		}

		if (_pData)
		{
			if (DFIsSet(pLeaf->m_Flags, DFDupData))
			{
				fg_XCursorInit1(_pCursor, pLeaf);
				Result = fg_CursorLast(&_pCursor->m_pXCursor->m_Cursor, _pData, nullptr);
				if (Result)
					return Result;
			}
			else
			{
				if ((Result = fg_NodeRead(_pCursor, pLeaf, _pData)) != DMdbSuccess)
					return Result;
			}
		}

		DMdbGetKey(pLeaf, _pKey);
		return DMdbSuccess;
	}

	/** Touch all the pages in the cursor stack. Set m_iTop.
	 *	Makes sure all the pages are writable, before attempting a write operation.
	 * @param[in] _pCursor The cursor to operate on.
	 */
	int fg_CursorTouch(CMdbCursor *_pCursor)
	{
		int Result = DMdbSuccess;

		if (_pCursor->m_DatabaseID >= DCoreDbs && !(*_pCursor->m_pDatabaseFlag & (DDbDirty | DDbDupData)))
		{
			/* Touch DB record of named DB */
			CMdbCursor MainCursor;
			CMdbXCursor MainXCursor;
			if (DTxnDbiChanged(_pCursor->m_pTransaction, _pCursor->m_DatabaseID))
				DMibError("Stale write transaction");
			fg_CursorInit(&MainCursor, _pCursor->m_pTransaction, DMainDbi, &MainXCursor);
			Result = fg_PageSearch(&MainCursor, &_pCursor->m_pDatabaseInfo->m_DatabaseName, DMdbPsModify);
			if (Result)
				return Result;
			*_pCursor->m_pDatabaseFlag |= DDbDirty;
		}
		_pCursor->m_iTop = 0;
		if (_pCursor->m_nPages)
		{
			do
			{
				Result = fg_PageTouch(_pCursor);
			}
			while (!Result && ++(_pCursor->m_iTop) < _pCursor->m_nPages);
			_pCursor->m_iTop = _pCursor->m_nPages - 1;
		}
		return Result;
	}

	/** Initial setup of a sorted-dups cursor.
	 * Sorted duplicates are implemented as a sub-database for the given key.
	 * The duplicate data items are actually keys of the sub-database.
	 * Operations on the duplicate data items are performed using a sub-cursor
	 * initialized when the sub-database is first accessed. This function does
	 * the preliminary setup of the sub-cursor, filling in the fields that
	 * depend only on the parent DB.
	 * @param[in] _pCursor The main cursor whose sorted-dups cursor is to be initialized.
	 */
	void fg_XCursorInit0(CMdbCursor *_pCursor)
	{
		CMdbXCursor *pXCursor = _pCursor->m_pXCursor;

		pXCursor->m_Cursor.m_pXCursor = nullptr;
		pXCursor->m_Cursor.m_pTransaction = _pCursor->m_pTransaction;
		pXCursor->m_Cursor.m_pDatabase = &pXCursor->m_Database;
		pXCursor->m_Cursor.m_pDatabaseInfo = &pXCursor->m_DatabaseInfo;
		pXCursor->m_Cursor.m_DatabaseID = _pCursor->m_DatabaseID;
		pXCursor->m_Cursor.m_pDatabaseFlag = &pXCursor->m_DatabaseFlag;
		pXCursor->m_Cursor.m_nPages = 0;
		pXCursor->m_Cursor.m_iTop = 0;
		DMcSetOvpg(&pXCursor->m_Cursor, NULL);
		pXCursor->m_Cursor.m_Flags = DCSub | (_pCursor->m_Flags & (DCOrigReadOnly | DCWriteMap));
		pXCursor->m_DatabaseInfo.m_DatabaseName.m_Size = 0;
		pXCursor->m_DatabaseInfo.m_DatabaseName.m_pData = nullptr;
		pXCursor->m_DatabaseInfo.m_fKeyCompare = _pCursor->m_pDatabaseInfo->m_fDataCompare;
		pXCursor->m_DatabaseInfo.m_fDataCompare = nullptr;
	}

	/** Final setup of a sorted-dups cursor.
	 *	Sets up the fields that depend on the data from the main cursor.
	 * @param[in] _pCursor The main cursor whose sorted-dups cursor is to be initialized.
	 * @param[in] _pNode The data containing the #CMdbDb record for the
	 * sorted-dup database.
	 */
	void fg_XCursorInit1(CMdbCursor *_pCursor, CMdbNode *_pNode)
	{
		CMdbXCursor *pXCursor = _pCursor->m_pXCursor;

		pXCursor->m_Cursor.m_Flags &= DCSub | DCOrigReadOnly | DCWriteMap;
		if (_pNode->m_Flags & DFSubData)
		{
			memcpy(&pXCursor->m_Database, DNodeData(_pNode), sizeof(CMdbDb));
			pXCursor->m_Cursor.m_Pages[0] = 0;
			pXCursor->m_Cursor.m_nPages = 0;
			pXCursor->m_Cursor.m_iTop = 0;
		}
		else
		{
			CMdbPage *pSubPage = (CMdbPage *)DNodeData(_pNode);
			pXCursor->m_Database.m_Pad = 0;
			pXCursor->m_Database.m_Flags = 0;
			pXCursor->m_Database.m_Depth = 1;
			pXCursor->m_Database.m_nBranchPages = 0;
			pXCursor->m_Database.m_nLeafPages = 1;
			pXCursor->m_Database.m_nOverflowPages = 0;
			pXCursor->m_Database.m_nEntries = DNumKeys(pSubPage);
			DCopyPgno(pXCursor->m_Database.m_RootPage, pSubPage->m_PageNumber);
			pXCursor->m_Cursor.m_nPages = 1;
			pXCursor->m_Cursor.m_iTop = 0;
			pXCursor->m_Cursor.m_Flags |= DCInitialized;
			pXCursor->m_Cursor.m_Pages[0] = pSubPage;
			pXCursor->m_Cursor.m_NodeIndices[0] = 0;
			if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)
			{
				pXCursor->m_Database.m_Flags = EDatabaseSubFlag_SortedDuplicatesFixedSize;
				pXCursor->m_Database.m_Pad = pSubPage->m_Pad;
				if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
					pXCursor->m_Database.m_Flags |= EDatabaseSubFlag_IntegerKeys;
			}
		}
		DMibDatabaseTrace("Sub-db -{} root page {}", pXCursor->m_Cursor.m_DatabaseID, pXCursor->m_Database.m_RootPage);
		pXCursor->m_DatabaseFlag = DDbValid | DDbUserValid | DDbDupData;
		if (DNeedCmpClong(pXCursor->m_DatabaseInfo.m_fKeyCompare, pXCursor->m_Database.m_Pad))
			pXCursor->m_DatabaseInfo.m_fKeyCompare = g_mdb_cmp_clong;
	}

	/** Fixup a sorted-dups cursor due to underlying update.
	 *	Sets up some fields that depend on the data from the main cursor.
	 *	Almost the same as init1, but skips initialization steps if the
	 *	xcursor had already been used.
	 * @param[in] _pCursor The main cursor whose sorted-dups cursor is to be fixed up.
	 * @param[in] _pSourceXCursor The xcursor of an up-to-date cursor.
	 * @param[in] _bNewDuplicateData True if converting from a non-#DFDupData item.
	 */
	void fg_XCursorInit2(CMdbCursor *_pCursor, CMdbXCursor *_pSourceXCursor, int _bNewDuplicateData)
	{
		CMdbXCursor *pXCursor = _pCursor->m_pXCursor;

		if (_bNewDuplicateData)
		{
			pXCursor->m_Cursor.m_nPages = 1;
			pXCursor->m_Cursor.m_iTop = 0;
			pXCursor->m_Cursor.m_Flags |= DCInitialized;
			pXCursor->m_Cursor.m_NodeIndices[0] = 0;
			pXCursor->m_DatabaseFlag = DDbValid | DDbUserValid | DDbDupData;
#if DMibPPtrBits < DMibDatabase_IDBits	/* matches fg_XCursorInit1:DNeedCmpClong() */
			pXCursor->m_DatabaseInfo.m_fKeyCompare = _pSourceXCursor->m_DatabaseInfo.m_fKeyCompare;
#endif
		}
		else if (!(pXCursor->m_Cursor.m_Flags & DCInitialized))
			return;
		pXCursor->m_Database = _pSourceXCursor->m_Database;
		pXCursor->m_Cursor.m_Pages[0] = _pSourceXCursor->m_Cursor.m_Pages[0];
		DMibDatabaseTrace("Sub-db -{} root page {}", pXCursor->m_Cursor.m_DatabaseID, pXCursor->m_Database.m_RootPage);
	}

	/** Initialize a cursor for a given transaction and database. */
	void fg_CursorInit(CMdbCursor *_pCursor, CMdbTxn *_pTransaction, CDatabaseSubID _DatabaseID, CMdbXCursor *_pXCursor)
	{
		_pCursor->m_pNext = nullptr;
		_pCursor->m_pBackup = nullptr;
		_pCursor->m_DatabaseID = _DatabaseID;
		_pCursor->m_pTransaction = _pTransaction;
		_pCursor->m_pDatabase = &_pTransaction->m_pDatabases[_DatabaseID];
		_pCursor->m_pDatabaseInfo = &_pTransaction->m_pDatabaseInfos[_DatabaseID];
		_pCursor->m_pDatabaseFlag = &_pTransaction->m_pDatabaseFlags[_DatabaseID];
		_pCursor->m_nPages = 0;
		_pCursor->m_iTop = 0;
		_pCursor->m_Pages[0] = 0;
		_pCursor->m_NodeIndices[0] = 0;
		DMcSetOvpg(_pCursor, NULL);
		_pCursor->m_Flags = _pTransaction->m_Flags & (DCOrigReadOnly | DCWriteMap);
		if (_pTransaction->m_pDatabases[_DatabaseID].m_Flags & EDatabaseSubFlag_SortedDuplicates)
		{
			DMibCheck(_pXCursor != nullptr);
			_pCursor->m_pXCursor = _pXCursor;
			fg_XCursorInit0(_pCursor);
		}
		else
			_pCursor->m_pXCursor = nullptr;
		if (*_pCursor->m_pDatabaseFlag & DDbStale)
			fg_PageSearch(_pCursor, nullptr, DMdbPsRootonly);
	}

	/** Copy the contents of a cursor.
	 * @param[in] _pSourceCursor The cursor to copy from.
	 * @param[out] _pDestinationCursor The cursor to copy to.
	 */
	void fg_CursorCopy(CMdbCursor const *_pSourceCursor, CMdbCursor *_pDestinationCursor)
	{
		uint32 iLevel;

		_pDestinationCursor->m_pTransaction = _pSourceCursor->m_pTransaction;
		_pDestinationCursor->m_DatabaseID = _pSourceCursor->m_DatabaseID;
		_pDestinationCursor->m_pDatabase = _pSourceCursor->m_pDatabase;
		_pDestinationCursor->m_pDatabaseInfo = _pSourceCursor->m_pDatabaseInfo;
		_pDestinationCursor->m_nPages = _pSourceCursor->m_nPages;
		_pDestinationCursor->m_iTop = _pSourceCursor->m_iTop;
		_pDestinationCursor->m_Flags = _pSourceCursor->m_Flags;
		DMcSetOvpg(_pDestinationCursor, DMcOvpg(_pSourceCursor));

		for (iLevel = 0; iLevel < _pSourceCursor->m_nPages; iLevel++)
		{
			_pDestinationCursor->m_Pages[iLevel] = _pSourceCursor->m_Pages[iLevel];
			_pDestinationCursor->m_NodeIndices[iLevel] = _pSourceCursor->m_NodeIndices[iLevel];
		}
	}

	/** Complete a delete operation started by #fg_CursorDel(). */
	int fg_CursorDel0(CMdbCursor *_pCursor)
	{
		int Result;
		CMdbPage *pPage;
		CIndx iNode;
		uint32 nKeys;
		CMdbCursor *pOther, *pOtherCursor;
		CDatabaseSubID DatabaseID = _pCursor->m_DatabaseID;

		iNode = _pCursor->m_NodeIndices[_pCursor->m_iTop];
		pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		fg_NodeDel(_pCursor, _pCursor->m_pDatabase->m_Pad);
		_pCursor->m_pDatabase->m_nEntries--;
		{
			/* Adjust other cursors pointing to pPage */
			for (pOther = _pCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
			{
				pOtherCursor = (_pCursor->m_Flags & DCSub) ? &pOther->m_pXCursor->m_Cursor : pOther;
				if (!(pOther->m_Flags & pOtherCursor->m_Flags & DCInitialized))
					continue;
				if (pOtherCursor == _pCursor || pOtherCursor->m_nPages < _pCursor->m_nPages)
					continue;
				if (pOtherCursor->m_Pages[_pCursor->m_iTop] == pPage)
				{
					if (pOtherCursor->m_NodeIndices[_pCursor->m_iTop] == iNode)
					{
						pOtherCursor->m_Flags |= DCDel;
						if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates)
						{
							/* Sub-cursor referred into dataset which is gone */
							pOtherCursor->m_pXCursor->m_Cursor.m_Flags &= ~(DCInitialized | DCEof);
						}
						continue;
					}
					else if (pOtherCursor->m_NodeIndices[_pCursor->m_iTop] > iNode)
						pOtherCursor->m_NodeIndices[_pCursor->m_iTop]--;
					DXCursorRefresh(pOtherCursor, _pCursor->m_iTop, pPage);
				}
			}
		}
		Result = fg_Rebalance(_pCursor);

		if (Result == DMdbSuccess)
		{
			/* DB is totally empty now, just bail out.
			 * Other cursors adjustments were already done
			 * by fg_Rebalance and aren't needed here.
			 */
			if (!_pCursor->m_nPages)
				return Result;

			pPage = _pCursor->m_Pages[_pCursor->m_iTop];
			nKeys = DNumKeys(pPage);

			/* Adjust other cursors pointing to pPage */
			for (pOther = _pCursor->m_pTransaction->m_pCursors[DatabaseID]; !Result && pOther; pOther = pOther->m_pNext)
			{
				pOtherCursor = (_pCursor->m_Flags & DCSub) ? &pOther->m_pXCursor->m_Cursor : pOther;
				if (!(pOther->m_Flags & pOtherCursor->m_Flags & DCInitialized))
					continue;
				if (pOtherCursor->m_nPages < _pCursor->m_nPages)
					continue;
				if (pOtherCursor->m_Pages[_pCursor->m_iTop] == pPage)
				{
					/* if pOtherCursor points past last node in page, find next sibling */
					if (pOtherCursor->m_NodeIndices[_pCursor->m_iTop] >= _pCursor->m_NodeIndices[_pCursor->m_iTop])
					{
						if (pOtherCursor->m_NodeIndices[_pCursor->m_iTop] >= nKeys)
						{
							Result = fg_CursorSibling(pOtherCursor, 1);
							if (Result == DMdbNotFound)
							{
								pOtherCursor->m_Flags |= DCEof;
								Result = DMdbSuccess;
								continue;
							}
						}
						if (_pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates)
						{
							CMdbNode *pNode = DNodePtr(pOtherCursor->m_Pages[pOtherCursor->m_iTop], pOtherCursor->m_NodeIndices[pOtherCursor->m_iTop]);
							/* If this node has dupdata, it may need to be reinited
							 * because its data has moved.
							 * If the xcursor was not initd it must be reinited.
							 * Else if node points to a subDB, nothing is needed.
							 * Else (xcursor was initd, not a subDB) needs m_Pages[0] reset.
							 */
							if (pNode->m_Flags & DFDupData)
							{
								if (pOtherCursor->m_pXCursor->m_Cursor.m_Flags & DCInitialized)
								{
									if (!(pNode->m_Flags & DFSubData))
										pOtherCursor->m_pXCursor->m_Cursor.m_Pages[0] = (CMdbPage *)DNodeData(pNode);
								}
								else
								{
									fg_XCursorInit1(pOtherCursor, pNode);
									pOtherCursor->m_pXCursor->m_Cursor.m_Flags |= DCDel;
								}
							}
						}
					}
				}
			}
			_pCursor->m_Flags |= DCDel;
		}

		if (Result)
			_pCursor->m_pTransaction->m_Flags |= DMdbTxnError;
		return Result;
	}
}
