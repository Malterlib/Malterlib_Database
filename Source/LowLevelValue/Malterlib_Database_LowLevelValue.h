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
		
		template <typename ...tfp_CValue>
		NContainer::TCVector<uint8> fg_EncodeOrderedByteArray(tfp_CValue const &...p_Value);
		
		template <typename ...tfp_CValue>
		NContainer::TCTuple<tfp_CValue...> fg_DecodeOrderedByteArray(CLowLevelValue const &_Value);

		struct CLowLevelValueDynamic : public CLowLevelValue
		{
			CLowLevelValueDynamic(NContainer::TCVector<uint8> &&_Value)
				: mp_Value(fg_Move(_Value)) 
			{
				m_Size = mp_Value.f_GetLen();
				m_pData = mp_Value.f_GetArray();
			}
			
		private:
			NContainer::TCVector<uint8> mp_Value;
		};
		
		template <typename ...tfp_CValue>
		CLowLevelValueDynamic fg_EncodeValue(tfp_CValue const &...p_Value)
		{
			return CLowLevelValueDynamic{fg_EncodeOrderedByteArray(p_Value...)};
		}
		
		template <typename ...tfp_CValue>
		auto fg_DecodeValue(CLowLevelValue const &_Value)
		{
			return fg_DecodeOrderedByteArray<tfp_CValue...>(_Value);
		}
	}
}

#include "Malterlib_Database_LowLevelValue.hpp"
