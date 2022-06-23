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
	CDatabaseCursorCommon::CDatabaseCursorCommon() = default;

	CDatabaseCursorCommon::~CDatabaseCursorCommon()
	{
		if (mp_pInternal)
			NPrivate::mdb_cursor_close(mp_pInternal.f_Detach());
	}

	CDatabaseCursorCommon::CDatabaseCursorCommon(CDatabaseCursorCommon &&_Other) = default;

	CDatabaseCursorCommon &CDatabaseCursorCommon::operator =(CDatabaseCursorCommon &&_Other) = default;

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

	void CDatabaseCursorCommon::CInternal::f_Put(CDatabaseValue *key, CDatabaseValue *data, EDatabaseWriteFlag _WriteFlags)
	{
		DMibRequire(key);
		DMibRequire(data);
		DMibRequire(!mc_txn->mt_env->f_IsInsideMemoryMap(key->m_pData) && !mc_txn->mt_env->f_IsInsideMemoryMap(data->m_pData))
			("You cannot use a value from the database in a database write. The database can change the value while writing, corrupting the database.")
		;

		using namespace NPrivate;

		if (_WriteFlags & ~(EDatabaseWriteFlag_NoOverwrite|EDatabaseWriteFlag_NoDuplicateData|EDatabaseWriteFlag_ReserveSpace|EDatabaseWriteFlag_Append|EDatabaseWriteFlag_AppendDuplicateData|EDatabaseWriteFlag_OverwriteAtCurrentCursorPos))
			DMibError("Invalid flags");

		if (mc_txn->mt_flags & (MDB_TXN_RDONLY))
			DMibError("Trying to write to read only transaction");

		int rc = mdb_cursor_put(this, key, data, _WriteFlags);
		if (rc)
			DMibError(NStr::fg_Format("Error putting data: {}", mdb_strerror(rc)));
	}

	void CDatabaseCursorCommon::CInternal::f_Delete(EDatabaseWriteFlag _WriteFlags)
	{
		using namespace NPrivate;

		if (_WriteFlags & ~(EDatabaseWriteFlag_NoDuplicateData))
			DMibError("Invalid flags");

		if (mc_txn->mt_flags & (MDB_TXN_RDONLY))
			DMibError("Trying to delete in read only transaction");

		int rc = mdb_cursor_del(this, _WriteFlags);
		if (rc)
			DMibError(NStr::fg_Format("Error deleting data: {}", mdb_strerror(rc)));
	}

	CDatabaseOffset CDatabaseCursorCommon::CInternal::f_CountDuplicates()
	{
		using namespace NPrivate;

		CDatabaseOffset Ret;
		int rc = mdb_cursor_count(this, &Ret);
		if (rc)
			DMibError(NStr::fg_Format("Error counting duplicates: {}", mdb_strerror(rc)));
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

	CDatabaseValue CDatabaseCursorWrite::f_SetValueInplace(mint _DataSize, bool _bAppend)
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

	CDatabaseValue CDatabaseCursorWrite::f_InsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend)
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

	CDatabaseValue CDatabaseCursorWrite::f_UpsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend)
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

	CDatabaseCursorCommon::operator bool() const
	{
		return mp_bValid;
	}

	CDatabaseValue const &CDatabaseCursorCommon::f_Key() const
	{
		return mp_Key;
	}

	void CDatabaseCursorCommon::fp_SetToCurrent()
	{
		fp_CheckValidityIgnoringPrefix(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_GET_CURRENT), "Error setting cursor to current: {}");
	}

	bool CDatabaseCursorCommon::fp_FindLowerBoundIgnoringPrefix(CDatabaseValue const &_Key)
	{
		mp_Key = _Key;

		return fp_CheckValidityIgnoringPrefix(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_SET_RANGE), "Error setting cursor to lower bound: {}");
	}

	bool CDatabaseCursorCommon::fp_CheckValidityIgnoringPrefix(int _Result, ch8 const *_pErrorFormat)
	{
		if (_Result == MDB_NOTFOUND)
			mp_bValid = false;
		else if (_Result)
			DMibError(NStr::fg_Format(_pErrorFormat, NPrivate::mdb_strerror(_Result)));
		else
			mp_bValid = true;
		return mp_bValid;
	}

	bool CDatabaseCursorCommon::fp_CheckValidity(int _Result, ch8 const *_pErrorFormat)
	{
		if (_Result == MDB_NOTFOUND)
			mp_bValid = false;
		else if (_Result)
			DMibError(NStr::fg_Format(_pErrorFormat, NPrivate::mdb_strerror(_Result)));
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

		return fp_CheckValidityIgnoringPrefix(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_FIRST), "Error setting cursor to first: {}");
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
				int Result = NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_LAST);
				if (Result == MDB_NOTFOUND)
				{
					mp_bValid = false;
					return false;
				}
				else if (Result)
					DMibError(NStr::fg_Format("Error setting cursor to last: {}", NPrivate::mdb_strerror(Result)));
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

		return fp_CheckValidityIgnoringPrefix(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_LAST), "Error setting cursor to last: {}");
	}

	bool CDatabaseCursorCommon::f_FindEqual(CDatabaseValue const &_Key) // MDB_SET, MDB_SET_KEY
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;

		return fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_SET_KEY), "Error setting cursor to key: {}");
	}

	bool CDatabaseCursorCommon::f_FindLowerBound(CDatabaseValue const &_Key) // MDB_SET_RANGE
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;

		return fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_SET_RANGE), "Error setting cursor to lower bound: {}");
	}

	void CDatabaseCursorCommon::operator ++()
	{
		fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_NEXT_NODUP), "Error setting cursor to next: {}");
	}

	void CDatabaseCursorCommon::operator --()
	{
		fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_PREV_NODUP), "Error setting cursor to previous: {}");
	}

	CDatabaseOffset CDatabaseCursorDuplicateData::f_DataCount()
	{
		auto &Cursor = *mp_pInternal;
		return Cursor.f_CountDuplicates();
	}

	bool CDatabaseCursorDuplicateData::f_NextData()
	{
		return fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_NEXT_DUP), "Error setting cursor to next data: {}");
	}

	bool CDatabaseCursorDuplicateData::f_PrevData()
	{
		return fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_PREV_DUP), "Error setting cursor to prev data: {}");
	}

	bool CDatabaseCursorDuplicateData::fp_FindLowerBoundIgnoringPrefix(CDatabaseValue const &_Key, CDatabaseValue const &_Value)
	{
		mp_Key = _Key;
		mp_Value = _Value;

		return fp_CheckValidityIgnoringPrefix
			(
				NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_GET_BOTH_RANGE)
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

		return fp_CheckValidityIgnoringPrefix(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_FIRST_DUP), "Error setting cursor to first data: {}");
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
				int Result = NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_LAST_DUP);
				if (Result == MDB_NOTFOUND)
				{
					mp_bValid = false;
					return false;
				}
				else if (Result)
					DMibError(NStr::fg_Format("Error setting cursor to last: {}", NPrivate::mdb_strerror(Result)));
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

		return fp_CheckValidityIgnoringPrefix(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_LAST_DUP), "Error setting cursor to last data: {}");
	}

	bool CDatabaseCursorDuplicateData::f_FindEqual(CDatabaseValue const &_Key, CDatabaseValue const &_Data)
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;
		mp_Value = _Data;

		return fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_GET_BOTH), "Error setting cursor to key and data: {}");
	}

	bool CDatabaseCursorDuplicateData::f_FindLowerBound(CDatabaseValue const &_Key, CDatabaseValue const &_Data)
	{
		DMibRequire(!mp_Prefix.f_IsValid() || _Key.f_HasPrefix(mp_Prefix));

		mp_Key = _Key;
		mp_Value = _Data;

		return fp_CheckValidity(NPrivate::mdb_cursor_get(mp_pInternal.f_Get(), &mp_Key, &mp_Value, NPrivate::MDB_GET_BOTH_RANGE), "Error setting cursor to lower bound key and data: {}");
	}

	/** @brief Create a cursor handle.
	 *
	 * A cursor is associated with a specific transaction and database.
	 * A cursor cannot be used when its database handle is closed.  Nor
	 * when its transaction has ended.
	 * It can be discarded with #mdb_cursor_close().
	 * A cursor in a write-transaction can be closed before its transaction
	 * ends, and will otherwise be closed when its transaction ends.
	 * A cursor in a read-only transaction must be closed explicitly, before
	 * or after its transaction ends.
	 * @note Earlier documentation said that cursors in every transaction
	 * were closed when the transaction committed or aborted.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[out] cursor Address where the new #MDB_cursor handle will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	NPrivate::MDB_cursor *CDatabaseTransactionCommon::CInternal::f_Cursor(CDatabaseSubID dbi)
	{
		using namespace NPrivate;

		MDB_cursor	*mc;
		mint size = sizeof(MDB_cursor);

		if (!TXN_DBI_EXIST(this, dbi, DB_VALID))
			DMibError("Database does not exists");

		f_CheckBlocked();

		if (dbi == FREE_DBI && !F_ISSET(mt_flags, MDB_TXN_RDONLY))
			DMibError("Cannot open free database as read/write");

		if (mt_dbs[dbi].md_flags & EDatabaseSubFlag_SortedDuplicates)
			size += sizeof(MDB_xcursor);

		mc = (MDB_cursor *)NMemory::fg_Alloc(size);
		auto Cleanup = g_OnScopeExit / [&]()
			{
				mdb_cursor_close(mc);
			}
		;
		mdb_cursor_init(mc, this, dbi, (MDB_xcursor *)(mc + 1));
		if (mt_cursors)
		{
			mc->mc_next = mt_cursors[dbi];
			mt_cursors[dbi] = mc;
			mc->mc_flags |= C_UNTRACK;
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
	 * @param[in] cursor A cursor handle returned by #f_Cursor()
	 */
	void mdb_cursor_close(MDB_cursor *mc)
	{
		if (mc) {
			MDB_CURSOR_UNREF(mc, 0);
		}
		if (mc && !mc->mc_backup) {
			/* Remove from txn, if tracked.
			 * A read-only txn (!C_UNTRACK) may have been freed already,
			 * so do not peek inside it.  Only write txns track cursors.
			 */
			if ((mc->mc_flags & C_UNTRACK) && mc->mc_txn->mt_cursors) {
				MDB_cursor **prev = &mc->mc_txn->mt_cursors[mc->mc_dbi];
				while (*prev && *prev != mc) prev = &(*prev)->mc_next;
				if (*prev == mc)
					*prev = mc->mc_next;
			}
			mint Size = sizeof(MDB_cursor);
			if (mc->mc_xcursor)
				Size += sizeof(MDB_xcursor);

			NMemory::fg_Free(mc, Size);
		}
	}

	/** @brief Retrieve by cursor.
	 *
	 * This function retrieves key/data pairs from the database. The address and length
	 * of the key are returned in the object to which \b key refers (except for the
	 * case of the #MDB_SET option, in which the \b key object is unchanged), and
	 * the address and length of the data are returned in the object to which \b data
	 * refers.
	 * See #f_Get() for restrictions on using the output values.
	 * @param[in] cursor A cursor handle returned by #f_Cursor()
	 * @param[in,out] key The key for a retrieved item
	 * @param[in,out] data The data of a retrieved item
	 * @param[in] op A cursor operation #EDatabaseCursorGet
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_NOTFOUND - no matching key found.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	int mdb_cursor_get(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op)
	{
		using namespace NPrivate;

		int		 rc;
		int		 exact = 0;
		int		 (*mfunc)(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data);

		if (mc == nullptr)
			return EINVAL;

		mc->mc_txn->f_CheckBlocked();

		switch (op) {
		case MDB_GET_CURRENT:
			if (!(mc->mc_flags & C_INITIALIZED)) {
				rc = EINVAL;
			} else {
				MDB_page *mp = mc->mc_pg[mc->mc_top];
				int nkeys = NUMKEYS(mp);
				if (!nkeys || mc->mc_ki[mc->mc_top] >= nkeys) {
					mc->mc_ki[mc->mc_top] = nkeys;
					rc = MDB_NOTFOUND;
					break;
				}
				rc = MDB_SUCCESS;
				if (IS_LEAF2(mp)) {
					key->m_Size = mc->mc_db->md_pad;
					key->m_pData = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->m_Size);
				} else {
					MDB_node *leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
					MDB_GET_KEY(leaf, key);
					if (data) {
						if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
							rc = mdb_cursor_get(&mc->mc_xcursor->mx_cursor, data, nullptr, MDB_GET_CURRENT);
						} else {
							rc = mdb_node_read(mc, leaf, data);
						}
					}
				}
			}
			break;
		case MDB_GET_BOTH:
		case MDB_GET_BOTH_RANGE:
			if (data == nullptr) {
				rc = EINVAL;
				break;
			}
			if (mc->mc_xcursor == nullptr) {
				rc = MDB_INCOMPATIBLE;
				break;
			}
			/* FALLTHRU */
		case MDB_SET:
		case MDB_SET_KEY:
		case MDB_SET_RANGE:
			if (key == nullptr) {
				rc = EINVAL;
			} else {
				rc = mdb_cursor_set(mc, key, data, op,
					op == MDB_SET_RANGE ? nullptr : &exact);
			}
			break;
		case MDB_GET_MULTIPLE:
			if (data == nullptr || !(mc->mc_flags & C_INITIALIZED)) {
				rc = EINVAL;
				break;
			}
			if (!(mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)) {
				rc = MDB_INCOMPATIBLE;
				break;
			}
			rc = MDB_SUCCESS;
			if (!(mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED) ||
				(mc->mc_xcursor->mx_cursor.mc_flags & C_EOF))
				break;
			goto fetchm;
		case MDB_NEXT_MULTIPLE:
			if (data == nullptr) {
				rc = EINVAL;
				break;
			}
			if (!(mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)) {
				rc = MDB_INCOMPATIBLE;
				break;
			}
			rc = mdb_cursor_next(mc, key, data, MDB_NEXT_DUP);
			if (rc == MDB_SUCCESS) {
				if (mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED) {
					MDB_cursor *mx;
	fetchm:
					mx = &mc->mc_xcursor->mx_cursor;
					data->m_Size = NUMKEYS(mx->mc_pg[mx->mc_top]) *
						mx->mc_db->md_pad;
					data->m_pData = METADATA(mx->mc_pg[mx->mc_top]);
					mx->mc_ki[mx->mc_top] = NUMKEYS(mx->mc_pg[mx->mc_top])-1;
				} else {
					rc = MDB_NOTFOUND;
				}
			}
			break;
		case MDB_PREV_MULTIPLE:
			if (data == nullptr) {
				rc = EINVAL;
				break;
			}
			if (!(mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)) {
				rc = MDB_INCOMPATIBLE;
				break;
			}
			if (!(mc->mc_flags & C_INITIALIZED))
				rc = mdb_cursor_last(mc, key, data);
			else
				rc = MDB_SUCCESS;
			if (rc == MDB_SUCCESS) {
				MDB_cursor *mx = &mc->mc_xcursor->mx_cursor;
				if (mx->mc_flags & C_INITIALIZED) {
					rc = mdb_cursor_sibling(mx, 0);
					if (rc == MDB_SUCCESS)
						goto fetchm;
				} else {
					rc = MDB_NOTFOUND;
				}
			}
			break;
		case MDB_NEXT:
		case MDB_NEXT_DUP:
		case MDB_NEXT_NODUP:
			rc = mdb_cursor_next(mc, key, data, op);
			break;
		case MDB_PREV:
		case MDB_PREV_DUP:
		case MDB_PREV_NODUP:
			rc = mdb_cursor_prev(mc, key, data, op);
			break;
		case MDB_FIRST:
			rc = mdb_cursor_first(mc, key, data);
			break;
		case MDB_FIRST_DUP:
			mfunc = mdb_cursor_first;
		mmove:
			if (data == nullptr || !(mc->mc_flags & C_INITIALIZED)) {
				rc = EINVAL;
				break;
			}
			if (mc->mc_xcursor == nullptr) {
				rc = MDB_INCOMPATIBLE;
				break;
			}
			if (mc->mc_ki[mc->mc_top] >= NUMKEYS(mc->mc_pg[mc->mc_top])) {
				mc->mc_ki[mc->mc_top] = NUMKEYS(mc->mc_pg[mc->mc_top]);
				rc = MDB_NOTFOUND;
				break;
			}
			{
				MDB_node *leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
				if (!F_ISSET(leaf->mn_flags, F_DUPDATA)) {
					MDB_GET_KEY(leaf, key);
					rc = mdb_node_read(mc, leaf, data);
					break;
				}
			}
			if (!(mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED)) {
				rc = EINVAL;
				break;
			}
			rc = mfunc(&mc->mc_xcursor->mx_cursor, data, nullptr);
			break;
		case MDB_LAST:
			rc = mdb_cursor_last(mc, key, data);
			break;
		case MDB_LAST_DUP:
			mfunc = mdb_cursor_last;
			goto mmove;
		default:
			DMibDatabaseTrace("Unhandled/unimplemented cursor operation {}", op);
			rc = EINVAL;
			break;
		}

		if (mc->mc_flags & C_DEL)
			mc->mc_flags ^= C_DEL;

		return rc;
	}

	/** @brief Store by cursor.
	 *
	 * This function stores key/data pairs into the database.
	 * The cursor is positioned at the new item, or on failure usually near it.
	 * @note Earlier documentation incorrectly said errors would leave the
	 * state of the cursor unchanged.
	 * @param[in] cursor A cursor handle returned by #f_Cursor()
	 * @param[in] key The key operated on.
	 * @param[in] data The data operated on.
	 * @param[in] flags Options for this operation. This parameter
	 * must be set to 0 or one of the values described here.
	 * <ul>
	 *	<li>#EDatabaseWriteFlag_OverwriteAtCurrentCursorPos - replace the item at the current cursor position.
	 *		The \b key parameter must still be provided, and must match it.
	 *		If using sorted duplicates (#EDatabaseSubFlag_SortedDuplicates) the data item must still
	 *		sort into the same place. This is intended to be used when the
	 *		new data is the same size as the old. Otherwise it will simply
	 *		perform a delete of the old record followed by an insert.
	 *	<li>#EDatabaseWriteFlag_NoDuplicateData - enter the new key/data pair only if it does not
	 *		already appear in the database. This flag may only be specified
	 *		if the database was opened with #EDatabaseSubFlag_SortedDuplicates. The function will
	 *		return #MDB_KEYEXIST if the key/data pair already appears in the
	 *		database.
	 *	<li>#EDatabaseWriteFlag_NoOverwrite - enter the new key/data pair only if the key
	 *		does not already appear in the database. The function will return
	 *		#MDB_KEYEXIST if the key already appears in the database, even if
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
	 *		a #MDB_KEYEXIST error.
	 *	<li>#EDatabaseWriteFlag_AppendDuplicateData - as above, but for sorted dup data.
	 *	<li>#EDatabaseWriteFlag_BatchDuplicateData - store multiple contiguous data elements in a
	 *		single request. This flag may only be specified if the database
	 *		was opened with #EDatabaseSubFlag_SortedDuplicatesFixedSize. The \b data argument must be an
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
	 *	<li>#MDB_MAP_FULL - the database is full, see #f_SetMapSize().
	 *	<li>#MDB_TXN_FULL - the transaction has too many dirty pages.
	 *	<li>EACCES - an attempt was made to write in a read-only transaction.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	int mdb_cursor_put(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseWriteFlag _Flags)
	{
		uint32 flags = _Flags;
		using namespace NPrivate;
		MDB_env		*env;
		MDB_node	*leaf = nullptr;
		MDB_page	*fp, *mp, *sub_root = nullptr;
		uint16	fp_flags;
		CDatabaseValue		xdata, *rdata, dkey, olddata;
		MDB_db dummy;
		int do_sub = 0, insert_key, insert_data;
		uint32 mcount = 0, dcount = 0, nospill;
		mint nsize;
		int rc, rc2;
		uint32 nflags;
		DKBUF;

		if (mc == nullptr || key == nullptr)
			return EINVAL;

		env = mc->mc_txn->mt_env;

		/* Check this first so counter will always be zero on any
		 * early failures.
		 */
		if (flags & EDatabaseWriteFlag_BatchDuplicateData) {
			dcount = data[1].m_Size;
			data[1].m_Size = 0;
			if (!F_ISSET(mc->mc_db->md_flags, EDatabaseSubFlag_SortedDuplicatesFixedSize))
				return MDB_INCOMPATIBLE;
		}

		nospill = flags & MDB_NOSPILL;
		flags &= ~MDB_NOSPILL;

		mc->mc_txn->f_CheckBlocked();

		if (mc->mc_txn->mt_flags & (MDB_TXN_RDONLY))
			return EACCES;

		if (key->m_Size-1 >= ENV_MAXKEY(env))
			return MDB_BAD_VALSIZE;

	#if SIZE_MAX > MAXDATASIZE
		if (data->m_Size > ((mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) ? ENV_MAXKEY(env) : MAXDATASIZE))
			return MDB_BAD_VALSIZE;
	#else
		if ((mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) && data->m_Size > ENV_MAXKEY(env))
			return MDB_BAD_VALSIZE;
	#endif

		DMibDatabaseTrace("==> put db {} key [{}], size {}, data size {}", DDBI(mc), DKEY(key), key ? key->m_Size : 0, data->m_Size);

		dkey.m_Size = 0;

		if (flags & EDatabaseWriteFlag_OverwriteAtCurrentCursorPos) {
			if (!(mc->mc_flags & C_INITIALIZED))
				return EINVAL;
			rc = MDB_SUCCESS;
		} else if (mc->mc_db->md_root == P_INVALID) {
			/* new database, cursor has nothing to point to */
			mc->mc_snum = 0;
			mc->mc_top = 0;
			mc->mc_flags &= ~C_INITIALIZED;
			rc = MDB_NO_ROOT;
		} else {
			int exact = 0;
			CDatabaseValue d2;
			if (flags & EDatabaseWriteFlag_Append) {
				CDatabaseValue k2;
				rc = mdb_cursor_last(mc, &k2, &d2);
				if (rc == 0) {
					rc = mc->mc_dbx->m_fKeyCompare(*key, k2);
					if (rc > 0) {
						rc = MDB_NOTFOUND;
						mc->mc_ki[mc->mc_top]++;
					} else {
						/* new key is <= last key */
						rc = MDB_KEYEXIST;
					}
				}
			} else {
				rc = mdb_cursor_set(mc, key, &d2, MDB_SET, &exact);
			}
			if ((flags & EDatabaseWriteFlag_NoOverwrite) && rc == 0) {
				DMibDatabaseTrace("duplicate key [{}]", DKEY(key));
				*data = d2;
				return MDB_KEYEXIST;
			}
			if (rc && rc != MDB_NOTFOUND)
				return rc;
		}

		if (mc->mc_flags & C_DEL)
			mc->mc_flags ^= C_DEL;

		/* Cursor is positioned, check for room in the dirty list */
		if (!nospill) {
			if (flags & EDatabaseWriteFlag_BatchDuplicateData) {
				rdata = &xdata;
				xdata.m_Size = data->m_Size * dcount;
			} else {
				rdata = data;
			}
			if ((rc2 = mdb_page_spill(mc, key, rdata)))
				return rc2;
		}

		if (rc == MDB_NO_ROOT) {
			MDB_page *np;
			/* new database, write a root leaf page */
			DPUTS("allocating new root leaf page");
			if ((rc2 = mdb_page_new(mc, P_LEAF, 1, &np))) {
				return rc2;
			}
			mdb_cursor_push(mc, np);
			mc->mc_db->md_root = np->mp_p.p_pgno;
			mc->mc_db->md_depth++;
			*mc->mc_dbflag |= DB_DIRTY;
			if ((mc->mc_db->md_flags & (EDatabaseSubFlag_SortedDuplicates|EDatabaseSubFlag_SortedDuplicatesFixedSize))
				== EDatabaseSubFlag_SortedDuplicatesFixedSize)
				np->mp_flags |= P_LEAF2;
			mc->mc_flags |= C_INITIALIZED;
		} else {
			/* make sure all cursor pages are writable */
			rc2 = mdb_cursor_touch(mc);
			if (rc2)
				return rc2;
		}

		unsigned offset = 0;

		insert_key = insert_data = rc;
		if (insert_key) {
			/* The key does not exist */
			DMibDatabaseTrace("inserting key at index {}", mc->mc_ki[mc->mc_top]);
			if ((mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) &&
				LEAFSIZE(key, data) > env->me_nodemax)
			{
				/* Too big for a node, insert in sub-DB.  Set up an empty
				 * "old sub-page" for prep_subDB to expand to a full page.
				 */
				fp_flags = P_LEAF|P_DIRTY;
				fp = (MDB_page *)env->me_pbuf;
				fp->mp_pad = data->m_Size; /* used if EDatabaseSubFlag_SortedDuplicatesFixedSize */
				fp->mp_lower = fp->mp_upper = (PAGEHDRSZ-PAGEBASE);
				olddata.m_Size = PAGEHDRSZ;
				goto prep_subDB;
			}
		} else {
			/* there's only a key anyway, so this is a no-op */
			if (IS_LEAF2(mc->mc_pg[mc->mc_top])) {
				{
					uint8 *ptr;
					uint32 ksize = mc->mc_db->md_pad;
					if (key->m_Size != ksize)
						return MDB_BAD_VALSIZE;
					ptr = LEAF2KEY(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], ksize);
					memcpy(ptr, key->m_pData, ksize);
				}
	fix_parent:
				/* if overwriting slot 0 of leaf, need to
				 * update branch key if there is a parent page
				 */
				if (mc->mc_top && !mc->mc_ki[mc->mc_top]) {
					unsigned short dtop = 1;
					mc->mc_top--;
					/* slot 0 is always an empty key, find real slot */
					while (mc->mc_top && !mc->mc_ki[mc->mc_top]) {
						mc->mc_top--;
						dtop++;
					}
					if (mc->mc_ki[mc->mc_top])
						rc2 = mdb_update_key(mc, key);
					else
						rc2 = MDB_SUCCESS;
					mc->mc_top += dtop;
					if (rc2)
						return rc2;
				}
				return MDB_SUCCESS;
			}

	more:
			leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
			olddata.m_Size = NODEDSZ(leaf);
			olddata.m_pData = NODEDATA(leaf);

			/* DB has dups? */
			if (F_ISSET(mc->mc_db->md_flags, EDatabaseSubFlag_SortedDuplicates)) {
				/* Prepare (sub-)page/sub-DB to accept the new item,
				 * if needed.  fp: old sub-page or a header faking
				 * it.  mp: new (sub-)page.  offset: growth in page
				 * size.  xdata: node data with new page or DB.
				 */
				unsigned	i;
				mp = fp = (MDB_page *)(xdata.m_pData = (uint8 *)env->me_pbuf);
				mp->mp_p.p_pgno = mc->mc_pg[mc->mc_top]->mp_p.p_pgno;

				/* Was a single item before, must convert now */
				if (!F_ISSET(leaf->mn_flags, F_DUPDATA)) {
					FDatabaseCompare *pDataCompare = nullptr;
					/* Just overwrite the current item */
					if (flags == EDatabaseWriteFlag_OverwriteAtCurrentCursorPos)
						goto current;
					pDataCompare = &(mc->mc_dbx->m_fDataCompare);
					if (NEED_CMP_CLONG(*pDataCompare, olddata.m_Size))
						pDataCompare = &g_mdb_cmp_clong;
					/* does data match? */
					if (!(*pDataCompare)(*data, olddata)) {
						if (flags & (EDatabaseWriteFlag_NoDuplicateData|EDatabaseWriteFlag_AppendDuplicateData))
							return MDB_KEYEXIST;
						/* overwrite it */
						goto current;
					}

					/* Back up original data item */
					dkey.m_Size = olddata.m_Size;
					dkey.m_pData = (uint8 *)memcpy(fp+1, olddata.m_pData, olddata.m_Size);

					/* Make sub-page header for the dup items, with dummy body */
					fp->mp_flags = P_LEAF|P_DIRTY|P_SUBP;
					fp->mp_lower = (PAGEHDRSZ-PAGEBASE);
					xdata.m_Size = PAGEHDRSZ + dkey.m_Size + data->m_Size;
					if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize) {
						fp->mp_flags |= P_LEAF2;
						fp->mp_pad = data->m_Size;
						xdata.m_Size += 2 * data->m_Size;	/* leave space for 2 more */
					} else {
						xdata.m_Size += 2 * (sizeof(indx_t) + NODESIZE) +
							(dkey.m_Size & 1) + (data->m_Size & 1);
					}
					fp->mp_upper = xdata.m_Size - PAGEBASE;
					olddata.m_Size = xdata.m_Size; /* pretend olddata is fp */
				} else if (leaf->mn_flags & F_SUBDATA) {
					/* Data is on sub-DB, just store it */
					flags |= F_DUPDATA|F_SUBDATA;
					goto put_sub;
				} else {
					/* Data is on sub-page */
					fp = (MDB_page *)olddata.m_pData;
					switch (flags) {
					default:
						if (!(mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize)) {
							offset = EVEN(NODESIZE + sizeof(indx_t) +
								data->m_Size);
							break;
						}
						offset = fp->mp_pad;
						if (SIZELEFT(fp) < offset) {
							offset *= 4; /* space for 4 more */
							break;
						}
						/* FALLTHRU: Big enough EDatabaseSubFlag_SortedDuplicatesFixedSize sub-page */
					case EDatabaseWriteFlag_OverwriteAtCurrentCursorPos:
						fp->mp_flags |= P_DIRTY;
						COPY_PGNO(fp->mp_p.p_pgno, mp->mp_p.p_pgno);
						mc->mc_xcursor->mx_cursor.mc_pg[0] = fp;
						flags |= F_DUPDATA;
						goto put_sub;
					}
					xdata.m_Size = olddata.m_Size + offset;
				}

				fp_flags = fp->mp_flags;
				if (NODESIZE + NODEKSZ(leaf) + xdata.m_Size > env->me_nodemax) {
						/* Too big for a sub-page, convert to sub-DB */
						fp_flags &= ~P_SUBP;
	prep_subDB:
						if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize) {
							fp_flags |= P_LEAF2;
							dummy.md_pad = fp->mp_pad;
							dummy.md_flags = EDatabaseSubFlag_SortedDuplicatesFixedSize;
							if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
								dummy.md_flags |= EDatabaseSubFlag_IntegerKeys;
						} else {
							dummy.md_pad = 0;
							dummy.md_flags = 0;
						}
						dummy.md_depth = 1;
						dummy.md_branch_pages = 0;
						dummy.md_leaf_pages = 1;
						dummy.md_overflow_pages = 0;
						dummy.md_entries = NUMKEYS(fp);
						xdata.m_Size = sizeof(MDB_db);
						xdata.m_pData = (uint8 *)&dummy;
						if ((rc = mdb_page_alloc(mc, 1, &mp)))
							return rc;
						offset = env->me_psize - olddata.m_Size;
						flags |= F_DUPDATA|F_SUBDATA;
						dummy.md_root = mp->mp_p.p_pgno;
						sub_root = mp;
				}
				if (mp != fp) {
					mp->mp_flags = fp_flags | P_DIRTY;
					mp->mp_pad   = fp->mp_pad;
					mp->mp_lower = fp->mp_lower;
					mp->mp_upper = fp->mp_upper + offset;
					if (fp_flags & P_LEAF2) {
						memcpy(METADATA(mp), METADATA(fp), NUMKEYS(fp) * fp->mp_pad);
					} else {
						memcpy((char *)mp + mp->mp_upper + PAGEBASE, (char *)fp + fp->mp_upper + PAGEBASE,
							olddata.m_Size - fp->mp_upper - PAGEBASE);
						memcpy((char *)(&mp->mp_ptrs), (char *)(&fp->mp_ptrs), NUMKEYS(fp) * sizeof(mp->mp_ptrs[0]));
						for (i=0; i<NUMKEYS(fp); i++)
							mp->mp_ptrs[i] += offset;
					}
				}

				rdata = &xdata;
				flags |= F_DUPDATA;
				do_sub = 1;
				if (!insert_key)
					mdb_node_del(mc, 0);
				goto new_sub;
			}
	current:
			/* LMDB passes F_SUBDATA in 'flags' to write a DB record */
			if ((leaf->mn_flags ^ flags) & F_SUBDATA)
				return MDB_INCOMPATIBLE;
			/* overflow page overwrites need special handling */
			if (F_ISSET(leaf->mn_flags, F_BIGDATA)) {
				MDB_page *omp;
				pgno_t pg;
				int level, ovpages, dpages = OVPAGES(data->m_Size, env->me_psize);

				memcpy(&pg, olddata.m_pData, sizeof(pg));
				if ((rc2 = mdb_page_get(mc, pg, &omp, &level)) != 0)
					return rc2;
				ovpages = omp->mp_pages;

				/* Is the ov page large enough? */
				if (ovpages >= dpages) {
				  if (!(omp->mp_flags & P_DIRTY) &&
					  (level || (env->me_flags & EDatabaseEnvironmentFlag_WriteMap)))
				  {
					rc = mdb_page_unspill(mc->mc_txn, omp, &omp);
					if (rc)
						return rc;
					level = 0;		/* dirty in this txn or clean */
				  }
				  /* Is it dirty? */
				  if (omp->mp_flags & P_DIRTY) {
					/* yes, overwrite it. Note in this case we don't
					 * bother to try shrinking the page if the new data
					 * is smaller than the overflow threshold.
					 */
					if (level > 1) {
						/* It is writable only in a parent txn */
						mint sz = (mint) env->me_psize * ovpages, off;
						MDB_page *np = mdb_page_malloc(mc->mc_txn, ovpages);
						MDB_ID2 id2;
						id2.mid = pg;
						id2.mptr = np;
						/* Note - this page is already counted in parent's dirty_room */
						rc2 = mdb_mid2l_insert(mc->mc_txn->mt_u.dirty_list, &id2);
						DMibCheck(rc2 == 0);
						/* Currently we make the page look as with put() in the
						 * parent txn, in case the user peeks at EDatabaseWriteFlag_ReserveSpaced
						 * or unused parts. Some users treat ovpages specially.
						 */
						if (!(flags & EDatabaseWriteFlag_ReserveSpace)) {
							/* Skip the part where LMDB will put *data.
							 * Copy end of page, adjusting alignment so
							 * compiler may copy words instead of bytes.
							 */
							off = (PAGEHDRSZ + data->m_Size) & -sizeof(mint);
							memcpy((mint *)((char *)np + off),
								(mint *)((char *)omp + off), sz - off);
							sz = PAGEHDRSZ;
						}
						memcpy(np, omp, sz); /* Copy beginning of page */
						omp = np;
					}
					SETDSZ(leaf, data->m_Size);
					if (F_ISSET(flags, EDatabaseWriteFlag_ReserveSpace))
						data->m_pData = METADATA(omp);
					else
						memcpy(METADATA(omp), data->m_pData, data->m_Size);
					return MDB_SUCCESS;
				  }
				}
				if ((rc2 = mdb_ovpage_free(mc, omp)) != MDB_SUCCESS)
					return rc2;
			} else if (data->m_Size == olddata.m_Size) {
				/* same size, just replace it. Note that we could
				 * also reuse this node if the new data is smaller,
				 * but instead we opt to shrink the node in that case.
				 */
				if (F_ISSET(flags, EDatabaseWriteFlag_ReserveSpace))
					data->m_pData = olddata.m_pData;
				else if (!(mc->mc_flags & C_SUB))
					memcpy((ch8 *)olddata.m_pData, data->m_pData, data->m_Size);
				else {
					memcpy(NODEKEY(leaf), key->m_pData, key->m_Size);
					goto fix_parent;
				}
				return MDB_SUCCESS;
			}
			mdb_node_del(mc, 0);
		}

		rdata = data;

	new_sub:
		nflags = flags & NODE_ADD_FLAGS;
		nsize = IS_LEAF2(mc->mc_pg[mc->mc_top]) ? key->m_Size : env->mdb_leaf_size(key, rdata);
		if (SIZELEFT(mc->mc_pg[mc->mc_top]) < nsize) {
			if (( flags & (F_DUPDATA|F_SUBDATA)) == F_DUPDATA )
				nflags &= ~EDatabaseWriteFlag_Append; /* sub-page may need room to grow */
			if (!insert_key)
				nflags |= MDB_SPLIT_REPLACE;
			rc = mdb_page_split(mc, key, rdata, P_INVALID, nflags);
		} else {
			/* There is room already in this leaf page. */
			rc = mdb_node_add(mc, mc->mc_ki[mc->mc_top], key, rdata, 0, nflags);
			if (rc == 0) {
				/* Adjust other cursors pointing to mp */
				MDB_cursor *m2, *m3;
				CDatabaseSubID dbi = mc->mc_dbi;
				unsigned i = mc->mc_top;
				MDB_page *mp = mc->mc_pg[i];

				for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
					if (mc->mc_flags & C_SUB)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (m3 == mc || m3->mc_snum < mc->mc_snum || m3->mc_pg[i] != mp) continue;
					if (m3->mc_ki[i] >= mc->mc_ki[i] && insert_key) {
						m3->mc_ki[i]++;
					}
					XCURSOR_REFRESH(m3, i, mp);
				}
			}
		}

		if (rc == MDB_SUCCESS) {
			/* Now store the actual data in the child DB. Note that we're
			 * storing the user data in the keys field, so there are strict
			 * size limits on dupdata. The actual data fields of the child
			 * DB are all zero size.
			 */
			if (do_sub) {
				int xflags, new_dupdata;
				CDatabaseOffset ecount;
	put_sub:
				xdata.m_Size = 0;
				xdata.m_pData = (uint8 *)"";
				leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
				if (flags == EDatabaseWriteFlag_OverwriteAtCurrentCursorPos) {
					xflags = EDatabaseWriteFlag_OverwriteAtCurrentCursorPos|MDB_NOSPILL;
				} else {
					mdb_xcursor_init1(mc, leaf);
					xflags = (flags & EDatabaseWriteFlag_NoDuplicateData) ?
						EDatabaseWriteFlag_NoOverwrite|MDB_NOSPILL : MDB_NOSPILL;
				}
				if (sub_root)
					mc->mc_xcursor->mx_cursor.mc_pg[0] = sub_root;
				new_dupdata = (int)dkey.m_Size;
				/* converted, write the original data first */
				if (dkey.m_Size) {
					rc = mdb_cursor_put(&mc->mc_xcursor->mx_cursor, &dkey, &xdata, (EDatabaseWriteFlag)xflags);
					if (rc)
						goto bad_sub;
					/* we've done our job */
					dkey.m_Size = 0;
				}
				if (!(leaf->mn_flags & F_SUBDATA) || sub_root) {
					/* Adjust other cursors pointing to mp */
					MDB_cursor *m2;
					MDB_xcursor *mx = mc->mc_xcursor;
					unsigned i = mc->mc_top;
					MDB_page *mp = mc->mc_pg[i];

					for (m2 = mc->mc_txn->mt_cursors[mc->mc_dbi]; m2; m2=m2->mc_next) {
						if (m2 == mc || m2->mc_snum < mc->mc_snum) continue;
						if (!(m2->mc_flags & C_INITIALIZED)) continue;
						if (m2->mc_pg[i] == mp) {
							if (m2->mc_ki[i] == mc->mc_ki[i]) {
								mdb_xcursor_init2(m2, mx, new_dupdata);
							} else if (!insert_key) {
								XCURSOR_REFRESH(m2, i, mp);
							}
						}
					}
				}
				ecount = mc->mc_xcursor->mx_db.md_entries;
				if (flags & EDatabaseWriteFlag_AppendDuplicateData)
					xflags |= EDatabaseWriteFlag_Append;
				rc = mdb_cursor_put(&mc->mc_xcursor->mx_cursor, data, &xdata, (EDatabaseWriteFlag)xflags);
				if (flags & F_SUBDATA) {
					void *db = NODEDATA(leaf);
					memcpy(db, &mc->mc_xcursor->mx_db, sizeof(MDB_db));
				}
				insert_data = mc->mc_xcursor->mx_db.md_entries - ecount;
			}
			/* Increment count unless we just replaced an existing item. */
			if (insert_data)
				mc->mc_db->md_entries++;
			if (insert_key) {
				/* Invalidate txn if we created an empty sub-DB */
				if (rc)
					goto bad_sub;
				/* If we succeeded and the key didn't exist before,
				 * make sure the cursor is marked valid.
				 */
				mc->mc_flags |= C_INITIALIZED;
			}
			if (flags & EDatabaseWriteFlag_BatchDuplicateData) {
				if (!rc) {
					mcount++;
					/* let caller know how many succeeded, if any */
					data[1].m_Size = mcount;
					if (mcount < dcount) {
						data[0].m_pData = (uint8 *)data[0].m_pData + data[0].m_Size;
						insert_key = insert_data = 0;
						goto more;
					}
				}
			}
			return rc;
	bad_sub:
			if (rc == MDB_KEYEXIST)	/* should not happen, we deleted that item */
				rc = MDB_PROBLEM;
		}
		mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
		return rc;
	}

	/** @brief Delete current key/data pair
	 *
	 * This function deletes the key/data pair to which the cursor refers.
	 * This does not invalidate the cursor, so operations such as MDB_NEXT
	 * can still be used on it.
	 * Both MDB_NEXT and MDB_GET_CURRENT will return the same record after
	 * this operation.
	 * @param[in] cursor A cursor handle returned by #f_Cursor()
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
	int mdb_cursor_del(MDB_cursor *mc, EDatabaseWriteFlag _Flags)
	{
		uint32 flags = _Flags;
		using namespace NPrivate;

		MDB_node	*leaf;
		MDB_page	*mp;
		int rc;

		mc->mc_txn->f_CheckBlocked();

		if (mc->mc_txn->mt_flags & (MDB_TXN_RDONLY))
			return EACCES;

		if (!(mc->mc_flags & C_INITIALIZED))
			return EINVAL;

		if (mc->mc_ki[mc->mc_top] >= NUMKEYS(mc->mc_pg[mc->mc_top]))
			return MDB_NOTFOUND;

		if (!(flags & MDB_NOSPILL) && (rc = mdb_page_spill(mc, nullptr, nullptr)))
			return rc;

		rc = mdb_cursor_touch(mc);
		if (rc)
			return rc;

		mp = mc->mc_pg[mc->mc_top];
		if (IS_LEAF2(mp))
			goto del_key;
		leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);

		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			if (flags & EDatabaseWriteFlag_NoDuplicateData) {
				/* mdb_cursor_del0() will subtract the final entry */
				mc->mc_db->md_entries -= mc->mc_xcursor->mx_db.md_entries - 1;
				mc->mc_xcursor->mx_cursor.mc_flags &= ~C_INITIALIZED;
			} else {
				if (!F_ISSET(leaf->mn_flags, F_SUBDATA)) {
					mc->mc_xcursor->mx_cursor.mc_pg[0] = (MDB_page *)NODEDATA(leaf);
				}
				rc = mdb_cursor_del(&mc->mc_xcursor->mx_cursor, (EDatabaseWriteFlag)MDB_NOSPILL);
				if (rc)
					return rc;
				/* If sub-DB still has entries, we're done */
				if (mc->mc_xcursor->mx_db.md_entries) {
					if (leaf->mn_flags & F_SUBDATA) {
						/* update subDB info */
						void *db = (MDB_page *)NODEDATA(leaf);
						memcpy(db, &mc->mc_xcursor->mx_db, sizeof(MDB_db));
					} else {
						MDB_cursor *m2;
						/* shrink fake page */
						mdb_node_shrink(mp, mc->mc_ki[mc->mc_top]);
						leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
						mc->mc_xcursor->mx_cursor.mc_pg[0] = (MDB_page *)NODEDATA(leaf);
						/* fix other sub-DB cursors pointed at fake pages on this page */
						for (m2 = mc->mc_txn->mt_cursors[mc->mc_dbi]; m2; m2=m2->mc_next) {
							if (m2 == mc || m2->mc_snum < mc->mc_snum) continue;
							if (!(m2->mc_flags & C_INITIALIZED)) continue;
							if (m2->mc_pg[mc->mc_top] == mp) {
								XCURSOR_REFRESH(m2, mc->mc_top, mp);
							}
						}
					}
					mc->mc_db->md_entries--;
					return rc;
				} else {
					mc->mc_xcursor->mx_cursor.mc_flags &= ~C_INITIALIZED;
				}
				/* otherwise fall thru and delete the sub-DB */
			}

			if (leaf->mn_flags & F_SUBDATA) {
				/* add all the child DB's pages to the free list */
				rc = mdb_drop0(&mc->mc_xcursor->mx_cursor, 0);
				if (rc)
					goto fail;
			}
		}
		/* LMDB passes F_SUBDATA in 'flags' to delete a DB record */
		else if ((leaf->mn_flags ^ flags) & F_SUBDATA) {
			rc = MDB_INCOMPATIBLE;
			goto fail;
		}

		/* add overflow pages to free list */
		if (F_ISSET(leaf->mn_flags, F_BIGDATA)) {
			MDB_page *omp;
			pgno_t pg;

			memcpy(&pg, NODEDATA(leaf), sizeof(pg));
			if ((rc = mdb_page_get(mc, pg, &omp, nullptr)) ||
				(rc = mdb_ovpage_free(mc, omp)))
				goto fail;
		}

	del_key:
		return mdb_cursor_del0(mc);

	fail:
		mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
		return rc;
	}

	/** @brief Return count of duplicates for current key.
	 *
	 * This call is only valid on databases that support sorted duplicate
	 * data items #EDatabaseSubFlag_SortedDuplicates.
	 * @param[in] cursor A cursor handle returned by #f_Cursor()
	 * @param[out] countp Address where the count will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - cursor is not initialized, or an invalid parameter was specified.
	 * </ul>
	 */
	int mdb_cursor_count(MDB_cursor *mc, CDatabaseOffset *countp)
	{
		using namespace NPrivate;

		// Return the count of duplicate data items for the current key
		MDB_node	*leaf;

		if (mc == nullptr || countp == nullptr)
			return EINVAL;

		if (mc->mc_xcursor == nullptr)
			return MDB_INCOMPATIBLE;

		mc->mc_txn->f_CheckBlocked();

		if (!(mc->mc_flags & C_INITIALIZED))
			return EINVAL;

		if (!mc->mc_snum)
			return MDB_NOTFOUND;

		if (mc->mc_flags & C_EOF) {
			if (mc->mc_ki[mc->mc_top] >= NUMKEYS(mc->mc_pg[mc->mc_top]))
				return MDB_NOTFOUND;
			mc->mc_flags ^= C_EOF;
		}

		leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
		if (!F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			*countp = 1;
		} else {
			if (!(mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED))
				return EINVAL;

			*countp = mc->mc_xcursor->mx_db.md_entries;
		}
		return MDB_SUCCESS;
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

	#ifdef DMibDatabase_Always64BitSupport
	void mdb_cursor_unref(MDB_cursor *mc)
	{
		int i;
		if (mc->mc_txn->mt_rpages[0].mid) {
			if (!mc->mc_snum || !mc->mc_pg[0] || IS_SUBP(mc->mc_pg[0]))
				return;
			for (i=0; i<mc->mc_snum; i++)
				mdb_page_unref(mc->mc_txn, mc->mc_pg[i]);
			if (mc->mc_ovpg) {
				mdb_page_unref(mc->mc_txn, mc->mc_ovpg);
				mc->mc_ovpg = 0;
			}
		}
		mc->mc_snum = mc->mc_top = 0;
		mc->mc_pg[0] = nullptr;
		mc->mc_flags &= ~C_INITIALIZED;
	}
	#endif /* DMibDatabase_Always64BitSupport */

	/** Back up parent txn's cursors, then grab the originals for tracking */
	void mdb_cursor_shadow(MDB_txn *src, MDB_txn *dst)
	{
		MDB_cursor *mc, *bk;
		MDB_xcursor *mx;
		mint size;
		int i;

		for (i = src->mt_numdbs; --i >= 0; ) {
			if ((mc = src->mt_cursors[i]) != nullptr) {
				size = sizeof(MDB_cursor);
				if (mc->mc_xcursor)
					size += sizeof(MDB_xcursor);
				for (; mc; mc = bk->mc_next) {
					bk = (MDB_cursor *)NMemory::fg_Alloc(size);
					*bk = *mc;
					mc->mc_backup = bk;
					mc->mc_db = &dst->mt_dbs[i];
					/* Kill pointers into src to reduce abuse: The
					 * user may not use mc until dst ends. But we need a valid
					 * txn pointer here for cursor fixups to keep working.
					 */
					mc->mc_txn    = dst;
					mc->mc_dbflag = &dst->mt_dbflags[i];
					if ((mx = mc->mc_xcursor) != nullptr) {
						*(MDB_xcursor *)(bk+1) = *mx;
						mx->mx_cursor.mc_txn = dst;
					}
					mc->mc_next = dst->mt_cursors[i];
					dst->mt_cursors[i] = mc;
				}
			}
		}
	}

	/** Close this write txn's cursors, give parent txn's cursors back to parent.
	 * @param[in] txn the transaction handle.
	 * @param[in] merge true to keep changes to parent cursors, false to revert.
	 * @return 0 on success, non-zero on failure.
	 */
	void mdb_cursors_close(MDB_txn *txn, unsigned merge)
	{
		MDB_cursor **cursors = txn->mt_cursors, *mc, *next, *bk;
		MDB_xcursor *mx;
		int i;

		for (i = txn->mt_numdbs; --i >= 0; ) {
			for (mc = cursors[i]; mc; mc = next) {
				next = mc->mc_next;
				if ((bk = mc->mc_backup) != nullptr) {
					if (merge) {
						/* Commit changes to parent txn */
						mc->mc_next = bk->mc_next;
						mc->mc_backup = bk->mc_backup;
						mc->mc_txn = bk->mc_txn;
						mc->mc_db = bk->mc_db;
						mc->mc_dbflag = bk->mc_dbflag;
						if ((mx = mc->mc_xcursor) != nullptr)
							mx->mx_cursor.mc_txn = bk->mc_txn;
					} else {
						/* Abort nested txn */
						*mc = *bk;
						if ((mx = mc->mc_xcursor) != nullptr)
							*mx = *(MDB_xcursor *)(bk+1);
					}
					mc = bk;
				}
				/* Only malloced cursors are permanently tracked. */
				mint Size = sizeof(MDB_cursor);
				if (mc->mc_xcursor)
					Size += sizeof(MDB_xcursor);

				NMemory::fg_Free(mc, Size);
			}
			cursors[i] = nullptr;
		}
	}

	/** Pop a page off the top of the cursor's stack. */
	void mdb_cursor_pop(MDB_cursor *mc)
	{
		if (mc->mc_snum) {
			DMibDatabaseTrace("popping page {} off db {} cursor {}", mc->mc_pg[mc->mc_top]->mp_p.p_pgno, DDBI(mc), (void *) mc);

			mc->mc_snum--;
			if (mc->mc_snum) {
				mc->mc_top--;
			} else {
				mc->mc_flags &= ~C_INITIALIZED;
			}
		}
	}

	/** Push a page onto the top of the cursor's stack.
	 * Set #MDB_TXN_ERROR on failure.
	 */
	int mdb_cursor_push(MDB_cursor *mc, MDB_page *mp)
	{
		DMibDatabaseTrace("pushing page {} on db {} cursor {}", mp->mp_p.p_pgno, DDBI(mc), (void *) mc);

		if (mc->mc_snum >= CURSOR_STACK) {
			mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
			return MDB_CURSOR_FULL;
		}

		mc->mc_top = mc->mc_snum++;
		mc->mc_pg[mc->mc_top] = mp;
		mc->mc_ki[mc->mc_top] = 0;

		return MDB_SUCCESS;
	}

	/** Find a sibling for a page.
	 * Replaces the page at the top of the cursor's stack with the
	 * specified sibling, if one exists.
	 * @param[in] mc The cursor for this operation.
	 * @param[in] move_right Non-zero if the right sibling is requested,
	 * otherwise the left sibling.
	 * @return 0 on success, non-zero on failure.
	 */
	int mdb_cursor_sibling(MDB_cursor *mc, int move_right)
	{
		int		 rc;
		MDB_node	*indx;
		MDB_page	*mp;
	#ifdef DMibDatabase_Always64BitSupport
		MDB_page	*op;
	#endif

		if (mc->mc_snum < 2) {
			return MDB_NOTFOUND;		/* root has no siblings */
		}

	#ifdef DMibDatabase_Always64BitSupport
		op = mc->mc_pg[mc->mc_top];
	#endif
		mdb_cursor_pop(mc);
		DMibDatabaseTrace("parent page is page {}, index {}", mc->mc_pg[mc->mc_top]->mp_p.p_pgno, mc->mc_ki[mc->mc_top]);

		if (move_right ? (mc->mc_ki[mc->mc_top] + 1u >= NUMKEYS(mc->mc_pg[mc->mc_top]))
				   : (mc->mc_ki[mc->mc_top] == 0)) {
			DMibDatabaseTrace("no more keys left, moving to {} sibling", move_right ? "right" : "left");
			if ((rc = mdb_cursor_sibling(mc, move_right)) != MDB_SUCCESS) {
				/* undo cursor_pop before returning */
				mc->mc_top++;
				mc->mc_snum++;
				return rc;
			}
		} else {
			if (move_right)
				mc->mc_ki[mc->mc_top]++;
			else
				mc->mc_ki[mc->mc_top]--;
			DMibDatabaseTrace("just moving to {} index key {}", move_right ? "right" : "left", mc->mc_ki[mc->mc_top]);
		}
		DMibCheck(IS_BRANCH(mc->mc_pg[mc->mc_top]));

		MDB_PAGE_UNREF(mc->mc_txn, op);

		indx = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
		if ((rc = mdb_page_get(mc, NODEPGNO(indx), &mp, nullptr)) != 0) {
			/* mc will be inconsistent if caller does mc_snum++ as above */
			mc->mc_flags &= ~(C_INITIALIZED|C_EOF);
			return rc;
		}

		mdb_cursor_push(mc, mp);
		if (!move_right)
			mc->mc_ki[mc->mc_top] = NUMKEYS(mp)-1;

		return MDB_SUCCESS;
	}

	/** Move the cursor to the next data item. */
	int mdb_cursor_next(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op)
	{
		MDB_page	*mp;
		MDB_node	*leaf;
		int rc;

		if ((mc->mc_flags & C_DEL && op == MDB_NEXT_DUP))
			return MDB_NOTFOUND;

		if (!(mc->mc_flags & C_INITIALIZED))
			return mdb_cursor_first(mc, key, data);

		mp = mc->mc_pg[mc->mc_top];

		if (mc->mc_flags & C_EOF) {
			if (mc->mc_ki[mc->mc_top] >= NUMKEYS(mp)-1)
				return MDB_NOTFOUND;
			mc->mc_flags ^= C_EOF;
		}

		if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) {
			leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
			if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
				if (op == MDB_NEXT || op == MDB_NEXT_DUP) {
					rc = mdb_cursor_next(&mc->mc_xcursor->mx_cursor, data, nullptr, MDB_NEXT);
					if (op != MDB_NEXT || rc != MDB_NOTFOUND) {
						if (rc == MDB_SUCCESS)
							MDB_GET_KEY(leaf, key);
						return rc;
					}
				}
				else {
					MDB_CURSOR_UNREF(&mc->mc_xcursor->mx_cursor, 0);
				}
			} else {
				mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
				if (op == MDB_NEXT_DUP)
					return MDB_NOTFOUND;
			}
		}

		DMibDatabaseTrace("cursor_next: top page is {} in cursor {}", mdb_dbg_pgno(mp), (void *) mc);
		if (mc->mc_flags & C_DEL) {
			mc->mc_flags ^= C_DEL;
			goto skip;
		}

		if (mc->mc_ki[mc->mc_top] + 1u >= NUMKEYS(mp)) {
			DPUTS("=====> move to next sibling page");
			if ((rc = mdb_cursor_sibling(mc, 1)) != MDB_SUCCESS) {
				mc->mc_flags |= C_EOF;
				return rc;
			}
			mp = mc->mc_pg[mc->mc_top];
			DMibDatabaseTrace("next page is {}, key index {}", mp->mp_p.p_pgno, mc->mc_ki[mc->mc_top]);
		} else
			mc->mc_ki[mc->mc_top]++;

	skip:
		DMibDatabaseTrace("==> cursor points to page {} with {} keys, key index {}", mdb_dbg_pgno(mp), NUMKEYS(mp), mc->mc_ki[mc->mc_top]);

		if (IS_LEAF2(mp)) {
			key->m_Size = mc->mc_db->md_pad;
			key->m_pData = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->m_Size);
			return MDB_SUCCESS;
		}

		DMibCheck(IS_LEAF(mp));
		leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);

		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			mdb_xcursor_init1(mc, leaf);
		}
		if (data) {
			if ((rc = mdb_node_read(mc, leaf, data)) != MDB_SUCCESS)
				return rc;

			if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
				rc = mdb_cursor_first(&mc->mc_xcursor->mx_cursor, data, nullptr);
				if (rc != MDB_SUCCESS)
					return rc;
			}
		}

		MDB_GET_KEY(leaf, key);
		return MDB_SUCCESS;
	}

	/** Move the cursor to the previous data item. */
	int mdb_cursor_prev(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op)
	{
		MDB_page	*mp;
		MDB_node	*leaf;
		int rc;

		if (!(mc->mc_flags & C_INITIALIZED))
		{
			rc = mdb_cursor_last(mc, key, data);
			if (rc)
				return rc;
			mc->mc_ki[mc->mc_top]++;
		}

		mp = mc->mc_pg[mc->mc_top];

		if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates)
		{
			leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
			if (F_ISSET(leaf->mn_flags, F_DUPDATA))
			{
				if (op == MDB_PREV || op == MDB_PREV_DUP)
				{
					rc = mdb_cursor_prev(&mc->mc_xcursor->mx_cursor, data, nullptr, MDB_PREV);
					if (op != MDB_PREV || rc != MDB_NOTFOUND)
					{
						if (rc == MDB_SUCCESS)
						{
							MDB_GET_KEY(leaf, key);
							mc->mc_flags &= ~C_EOF;
						}
						return rc;
					}
				}
				else {
					MDB_CURSOR_UNREF(&mc->mc_xcursor->mx_cursor, 0);
				}
			}
			else
			{
				mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
				if (op == MDB_PREV_DUP)
					return MDB_NOTFOUND;
			}
		}

		DMibDatabaseTrace("cursor_prev: top page is {} in cursor {}", mdb_dbg_pgno(mp), (void *)mc);

		mc->mc_flags &= ~(C_EOF|C_DEL);

		if (mc->mc_ki[mc->mc_top] == 0)
		{
			DPUTS("=====> move to prev sibling page");
			if ((rc = mdb_cursor_sibling(mc, 0)) != MDB_SUCCESS)
			{
				return rc;
			}
			mp = mc->mc_pg[mc->mc_top];
			mc->mc_ki[mc->mc_top] = NUMKEYS(mp) - 1;
			DMibDatabaseTrace("prev page is {}, key index {}", mp->mp_p.p_pgno, mc->mc_ki[mc->mc_top]);
		}
		else
			mc->mc_ki[mc->mc_top]--;

		DMibDatabaseTrace("==> cursor points to page {} with {} keys, key index {}", mdb_dbg_pgno(mp), NUMKEYS(mp), mc->mc_ki[mc->mc_top]);

		if (IS_LEAF2(mp))
		{
			key->m_Size = mc->mc_db->md_pad;
			key->m_pData = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->m_Size);
			return MDB_SUCCESS;
		}

		DMibCheck(IS_LEAF(mp));
		leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);

		if (F_ISSET(leaf->mn_flags, F_DUPDATA))
			mdb_xcursor_init1(mc, leaf);

		if (data)
		{
			if ((rc = mdb_node_read(mc, leaf, data)) != MDB_SUCCESS)
				return rc;

			if (F_ISSET(leaf->mn_flags, F_DUPDATA))
			{
				rc = mdb_cursor_last(&mc->mc_xcursor->mx_cursor, data, nullptr);
				if (rc != MDB_SUCCESS)
					return rc;
			}
		}

		MDB_GET_KEY(leaf, key);
		return MDB_SUCCESS;
	}

	/** Set the cursor on a specific data item. */
	int mdb_cursor_set(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data, EDatabaseCursorGet op, int *exactp)
	{
		int		 rc;
		MDB_page	*mp;
		MDB_node	*leaf = nullptr;
		DKBUF;

		if (key->m_Size == 0)
			return MDB_BAD_VALSIZE;

		if (mc->mc_xcursor) {
			MDB_CURSOR_UNREF(&mc->mc_xcursor->mx_cursor, 0);
			mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
		}

		/* See if we're already on the right page */
		if (mc->mc_flags & C_INITIALIZED) {
			CDatabaseValue nodekey;

			mp = mc->mc_pg[mc->mc_top];
			if (!NUMKEYS(mp)) {
				mc->mc_ki[mc->mc_top] = 0;
				return MDB_NOTFOUND;
			}
			if (mp->mp_flags & P_LEAF2) {
				nodekey.m_Size = mc->mc_db->md_pad;
				nodekey.m_pData = LEAF2KEY(mp, 0, nodekey.m_Size);
			} else {
				leaf = NODEPTR(mp, 0);
				MDB_GET_KEY2(leaf, nodekey);
			}
			rc = mc->mc_dbx->m_fKeyCompare(*key, nodekey);
			if (rc == 0) {
				/* Probably happens rarely, but first node on the page
				 * was the one we wanted.
				 */
				mc->mc_ki[mc->mc_top] = 0;
				if (exactp)
					*exactp = 1;
				goto set1;
			}
			if (rc > 0) {
				uint32 i;
				uint32 nkeys = NUMKEYS(mp);
				if (nkeys > 1) {
					if (mp->mp_flags & P_LEAF2) {
						nodekey.m_pData = LEAF2KEY(mp,
							 nkeys-1, nodekey.m_Size);
					} else {
						leaf = NODEPTR(mp, nkeys-1);
						MDB_GET_KEY2(leaf, nodekey);
					}
					rc = mc->mc_dbx->m_fKeyCompare(*key, nodekey);
					if (rc == 0) {
						/* last node was the one we wanted */
						mc->mc_ki[mc->mc_top] = nkeys-1;
						if (exactp)
							*exactp = 1;
						goto set1;
					}
					if (rc < 0) {
						if (mc->mc_ki[mc->mc_top] < NUMKEYS(mp)) {
							/* This is definitely the right page, skip search_page */
							if (mp->mp_flags & P_LEAF2) {
								nodekey.m_pData = LEAF2KEY(mp,
									 mc->mc_ki[mc->mc_top], nodekey.m_Size);
							} else {
								leaf = NODEPTR(mp, mc->mc_ki[mc->mc_top]);
								MDB_GET_KEY2(leaf, nodekey);
							}
							rc = mc->mc_dbx->m_fKeyCompare(*key, nodekey);
							if (rc == 0) {
								/* current node was the one we wanted */
								if (exactp)
									*exactp = 1;
								goto set1;
							}
						}
						rc = 0;
						mc->mc_flags &= ~C_EOF;
						goto set2;
					}
				}
				/* If any parents have right-sibs, search.
				 * Otherwise, there's nothing further.
				 */
				for (i=0; i<mc->mc_top; i++)
					if (mc->mc_ki[i] <
						NUMKEYS(mc->mc_pg[i])-1)
						break;
				if (i == mc->mc_top) {
					/* There are no other pages */
					mc->mc_ki[mc->mc_top] = nkeys;
					return MDB_NOTFOUND;
				}
			}
			if (!mc->mc_top) {
				/* There are no other pages */
				mc->mc_ki[mc->mc_top] = 0;
				if (op == MDB_SET_RANGE && !exactp) {
					rc = 0;
					goto set1;
				} else
					return MDB_NOTFOUND;
			}
		} else {
			mc->mc_pg[0] = 0;
		}

		rc = mdb_page_search(mc, key, 0);
		if (rc != MDB_SUCCESS)
			return rc;

		mp = mc->mc_pg[mc->mc_top];
		DMibCheck(IS_LEAF(mp));

	set2:
		leaf = mdb_node_search(mc, key, exactp);
		if (exactp != nullptr && !*exactp) {
			/* MDB_SET specified and not an exact match. */
			return MDB_NOTFOUND;
		}

		if (leaf == nullptr) {
			DPUTS("===> inexact leaf not found, goto sibling");
			if ((rc = mdb_cursor_sibling(mc, 1)) != MDB_SUCCESS) {
				mc->mc_flags |= C_EOF;
				return rc;		/* no entries matched */
			}
			mp = mc->mc_pg[mc->mc_top];
			DMibCheck(IS_LEAF(mp));
			leaf = NODEPTR(mp, 0);
		}

	set1:
		mc->mc_flags |= C_INITIALIZED;
		mc->mc_flags &= ~C_EOF;

		if (IS_LEAF2(mp)) {
			if (op == MDB_SET_RANGE || op == MDB_SET_KEY) {
				key->m_Size = mc->mc_db->md_pad;
				key->m_pData = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], key->m_Size);
			}
			return MDB_SUCCESS;
		}

		if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
			mdb_xcursor_init1(mc, leaf);
		}
		if (data) {
			if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
				if (op == MDB_SET || op == MDB_SET_KEY || op == MDB_SET_RANGE) {
					rc = mdb_cursor_first(&mc->mc_xcursor->mx_cursor, data, nullptr);
				} else {
					int ex2, *ex2p;
					if (op == MDB_GET_BOTH) {
						ex2p = &ex2;
						ex2 = 0;
					} else {
						ex2p = nullptr;
					}
					rc = mdb_cursor_set(&mc->mc_xcursor->mx_cursor, data, nullptr, MDB_SET_RANGE, ex2p);
					if (rc != MDB_SUCCESS)
						return rc;
				}
			} else if (op == MDB_GET_BOTH || op == MDB_GET_BOTH_RANGE) {
				CDatabaseValue olddata;
				FDatabaseCompare *pDataCompare = nullptr;
				if ((rc = mdb_node_read(mc, leaf, &olddata)) != MDB_SUCCESS)
					return rc;
				pDataCompare = &(mc->mc_dbx->m_fDataCompare);
				if (NEED_CMP_CLONG(*pDataCompare, olddata.m_Size))
					pDataCompare = &g_mdb_cmp_clong;
				rc = (*pDataCompare)(*data, olddata);
				if (rc) {
					if (op == MDB_GET_BOTH || rc > 0)
						return MDB_NOTFOUND;
					rc = 0;
				}
				*data = olddata;

			} else {
				if (mc->mc_xcursor)
					mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
				if ((rc = mdb_node_read(mc, leaf, data)) != MDB_SUCCESS)
					return rc;
			}
		}

		/* The key already matches in all other cases */
		if (op == MDB_SET_RANGE || op == MDB_SET_KEY)
			MDB_GET_KEY(leaf, key);
		DMibDatabaseTrace("==> cursor placed on key [{}]", DKEY(key));

		return rc;
	}

	/** Move the cursor to the first item in the database. */
	int mdb_cursor_first(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data)
	{
		int		 rc;
		MDB_node	*leaf;

		if (mc->mc_xcursor) {
			MDB_CURSOR_UNREF(&mc->mc_xcursor->mx_cursor, 0);
			mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
		}

		if (!(mc->mc_flags & C_INITIALIZED) || mc->mc_top) {
			rc = mdb_page_search(mc, nullptr, MDB_PS_FIRST);
			if (rc != MDB_SUCCESS)
				return rc;
		}
		DMibCheck(IS_LEAF(mc->mc_pg[mc->mc_top]));

		leaf = NODEPTR(mc->mc_pg[mc->mc_top], 0);
		mc->mc_flags |= C_INITIALIZED;
		mc->mc_flags &= ~C_EOF;

		mc->mc_ki[mc->mc_top] = 0;

		if (IS_LEAF2(mc->mc_pg[mc->mc_top])) {
			key->m_Size = mc->mc_db->md_pad;
			key->m_pData = LEAF2KEY(mc->mc_pg[mc->mc_top], 0, key->m_Size);
			return MDB_SUCCESS;
		}

		if (data) {
			if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
				mdb_xcursor_init1(mc, leaf);
				rc = mdb_cursor_first(&mc->mc_xcursor->mx_cursor, data, nullptr);
				if (rc)
					return rc;
			} else {
				if ((rc = mdb_node_read(mc, leaf, data)) != MDB_SUCCESS)
					return rc;
			}
		}
		MDB_GET_KEY(leaf, key);
		return MDB_SUCCESS;
	}

	/** Move the cursor to the last item in the database. */
	int mdb_cursor_last(MDB_cursor *mc, CDatabaseValue *key, CDatabaseValue *data)
	{
		int		 rc;
		MDB_node	*leaf;

		if (mc->mc_xcursor) {
			MDB_CURSOR_UNREF(&mc->mc_xcursor->mx_cursor, 0);
			mc->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
		}

		if (!(mc->mc_flags & C_INITIALIZED) || mc->mc_top) {
			rc = mdb_page_search(mc, nullptr, MDB_PS_LAST);
			if (rc != MDB_SUCCESS)
				return rc;
		}
		DMibCheck(IS_LEAF(mc->mc_pg[mc->mc_top]));

		mc->mc_ki[mc->mc_top] = NUMKEYS(mc->mc_pg[mc->mc_top]) - 1;
		mc->mc_flags |= C_INITIALIZED|C_EOF;
		leaf = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);

		if (IS_LEAF2(mc->mc_pg[mc->mc_top])) {
			key->m_Size = mc->mc_db->md_pad;
			key->m_pData = LEAF2KEY(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top], key->m_Size);
			return MDB_SUCCESS;
		}

		if (data) {
			if (F_ISSET(leaf->mn_flags, F_DUPDATA)) {
				mdb_xcursor_init1(mc, leaf);
				rc = mdb_cursor_last(&mc->mc_xcursor->mx_cursor, data, nullptr);
				if (rc)
					return rc;
			} else {
				if ((rc = mdb_node_read(mc, leaf, data)) != MDB_SUCCESS)
					return rc;
			}
		}

		MDB_GET_KEY(leaf, key);
		return MDB_SUCCESS;
	}


	/** Touch all the pages in the cursor stack. Set mc_top.
	 *	Makes sure all the pages are writable, before attempting a write operation.
	 * @param[in] mc The cursor to operate on.
	 */
	int mdb_cursor_touch(MDB_cursor *mc)
	{
		int rc = MDB_SUCCESS;

		if (mc->mc_dbi >= CORE_DBS && !(*mc->mc_dbflag & (DB_DIRTY|DB_DUPDATA))) {
			/* Touch DB record of named DB */
			MDB_cursor mc2;
			MDB_xcursor mcx;
			if (TXN_DBI_CHANGED(mc->mc_txn, mc->mc_dbi))
				DMibError("Stale write transaction");
			mdb_cursor_init(&mc2, mc->mc_txn, MAIN_DBI, &mcx);
			rc = mdb_page_search(&mc2, &mc->mc_dbx->m_DatabaseName, MDB_PS_MODIFY);
			if (rc)
				 return rc;
			*mc->mc_dbflag |= DB_DIRTY;
		}
		mc->mc_top = 0;
		if (mc->mc_snum) {
			do {
				rc = mdb_page_touch(mc);
			} while (!rc && ++(mc->mc_top) < mc->mc_snum);
			mc->mc_top = mc->mc_snum-1;
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
	 * @param[in] mc The main cursor whose sorted-dups cursor is to be initialized.
	 */
	void mdb_xcursor_init0(MDB_cursor *mc)
	{
		MDB_xcursor *mx = mc->mc_xcursor;

		mx->mx_cursor.mc_xcursor = nullptr;
		mx->mx_cursor.mc_txn = mc->mc_txn;
		mx->mx_cursor.mc_db = &mx->mx_db;
		mx->mx_cursor.mc_dbx = &mx->mx_dbx;
		mx->mx_cursor.mc_dbi = mc->mc_dbi;
		mx->mx_cursor.mc_dbflag = &mx->mx_dbflag;
		mx->mx_cursor.mc_snum = 0;
		mx->mx_cursor.mc_top = 0;
		MC_SET_OVPG(&mx->mx_cursor, NULL);
		mx->mx_cursor.mc_flags = C_SUB | (mc->mc_flags & (C_ORIG_RDONLY|C_WRITEMAP));
		mx->mx_dbx.m_DatabaseName.m_Size = 0;
		mx->mx_dbx.m_DatabaseName.m_pData = nullptr;
		mx->mx_dbx.m_fKeyCompare = mc->mc_dbx->m_fDataCompare;
		mx->mx_dbx.m_fDataCompare = nullptr;
	}

	/** Final setup of a sorted-dups cursor.
	 *	Sets up the fields that depend on the data from the main cursor.
	 * @param[in] mc The main cursor whose sorted-dups cursor is to be initialized.
	 * @param[in] node The data containing the #MDB_db record for the
	 * sorted-dup database.
	 */
	void mdb_xcursor_init1(MDB_cursor *mc, MDB_node *node)
	{
		MDB_xcursor *mx = mc->mc_xcursor;

		mx->mx_cursor.mc_flags &= C_SUB|C_ORIG_RDONLY|C_WRITEMAP;
		if (node->mn_flags & F_SUBDATA) {
			memcpy(&mx->mx_db, NODEDATA(node), sizeof(MDB_db));
			mx->mx_cursor.mc_pg[0] = 0;
			mx->mx_cursor.mc_snum = 0;
			mx->mx_cursor.mc_top = 0;
		} else {
			MDB_page *fp = (MDB_page *)NODEDATA(node);
			mx->mx_db.md_pad = 0;
			mx->mx_db.md_flags = 0;
			mx->mx_db.md_depth = 1;
			mx->mx_db.md_branch_pages = 0;
			mx->mx_db.md_leaf_pages = 1;
			mx->mx_db.md_overflow_pages = 0;
			mx->mx_db.md_entries = NUMKEYS(fp);
			COPY_PGNO(mx->mx_db.md_root, fp->mp_p.p_pgno);
			mx->mx_cursor.mc_snum = 1;
			mx->mx_cursor.mc_top = 0;
			mx->mx_cursor.mc_flags |= C_INITIALIZED;
			mx->mx_cursor.mc_pg[0] = fp;
			mx->mx_cursor.mc_ki[0] = 0;
			if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesFixedSize) {
				mx->mx_db.md_flags = EDatabaseSubFlag_SortedDuplicatesFixedSize;
				mx->mx_db.md_pad = fp->mp_pad;
				if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicatesIntegerKeys)
					mx->mx_db.md_flags |= EDatabaseSubFlag_IntegerKeys;
			}
		}
		DMibDatabaseTrace("Sub-db -{} root page {}", mx->mx_cursor.mc_dbi, mx->mx_db.md_root);
		mx->mx_dbflag = DB_VALID|DB_USRVALID|DB_DUPDATA;
		if (NEED_CMP_CLONG(mx->mx_dbx.m_fKeyCompare, mx->mx_db.md_pad))
			mx->mx_dbx.m_fKeyCompare = g_mdb_cmp_clong;
	}

	/** Fixup a sorted-dups cursor due to underlying update.
	 *	Sets up some fields that depend on the data from the main cursor.
	 *	Almost the same as init1, but skips initialization steps if the
	 *	xcursor had already been used.
	 * @param[in] mc The main cursor whose sorted-dups cursor is to be fixed up.
	 * @param[in] src_mx The xcursor of an up-to-date cursor.
	 * @param[in] new_dupdata True if converting from a non-#F_DUPDATA item.
	 */
	void mdb_xcursor_init2(MDB_cursor *mc, MDB_xcursor *src_mx, int new_dupdata)
	{
		MDB_xcursor *mx = mc->mc_xcursor;

		if (new_dupdata) {
			mx->mx_cursor.mc_snum = 1;
			mx->mx_cursor.mc_top = 0;
			mx->mx_cursor.mc_flags |= C_INITIALIZED;
			mx->mx_cursor.mc_ki[0] = 0;
			mx->mx_dbflag = DB_VALID|DB_USRVALID|DB_DUPDATA;
	#if DMibPPtrBits < DMibDatabase_IDBits	/* matches mdb_xcursor_init1:NEED_CMP_CLONG() */
			mx->mx_dbx.m_fKeyCompare = src_mx->mx_dbx.m_fKeyCompare;
	#endif
		} else if (!(mx->mx_cursor.mc_flags & C_INITIALIZED)) {
			return;
		}
		mx->mx_db = src_mx->mx_db;
		mx->mx_cursor.mc_pg[0] = src_mx->mx_cursor.mc_pg[0];
		DMibDatabaseTrace("Sub-db -{} root page {}", mx->mx_cursor.mc_dbi, mx->mx_db.md_root);
	}

	/** Initialize a cursor for a given transaction and database. */
	void mdb_cursor_init(MDB_cursor *mc, MDB_txn *txn, CDatabaseSubID dbi, MDB_xcursor *mx)
	{
		mc->mc_next = nullptr;
		mc->mc_backup = nullptr;
		mc->mc_dbi = dbi;
		mc->mc_txn = txn;
		mc->mc_db = &txn->mt_dbs[dbi];
		mc->mc_dbx = &txn->mt_dbxs[dbi];
		mc->mc_dbflag = &txn->mt_dbflags[dbi];
		mc->mc_snum = 0;
		mc->mc_top = 0;
		mc->mc_pg[0] = 0;
		mc->mc_ki[0] = 0;
		MC_SET_OVPG(mc, NULL);
		mc->mc_flags = txn->mt_flags & (C_ORIG_RDONLY|C_WRITEMAP);
		if (txn->mt_dbs[dbi].md_flags & EDatabaseSubFlag_SortedDuplicates) {
			DMibCheck(mx != nullptr);
			mc->mc_xcursor = mx;
			mdb_xcursor_init0(mc);
		} else {
			mc->mc_xcursor = nullptr;
		}
		if (*mc->mc_dbflag & DB_STALE)
		{
			mdb_page_search(mc, nullptr, MDB_PS_ROOTONLY);
		}
	}

	/** Copy the contents of a cursor.
	 * @param[in] csrc The cursor to copy from.
	 * @param[out] cdst The cursor to copy to.
	 */
	void mdb_cursor_copy(const MDB_cursor *csrc, MDB_cursor *cdst)
	{
		uint32 i;

		cdst->mc_txn = csrc->mc_txn;
		cdst->mc_dbi = csrc->mc_dbi;
		cdst->mc_db  = csrc->mc_db;
		cdst->mc_dbx = csrc->mc_dbx;
		cdst->mc_snum = csrc->mc_snum;
		cdst->mc_top = csrc->mc_top;
		cdst->mc_flags = csrc->mc_flags;
		MC_SET_OVPG(cdst, MC_OVPG(csrc));

		for (i=0; i<csrc->mc_snum; i++) {
			cdst->mc_pg[i] = csrc->mc_pg[i];
			cdst->mc_ki[i] = csrc->mc_ki[i];
		}
	}

	/** Complete a delete operation started by #mdb_cursor_del(). */
	int mdb_cursor_del0(MDB_cursor *mc)
	{
		int rc;
		MDB_page *mp;
		indx_t ki;
		uint32 nkeys;
		MDB_cursor *m2, *m3;
		CDatabaseSubID dbi = mc->mc_dbi;

		ki = mc->mc_ki[mc->mc_top];
		mp = mc->mc_pg[mc->mc_top];
		mdb_node_del(mc, mc->mc_db->md_pad);
		mc->mc_db->md_entries--;
		{
			/* Adjust other cursors pointing to mp */
			for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
				m3 = (mc->mc_flags & C_SUB) ? &m2->mc_xcursor->mx_cursor : m2;
				if (! (m2->mc_flags & m3->mc_flags & C_INITIALIZED))
					continue;
				if (m3 == mc || m3->mc_snum < mc->mc_snum)
					continue;
				if (m3->mc_pg[mc->mc_top] == mp) {
					if (m3->mc_ki[mc->mc_top] == ki) {
						m3->mc_flags |= C_DEL;
						if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) {
							/* Sub-cursor referred into dataset which is gone */
							m3->mc_xcursor->mx_cursor.mc_flags &= ~(C_INITIALIZED|C_EOF);
						}
						continue;
					} else if (m3->mc_ki[mc->mc_top] > ki) {
						m3->mc_ki[mc->mc_top]--;
					}
					XCURSOR_REFRESH(m3, mc->mc_top, mp);
				}
			}
		}
		rc = mdb_rebalance(mc);

		if (rc == MDB_SUCCESS) {
			/* DB is totally empty now, just bail out.
			 * Other cursors adjustments were already done
			 * by mdb_rebalance and aren't needed here.
			 */
			if (!mc->mc_snum)
				return rc;

			mp = mc->mc_pg[mc->mc_top];
			nkeys = NUMKEYS(mp);

			/* Adjust other cursors pointing to mp */
			for (m2 = mc->mc_txn->mt_cursors[dbi]; !rc && m2; m2=m2->mc_next) {
				m3 = (mc->mc_flags & C_SUB) ? &m2->mc_xcursor->mx_cursor : m2;
				if (! (m2->mc_flags & m3->mc_flags & C_INITIALIZED))
					continue;
				if (m3->mc_snum < mc->mc_snum)
					continue;
				if (m3->mc_pg[mc->mc_top] == mp) {
					/* if m3 points past last node in page, find next sibling */
					if (m3->mc_ki[mc->mc_top] >= mc->mc_ki[mc->mc_top]) {
						if (m3->mc_ki[mc->mc_top] >= nkeys) {
							rc = mdb_cursor_sibling(m3, 1);
							if (rc == MDB_NOTFOUND) {
								m3->mc_flags |= C_EOF;
								rc = MDB_SUCCESS;
								continue;
							}
						}
						if (mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates) {
							MDB_node *node = NODEPTR(m3->mc_pg[m3->mc_top], m3->mc_ki[m3->mc_top]);
							/* If this node has dupdata, it may need to be reinited
							 * because its data has moved.
							 * If the xcursor was not initd it must be reinited.
							 * Else if node points to a subDB, nothing is needed.
							 * Else (xcursor was initd, not a subDB) needs mc_pg[0] reset.
							 */
							if (node->mn_flags & F_DUPDATA) {
								if (m3->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED) {
									if (!(node->mn_flags & F_SUBDATA))
										m3->mc_xcursor->mx_cursor.mc_pg[0] = (MDB_page *)NODEDATA(node);
								} else {
									mdb_xcursor_init1(m3, node);
									m3->mc_xcursor->mx_cursor.mc_flags |= C_DEL;
								}
							}
						}
					}
				}
			}
			mc->mc_flags |= C_DEL;
		}

		if (rc)
			mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
		return rc;
	}
}
