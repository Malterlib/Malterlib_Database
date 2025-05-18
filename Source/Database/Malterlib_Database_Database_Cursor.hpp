// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename ...tfp_CKey>
	void CDatabaseCursorCommon::f_SetPrefix(tfp_CKey const & ...p_Prefixes)
	{
		return f_SetPrefix(CDatabaseValue::fs_ToLexicographic(mp_PrefixCache, p_Prefixes...));
	}

	template <typename tf_CKey>
	auto CDatabaseCursorCommon::f_Key() const
	{
		return f_Key().f_FromLexicographic<tf_CKey>();
	}

	template <typename ...tfp_CKeys>
	auto CDatabaseCursorCommon::f_KeyTuple() const
	{
		return f_Key().f_TupleFromLexicographic<tfp_CKeys...>();
	}

	template <typename ...tfp_CKeys>
	bool CDatabaseCursorCommon::f_FindEqual(tfp_CKeys const & ...p_Keys)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_FindEqual(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, p_Keys...));
	}

	template <typename ...tfp_CKeys>
	bool CDatabaseCursorCommon::f_FindLowerBound(tfp_CKeys const & ...p_Keys)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_FindLowerBound(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, p_Keys...));
	}

	inline CDatabaseValue const &CDatabaseCursor::f_Value() const
	{
		return mp_Value;
	}

	template <typename tf_CValue>
	auto CDatabaseCursor::f_Value() const
	{
		return f_Value().f_FromStream<tf_CValue>();
	}

	template <typename ...tfp_CValue>
	auto CDatabaseCursor::f_ValueTuple() const
	{
		return f_Value().f_TupleFromStream<tfp_CValue...>();
	}

	template <typename tf_CValue>
	void CDatabaseCursorWrite::f_SetValue(tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		f_SetValue(CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorWrite::f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::cIsSame<tf_CKey, CDatabaseValue>)
			f_Insert(_Key, CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::cIsSame<tf_CValue, CDatabaseValue>)
			f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey>
	CDatabaseValue CDatabaseCursorWrite::f_InsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_InsertInplace(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorWrite::f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::cIsSame<tf_CKey, CDatabaseValue>)
			f_Upsert(_Key, CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::cIsSame<tf_CValue, CDatabaseValue>)
			f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToStream(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey>
	CDatabaseValue CDatabaseCursorWrite::f_UpsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;
		return f_UpsertInplace(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _bAppend);
	}
}
