// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CType>
	struct TCDatabaseStreamLexicographic<t_CType, false, TCEnableIfType<NTraits::TCIsInteger<t_CType>::mc_Value && NTraits::TCIsSigned<t_CType>::mc_Value>>
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
	struct TCDatabaseStreamLexicographic<t_CType, false, TCEnableIfType<NTraits::TCIsInteger<t_CType>::mc_Value && !NTraits::TCIsSigned<t_CType>::mc_Value>>
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
