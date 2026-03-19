// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CTCStrTraits>
	struct TCDatabaseStreamLexicographic<NStr::TCStr<t_CTCStrTraits>, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NStr::TCStr<t_CTCStrTraits> const &_Value)
		{
			_Stream.f_FeedBytes(_Value.f_GetStr(), _Value.f_GetLen() + 1);
		}
		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NStr::TCStr<t_CTCStrTraits> &o_Value)
		{
			NStr::TCStr<t_CTCStrTraits> Return;

			while (true)
			{
				ch8 Character;
				_Stream >> Character;
				if (!Character)
					break;
				Return.f_AddChar(Character);
			}

			o_Value = fg_Move(Return);
		}
	};

	template <>
	struct TCDatabaseStreamLexicographic<ch8 const *, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, ch8 const *_pValue)
		{
			_Stream.f_FeedBytes(_pValue, NStr::fg_StrLen(_pValue) + 1);
		}
	};

	template <umint t_Len>
	struct TCDatabaseStreamLexicographic<ch8 [t_Len], false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, ch8 const _Value[t_Len])
		{
			DMibRequire(_Value[t_Len - 1] == 0);
			_Stream.f_FeedBytes(_Value, t_Len);
		}
	};
}
