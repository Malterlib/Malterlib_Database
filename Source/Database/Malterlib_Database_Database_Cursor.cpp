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
		DMibRequire(!mc_txn->mt_env->f_IsInsideMemoryMap(_pKey->m_pData) && !mc_txn->mt_env->f_IsInsideMemoryMap(_pData->m_pData))
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

		if (mc_txn->mt_flags & (DMdbTxnReadOnly))
			DMibError("Trying to write to read only transaction");

		int rc = fg_CursorPut(this, _pKey, _pData, _WriteFlags);
		if (rc)
			DMibError(NStr::fg_Format("Error putting data: {}", fg_StrError(rc)));
	}

	void CDatabaseCursorCommon::CInternal::f_Delete(EDatabaseWriteFlag _WriteFlags)
	{
		using namespace NPrivate;

		if (_WriteFlags & ~(EDatabaseWriteFlag_NoDuplicateData))
			DMibError("Invalid flags");

		if (mc_txn->mt_flags & (DMdbTxnReadOnly))
			DMibError("Trying to delete in read only transaction");

		int rc = fg_CursorDel(this, _WriteFlags);
		if (rc)
			DMibError(NStr::fg_Format("Error deleting data: {}", fg_StrError(rc)));
	}

	CDatabaseOffset CDatabaseCursorCommon::CInternal::f_CountDuplicates()
	{
		using namespace NPrivate;

		CDatabaseOffset Ret;
		int rc = fg_CursorCount(this, &Ret);
		if (rc)
			DMibError(NStr::fg_Format("Error counting duplicates: {}", fg_StrError(rc)));
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

		CMdbCursor *mc;
		umint size = sizeof(CMdbCursor);

		if (!DTxnDbiExist(this, _DatabaseID, DDbValid))
			DMibError("Database does not exists");

		f_CheckBlocked();

		if (_DatabaseID == DFreeDbi && !DFIsSet(mt_flags, DMdbTxnReadOnly))
			DMibError("Cannot open free database as read/write");

		if (mt_dbs[_DatabaseID].md_flags & EDatabaseSubFlag_SortedDuplicates)
			size += sizeof(CMdbXCursor);

		mc = (CMdbCursor *)NMemory::fg_Alloc(size);
		auto Cleanup = g_OnScopeExit / [&]()
			{
				fg_CursorClose(mc);
			}
		;
		fg_CursorInit(mc, this, _DatabaseID, (CMdbXCursor *)(mc + 1));
		if (mt_cursors)
		{
			mc->mc_next = mt_cursors[_DatabaseID];
			mt_cursors[_DatabaseID] = mc;
			mc->mc_flags |= DCUntrack;
		}

		Cleanup.f_Clear();
		return mc;
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
		if (_pCursor && !_pCursor->mc_backup)
		{
			/* Remove from txn, if tracked.
			 * A read-only txn (!DCUntrack) may have been freed already,
			 * so do not peek inside it.  Only write txns track cursors.
			 */
			if ((_pCursor->mc_flags & DCUntrack) && _pCursor->mc_txn->mt_cursors)
			{
				CMdbCursor **prev = &_pCursor->mc_txn->mt_cursors[_pCursor->mc_dbi];
				while (*prev && *prev != _pCursor)
					prev = &(*prev)->mc_next;
				if (*prev == _pCursor)
					*prev = _pCursor->mc_next;
			}
			umint Size = sizeof(CMdbCursor);
			if (_pCursor->mc_xcursor)
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

		int rc;
		int exact = 0;
		int (*mfunc)(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData);

		if (_pCursor == nullptr)
			return EINVAL;

		_pCursor->mc_txn->f_CheckBlocked();

		switch (_Operation)
		{
		case EDatabaseCursorGet_GetCurrent:
			if (!(_pCursor->mc_flags & DCInitialized))
				rc = EINVAL;
			else
			{
				CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top];
				int nkeys = DNumKeys(mp);
				if (!nkeys || _pCursor->mc_ki[_pCursor->mc_top] >= nkeys)
				{
					_pCursor->mc_ki[_pCursor->mc_top] = nkeys;
					rc = DMdbNotFound;
					break;
				}
				rc = DMdbSuccess;
				if (DIsLeaf2(mp))
				{
					_pKey->m_Size = _pCursor->mc_db->md_pad;
					_pKey->m_pData = DLeaf2Key(mp, _pCursor->mc_ki[_pCursor->mc_top], _pKey->m_Size);
				}
				else
				{
					CMdbNode *leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);
					DMdbGetKey(leaf, _pKey);
					if (_pData)
					{
						if (DFIsSet(leaf->mn_flags, DFDupData))
							rc = fg_CursorGet(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr, EDatabaseCursorGet_GetCurrent);
						else
							rc = fg_NodeRead(_pCursor, leaf, _pData);
					}
				}
			}
			break;
		case EDatabaseCursorGet_GetBoth:
		case EDatabaseCursorGet_GetBothRange:
			if (_pData == nullptr)
			{
				rc = EINVAL;
				break;
			}
			if (_pCursor->mc_xcursor == nullptr)
			{
				rc = DMdbIncompatible;
				break;
			}
			/* FALLTHRU */
		case EDatabaseCursorGet_Set:
		case EDatabaseCursorGet_SetKey:
		case EDatabaseCursorGet_SetRange:
			if (_pKey == nullptr)
				rc = EINVAL;
			else
				rc = fg_CursorSet(_pCursor, _pKey, _pData, _Operation, _Operation == EDatabaseCursorGet_SetRange ? nullptr : &exact);
			break;
		case EDatabaseCursorGet_GetMultiple:
			if (_pData == nullptr || !(_pCursor->mc_flags & DCInitialized))
			{
				rc = EINVAL;
				break;
			}
			if (!(_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
			{
				rc = DMdbIncompatible;
				break;
			}
			rc = DMdbSuccess;
			if (!(_pCursor->mc_xcursor->mx_cursor.mc_flags & DCInitialized) || (_pCursor->mc_xcursor->mx_cursor.mc_flags & DCEof))
				break;
			goto fetchm;
		case EDatabaseCursorGet_NextMultiple:
			if (_pData == nullptr)
			{
				rc = EINVAL;
				break;
			}
			if (!(_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
			{
				rc = DMdbIncompatible;
				break;
			}
			rc = fg_CursorNext(_pCursor, _pKey, _pData, EDatabaseCursorGet_NextDup);
			if (rc == DMdbSuccess)
			{
				if (_pCursor->mc_xcursor->mx_cursor.mc_flags & DCInitialized)
				{
					CMdbCursor *mx;
				fetchm:
					mx = &_pCursor->mc_xcursor->mx_cursor;
					_pData->m_Size = DNumKeys(mx->mc_pg[mx->mc_top]) * mx->mc_db->md_pad;
					_pData->m_pData = DMetaData(mx->mc_pg[mx->mc_top]);
					mx->mc_ki[mx->mc_top] = DNumKeys(mx->mc_pg[mx->mc_top]) - 1;
				}
				else
					rc = DMdbNotFound;
			}
			break;
		case EDatabaseCursorGet_PrevMultiple:
			if (_pData == nullptr)
			{
				rc = EINVAL;
				break;
			}
			if (!(_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
			{
				rc = DMdbIncompatible;
				break;
			}
			if (!(_pCursor->mc_flags & DCInitialized))
				rc = fg_CursorLast(_pCursor, _pKey, _pData);
			else
				rc = DMdbSuccess;
			if (rc == DMdbSuccess)
			{
				CMdbCursor *mx = &_pCursor->mc_xcursor->mx_cursor;
				if (mx->mc_flags & DCInitialized)
				{
					rc = fg_CursorSibling(mx, 0);
					if (rc == DMdbSuccess)
						goto fetchm;
				}
				else
					rc = DMdbNotFound;
			}
			break;
		case EDatabaseCursorGet_Next:
		case EDatabaseCursorGet_NextDup:
		case EDatabaseCursorGet_NextNoDup:
			rc = fg_CursorNext(_pCursor, _pKey, _pData, _Operation);
			break;
		case EDatabaseCursorGet_Prev:
		case EDatabaseCursorGet_PrevDup:
		case EDatabaseCursorGet_PrevNoDup:
			rc = fg_CursorPrev(_pCursor, _pKey, _pData, _Operation);
			break;
		case EDatabaseCursorGet_First: rc = fg_CursorFirst(_pCursor, _pKey, _pData); break;
		case EDatabaseCursorGet_FirstDup: mfunc = fg_CursorFirst;
		mmove:
			if (_pData == nullptr || !(_pCursor->mc_flags & DCInitialized))
			{
				rc = EINVAL;
				break;
			}
			if (_pCursor->mc_xcursor == nullptr)
			{
				rc = DMdbIncompatible;
				break;
			}
			if (_pCursor->mc_ki[_pCursor->mc_top] >= DNumKeys(_pCursor->mc_pg[_pCursor->mc_top]))
			{
				_pCursor->mc_ki[_pCursor->mc_top] = DNumKeys(_pCursor->mc_pg[_pCursor->mc_top]);
				rc = DMdbNotFound;
				break;
			}
			{
				CMdbNode *leaf = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);
				if (!DFIsSet(leaf->mn_flags, DFDupData))
				{
					DMdbGetKey(leaf, _pKey);
					rc = fg_NodeRead(_pCursor, leaf, _pData);
					break;
				}
			}
			if (!(_pCursor->mc_xcursor->mx_cursor.mc_flags & DCInitialized))
			{
				rc = EINVAL;
				break;
			}
			rc = mfunc(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr);
			break;
		case EDatabaseCursorGet_Last: rc = fg_CursorLast(_pCursor, _pKey, _pData); break;
		case EDatabaseCursorGet_LastDup:
			mfunc = fg_CursorLast;
			goto mmove;
		default:
			DMibDatabaseTrace("Unhandled/unimplemented cursor operation {}", _Operation);
			rc = EINVAL;
			break;
		}

		if (_pCursor->mc_flags & DCDel)
			_pCursor->mc_flags ^= DCDel;

		return rc;
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
		uint32 flags = _Flags;
		using namespace NPrivate;
		CMdbEnv *env;
		CMdbNode *leaf = nullptr;
		CMdbPage *fp, *mp, *sub_root = nullptr;
		uint16 fp_flags;
		CDatabaseValue xdata, *rdata, dkey, olddata;
		CMdbDb dummy;
		int do_sub = 0, insert_key, insert_data;
		uint32 mcount = 0, dcount = 0, nospill;
		umint nsize;
		int rc, rc2;
		uint32 nflags;
		DKBuf;

		if (_pCursor == nullptr || _pKey == nullptr)
			return EINVAL;

		env = _pCursor->mc_txn->mt_env;

		/* Check this first so counter will always be zero on any
		 * early failures.
		 */
		if (flags & EDatabaseWriteFlag_BatchDuplicateData)
		{
			dcount = _pData[1].m_Size;
			_pData[1].m_Size = 0;
			if (!DFIsSet(_pCursor->mc_db->md_flags, EDatabaseSubFlag_SortedDuplicatesFixedSize))
				return DMdbIncompatible;
		}

		nospill = flags & DMdbNoSpill;
		flags &= ~DMdbNoSpill;

		_pCursor->mc_txn->f_CheckBlocked();

		if (_pCursor->mc_txn->mt_flags & (DMdbTxnReadOnly))
			return EACCES;

		if (_pKey->m_Size - 1 >= DEnvMaxkey(env))
			return DMdbBadValsize;

#if SIZE_MAX > DMaxDataSize
		if (_pData->m_Size > ((_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) ? DEnvMaxkey(env) : DMaxDataSize))
			return DMdbBadValsize;
#else
		if ((_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) && _pData->m_Size > DEnvMaxkey(env))
			return DMdbBadValsize;
#endif

		DMibDatabaseTrace("==> put db {} key [{}], size {}, data size {}", DDbi(_pCursor), DKey(_pKey), _pKey ? _pKey->m_Size : 0, _pData->m_Size);

		dkey.m_Size = 0;

		if (flags & EDatabaseWriteFlag_OverwriteAtCurrentCursorPos)
		{
			if (!(_pCursor->mc_flags & DCInitialized))
				return EINVAL;
			rc = DMdbSuccess;
		}
		else if (_pCursor->mc_db->md_root == DPInvalid)
		{
			/* new database, cursor has nothing to point to */
			_pCursor->mc_snum = 0;
			_pCursor->mc_top = 0;
			_pCursor->mc_flags &= ~DCInitialized;
			rc = DMdbNoRoot;
		}
		else
		{
			int exact = 0;
			CDatabaseValue d2;
			if (flags & EDatabaseWriteFlag_Append)
			{
				CDatabaseValue k2;
				rc = fg_CursorLast(_pCursor, &k2, &d2);
				if (rc == 0)
				{
					rc = _pCursor->mc_dbx->m_fKeyCompare(*_pKey, k2);
					if (rc > 0)
					{
						rc = DMdbNotFound;
						_pCursor->mc_ki[_pCursor->mc_top]++;
					}
					else
					{
						/* new key is <= last key */
						rc = DMdbKeyExist;
					}
				}
			}
			else
				rc = fg_CursorSet(_pCursor, _pKey, &d2, EDatabaseCursorGet_Set, &exact);
			if ((flags & EDatabaseWriteFlag_NoOverwrite) && rc == 0)
			{
				DMibDatabaseTrace("duplicate key [{}]", DKey(_pKey));
				*_pData = d2;
				return DMdbKeyExist;
			}
			if (rc && rc != DMdbNotFound)
				return rc;
		}

		if (_pCursor->mc_flags & DCDel)
			_pCursor->mc_flags ^= DCDel;

		/* Cursor is positioned, check for room in the dirty list */
		if (!nospill)
		{
			if (flags & EDatabaseWriteFlag_BatchDuplicateData)
			{
				rdata = &xdata;
				xdata.m_Size = _pData->m_Size * dcount;
			}
			else
				rdata = _pData;
			if ((rc2 = fg_PageSpill(_pCursor, _pKey, rdata)))
				return rc2;
		}

		if (rc == DMdbNoRoot)
		{
			CMdbPage *np;
			/* new database, write a root leaf page */
			DPuts("allocating new root leaf page");
			if ((rc2 = fg_PageNew(_pCursor, DPLeaf, 1, &np)))
				return rc2;
			fg_CursorPush(_pCursor, np);
			_pCursor->mc_db->md_root = np->mp_p.p_pgno;
			_pCursor->mc_db->md_depth++;
			*_pCursor->mc_dbflag |= DDbDirty;
			if ((_pCursor->mc_db->md_flags & (EDatabaseSubFlag_SortedDuplicates | EDatabaseSubFlag_SortedDuplicatesFixedSize)) == EDatabaseSubFlag_SortedDuplicatesFixedSize)
				np->mp_flags |= DPLeaf2;
			_pCursor->mc_flags |= DCInitialized;
		}
		else
		{
			/* make sure all cursor pages are writable */
			rc2 = fg_CursorTouch(_pCursor);
			if (rc2)
				return rc2;
		}

		unsigned offset = 0;

		insert_key = insert_data = rc;
		if (insert_key)
		{
			/* The key does not exist */
			DMibDatabaseTrace("inserting key at index {}", _pCursor->mc_ki[_pCursor->mc_top]);
			if ((_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) && DLeafSize(_pKey, _pData) > env->me_nodemax)
			{
				/* Too big for a node, insert in sub-DB.  Set up an empty
				 * "old sub-page" for prep_subDB to expand to a full page.
				 */
				fp_flags = DPLeaf | DPDirty;
				fp = (CMdbPage *)env->me_pbuf;
				fp->mp_pad = _pData->m_Size; // used if EDatabaseSubFlag_SortedDuplicatesFixedSize
				fp->mp_lower = fp->mp_upper = (DPageHeaderSize - DPageBase);
				olddata.m_Size = DPageHeaderSize;
				goto prep_subDB;
			}
		}
		else
		{
			/* there's only a key anyway, so this is a no-op */
			if (DIsLeaf2(_pCursor->mc_pg[_pCursor->mc_top]))
			{
				{
					uint8 *ptr;
					uint32 ksize = _pCursor->mc_db->md_pad;
					if (_pKey->m_Size != ksize)
						return DMdbBadValsize;
					ptr = DLeaf2Key(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top], ksize);
					memcpy(ptr, _pKey->m_pData, ksize);
				}
			fix_parent:
				/* if overwriting slot 0 of leaf, need to
				 * update branch key if there is a parent page
				 */
				if (_pCursor->mc_top && !_pCursor->mc_ki[_pCursor->mc_top])
				{
					unsigned short dtop = 1;
					_pCursor->mc_top--;
					/* slot 0 is always an empty key, find real slot */
					while (_pCursor->mc_top && !_pCursor->mc_ki[_pCursor->mc_top])
					{
						_pCursor->mc_top--;
						dtop++;
					}
					if (_pCursor->mc_ki[_pCursor->mc_top])
						rc2 = fg_UpdateKey(_pCursor, _pKey);
					else
						rc2 = DMdbSuccess;
					_pCursor->mc_top += dtop;
					if (rc2)
						return rc2;
				}
				return DMdbSuccess;
			}

		more:
			leaf = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);
			olddata.m_Size = DNodeDataSize(leaf);
			olddata.m_pData = DNodeData(leaf);

			/* DB has dups? */
			if (DFIsSet(_pCursor->mc_db->md_flags, EDatabaseSubFlag_SortedDuplicates))
			{
				/* Prepare (sub-)page/sub-DB to accept the new item,
				 * if needed.  fp: old sub-page or a header faking
				 * it.  mp: new (sub-)page.  offset: growth in page
				 * size.  xdata: node data with new page or DB.
				 */
				unsigned i;
				mp = fp = (CMdbPage *)(xdata.m_pData = (uint8 *)env->me_pbuf);
				mp->mp_p.p_pgno = _pCursor->mc_pg[_pCursor->mc_top]->mp_p.p_pgno;

				/* Was a single item before, must convert now */
				if (!DFIsSet(leaf->mn_flags, DFDupData))
				{
					FDatabaseCompare *pDataCompare = nullptr;
					/* Just overwrite the current item */
					if (flags == EDatabaseWriteFlag_OverwriteAtCurrentCursorPos)
						goto current;
					pDataCompare = &(_pCursor->mc_dbx->m_fDataCompare);
					if (DNeedCmpClong(*pDataCompare, olddata.m_Size))
						pDataCompare = &g_mdb_cmp_clong;
					/* does data match? */
					if (!(*pDataCompare)(*_pData, olddata))
					{
						if (flags & (EDatabaseWriteFlag_NoDuplicateData | EDatabaseWriteFlag_AppendDuplicateData))
							return DMdbKeyExist;
						/* overwrite it */
						goto current;
					}

					/* Back up original data item */
					dkey.m_Size = olddata.m_Size;
					dkey.m_pData = (uint8 *)memcpy(fp + 1, olddata.m_pData, olddata.m_Size);

					/* Make sub-page header for the dup items, with dummy body */
					fp->mp_flags = DPLeaf | DPDirty | DPSubp;
					fp->mp_lower = (DPageHeaderSize - DPageBase);
					xdata.m_Size = DPageHeaderSize + dkey.m_Size + _pData->m_Size;
					if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)
					{
						fp->mp_flags |= DPLeaf2;
						fp->mp_pad = _pData->m_Size;
						xdata.m_Size += 2 * _pData->m_Size; // leave space for 2 more
					}
					else
						xdata.m_Size += 2 * (sizeof(CIndx) + DNodeSize) + (dkey.m_Size & 1) + (_pData->m_Size & 1);
					fp->mp_upper = xdata.m_Size - DPageBase;
					olddata.m_Size = xdata.m_Size; // pretend olddata is fp
				}
				else if (leaf->mn_flags & DFSubData)
				{
					/* Data is on sub-DB, just store it */
					flags |= DFDupData | DFSubData;
					goto put_sub;
				}
				else
				{
					/* Data is on sub-page */
					fp = (CMdbPage *)olddata.m_pData;
					switch (flags)
					{
					default:
						if (!(_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize))
						{
							offset = DEven(DNodeSize + sizeof(CIndx) + _pData->m_Size);
							break;
						}
						offset = fp->mp_pad;
						if (DSizeLeft(fp) < offset)
						{
							offset *= 4; // space for 4 more
							break;
						}
						/* FALLTHRU: Big enough EDatabaseSubFlag_SortedDuplicatesFixedSize sub-page */
					case EDatabaseWriteFlag_OverwriteAtCurrentCursorPos:
						fp->mp_flags |= DPDirty;
						DCopyPgno(fp->mp_p.p_pgno, mp->mp_p.p_pgno);
						_pCursor->mc_xcursor->mx_cursor.mc_pg[0] = fp;
						flags |= DFDupData;
						goto put_sub;
					}
					xdata.m_Size = olddata.m_Size + offset;
				}

				fp_flags = fp->mp_flags;
				if (DNodeSize + DNodeKeySize(leaf) + xdata.m_Size > env->me_nodemax)
				{
					/* Too big for a sub-page, convert to sub-DB */
					fp_flags &= ~DPSubp;
				prep_subDB:
					if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)
					{
						fp_flags |= DPLeaf2;
						dummy.md_pad = fp->mp_pad;
						dummy.md_flags = EDatabaseSubFlag_SortedDuplicatesFixedSize;
						if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
							dummy.md_flags |= EDatabaseSubFlag_IntegerKeys;
					}
					else
					{
						dummy.md_pad = 0;
						dummy.md_flags = 0;
					}
					dummy.md_depth = 1;
					dummy.md_branch_pages = 0;
					dummy.md_leaf_pages = 1;
					dummy.md_overflow_pages = 0;
					dummy.md_entries = DNumKeys(fp);
					xdata.m_Size = sizeof(CMdbDb);
					xdata.m_pData = (uint8 *)&dummy;
					if ((rc = fg_PageAlloc(_pCursor, 1, &mp)))
						return rc;
					offset = env->me_psize - olddata.m_Size;
					flags |= DFDupData | DFSubData;
					dummy.md_root = mp->mp_p.p_pgno;
					sub_root = mp;
				}
				if (mp != fp)
				{
					mp->mp_flags = fp_flags | DPDirty;
					mp->mp_pad = fp->mp_pad;
					mp->mp_lower = fp->mp_lower;
					mp->mp_upper = fp->mp_upper + offset;
					if (fp_flags & DPLeaf2)
						memcpy(DMetaData(mp), DMetaData(fp), DNumKeys(fp) * fp->mp_pad);
					else
					{
						memcpy((char *)mp + mp->mp_upper + DPageBase, (char *)fp + fp->mp_upper + DPageBase, olddata.m_Size - fp->mp_upper - DPageBase);
						memcpy((char *)(&mp->mp_ptrs), (char *)(&fp->mp_ptrs), DNumKeys(fp) * sizeof(mp->mp_ptrs[0]));
						for (i = 0; i < DNumKeys(fp); i++)
							mp->mp_ptrs[i] += offset;
					}
				}

				rdata = &xdata;
				flags |= DFDupData;
				do_sub = 1;
				if (!insert_key)
					fg_NodeDel(_pCursor, 0);
				goto new_sub;
			}
		current:
			/* LMDB passes DFSubData in 'flags' to write a DB record */
			if ((leaf->mn_flags ^ flags) & DFSubData)
				return DMdbIncompatible;
			/* overflow page overwrites need special handling */
			if (DFIsSet(leaf->mn_flags, DFBigData))
			{
				CMdbPage *omp;
				CPgno pg;
				int level, ovpages, dpages = DOverflowPages(_pData->m_Size, env->me_psize);

				memcpy(&pg, olddata.m_pData, sizeof(pg));
				if ((rc2 = fg_PageGet(_pCursor, pg, &omp, &level)) != 0)
					return rc2;
				ovpages = omp->mp_pages;

				/* Is the ov page large enough? */
				if (ovpages >= dpages)
				{
					if (!(omp->mp_flags & DPDirty) && (level || (env->me_flags & EDatabaseEnvironmentFlag_WriteMap)))
					{
						rc = fg_PageUnspill(_pCursor->mc_txn, omp, &omp);
						if (rc)
							return rc;
						level = 0; // dirty in this txn or clean
					}
					/* Is it dirty? */
					if (omp->mp_flags & DPDirty)
					{
						/* yes, overwrite it. Note in this case we don't
						 * bother to try shrinking the page if the new data
						 * is smaller than the overflow threshold.
						 */
						if (level > 1)
						{
							/* It is writable only in a parent txn */
							umint sz = (umint)env->me_psize * ovpages, off;
							CMdbPage *np = fg_PageMalloc(_pCursor->mc_txn, ovpages);
							CMdbId2 id2;
							id2.mid = pg;
							id2.mptr = np;
							/* Note - this page is already counted in parent's dirty_room */
							rc2 = fg_Mid2lInsert(_pCursor->mc_txn->mt_u.dirty_list, &id2);
							DMibCheck(rc2 == 0);
							/* Currently we make the page look as with put() in the
							 * parent txn, in case the user peeks at EDatabaseWriteFlag_ReserveSpaced
							 * or unused parts. Some users treat ovpages specially.
							 */
							if (!(flags & EDatabaseWriteFlag_ReserveSpace))
							{
								/* Skip the part where LMDB will put *data.
								 * Copy end of page, adjusting alignment so
								 * compiler may copy words instead of bytes.
								 */
								off = (DPageHeaderSize + _pData->m_Size) & -sizeof(umint);
								memcpy((umint *)((char *)np + off), (umint *)((char *)omp + off), sz - off);
								sz = DPageHeaderSize;
							}
							memcpy(np, omp, sz); // Copy beginning of page
							omp = np;
						}
						DSetDataSize(leaf, _pData->m_Size);
						if (DFIsSet(flags, EDatabaseWriteFlag_ReserveSpace))
							_pData->m_pData = DMetaData(omp);
						else
							memcpy(DMetaData(omp), _pData->m_pData, _pData->m_Size);
						return DMdbSuccess;
					}
				}
				if ((rc2 = fg_OvPageFree(_pCursor, omp)) != DMdbSuccess)
					return rc2;
			}
			else if (_pData->m_Size == olddata.m_Size)
			{
				/* same size, just replace it. Note that we could
				 * also reuse this node if the new data is smaller,
				 * but instead we opt to shrink the node in that case.
				 */
				if (DFIsSet(flags, EDatabaseWriteFlag_ReserveSpace))
					_pData->m_pData = olddata.m_pData;
				else if (!(_pCursor->mc_flags & DCSub))
					memcpy((ch8 *)olddata.m_pData, _pData->m_pData, _pData->m_Size);
				else
				{
					memcpy(DNodeKey(leaf), _pKey->m_pData, _pKey->m_Size);
					goto fix_parent;
				}
				return DMdbSuccess;
			}
			fg_NodeDel(_pCursor, 0);
		}

		rdata = _pData;

	new_sub:
		nflags = flags & DNodeAddFlags;
		nsize = DIsLeaf2(_pCursor->mc_pg[_pCursor->mc_top]) ? _pKey->m_Size : env->f_LeafSize(_pKey, rdata);
		if (DSizeLeft(_pCursor->mc_pg[_pCursor->mc_top]) < nsize)
		{
			if ((flags & (DFDupData | DFSubData)) == DFDupData)
				nflags &= ~EDatabaseWriteFlag_Append; // sub-page may need room to grow
			if (!insert_key)
				nflags |= DMdbSplitReplace;
			rc = fg_PageSplit(_pCursor, _pKey, rdata, DPInvalid, nflags);
		}
		else
		{
			/* There is room already in this leaf page. */
			rc = fg_NodeAdd(_pCursor, _pCursor->mc_ki[_pCursor->mc_top], _pKey, rdata, 0, nflags);
			if (rc == 0)
			{
				/* Adjust other cursors pointing to mp */
				CMdbCursor *m2, *m3;
				CDatabaseSubID dbi = _pCursor->mc_dbi;
				unsigned i = _pCursor->mc_top;
				CMdbPage *mp = _pCursor->mc_pg[i];

				for (m2 = _pCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
				{
					if (_pCursor->mc_flags & DCSub)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (m3 == _pCursor || m3->mc_snum < _pCursor->mc_snum || m3->mc_pg[i] != mp)
						continue;
					if (m3->mc_ki[i] >= _pCursor->mc_ki[i] && insert_key)
						m3->mc_ki[i]++;
					DXCursorRefresh(m3, i, mp);
				}
			}
		}

		if (rc == DMdbSuccess)
		{
			/* Now store the actual data in the child DB. Note that we're
			 * storing the user data in the keys field, so there are strict
			 * size limits on dupdata. The actual data fields of the child
			 * DB are all zero size.
			 */
			if (do_sub)
			{
				int xflags, new_dupdata;
				CDatabaseOffset ecount;
			put_sub:
				xdata.m_Size = 0;
				xdata.m_pData = (uint8 *)"";
				leaf = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);
				if (flags == EDatabaseWriteFlag_OverwriteAtCurrentCursorPos)
					xflags = EDatabaseWriteFlag_OverwriteAtCurrentCursorPos | DMdbNoSpill;
				else
				{
					fg_XCursorInit1(_pCursor, leaf);
					xflags = (flags & EDatabaseWriteFlag_NoDuplicateData) ? EDatabaseWriteFlag_NoOverwrite | DMdbNoSpill : DMdbNoSpill;
				}
				if (sub_root)
					_pCursor->mc_xcursor->mx_cursor.mc_pg[0] = sub_root;
				new_dupdata = (int)dkey.m_Size;
				/* converted, write the original data first */
				if (dkey.m_Size)
				{
					rc = fg_CursorPut(&_pCursor->mc_xcursor->mx_cursor, &dkey, &xdata, (EDatabaseWriteFlag)xflags);
					if (rc)
						goto bad_sub;
					/* we've done our job */
					dkey.m_Size = 0;
				}
				if (!(leaf->mn_flags & DFSubData) || sub_root)
				{
					/* Adjust other cursors pointing to mp */
					CMdbCursor *m2;
					CMdbXCursor *mx = _pCursor->mc_xcursor;
					unsigned i = _pCursor->mc_top;
					CMdbPage *mp = _pCursor->mc_pg[i];

					for (m2 = _pCursor->mc_txn->mt_cursors[_pCursor->mc_dbi]; m2; m2 = m2->mc_next)
					{
						if (m2 == _pCursor || m2->mc_snum < _pCursor->mc_snum)
							continue;
						if (!(m2->mc_flags & DCInitialized))
							continue;
						if (m2->mc_pg[i] == mp)
						{
							if (m2->mc_ki[i] == _pCursor->mc_ki[i])
								fg_XCursorInit2(m2, mx, new_dupdata);
							else if (!insert_key)
								DXCursorRefresh(m2, i, mp);
						}
					}
				}
				ecount = _pCursor->mc_xcursor->mx_db.md_entries;
				if (flags & EDatabaseWriteFlag_AppendDuplicateData)
					xflags |= EDatabaseWriteFlag_Append;
				rc = fg_CursorPut(&_pCursor->mc_xcursor->mx_cursor, _pData, &xdata, (EDatabaseWriteFlag)xflags);
				if (flags & DFSubData)
				{
					void *db = DNodeData(leaf);
					memcpy(db, &_pCursor->mc_xcursor->mx_db, sizeof(CMdbDb));
				}
				insert_data = _pCursor->mc_xcursor->mx_db.md_entries - ecount;
			}
			/* Increment count unless we just replaced an existing item. */
			if (insert_data)
				_pCursor->mc_db->md_entries++;
			if (insert_key)
			{
				/* Invalidate txn if we created an empty sub-DB */
				if (rc)
					goto bad_sub;
				/* If we succeeded and the key didn't exist before,
				 * make sure the cursor is marked valid.
				 */
				_pCursor->mc_flags |= DCInitialized;
			}
			if (flags & EDatabaseWriteFlag_BatchDuplicateData)
			{
				if (!rc)
				{
					mcount++;
					/* let caller know how many succeeded, if any */
					_pData[1].m_Size = mcount;
					if (mcount < dcount)
					{
						_pData[0].m_pData = (uint8 *)_pData[0].m_pData + _pData[0].m_Size;
						insert_key = insert_data = 0;
						goto more;
					}
				}
			}
			return rc;
		bad_sub:
			if (rc == DMdbKeyExist) // should not happen, we deleted that item
				rc = DMdbProblem;
		}
		_pCursor->mc_txn->mt_flags |= DMdbTxnError;
		return rc;
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
		uint32 flags = _Flags;
		using namespace NPrivate;

		CMdbNode *leaf;
		CMdbPage *mp;
		int rc;

		_pCursor->mc_txn->f_CheckBlocked();

		if (_pCursor->mc_txn->mt_flags & (DMdbTxnReadOnly))
			return EACCES;

		if (!(_pCursor->mc_flags & DCInitialized))
			return EINVAL;

		if (_pCursor->mc_ki[_pCursor->mc_top] >= DNumKeys(_pCursor->mc_pg[_pCursor->mc_top]))
			return DMdbNotFound;

		if (!(flags & DMdbNoSpill) && (rc = fg_PageSpill(_pCursor, nullptr, nullptr)))
			return rc;

		rc = fg_CursorTouch(_pCursor);
		if (rc)
			return rc;

		mp = _pCursor->mc_pg[_pCursor->mc_top];
		if (DIsLeaf2(mp))
			goto del_key;
		leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);

		if (DFIsSet(leaf->mn_flags, DFDupData))
		{
			if (flags & EDatabaseWriteFlag_NoDuplicateData)
			{
				/* fg_CursorDel0() will subtract the final entry */
				_pCursor->mc_db->md_entries -= _pCursor->mc_xcursor->mx_db.md_entries - 1;
				_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~DCInitialized;
			}
			else
			{
				if (!DFIsSet(leaf->mn_flags, DFSubData))
					_pCursor->mc_xcursor->mx_cursor.mc_pg[0] = (CMdbPage *)DNodeData(leaf);
				rc = fg_CursorDel(&_pCursor->mc_xcursor->mx_cursor, (EDatabaseWriteFlag)DMdbNoSpill);
				if (rc)
					return rc;
				/* If sub-DB still has entries, we're done */
				if (_pCursor->mc_xcursor->mx_db.md_entries)
				{
					if (leaf->mn_flags & DFSubData)
					{
						/* update subDB info */
						void *db = (CMdbPage *)DNodeData(leaf);
						memcpy(db, &_pCursor->mc_xcursor->mx_db, sizeof(CMdbDb));
					}
					else
					{
						CMdbCursor *m2;
						/* shrink fake page */
						fg_NodeShrink(mp, _pCursor->mc_ki[_pCursor->mc_top]);
						leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);
						_pCursor->mc_xcursor->mx_cursor.mc_pg[0] = (CMdbPage *)DNodeData(leaf);
						/* fix other sub-DB cursors pointed at fake pages on this page */
						for (m2 = _pCursor->mc_txn->mt_cursors[_pCursor->mc_dbi]; m2; m2 = m2->mc_next)
						{
							if (m2 == _pCursor || m2->mc_snum < _pCursor->mc_snum)
								continue;
							if (!(m2->mc_flags & DCInitialized))
								continue;
							if (m2->mc_pg[_pCursor->mc_top] == mp)
								DXCursorRefresh(m2, _pCursor->mc_top, mp);
						}
					}
					_pCursor->mc_db->md_entries--;
					return rc;
				}
				else
					_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~DCInitialized;
				/* otherwise fall thru and delete the sub-DB */
			}

			if (leaf->mn_flags & DFSubData)
			{
				/* add all the child DB's pages to the free list */
				rc = fg_Drop0(&_pCursor->mc_xcursor->mx_cursor, 0);
				if (rc)
					goto fail;
			}
		}
		/* LMDB passes DFSubData in 'flags' to delete a DB record */
		else if ((leaf->mn_flags ^ flags) & DFSubData)
		{
			rc = DMdbIncompatible;
			goto fail;
		}

		/* add overflow pages to free list */
		if (DFIsSet(leaf->mn_flags, DFBigData))
		{
			CMdbPage *omp;
			CPgno pg;

			memcpy(&pg, DNodeData(leaf), sizeof(pg));
			if ((rc = fg_PageGet(_pCursor, pg, &omp, nullptr)) || (rc = fg_OvPageFree(_pCursor, omp)))
				goto fail;
		}

	del_key:
		return fg_CursorDel0(_pCursor);

	fail:
		_pCursor->mc_txn->mt_flags |= DMdbTxnError;
		return rc;
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
		CMdbNode *leaf;

		if (_pCursor == nullptr || o_pCount == nullptr)
			return EINVAL;

		if (_pCursor->mc_xcursor == nullptr)
			return DMdbIncompatible;

		_pCursor->mc_txn->f_CheckBlocked();

		if (!(_pCursor->mc_flags & DCInitialized))
			return EINVAL;

		if (!_pCursor->mc_snum)
			return DMdbNotFound;

		if (_pCursor->mc_flags & DCEof)
		{
			if (_pCursor->mc_ki[_pCursor->mc_top] >= DNumKeys(_pCursor->mc_pg[_pCursor->mc_top]))
				return DMdbNotFound;
			_pCursor->mc_flags ^= DCEof;
		}

		leaf = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);
		if (!DFIsSet(leaf->mn_flags, DFDupData))
			*o_pCount = 1;
		else
		{
			if (!(_pCursor->mc_xcursor->mx_cursor.mc_flags & DCInitialized))
				return EINVAL;

			*o_pCount = _pCursor->mc_xcursor->mx_db.md_entries;
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
		if (mc->mc_txn->mt_rpages[0].mid)
		{
			if (!mc->mc_snum || !mc->mc_pg[0] || DIsSubp(mc->mc_pg[0]))
				return;
			for (i = 0; i < mc->mc_snum; i++)
				fg_PageUnref(mc->mc_txn, mc->mc_pg[i]);
			if (mc->mc_ovpg)
			{
				fg_PageUnref(mc->mc_txn, mc->mc_ovpg);
				mc->mc_ovpg = 0;
			}
		}
		mc->mc_snum = mc->mc_top = 0;
		mc->mc_pg[0] = nullptr;
		mc->mc_flags &= ~DCInitialized;
	}
