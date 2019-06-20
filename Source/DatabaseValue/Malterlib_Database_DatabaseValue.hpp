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
#include "Malterlib_Database_DatabaseValue_Descending.hpp"
#include "Malterlib_Database_DatabaseValue_Private.hpp"

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
	struct TCOrderedByteArrayCoder
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			if constexpr (t_bDescending)
				_Value.f_EncodeDescending(_Stream);
			else
				_Value.f_Encode(_Stream);
		}
		
		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			if constexpr (t_bDescending)
				o_Value.f_DecodeDescending(_Stream);
			else
				o_Value.f_Decode(_Stream);
		}
	};
	
	template <typename ...tfp_CValue>
	CDatabaseValue fg_EncodeLexicographicValue(CDatabaseValueCache &o_StreamCache, tfp_CValue const &...p_Value)
	{
		{
			NStream::CBinaryStreamMemoryRef<NStream::CBinaryStreamBigEndian, NContainer::TCGrowingVector<uint8>> Stream{o_StreamCache, 0};
			std::initializer_list<bool> Dummy =
				{
					[&]
					{
						fg_EncodeLexicographic(Stream, p_Value);
						return true;
					}
					()...
				}
			;
			(void)Dummy;
		}
		return CDatabaseValue{o_StreamCache};
	}
	
	template <typename ...tfp_CValue>
	auto fg_DecodeLexicographicTuple(CDatabaseValue const &_Value)
	{
		return NPrivate::fg_DecodeOrderedByteArrayImpl<tfp_CValue...>(_Value, typename NMeta::TCMakeConsecutiveIndices<sizeof...(tfp_CValue)>::CType());
	}
	
	template <typename tf_CValue>
	void fg_DecodeLexicographicValue(CDatabaseValue const &_Value, tf_CValue &o_Value)
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
		Stream.f_OpenRead(_Value.m_pData, _Value.m_Size);
		fg_DecodeLexicographic(Stream, o_Value);
	}

	template <typename tf_CValue>
	auto fg_DecodeLexicographicValue(CDatabaseValue const &_Value)
	{
		using CDecodedDescending = TCGetDescending<typename NTraits::TCRemoveQualifiers<tf_CValue>::CType>;

		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
		Stream.f_OpenRead(_Value.m_pData, _Value.m_Size);
		if constexpr (CDecodedDescending::mc_bDescending)
		{
			typename CDecodedDescending::CType Value;
			auto Descending = fg_Descending<CDecodedDescending::mc_bDescending>(Value);
			fg_DecodeLexicographic(Stream, Descending);
			return Value;
		}
		else
		{
			typename CDecodedDescending::CType Value;
			fg_DecodeLexicographic(Stream, Value);
			return Value;
		}
	}
	
	template <typename tf_CStream, typename tf_CValue>
	void fg_EncodeLexicographic(tf_CStream &_Stream, tf_CValue const &_Value)
	{
		using CDecodedDescending = TCGetDescending<typename NTraits::TCRemoveQualifiers<tf_CValue>::CType>;
		TCOrderedByteArrayCoder<typename NTraits::TCRemoveQualifiers<typename CDecodedDescending::CType>::CType, CDecodedDescending::mc_bDescending>::fs_Encode
			(
			 	_Stream
			 	, CDecodedDescending::fs_GetValue(_Value)
			)
		;
	}
	
	template <typename tf_CStream, typename tf_CValue>
	void fg_DecodeLexicographic(tf_CStream &_Stream, tf_CValue &o_Value)
	{
		using CDecodedDescending = TCGetDescending<typename NTraits::TCRemoveQualifiers<tf_CValue>::CType>;
		TCOrderedByteArrayCoder<typename NTraits::TCRemoveQualifiers<typename CDecodedDescending::CType>::CType, CDecodedDescending::mc_bDescending>::fs_Decode
			(
			 	_Stream
			 	, CDecodedDescending::fs_GetValue(o_Value)
			)
		;
	}
	
	template <typename tf_CValue>
	CDatabaseValue fg_RawDatabaseValue(tf_CValue const &_Value)
	{
		CDatabaseValue DatabaseValue;
		DatabaseValue.m_pData = reinterpret_cast<uint8 const *>(&_Value);
		DatabaseValue.m_Size = sizeof(_Value);
		return DatabaseValue;
	}
	
	template <typename tf_CValue>
	tf_CValue const &fg_RawDatabaseValue(CDatabaseValue const &_Value)
	{
		DMibFastCheck(sizeof(tf_CValue) == _Value.m_Size);
		return *reinterpret_cast<tf_CValue const *>(_Value.m_pData);
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
}
