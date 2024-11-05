// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include "Malterlib_Database_DatabaseValue_Integer.hpp"
#include "Malterlib_Database_DatabaseValue_Float.hpp"
#include "Malterlib_Database_DatabaseValue_String.hpp"
#include "Malterlib_Database_DatabaseValue_Vector.hpp"
#include "Malterlib_Database_DatabaseValue_Set.hpp"
#include "Malterlib_Database_DatabaseValue_Map.hpp"
#include "Malterlib_Database_DatabaseValue_Tuple.hpp"
#include "Malterlib_Database_DatabaseValue_Variant.hpp"
#include "Malterlib_Database_DatabaseValue_Optional.hpp"
#include "Malterlib_Database_DatabaseValue_Time.hpp"
#include "Malterlib_Database_DatabaseValue_Descending.hpp"
#include "Malterlib_Database_DatabaseValue_Private.hpp"
#include "../Database/Malterlib_Database_Database_ThreadLocal.h"

namespace NMib::NDatabase
{
	inline CDatabaseValue::CDatabaseValue(CDatabaseValueCache const &_Value)
		: m_Size(_Value.f_GetLen())
		, m_pData(_Value.f_GetArray())
	{
	}

	inline bool CDatabaseValue::f_IsValid() const
	{
		return !!m_pData;
	}