#endif /* DMibDatabase_Always64BitSupport */

	/** Back up parent txn's cursors, then grab the originals for tracking */
	void fg_CursorShadow(CMdbTxn *_pSource, CMdbTxn *_pDestination)
	{
		CMdbCursor *mc, *bk;
		CMdbXCursor *mx;
		umint size;
		int i;

		for (i = _pSource->mt_numdbs; --i >= 0;)
		{
			if ((mc = _pSource->mt_cursors[i]) != nullptr)
			{
				size = sizeof(CMdbCursor);
				if (mc->mc_xcursor)
					size += sizeof(CMdbXCursor);
				for (; mc; mc = bk->mc_next)
				{
					bk = (CMdbCursor *)NMemory::fg_Alloc(size);
					*bk = *mc;
					mc->mc_backup = bk;
					mc->mc_db = &_pDestination->mt_dbs[i];
					/* Kill pointers into _pSource to reduce abuse: The
					 * user may not use mc until _pDestination ends. But we need a valid
					 * txn pointer here for cursor fixups to keep working.
					 */
					mc->mc_txn = _pDestination;
					mc->mc_dbflag = &_pDestination->mt_dbflags[i];
					if ((mx = mc->mc_xcursor) != nullptr)
					{
						*(CMdbXCursor *)(bk + 1) = *mx;
						mx->mx_cursor.mc_txn = _pDestination;
					}
					mc->mc_next = _pDestination->mt_cursors[i];
					_pDestination->mt_cursors[i] = mc;
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
		CMdbCursor **cursors = _pTransaction->mt_cursors, *mc, *next, *bk;
		CMdbXCursor *mx;
		int i;

		for (i = _pTransaction->mt_numdbs; --i >= 0;)
		{
			for (mc = cursors[i]; mc; mc = next)
			{
				next = mc->mc_next;
				if ((bk = mc->mc_backup) != nullptr)
				{
					if (_bMerge)
					{
						/* Commit changes to parent txn */
						mc->mc_next = bk->mc_next;
						mc->mc_backup = bk->mc_backup;
						mc->mc_txn = bk->mc_txn;
						mc->mc_db = bk->mc_db;
						mc->mc_dbflag = bk->mc_dbflag;
						if ((mx = mc->mc_xcursor) != nullptr)
							mx->mx_cursor.mc_txn = bk->mc_txn;
					}
					else
					{
						/* Abort nested txn */
						*mc = *bk;
						if ((mx = mc->mc_xcursor) != nullptr)
							*mx = *(CMdbXCursor *)(bk + 1);
					}
					mc = bk;
				}
				/* Only malloced cursors are permanently tracked. */
				umint Size = sizeof(CMdbCursor);
				if (mc->mc_xcursor)
					Size += sizeof(CMdbXCursor);

				NMemory::fg_Free(mc, Size);
			}
			cursors[i] = nullptr;
		}
	}

	/** Pop a page off the top of the cursor's stack. */
	void fg_CursorPop(CMdbCursor *_pCursor)
	{
		if (_pCursor->mc_snum)
		{
			DMibDatabaseTrace("popping page {} off db {} cursor {}", _pCursor->mc_pg[_pCursor->mc_top]->mp_p.p_pgno, DDbi(_pCursor), (void *)_pCursor);

			_pCursor->mc_snum--;
			if (_pCursor->mc_snum)
				_pCursor->mc_top--;
			else
				_pCursor->mc_flags &= ~DCInitialized;
		}
	}

	/** Push a page onto the top of the cursor's stack.
	 * Set #DMdbTxnError on failure.
	 */
	int fg_CursorPush(CMdbCursor *_pCursor, CMdbPage *_pPage)
	{
		DMibDatabaseTrace("pushing page {} on db {} cursor {}", _pPage->mp_p.p_pgno, DDbi(_pCursor), (void *)_pCursor);

		if (_pCursor->mc_snum >= DCursorStack)
		{
			_pCursor->mc_txn->mt_flags |= DMdbTxnError;
			return DMdbCursorFull;
		}

		_pCursor->mc_top = _pCursor->mc_snum++;
		_pCursor->mc_pg[_pCursor->mc_top] = _pPage;
		_pCursor->mc_ki[_pCursor->mc_top] = 0;

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
		int rc;
		CMdbNode *indx;
		CMdbPage *mp;
#ifdef DMibDatabase_Always64BitSupport
		CMdbPage *op;
#endif

		if (_pCursor->mc_snum < 2)
			return DMdbNotFound; // root has no siblings

#ifdef DMibDatabase_Always64BitSupport
		op = _pCursor->mc_pg[_pCursor->mc_top];
#endif
		fg_CursorPop(_pCursor);
		DMibDatabaseTrace("parent page is page {}, index {}", _pCursor->mc_pg[_pCursor->mc_top]->mp_p.p_pgno, _pCursor->mc_ki[_pCursor->mc_top]);

		if (_bMoveRight ? (_pCursor->mc_ki[_pCursor->mc_top] + 1u >= DNumKeys(_pCursor->mc_pg[_pCursor->mc_top])) : (_pCursor->mc_ki[_pCursor->mc_top] == 0))
		{
			DMibDatabaseTrace("no more keys left, moving to {} sibling", _bMoveRight ? "right" : "left");
			if ((rc = fg_CursorSibling(_pCursor, _bMoveRight)) != DMdbSuccess)
			{
				/* undo cursor_pop before returning */
				_pCursor->mc_top++;
				_pCursor->mc_snum++;
				return rc;
			}
		}
		else
		{
			if (_bMoveRight)
				_pCursor->mc_ki[_pCursor->mc_top]++;
			else
				_pCursor->mc_ki[_pCursor->mc_top]--;
			DMibDatabaseTrace("just moving to {} index key {}", _bMoveRight ? "right" : "left", _pCursor->mc_ki[_pCursor->mc_top]);
		}
		DMibCheck(DIsBranch(_pCursor->mc_pg[_pCursor->mc_top]));

		DMdbPageUnref(_pCursor->mc_txn, op);

		indx = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);
		if ((rc = fg_PageGet(_pCursor, DNodePgno(indx), &mp, nullptr)) != 0)
		{
			/* _pCursor will be inconsistent if caller does mc_snum++ as above */
			_pCursor->mc_flags &= ~(DCInitialized | DCEof);
			return rc;
		}

		fg_CursorPush(_pCursor, mp);
		if (!_bMoveRight)
			_pCursor->mc_ki[_pCursor->mc_top] = DNumKeys(mp) - 1;

		return DMdbSuccess;
	}

	/** Move the cursor to the next data item. */
	int fg_CursorNext(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation)
	{
		DMibFastCheck(_pKey);
		CMdbPage *mp;
		CMdbNode *leaf;
		int rc;

		if ((_pCursor->mc_flags & DCDel && _Operation == EDatabaseCursorGet_NextDup))
			return DMdbNotFound;

		if (!(_pCursor->mc_flags & DCInitialized))
			return fg_CursorFirst(_pCursor, _pKey, _pData);

		mp = _pCursor->mc_pg[_pCursor->mc_top];

		if (_pCursor->mc_flags & DCEof)
		{
			if (_pCursor->mc_ki[_pCursor->mc_top] >= DNumKeys(mp) - 1)
				return DMdbNotFound;
			_pCursor->mc_flags ^= DCEof;
		}

		if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates)
		{
			leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);
			if (DFIsSet(leaf->mn_flags, DFDupData))
			{
				if (_Operation == EDatabaseCursorGet_Next || _Operation == EDatabaseCursorGet_NextDup)
				{
					rc = fg_CursorNext(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr, EDatabaseCursorGet_Next);
					if (_Operation != EDatabaseCursorGet_Next || rc != DMdbNotFound)
					{
						if (rc == DMdbSuccess)
							DMdbGetKey(leaf, _pKey);
						return rc;
					}
				}
				else
					DMdbCursorUnref(&_pCursor->mc_xcursor->mx_cursor, 0);
			}
			else
			{
				_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~(DCInitialized | DCEof);
				if (_Operation == EDatabaseCursorGet_NextDup)
					return DMdbNotFound;
			}
		}

		DMibDatabaseTrace("cursor_next: top page is {} in cursor {}", fg_DbgPgno(mp), (void *)_pCursor);
		if (_pCursor->mc_flags & DCDel)
		{
			_pCursor->mc_flags ^= DCDel;
			goto skip;
		}

		if (_pCursor->mc_ki[_pCursor->mc_top] + 1u >= DNumKeys(mp))
		{
			DPuts("=====> move to next sibling page");
			if ((rc = fg_CursorSibling(_pCursor, 1)) != DMdbSuccess)
			{
				_pCursor->mc_flags |= DCEof;
				return rc;
			}
			mp = _pCursor->mc_pg[_pCursor->mc_top];
			DMibDatabaseTrace("next page is {}, key index {}", mp->mp_p.p_pgno, _pCursor->mc_ki[_pCursor->mc_top]);
		}
		else
			_pCursor->mc_ki[_pCursor->mc_top]++;

	skip:
		DMibDatabaseTrace("==> cursor points to page {} with {} keys, key index {}", fg_DbgPgno(mp), DNumKeys(mp), _pCursor->mc_ki[_pCursor->mc_top]);

		if (DIsLeaf2(mp))
		{
			_pKey->m_Size = _pCursor->mc_db->md_pad;
			_pKey->m_pData = DLeaf2Key(mp, _pCursor->mc_ki[_pCursor->mc_top], _pKey->m_Size);
			return DMdbSuccess;
		}

		DMibCheck(DIsLeaf(mp));
		leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);

		if (DFIsSet(leaf->mn_flags, DFDupData))
			fg_XCursorInit1(_pCursor, leaf);
		if (_pData)
		{
			if ((rc = fg_NodeRead(_pCursor, leaf, _pData)) != DMdbSuccess)
				return rc;

			if (DFIsSet(leaf->mn_flags, DFDupData))
			{
				rc = fg_CursorFirst(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr);
				if (rc != DMdbSuccess)
					return rc;
			}
		}

		DMdbGetKey(leaf, _pKey);
		return DMdbSuccess;
	}

	/** Move the cursor to the previous data item. */
	int fg_CursorPrev(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation)
	{
		DMibFastCheck(_pKey);

		CMdbPage *mp;
		CMdbNode *leaf;
		int rc;

		if (!(_pCursor->mc_flags & DCInitialized))
		{
			rc = fg_CursorLast(_pCursor, _pKey, _pData);
			if (rc)
				return rc;
			_pCursor->mc_ki[_pCursor->mc_top]++;
		}

		mp = _pCursor->mc_pg[_pCursor->mc_top];

		if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates)
		{
			leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);
			if (DFIsSet(leaf->mn_flags, DFDupData))
			{
				if (_Operation == EDatabaseCursorGet_Prev || _Operation == EDatabaseCursorGet_PrevDup)
				{
					rc = fg_CursorPrev(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr, EDatabaseCursorGet_Prev);
					if (_Operation != EDatabaseCursorGet_Prev || rc != DMdbNotFound)
					{
						if (rc == DMdbSuccess)
						{
							DMdbGetKey(leaf, _pKey);
							_pCursor->mc_flags &= ~DCEof;
						}
						return rc;
					}
				}
				else
					DMdbCursorUnref(&_pCursor->mc_xcursor->mx_cursor, 0);
			}
			else
			{
				_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~(DCInitialized | DCEof);
				if (_Operation == EDatabaseCursorGet_PrevDup)
					return DMdbNotFound;
			}
		}

		DMibDatabaseTrace("cursor_prev: top page is {} in cursor {}", fg_DbgPgno(mp), (void *)_pCursor);

		_pCursor->mc_flags &= ~(DCEof | DCDel);

		if (_pCursor->mc_ki[_pCursor->mc_top] == 0)
		{
			DPuts("=====> move to prev sibling page");
			if ((rc = fg_CursorSibling(_pCursor, 0)) != DMdbSuccess)
				return rc;
			mp = _pCursor->mc_pg[_pCursor->mc_top];
			_pCursor->mc_ki[_pCursor->mc_top] = DNumKeys(mp) - 1;
			DMibDatabaseTrace("prev page is {}, key index {}", mp->mp_p.p_pgno, _pCursor->mc_ki[_pCursor->mc_top]);
		}
		else
			_pCursor->mc_ki[_pCursor->mc_top]--;

		DMibDatabaseTrace("==> cursor points to page {} with {} keys, key index {}", fg_DbgPgno(mp), DNumKeys(mp), _pCursor->mc_ki[_pCursor->mc_top]);

		if (DIsLeaf2(mp))
		{
			_pKey->m_Size = _pCursor->mc_db->md_pad;
			_pKey->m_pData = DLeaf2Key(mp, _pCursor->mc_ki[_pCursor->mc_top], _pKey->m_Size);
			return DMdbSuccess;
		}

		DMibCheck(DIsLeaf(mp));
		leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);

		if (DFIsSet(leaf->mn_flags, DFDupData))
			fg_XCursorInit1(_pCursor, leaf);

		if (_pData)
		{
			if ((rc = fg_NodeRead(_pCursor, leaf, _pData)) != DMdbSuccess)
				return rc;

			if (DFIsSet(leaf->mn_flags, DFDupData))
			{
				rc = fg_CursorLast(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr);
				if (rc != DMdbSuccess)
					return rc;
			}
		}

		DMdbGetKey(leaf, _pKey);
		return DMdbSuccess;
	}

	/** Set the cursor on a specific data item. */
	int fg_CursorSet(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData, EDatabaseCursorGet _Operation, int *o_pExact)
	{
		int rc;
		CMdbPage *mp;
		CMdbNode *leaf = nullptr;
		DKBuf;

		if (_pKey->m_Size == 0)
			return DMdbBadValsize;

		if (_pCursor->mc_xcursor)
		{
			DMdbCursorUnref(&_pCursor->mc_xcursor->mx_cursor, 0);
			_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~(DCInitialized | DCEof);
		}

		/* See if we're already on the right page */
		if (_pCursor->mc_flags & DCInitialized)
		{
			CDatabaseValue nodekey;

			mp = _pCursor->mc_pg[_pCursor->mc_top];
			if (!DNumKeys(mp))
			{
				_pCursor->mc_ki[_pCursor->mc_top] = 0;
				return DMdbNotFound;
			}
			if (mp->mp_flags & DPLeaf2)
			{
				nodekey.m_Size = _pCursor->mc_db->md_pad;
				nodekey.m_pData = DLeaf2Key(mp, 0, nodekey.m_Size);
			}
			else
			{
				leaf = DNodePtr(mp, 0);
				DMdbGetKey2(leaf, nodekey);
			}
			rc = _pCursor->mc_dbx->m_fKeyCompare(*_pKey, nodekey);
			if (rc == 0)
			{
				/* Probably happens rarely, but first node on the page
				 * was the one we wanted.
				 */
				_pCursor->mc_ki[_pCursor->mc_top] = 0;
				if (o_pExact)
					*o_pExact = 1;
				goto set1;
			}
			if (rc > 0)
			{
				uint32 i;
				uint32 nkeys = DNumKeys(mp);
				if (nkeys > 1)
				{
					if (mp->mp_flags & DPLeaf2)
						nodekey.m_pData = DLeaf2Key(mp, nkeys - 1, nodekey.m_Size);
					else
					{
						leaf = DNodePtr(mp, nkeys - 1);
						DMdbGetKey2(leaf, nodekey);
					}
					rc = _pCursor->mc_dbx->m_fKeyCompare(*_pKey, nodekey);
					if (rc == 0)
					{
						/* last node was the one we wanted */
						_pCursor->mc_ki[_pCursor->mc_top] = nkeys - 1;
						if (o_pExact)
							*o_pExact = 1;
						goto set1;
					}
					if (rc < 0)
					{
						if (_pCursor->mc_ki[_pCursor->mc_top] < DNumKeys(mp))
						{
							/* This is definitely the right page, skip search_page */
							if (mp->mp_flags & DPLeaf2)
								nodekey.m_pData = DLeaf2Key(mp, _pCursor->mc_ki[_pCursor->mc_top], nodekey.m_Size);
							else
							{
								leaf = DNodePtr(mp, _pCursor->mc_ki[_pCursor->mc_top]);
								DMdbGetKey2(leaf, nodekey);
							}
							rc = _pCursor->mc_dbx->m_fKeyCompare(*_pKey, nodekey);
							if (rc == 0)
							{
								/* current node was the one we wanted */
								if (o_pExact)
									*o_pExact = 1;
								goto set1;
							}
						}
						rc = 0;
						_pCursor->mc_flags &= ~DCEof;
						goto set2;
					}
				}
				/* If any parents have right-sibs, search.
				 * Otherwise, there's nothing further.
				 */
				for (i = 0; i < _pCursor->mc_top; i++)
				{
					if (_pCursor->mc_ki[i] < DNumKeys(_pCursor->mc_pg[i]) - 1)
						break;
				}
				if (i == _pCursor->mc_top)
				{
					/* There are no other pages */
					_pCursor->mc_ki[_pCursor->mc_top] = nkeys;
					return DMdbNotFound;
				}
			}
			if (!_pCursor->mc_top)
			{
				/* There are no other pages */
				_pCursor->mc_ki[_pCursor->mc_top] = 0;
				if (_Operation == EDatabaseCursorGet_SetRange && !o_pExact)
				{
					rc = 0;
					goto set1;
				}
				else
					return DMdbNotFound;
			}
		}
		else
			_pCursor->mc_pg[0] = 0;

		rc = fg_PageSearch(_pCursor, _pKey, 0);
		if (rc != DMdbSuccess)
			return rc;

		mp = _pCursor->mc_pg[_pCursor->mc_top];
		DMibCheck(DIsLeaf(mp));

	set2:
		leaf = fg_NodeSearch(_pCursor, _pKey, o_pExact);
		if (o_pExact != nullptr && !*o_pExact)
		{
			/* EDatabaseCursorGet_Set specified and not an exact match. */
			return DMdbNotFound;
		}

		if (leaf == nullptr)
		{
			DPuts("===> inexact leaf not found, goto sibling");
			if ((rc = fg_CursorSibling(_pCursor, 1)) != DMdbSuccess)
			{
				_pCursor->mc_flags |= DCEof;
				return rc; // no entries matched
			}
			mp = _pCursor->mc_pg[_pCursor->mc_top];
			DMibCheck(DIsLeaf(mp));
			leaf = DNodePtr(mp, 0);
		}

	set1:
		_pCursor->mc_flags |= DCInitialized;
		_pCursor->mc_flags &= ~DCEof;

		if (DIsLeaf2(mp))
		{
			if (_Operation == EDatabaseCursorGet_SetRange || _Operation == EDatabaseCursorGet_SetKey)
			{
				_pKey->m_Size = _pCursor->mc_db->md_pad;
				_pKey->m_pData = DLeaf2Key(mp, _pCursor->mc_ki[_pCursor->mc_top], _pKey->m_Size);
			}
			return DMdbSuccess;
		}

		DMibFastCheck(leaf);
		if (DFIsSet(leaf->mn_flags, DFDupData))
			fg_XCursorInit1(_pCursor, leaf);
		if (_pData)
		{
			if (DFIsSet(leaf->mn_flags, DFDupData))
			{
				if (_Operation == EDatabaseCursorGet_Set || _Operation == EDatabaseCursorGet_SetKey || _Operation == EDatabaseCursorGet_SetRange)
					rc = fg_CursorFirst(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr);
				else
				{
					int ex2, *ex2p;
					if (_Operation == EDatabaseCursorGet_GetBoth)
					{
						ex2p = &ex2;
						ex2 = 0;
					}
					else
						ex2p = nullptr;
					rc = fg_CursorSet(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr, EDatabaseCursorGet_SetRange, ex2p);
					if (rc != DMdbSuccess)
						return rc;
				}
			}
			else if (_Operation == EDatabaseCursorGet_GetBoth || _Operation == EDatabaseCursorGet_GetBothRange)
			{
				CDatabaseValue olddata;
				FDatabaseCompare *pDataCompare = nullptr;
				if ((rc = fg_NodeRead(_pCursor, leaf, &olddata)) != DMdbSuccess)
					return rc;
				pDataCompare = &(_pCursor->mc_dbx->m_fDataCompare);
				if (DNeedCmpClong(*pDataCompare, olddata.m_Size))
					pDataCompare = &g_mdb_cmp_clong;
				rc = (*pDataCompare)(*_pData, olddata);
				if (rc)
				{
					if (_Operation == EDatabaseCursorGet_GetBoth || rc > 0)
						return DMdbNotFound;
					rc = 0;
				}
				*_pData = olddata;
			}
			else
			{
				if (_pCursor->mc_xcursor)
					_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~(DCInitialized | DCEof);
				if ((rc = fg_NodeRead(_pCursor, leaf, _pData)) != DMdbSuccess)
					return rc;
			}
		}

		/* The key already matches in all other cases */
		if (_Operation == EDatabaseCursorGet_SetRange || _Operation == EDatabaseCursorGet_SetKey)
			DMdbGetKey(leaf, _pKey);
		DMibDatabaseTrace("==> cursor placed on key [{}]", DKey(_pKey));

		return rc;
	}

	/** Move the cursor to the first item in the database. */
	int fg_CursorFirst(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData)
	{
		int rc;
		CMdbNode *leaf;

		if (_pCursor->mc_xcursor)
		{
			DMdbCursorUnref(&_pCursor->mc_xcursor->mx_cursor, 0);
			_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~(DCInitialized | DCEof);
		}

		if (!(_pCursor->mc_flags & DCInitialized) || _pCursor->mc_top)
		{
			rc = fg_PageSearch(_pCursor, nullptr, DMdbPsFirst);
			if (rc != DMdbSuccess)
				return rc;
		}
		DMibCheck(DIsLeaf(_pCursor->mc_pg[_pCursor->mc_top]));

		leaf = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], 0);
		_pCursor->mc_flags |= DCInitialized;
		_pCursor->mc_flags &= ~DCEof;

		_pCursor->mc_ki[_pCursor->mc_top] = 0;

		if (DIsLeaf2(_pCursor->mc_pg[_pCursor->mc_top]))
		{
			_pKey->m_Size = _pCursor->mc_db->md_pad;
			_pKey->m_pData = DLeaf2Key(_pCursor->mc_pg[_pCursor->mc_top], 0, _pKey->m_Size);
			return DMdbSuccess;
		}

		if (_pData)
		{
			if (DFIsSet(leaf->mn_flags, DFDupData))
			{
				fg_XCursorInit1(_pCursor, leaf);
				rc = fg_CursorFirst(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr);
				if (rc)
					return rc;
			}
			else
			{
				if ((rc = fg_NodeRead(_pCursor, leaf, _pData)) != DMdbSuccess)
					return rc;
			}
		}
		DMdbGetKey(leaf, _pKey);
		return DMdbSuccess;
	}

	/** Move the cursor to the last item in the database. */
	int fg_CursorLast(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData)
	{
		int rc;
		CMdbNode *leaf;

		if (_pCursor->mc_xcursor)
		{
			DMdbCursorUnref(&_pCursor->mc_xcursor->mx_cursor, 0);
			_pCursor->mc_xcursor->mx_cursor.mc_flags &= ~(DCInitialized | DCEof);
		}

		if (!(_pCursor->mc_flags & DCInitialized) || _pCursor->mc_top)
		{
			rc = fg_PageSearch(_pCursor, nullptr, DMdbPsLast);
			if (rc != DMdbSuccess)
				return rc;
		}
		DMibCheck(DIsLeaf(_pCursor->mc_pg[_pCursor->mc_top]));

		_pCursor->mc_ki[_pCursor->mc_top] = DNumKeys(_pCursor->mc_pg[_pCursor->mc_top]) - 1;
		_pCursor->mc_flags |= DCInitialized | DCEof;
		leaf = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);

		if (DIsLeaf2(_pCursor->mc_pg[_pCursor->mc_top]))
		{
			_pKey->m_Size = _pCursor->mc_db->md_pad;
			_pKey->m_pData = DLeaf2Key(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top], _pKey->m_Size);
			return DMdbSuccess;
		}

		if (_pData)
		{
			if (DFIsSet(leaf->mn_flags, DFDupData))
			{
				fg_XCursorInit1(_pCursor, leaf);
				rc = fg_CursorLast(&_pCursor->mc_xcursor->mx_cursor, _pData, nullptr);
				if (rc)
					return rc;
			}
			else
			{
				if ((rc = fg_NodeRead(_pCursor, leaf, _pData)) != DMdbSuccess)
					return rc;
			}
		}

		DMdbGetKey(leaf, _pKey);
		return DMdbSuccess;
	}

	/** Touch all the pages in the cursor stack. Set mc_top.
	 *	Makes sure all the pages are writable, before attempting a write operation.
	 * @param[in] _pCursor The cursor to operate on.
	 */
	int fg_CursorTouch(CMdbCursor *_pCursor)
	{
		int rc = DMdbSuccess;

		if (_pCursor->mc_dbi >= DCoreDbs && !(*_pCursor->mc_dbflag & (DDbDirty | DDbDupData)))
		{
			/* Touch DB record of named DB */
			CMdbCursor mc2;
			CMdbXCursor mcx;
			if (DTxnDbiChanged(_pCursor->mc_txn, _pCursor->mc_dbi))
				DMibError("Stale write transaction");
			fg_CursorInit(&mc2, _pCursor->mc_txn, DMainDbi, &mcx);
			rc = fg_PageSearch(&mc2, &_pCursor->mc_dbx->m_DatabaseName, DMdbPsModify);
			if (rc)
				return rc;
			*_pCursor->mc_dbflag |= DDbDirty;
		}
		_pCursor->mc_top = 0;
		if (_pCursor->mc_snum)
		{
			do
			{
				rc = fg_PageTouch(_pCursor);
			}
			while (!rc && ++(_pCursor->mc_top) < _pCursor->mc_snum);
			_pCursor->mc_top = _pCursor->mc_snum - 1;
		}
		return rc;
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
		CMdbXCursor *mx = _pCursor->mc_xcursor;

		mx->mx_cursor.mc_xcursor = nullptr;
		mx->mx_cursor.mc_txn = _pCursor->mc_txn;
		mx->mx_cursor.mc_db = &mx->mx_db;
		mx->mx_cursor.mc_dbx = &mx->mx_dbx;
		mx->mx_cursor.mc_dbi = _pCursor->mc_dbi;
		mx->mx_cursor.mc_dbflag = &mx->mx_dbflag;
		mx->mx_cursor.mc_snum = 0;
		mx->mx_cursor.mc_top = 0;
		DMcSetOvpg(&mx->mx_cursor, NULL);
		mx->mx_cursor.mc_flags = DCSub | (_pCursor->mc_flags & (DCOrigReadOnly | DCWriteMap));
		mx->mx_dbx.m_DatabaseName.m_Size = 0;
		mx->mx_dbx.m_DatabaseName.m_pData = nullptr;
		mx->mx_dbx.m_fKeyCompare = _pCursor->mc_dbx->m_fDataCompare;
		mx->mx_dbx.m_fDataCompare = nullptr;
	}

	/** Final setup of a sorted-dups cursor.
	 *	Sets up the fields that depend on the data from the main cursor.
	 * @param[in] _pCursor The main cursor whose sorted-dups cursor is to be initialized.
	 * @param[in] _pNode The data containing the #CMdbDb record for the
	 * sorted-dup database.
	 */
	void fg_XCursorInit1(CMdbCursor *_pCursor, CMdbNode *_pNode)
	{
		CMdbXCursor *mx = _pCursor->mc_xcursor;

		mx->mx_cursor.mc_flags &= DCSub | DCOrigReadOnly | DCWriteMap;
		if (_pNode->mn_flags & DFSubData)
		{
			memcpy(&mx->mx_db, DNodeData(_pNode), sizeof(CMdbDb));
			mx->mx_cursor.mc_pg[0] = 0;
			mx->mx_cursor.mc_snum = 0;
			mx->mx_cursor.mc_top = 0;
		}
		else
		{
			CMdbPage *fp = (CMdbPage *)DNodeData(_pNode);
			mx->mx_db.md_pad = 0;
			mx->mx_db.md_flags = 0;
			mx->mx_db.md_depth = 1;
			mx->mx_db.md_branch_pages = 0;
			mx->mx_db.md_leaf_pages = 1;
			mx->mx_db.md_overflow_pages = 0;
			mx->mx_db.md_entries = DNumKeys(fp);
			DCopyPgno(mx->mx_db.md_root, fp->mp_p.p_pgno);
			mx->mx_cursor.mc_snum = 1;
			mx->mx_cursor.mc_top = 0;
			mx->mx_cursor.mc_flags |= DCInitialized;
			mx->mx_cursor.mc_pg[0] = fp;
			mx->mx_cursor.mc_ki[0] = 0;
			if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)
			{
				mx->mx_db.md_flags = EDatabaseSubFlag_SortedDuplicatesFixedSize;
				mx->mx_db.md_pad = fp->mp_pad;
				if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
					mx->mx_db.md_flags |= EDatabaseSubFlag_IntegerKeys;
			}
		}
		DMibDatabaseTrace("Sub-db -{} root page {}", mx->mx_cursor.mc_dbi, mx->mx_db.md_root);
		mx->mx_dbflag = DDbValid | DDbUserValid | DDbDupData;
		if (DNeedCmpClong(mx->mx_dbx.m_fKeyCompare, mx->mx_db.md_pad))
			mx->mx_dbx.m_fKeyCompare = g_mdb_cmp_clong;
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
		CMdbXCursor *mx = _pCursor->mc_xcursor;

		if (_bNewDuplicateData)
		{
			mx->mx_cursor.mc_snum = 1;
			mx->mx_cursor.mc_top = 0;
			mx->mx_cursor.mc_flags |= DCInitialized;
			mx->mx_cursor.mc_ki[0] = 0;
			mx->mx_dbflag = DDbValid | DDbUserValid | DDbDupData;
#if DMibPPtrBits < DMibDatabase_IDBits	/* matches fg_XCursorInit1:DNeedCmpClong() */
			mx->mx_dbx.m_fKeyCompare = _pSourceXCursor->mx_dbx.m_fKeyCompare;
#endif
		}
		else if (!(mx->mx_cursor.mc_flags & DCInitialized))
			return;
		mx->mx_db = _pSourceXCursor->mx_db;
		mx->mx_cursor.mc_pg[0] = _pSourceXCursor->mx_cursor.mc_pg[0];
		DMibDatabaseTrace("Sub-db -{} root page {}", mx->mx_cursor.mc_dbi, mx->mx_db.md_root);
	}

	/** Initialize a cursor for a given transaction and database. */
	void fg_CursorInit(CMdbCursor *_pCursor, CMdbTxn *_pTransaction, CDatabaseSubID _DatabaseID, CMdbXCursor *_pXCursor)
	{
		_pCursor->mc_next = nullptr;
		_pCursor->mc_backup = nullptr;
		_pCursor->mc_dbi = _DatabaseID;
		_pCursor->mc_txn = _pTransaction;
		_pCursor->mc_db = &_pTransaction->mt_dbs[_DatabaseID];
		_pCursor->mc_dbx = &_pTransaction->mt_dbxs[_DatabaseID];
		_pCursor->mc_dbflag = &_pTransaction->mt_dbflags[_DatabaseID];
		_pCursor->mc_snum = 0;
		_pCursor->mc_top = 0;
		_pCursor->mc_pg[0] = 0;
		_pCursor->mc_ki[0] = 0;
		DMcSetOvpg(_pCursor, NULL);
		_pCursor->mc_flags = _pTransaction->mt_flags & (DCOrigReadOnly | DCWriteMap);
		if (_pTransaction->mt_dbs[_DatabaseID].md_flags & EDatabaseSubFlag_SortedDuplicates)
		{
			DMibCheck(_pXCursor != nullptr);
			_pCursor->mc_xcursor = _pXCursor;
			fg_XCursorInit0(_pCursor);
		}
		else
			_pCursor->mc_xcursor = nullptr;
		if (*_pCursor->mc_dbflag & DDbStale)
			fg_PageSearch(_pCursor, nullptr, DMdbPsRootonly);
	}

	/** Copy the contents of a cursor.
	 * @param[in] _pSourceCursor The cursor to copy from.
	 * @param[out] _pDestinationCursor The cursor to copy to.
	 */
	void fg_CursorCopy(CMdbCursor const *_pSourceCursor, CMdbCursor *_pDestinationCursor)
	{
		uint32 i;

		_pDestinationCursor->mc_txn = _pSourceCursor->mc_txn;
		_pDestinationCursor->mc_dbi = _pSourceCursor->mc_dbi;
		_pDestinationCursor->mc_db = _pSourceCursor->mc_db;
		_pDestinationCursor->mc_dbx = _pSourceCursor->mc_dbx;
		_pDestinationCursor->mc_snum = _pSourceCursor->mc_snum;
		_pDestinationCursor->mc_top = _pSourceCursor->mc_top;
		_pDestinationCursor->mc_flags = _pSourceCursor->mc_flags;
		DMcSetOvpg(_pDestinationCursor, DMcOvpg(_pSourceCursor));

		for (i = 0; i < _pSourceCursor->mc_snum; i++)
		{
			_pDestinationCursor->mc_pg[i] = _pSourceCursor->mc_pg[i];
			_pDestinationCursor->mc_ki[i] = _pSourceCursor->mc_ki[i];
		}
	}

	/** Complete a delete operation started by #fg_CursorDel(). */
	int fg_CursorDel0(CMdbCursor *_pCursor)
	{
		int rc;
		CMdbPage *mp;
		CIndx ki;
		uint32 nkeys;
		CMdbCursor *m2, *m3;
		CDatabaseSubID dbi = _pCursor->mc_dbi;

		ki = _pCursor->mc_ki[_pCursor->mc_top];
		mp = _pCursor->mc_pg[_pCursor->mc_top];
		fg_NodeDel(_pCursor, _pCursor->mc_db->md_pad);
		_pCursor->mc_db->md_entries--;
		{
			/* Adjust other cursors pointing to mp */
			for (m2 = _pCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
			{
				m3 = (_pCursor->mc_flags & DCSub) ? &m2->mc_xcursor->mx_cursor : m2;
				if (!(m2->mc_flags & m3->mc_flags & DCInitialized))
					continue;
				if (m3 == _pCursor || m3->mc_snum < _pCursor->mc_snum)
					continue;
				if (m3->mc_pg[_pCursor->mc_top] == mp)
				{
					if (m3->mc_ki[_pCursor->mc_top] == ki)
					{
						m3->mc_flags |= DCDel;
						if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates)
						{
							/* Sub-cursor referred into dataset which is gone */
							m3->mc_xcursor->mx_cursor.mc_flags &= ~(DCInitialized | DCEof);
						}
						continue;
					}
					else if (m3->mc_ki[_pCursor->mc_top] > ki)
						m3->mc_ki[_pCursor->mc_top]--;
					DXCursorRefresh(m3, _pCursor->mc_top, mp);
				}
			}
		}
		rc = fg_Rebalance(_pCursor);

		if (rc == DMdbSuccess)
		{
			/* DB is totally empty now, just bail out.
			 * Other cursors adjustments were already done
			 * by fg_Rebalance and aren't needed here.
			 */
			if (!_pCursor->mc_snum)
				return rc;

			mp = _pCursor->mc_pg[_pCursor->mc_top];
			nkeys = DNumKeys(mp);

			/* Adjust other cursors pointing to mp */
			for (m2 = _pCursor->mc_txn->mt_cursors[dbi]; !rc && m2; m2 = m2->mc_next)
			{
				m3 = (_pCursor->mc_flags & DCSub) ? &m2->mc_xcursor->mx_cursor : m2;
				if (!(m2->mc_flags & m3->mc_flags & DCInitialized))
					continue;
				if (m3->mc_snum < _pCursor->mc_snum)
					continue;
				if (m3->mc_pg[_pCursor->mc_top] == mp)
				{
					/* if m3 points past last node in page, find next sibling */
					if (m3->mc_ki[_pCursor->mc_top] >= _pCursor->mc_ki[_pCursor->mc_top])
					{
						if (m3->mc_ki[_pCursor->mc_top] >= nkeys)
						{
							rc = fg_CursorSibling(m3, 1);
							if (rc == DMdbNotFound)
							{
								m3->mc_flags |= DCEof;
								rc = DMdbSuccess;
								continue;
							}
						}
						if (_pCursor->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates)
						{
							CMdbNode *node = DNodePtr(m3->mc_pg[m3->mc_top], m3->mc_ki[m3->mc_top]);
							/* If this node has dupdata, it may need to be reinited
							 * because its data has moved.
							 * If the xcursor was not initd it must be reinited.
							 * Else if node points to a subDB, nothing is needed.
							 * Else (xcursor was initd, not a subDB) needs mc_pg[0] reset.
							 */
							if (node->mn_flags & DFDupData)
							{
								if (m3->mc_xcursor->mx_cursor.mc_flags & DCInitialized)
								{
									if (!(node->mn_flags & DFSubData))
										m3->mc_xcursor->mx_cursor.mc_pg[0] = (CMdbPage *)DNodeData(node);
								}
								else
								{
									fg_XCursorInit1(m3, node);
									m3->mc_xcursor->mx_cursor.mc_flags |= DCDel;
								}
							}
						}
					}
				}
			}
			_pCursor->mc_flags |= DCDel;
		}

		if (rc)
			_pCursor->mc_txn->mt_flags |= DMdbTxnError;
		return rc;
	}
}
