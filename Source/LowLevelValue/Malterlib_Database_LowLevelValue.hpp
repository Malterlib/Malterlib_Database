// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include "Malterlib_Database_LowLevelValue_Integer.hpp"
#include "Malterlib_Database_LowLevelValue_Float.hpp"
#include "Malterlib_Database_LowLevelValue_String.hpp"
#include "Malterlib_Database_LowLevelValue_Vector.hpp"
#include "Malterlib_Database_LowLevelValue_Set.hpp"
#include "Malterlib_Database_LowLevelValue_Map.hpp"
#include "Malterlib_Database_LowLevelValue_Private.hpp"

namespace NMib::NDatabase
{
	template <typename t_CType, bool t_bDescending, typename t_CEnableIf>
	struct TCOrderedByteArrayCoder
	{
		using CInteger = typename NTraits::TCIntFromSize<sizeof(t_CType)>::CType;
		
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			_Value.f_Encode(_Stream);
		}
		
		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			o_Value.f_Decode(_Stream);
		}			
	};
	
	template <typename t_CType>
	TCDescending<t_CType>::TCDescending(t_CType &_Value)
		: m_Value(_Value)
	{
	}
	
	template <typename tf_CValue>
	TCDescending<tf_CValue> fg_Descending(tf_CValue &_Value)
	{
		return TCDescending<tf_CValue>(_Value); 
	}

	template <typename ...tfp_CValue>
	CLowLevelValueDynamic fg_EncodeValue(CLowLevelValueCache &o_StreamCache, tfp_CValue const &...p_Value)
	{
		{
			NStream::CBinaryStreamMemoryRef<NStream::CBinaryStreamBigEndian, NContainer::TCGrowingVector<uint8>> Stream{o_StreamCache, 0};
			std::initializer_list<bool> Dummy =
				{
					[&]
					{
						fg_Encode(Stream, p_Value);
						return true;
					}
					()...
				}
			;
			(void)Dummy;
		}
		return CLowLevelValueDynamic{o_StreamCache};
	}
	
	template <typename ...tfp_CValue>
	NContainer::TCTuple<tfp_CValue...> fg_DecodeValue(CLowLevelValue const &_Value)
	{
		return NPrivate::fg_DecodeOrderedByteArrayImpl<tfp_CValue...>(_Value, typename NMeta::TCMakeConsecutiveIndices<sizeof...(tfp_CValue)>::CType());
	}
	
	template <typename tf_CValue>
	void fg_DecodeValue(CLowLevelValue const &_Value, tf_CValue &o_Value)
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
		Stream.f_OpenRead(_Value.m_pData, _Value.m_Size);
		fg_Decode(Stream, o_Value);
	}

	template <typename tf_CValue>
	tf_CValue fg_DecodeOneValue(CLowLevelValue const &_Value)
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
		Stream.f_OpenRead(_Value.m_pData, _Value.m_Size);
		tf_CValue Value;
		fg_Decode(Stream, Value);
		return Value;
	}
	
	template <typename tf_CStream, typename tf_CValue>
	void fg_Encode(tf_CStream &_Stream, tf_CValue const &_Value)
	{
		using CDecodedDescending = NPrivate::TCGetDescending<tf_CValue>;
		TCOrderedByteArrayCoder<typename CDecodedDescending::CType, CDecodedDescending::mc_bDescending>::fs_Encode(_Stream, CDecodedDescending::fs_GetValue(_Value));
	}
	
	template <typename tf_CStream, typename tf_CValue>
	void fg_Decode(tf_CStream &_Stream, tf_CValue &o_Value)
	{
		using CDecodedDescending = NPrivate::TCGetDescending<tf_CValue>;
		TCOrderedByteArrayCoder<tf_CValue, CDecodedDescending::mc_bDescending>::fs_Decode(_Stream, o_Value);
	}
	
	template <typename tf_CValue>
	CLowLevelValue fg_RawValue(tf_CValue const &_Value)
	{
		CLowLevelValue LowLevelValue;
		LowLevelValue.m_pData = reinterpret_cast<uint8 const *>(&_Value);
		LowLevelValue.m_Size = sizeof(_Value);
		return LowLevelValue;
	}
	
	template <typename tf_CValue>
	tf_CValue const &fg_RawValue(CLowLevelValue const &_Value)
	{
		DMibFastCheck(sizeof(tf_CValue) == _Value.m_Size);
		return *reinterpret_cast<tf_CValue const *>(_Value.m_pData);
	}
}
