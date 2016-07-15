// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <bool t_bDescending>
	struct TCOrderedByteArrayCoder<NStr::CStr, t_bDescending>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NStr::CStr const &_Value)
		{
			_Stream.f_FeedBytes(_Value.f_GetStr(), _Value.f_GetLen() + 1);
		}			
		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NStr::CStr &o_Value)
		{
			NStr::CStr Return;
			
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

	template <bool t_bDescending>
	struct TCOrderedByteArrayCoder<ch8 const *, t_bDescending>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, ch8 const *_pValue)
		{
			_Stream.f_FeedBytes(_pValue, NStr::fg_StrLen(_pValue) + 1);
		}			
	};

	template <mint t_Len, bool t_bDescending>
	struct TCOrderedByteArrayCoder<ch8 [t_Len], t_bDescending>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, ch8 const _Value[t_Len])
		{
			DMibRequire(_Value[t_Len-1] == 0);
			_Stream.f_FeedBytes(_Value, t_Len);
		}			
	};
}
