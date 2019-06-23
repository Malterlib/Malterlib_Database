// Copyright © 2016 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	inline CDatabaseValue const &CDatabaseCursorDuplicateData::f_Value() const
	{
		return mp_Value;
	}

	template <typename tf_CValue>
	auto CDatabaseCursorDuplicateData::f_Value() const
	{
		return f_Value().f_FromLexicographic<tf_CValue>();
	}

	template <typename ...tfp_CValue>
	auto CDatabaseCursorDuplicateData::f_ValueTuple() const
	{
		return f_Value().f_TupleFromLexicographic<tfp_CValue...>();
	}

	template <typename tf_CKey, typename tf_CValue>
	bool CDatabaseCursorDuplicateData::f_FindEqual(tf_CKey const &_Key, tf_CValue const &_Value)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_FindEqual(_Key, CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value));
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_FindEqual(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value);
		else
			return f_FindEqual(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value));
	}

	template <typename tf_CKey, typename tf_CValue>
	bool CDatabaseCursorDuplicateData::f_FindLowerBound(tf_CKey const &_Key, tf_CValue const &_Value)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_FindLowerBound(_Key, CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value));
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_FindLowerBound(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value);
		else
			return f_FindLowerBound(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value));
	}

	template <typename tf_CValue>
	void CDatabaseCursorDuplicateDataWrite::f_SetValue(tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		f_SetValue(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorDuplicateDataWrite::f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_Insert(_Key, CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			return f_Insert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorDuplicateDataWrite::f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_Upsert(_Key, CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			return f_Upsert(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_KeyCache, _Key), CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}
}
