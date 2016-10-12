// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	namespace NPrivate
	{
		template <typename t_CValue>
		struct TCGetDescending
		{
			static constexpr bool mc_bDescending = false;
			using CType = t_CValue;
			template <typename tf_CValue>
			static auto fs_GetValue(tf_CValue &&_Value)
			{
				return fg_Forward<tf_CValue>(_Value);
			}
		};
		
		template <typename ...tfp_CValue, mint ...tfp_Indices>
		NContainer::TCTuple<typename NPrivate::TCGetDescending<tfp_CValue>::CType...> fg_DecodeOrderedByteArrayImpl(CLowLevelValue const &_Value, NMeta::TCIndices<tfp_Indices...> _Dummy)
		{
			NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
			Stream.f_OpenRead(_Value.m_pData, _Value.m_Size);
			NContainer::TCTuple<typename NPrivate::TCGetDescending<tfp_CValue>::CType...> Values;
			
			std::initializer_list<bool> Dummy =
				{
					[&]
					{
						fg_Decode(Stream, NContainer::fg_Get<tfp_Indices>(Values));
						return true;
					}
					()...
				}
			;
			(void)Dummy;
			
			return Values;
		}
	}
}
