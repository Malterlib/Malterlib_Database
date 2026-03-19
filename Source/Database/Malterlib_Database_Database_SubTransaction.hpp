// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename tf_CKey, typename tf_CValue>
	bool CDatabaseSubReadTransaction::f_Get(tf_CKey const &_Key, tf_CValue &o_Value)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		CDatabaseValue Value;

		if (!f_Get(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), Value))
			return false;

		Value.f_FromStream(o_Value);

		return true;
	}

	template <typename tf_CKey>
	bool CDatabaseSubReadTransaction::f_Exists(tf_CKey const &_Key)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;
		return f_Exists(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key));
	}

	template <typename tf_CKey, typename tf_CValue>
	bool CDatabaseSubReadTransaction::f_GetDuplicateData(tf_CKey const &_Key, tf_CValue &o_Value)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		CDatabaseValue Value;

		if (!f_Get(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), Value))
			return false;

		Value.f_FromLexicographic(o_Value);

		return true;
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseSubWriteTransaction::f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::cIsSame<tf_CKey, CDatabaseValue>)
			f_Insert(_Key, CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::cIsSame<tf_CValue, CDatabaseValue>)
			f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseSubWriteTransaction::f_InsertDuplicateData(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::cIsSame<tf_CKey, CDatabaseValue>)
			f_Insert(_Key, CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::cIsSame<tf_CValue, CDatabaseValue>)
			f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey>
	CDatabaseValue CDatabaseSubWriteTransaction::f_InsertInplace(tf_CKey const &_Key, umint _DataSize, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_InsertInplace(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseSubWriteTransaction::f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::cIsSame<tf_CKey, CDatabaseValue>)
			f_Upsert(_Key, CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::cIsSame<tf_CValue, CDatabaseValue>)
			f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseSubWriteTransaction::f_UpsertDuplicateData(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::cIsSame<tf_CKey, CDatabaseValue>)
			f_Upsert(_Key, CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::cIsSame<tf_CValue, CDatabaseValue>)
			f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey>
	CDatabaseValue CDatabaseSubWriteTransaction::f_UpsertInplace(tf_CKey const &_Key, umint _DataSize, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_UpsertInplace(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _bAppend);
	}

	template <typename tf_CKey>
	void CDatabaseSubWriteTransaction::f_Delete(tf_CKey const &_Key)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_Delete(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key));
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseSubWriteTransaction::f_DeleteData(tf_CKey const &_Key, tf_CValue const &_Value)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::cIsSame<tf_CKey, CDatabaseValue>)
			f_DeleteData(_Key, CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value));
		else if constexpr (NTraits::cIsSame<tf_CValue, CDatabaseValue>)
			f_DeleteData(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value);
		else
			f_DeleteData(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value));
	}

	template <typename ...tfp_CKeys>
	CDatabaseCursor CDatabaseSubReadTransaction::f_ReadCursor(tfp_CKeys const & ...p_Prefixes)
	{
		auto Cursor = f_ReadCursor();
		Cursor.f_SetPrefix(p_Prefixes...);
		return Cursor;
	}

	template <typename ...tfp_CKeys>
	CDatabaseCursorDuplicateData CDatabaseSubReadTransaction::f_ReadCursorDuplicateData(tfp_CKeys const & ...p_Prefixes)
	{
		auto Cursor = f_ReadCursorDuplicateData();
		Cursor.f_SetPrefix(p_Prefixes...);
		return Cursor;
	}

	template <typename ...tfp_CKeys>
	CDatabaseCursorWrite CDatabaseSubWriteTransaction::f_WriteCursor(tfp_CKeys const & ...p_Prefixes)
	{
		auto Cursor = f_WriteCursor();
		Cursor.f_SetPrefix(p_Prefixes...);
		return Cursor;
	}

	template <typename ...tfp_CKeys>
	CDatabaseCursorDuplicateDataWrite CDatabaseSubWriteTransaction::f_WriteCursorDuplicateData(tfp_CKeys const & ...p_Prefixes)
	{
		auto Cursor = f_WriteCursorDuplicateData();
		Cursor.f_SetPrefix(p_Prefixes...);
		return Cursor;
	}
}
