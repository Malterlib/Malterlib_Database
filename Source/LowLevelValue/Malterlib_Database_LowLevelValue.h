// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Type/Traits>

#include <Mib/Database/LowLevel>

namespace NMib
{
	namespace NDatabase
	{
		template <typename t_CType, bool t_bDescending, typename t_CEnableIf = void>
		struct TCOrderedByteArrayCoder;

		template <typename t_CType>
		struct TCDescending
		{
			t_CType &m_Value;
			TCDescending(t_CType &_Value);
		};
		
		template <typename tf_CValue>
		TCDescending<tf_CValue> fg_Descending(tf_CValue &_Value);
		
		struct CLowLevelValueDynamic : public CLowLevelValue
		{
			CLowLevelValueDynamic(NContainer::TCGrowingVector<uint8> const &_Value)
			{
				m_Size = _Value.f_GetLen();
				m_pData = _Value.f_GetArray();
			}
		};
		
		using CLowLevelValueCache = NContainer::TCGrowingVector<uint8>;
		
		template <typename tf_CStream, typename tf_CValue>
		void fg_Encode(tf_CStream &_Stream, tf_CValue const &_Value);

		template <typename tf_CStream, typename tf_CValue>
		void fg_Decode(tf_CStream &_Stream, tf_CValue &o_Value);
		
		template <typename ...tfp_CValue>
		CLowLevelValueDynamic fg_EncodeValue(CLowLevelValueCache &o_StreamCache, tfp_CValue const &...p_Value);
		
		template <typename ...tfp_CValue>
		NContainer::TCTuple<tfp_CValue...> fg_DecodeValue(CLowLevelValue const &_Value);

		template <typename tf_CValue>
		void fg_DecodeValue(CLowLevelValue const &_Value, tf_CValue &o_Value);
		
		template <typename tf_CValue>
		CLowLevelValue fg_RawValue(tf_CValue const &_Value);
		
		template <typename tf_CValue>
		tf_CValue const &fg_RawValue(CLowLevelValue const &_Value);
		
	}
}

#include "Malterlib_Database_LowLevelValue.hpp"

#ifndef DMibPNoShortCuts
using namespace NMib::NDatabase;
#endif