	template <typename t_CType, bool t_bDescending, typename t_CEnableIf>
	struct TCDatabaseStreamLexicographic
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			if constexpr (t_bDescending)
				_Value.f_FeedLexicographicDescending(_Stream);
			else
				_Value.f_FeedLexicographic(_Stream);
		}
		
		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			if constexpr (t_bDescending)
				o_Value.f_ConsumeLexicographicDescending(_Stream);
			else
				o_Value.f_ConsumeLexicographic(_Stream);
		}
	};
	
	template <typename ...tfp_CValue>
	CDatabaseValue CDatabaseValue::fs_ToLexicographic(CDatabaseValueCache &o_StreamCache, tfp_CValue const &...p_Value)
	{
		{
			NStream::CBinaryStreamMemoryRef<NStream::CBinaryStreamBigEndian, CDatabaseValueCache> Stream{o_StreamCache, 0};
			(
				[&]
				{
					CDatabaseValue::fs_FeedLexicographic(Stream, p_Value);
				}
				()
				, ...
			);
		}
		return CDatabaseValue{o_StreamCache};
	}

	template <typename ...tfp_CValue>
	CDatabaseValue CDatabaseValue::fs_ToStream(CDatabaseValueCache &o_StreamCache, NStorage::TCTuple<tfp_CValue...> const &_Value)
	{
		NStream::CBinaryStreamMemoryRef<NStream::CBinaryStreamDefault, CDatabaseValueCache> Stream{o_StreamCache, 0};

		NStorage::fg_TupleApply
			(
				[&](auto && ...p_Values) -> void
				{
					(
						[&]
						{
							Stream << p_Values;
						}
						()
						, ...
					);
				}
				, _Value
			)
		;
		return CDatabaseValue{o_StreamCache};
	}

	template <typename ...tfp_CValue>
	CDatabaseValue CDatabaseValue::fs_ToStream(CDatabaseValueCache &o_StreamCache, tfp_CValue const &...p_Value)
	{
		{
			NStream::CBinaryStreamMemoryRef<NStream::CBinaryStreamDefault, CDatabaseValueCache> Stream{o_StreamCache, 0};
			(
				[&]
				{
					Stream << p_Value;
				}
				()
				, ...
			);
		}
		return CDatabaseValue{o_StreamCache};
	}
	
	template <typename ...tfp_CValue>
	auto CDatabaseValue::f_TupleFromLexicographic() const
	{
		return NPrivate::fg_ConsumeLexicographicByteArrayImpl<tfp_CValue...>(*this, typename NMeta::TCMakeConsecutiveIndices<sizeof...(tfp_CValue)>::CType());
	}
	
	template <typename tf_CValue>
	void CDatabaseValue::f_FromLexicographic(tf_CValue &o_Value) const
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
		Stream.f_OpenRead(m_pData, m_Size);
		CDatabaseValue::fs_ConsumeLexicographic(Stream, o_Value);
	}

	template <typename tf_CValue>
	auto CDatabaseValue::f_FromLexicographic() const
	{
		using CDecodedDescending = TCGetDescending<typename NTraits::TCRemoveQualifiers<tf_CValue>::CType>;

		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
		Stream.f_OpenRead(m_pData, m_Size);
		if constexpr (CDecodedDescending::mc_bDescending)
		{
			typename CDecodedDescending::CType Value;
			auto Descending = fg_Descending<CDecodedDescending::mc_bDescending>(Value);
			CDatabaseValue::fs_ConsumeLexicographic(Stream, Descending);
			return Value;
		}
		else
		{
			typename CDecodedDescending::CType Value;
			CDatabaseValue::fs_ConsumeLexicographic(Stream, Value);
			return Value;
		}
	}

	template <typename ...tfp_CValue>
	auto CDatabaseValue::f_TupleFromStream() const
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamDefault> Stream;
		Stream.f_OpenRead(m_pData, m_Size);

		NStorage::TCTuple<tfp_CValue...> Tuple;
		Stream >> Tuple;
		return Tuple;
	}

	template <typename tf_CValue>
	auto CDatabaseValue::f_FromStream() const
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamDefault> Stream;
		Stream.f_OpenRead(m_pData, m_Size);

		tf_CValue Value;
		Stream >> Value;
		return Value;
	}

	template <typename tf_CValue>
	void CDatabaseValue::f_FromStream(tf_CValue &o_Value) const
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamDefault> Stream;
		Stream.f_OpenRead(m_pData, m_Size);

		Stream >> o_Value;
	}

	template <typename tf_CStream, typename tf_CValue>
	void CDatabaseValue::fs_FeedLexicographic(tf_CStream &_Stream, tf_CValue const &_Value)
	{
		using CDecodedDescending = TCGetDescending<typename NTraits::TCRemoveQualifiers<tf_CValue>::CType>;
		TCDatabaseStreamLexicographic<typename NTraits::TCRemoveQualifiers<typename CDecodedDescending::CType>::CType, CDecodedDescending::mc_bDescending>::fs_Encode
			(
				_Stream
				, CDecodedDescending::fs_GetValue(_Value)
			)
		;
	}
	
	template <typename tf_CStream, typename tf_CValue>
	void CDatabaseValue::fs_ConsumeLexicographic(tf_CStream &_Stream, tf_CValue &&o_Value)
	{
		using CDecodedDescending = TCGetDescending<typename NTraits::TCRemoveReferenceAndQualifiers<tf_CValue>::CType>;
		TCDatabaseStreamLexicographic<typename NTraits::TCRemoveQualifiers<typename CDecodedDescending::CType>::CType, CDecodedDescending::mc_bDescending>::fs_Decode
			(
				_Stream
				, CDecodedDescending::fs_GetValue(o_Value)
			)
		;
	}
	
	template <typename tf_CValue>
	CDatabaseValue CDatabaseValue::fs_Raw(tf_CValue const &_Value)
	{
		CDatabaseValue DatabaseValue;
		DatabaseValue.m_pData = reinterpret_cast<uint8 const *>(&_Value);
		DatabaseValue.m_Size = sizeof(_Value);
		return DatabaseValue;
	}
	
	template <typename tf_CValue>
	tf_CValue const &CDatabaseValue::f_Raw() const
	{
		DMibFastCheck(sizeof(tf_CValue) == m_Size);
		return *reinterpret_cast<tf_CValue const *>(m_pData);
	}

	template <typename tf_CStr>
	void CDatabaseValue::f_Format(tf_CStr &o_String) const
	{
		uint8 const *pDataEnd = m_pData + m_Size;
		typename tf_CStr::CFormat Formatter{"{nfh,sf0,sj2}"};
		uint8 ValueToFormat;
		Formatter << ValueToFormat;

		o_String += "[";

		for (uint8 const *pData = m_pData; pData != pDataEnd; ++pData)
		{
			ValueToFormat = *pData;
			o_String += Formatter;
		}

		o_String += "]";
	}

	template <typename tf_CValue>
	bool CDatabaseValue::f_HasPrefix(tf_CValue const &_Right) const
	{
		auto &ThreadLocal = **g_DatabaseThreadLocal;
		return f_HasPrefix(CDatabaseValue::fs_ToLexicographic(ThreadLocal.m_ValueCache, _Right));
	}
}
