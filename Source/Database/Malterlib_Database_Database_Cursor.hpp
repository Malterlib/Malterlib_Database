// Copyright © 2016 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename tf_CValue>
	void CDatabaseCursorWrite::f_SetValue(tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		f_SetValue(fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorWrite::f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			f_Insert(_Key, fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			f_Insert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Insert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey>
	CDatabaseValue CDatabaseCursorWrite::f_InsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_InsertInplace(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorWrite::f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			f_Upsert(_Key, fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			f_Upsert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			f_Upsert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey>
	CDatabaseValue CDatabaseCursorWrite::f_UpsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;
		return f_UpsertInplace(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	bool CDatabaseCursorDuplicateData::f_FindEqual(tf_CKey const &_Key, tf_CValue const &_Value)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_FindEqual(_Key, fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value));
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_FindEqual(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _Value);
		else
			return f_FindEqual(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value));
	}

	template <typename tf_CKey, typename tf_CValue>
	bool CDatabaseCursorDuplicateData::f_FindLowerBound(tf_CKey const &_Key, tf_CValue const &_Value)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_FindLowerBound(_Key, fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value));
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_FindLowerBound(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _Value);
		else
			return f_FindLowerBound(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value));
	}

	template <typename tf_CValue>
	void CDatabaseCursorDuplicateDataWrite::f_SetValue(tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		f_SetValue(fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorDuplicateDataWrite::f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_Insert(_Key, fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_Insert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			return f_Insert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename tf_CKey, typename tf_CValue>
	void CDatabaseCursorDuplicateDataWrite::f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		if constexpr (NTraits::TCIsSame<tf_CKey, CDatabaseValue>::mc_Value)
			return f_Upsert(_Key, fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
		else if constexpr (NTraits::TCIsSame<tf_CValue, CDatabaseValue>::mc_Value)
			return f_Upsert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), _Value, _bAppend);
		else
			return f_Upsert(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, _Key), fg_EncodeLexicographicValue(ThreadLocal.m_ValueCache, _Value), _bAppend);
	}

	template <typename ...tfp_CKey>
	void CDatabaseCursorCommon::f_SetPrefix(tfp_CKey const & ...p_Prefixes)
	{
		return f_SetPrefix(fg_EncodeLexicographicValue(mp_PrefixCache, p_Prefixes...));
	}

	template <typename tf_CKey>
	auto CDatabaseCursorCommon::f_Key() const
	{
		return fg_DecodeLexicographicValue<tf_CKey>(f_Key());
	}

	template <typename ...tfp_CKeys>
	auto CDatabaseCursorCommon::f_KeyTuple() const
	{
		return fg_DecodeLexicographicTuple<tfp_CKeys...>(f_Key());
	}

	template <typename ...tfp_CKeys>
	bool CDatabaseCursorCommon::f_FindEqual(tfp_CKeys const & ...p_Keys)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_FindEqual(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, p_Keys...));
	}

	template <typename ...tfp_CKeys>
	bool CDatabaseCursorCommon::f_FindLowerBound(tfp_CKeys const & ...p_Keys)
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;

		return f_FindLowerBound(fg_EncodeLexicographicValue(ThreadLocal.m_KeyCache, p_Keys...));
	}

	template <typename tf_CValue>
	auto CDatabaseCursor::f_Value() const
	{
		return fg_DecodeLexicographicValue<tf_CValue>(f_Value());
	}
	
	template <typename ...tfp_CValue>
	auto CDatabaseCursor::f_ValueTuple() const
	{
		return fg_DecodeLexicographicTuple<tfp_CValue...>(f_Value());
	}
}
