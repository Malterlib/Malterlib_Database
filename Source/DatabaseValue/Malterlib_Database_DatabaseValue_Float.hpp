// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CType>
	struct TCDatabaseStreamLexicographic<t_CType, false, TCEnableIf<NTraits::cIsFloat<t_CType>>>
	{
		using CInteger = NTraits::TCIntFromSize<sizeof(t_CType)>;

		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			CInteger Integer;
			if constexpr (requires() { _Value.f_GetAsInteger(); })
				Integer = _Value.f_GetAsInteger();
			else
				Integer = fg_BitCast<CInteger>(_Value);

			if (t_CType::mc_SignBits)
			{
				auto SignBit = CInteger(1) << t_CType::mc_SignBitPos;
				auto SignExtended = (Integer << ((sizeof(CInteger)*8 - 1) - t_CType::mc_SignBitPos)) >> (sizeof(CInteger)*8 - 1);
				Integer ^= (SignExtended | SignBit);
			}

			_Stream << Integer;
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			CInteger Integer;
			
			_Stream >> Integer;

			if (t_CType::mc_SignBits)
			{
				auto SignBit = CInteger(1) << t_CType::mc_SignBitPos;
				auto SignExtended = ((~Integer) << ((sizeof(CInteger)*8 - 1) - t_CType::mc_SignBitPos)) >> (sizeof(CInteger)*8 - 1);
				Integer ^= SignExtended | SignBit; 
			}
			
			if constexpr (requires(CInteger _Int) { o_Value.f_SetAsInteger(_Int); })
				o_Value.f_SetAsInteger(Integer);
			else
				o_Value = fg_BitCast<t_CType>(Integer);
		}
	};
}
