// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CType>
	struct TCDatabaseStreamLexicographic<t_CType, false, TCEnableIf<NTraits::cIsInteger<t_CType> && NTraits::cIsSigned<t_CType>>>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			t_CType Value;
			Value = _Value ^ (t_CType(1) << (sizeof(t_CType) * 8 - 1));
			_Stream << Value;
		}
		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			t_CType Value;
			_Stream >> Value;
			o_Value = Value ^ (t_CType(1) << (sizeof(t_CType) * 8 - 1));
		}
	};

	template <typename t_CType>
	struct TCDatabaseStreamLexicographic<t_CType, false, TCEnableIf<NTraits::cIsInteger<t_CType> && !NTraits::cIsSigned<t_CType>>>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			_Stream << _Value;
		}
		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			_Stream >> o_Value;
		}
	};
}
