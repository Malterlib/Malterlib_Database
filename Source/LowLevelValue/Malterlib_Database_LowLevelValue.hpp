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
	NContainer::TCVector<uint8> fg_EncodeOrderedByteArray(tfp_CValue const &...p_Value)
	{
		NStream::CBinaryStreamMemory<NStream::CBinaryStreamBigEndian> Stream;
		std::initializer_list<bool> Dummy =
			{
				[&]
				{
					using CDecodedDescending = NPrivate::TCGetDescending<typename NTraits::TCRemoveReferenceAndQualifiers<decltype(p_Value)>::CType>;
					TCOrderedByteArrayCoder<typename CDecodedDescending::CType, CDecodedDescending::mc_bDescending>::fs_Encode(Stream, CDecodedDescending::fs_GetValue(p_Value));
					return true;
				}
				()...
			}
		;
		return Stream.f_MoveVector();
	}

	template <typename ...tfp_CValue>
	NContainer::TCTuple<tfp_CValue...> fg_DecodeOrderedByteArray(CLowLevelValue const &_Value)
	{
		return NPrivate::fg_DecodeOrderedByteArrayImpl<tfp_CValue...>(_Value, typename NMeta::TCMakeConsecutiveIndices<sizeof...(tfp_CValue)>::CType());
	}
}
