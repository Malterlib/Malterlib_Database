// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CType>
	struct TCOrderedByteArrayCoderAutoDescending<t_CType, TCEnableIfType<NTraits::TCIsFloat<t_CType>::mc_Value>>
	{
		static constexpr bool mc_bValue = false;
	};

	template <typename t_CType, bool t_bDescending>
	struct TCOrderedByteArrayCoder<t_CType, t_bDescending, TCEnableIfType<NTraits::TCIsFloat<t_CType>::mc_Value>>
	{
		using CInteger = typename NTraits::TCIntFromSize<sizeof(t_CType)>::CType;

		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			CInteger Integer = reinterpret_cast<CInteger const &>(_Value);

			if (t_CType::ESignBits)
			{
				auto SignBit = CInteger(1) << t_CType::ESignBitPos;
				auto SignExtended = (Integer << ((sizeof(CInteger)*8 - 1) - t_CType::ESignBitPos)) >> (sizeof(CInteger)*8 - 1);
				Integer ^= (SignExtended | SignBit);
			}

			if constexpr (t_bDescending)
				_Stream << ~Integer;
			else
				_Stream << Integer;
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			CInteger Integer;
			
			_Stream >> Integer;
			if constexpr (t_bDescending)
				Integer = ~Integer;

			if (t_CType::ESignBits)
			{
				auto SignBit = CInteger(1) << t_CType::ESignBitPos;
				auto SignExtended = ((~Integer) << ((sizeof(CInteger)*8 - 1) - t_CType::ESignBitPos)) >> (sizeof(CInteger)*8 - 1);
				Integer ^= SignExtended | SignBit; 
			}
			
			o_Value = reinterpret_cast<t_CType const &>(Integer);
		}
	};
}
