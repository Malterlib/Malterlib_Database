// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CKey, typename t_CCompare, typename t_CAllocator>
	struct TCDatabaseStreamLexicographic<NContainer::TCSet<t_CKey, t_CCompare, t_CAllocator>, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NContainer::TCSet<t_CKey, t_CCompare, t_CAllocator> const &_Values)
		{
			{
				NEncoding::TCBinaryStream_Bin128<> Stream(true);
				Stream.f_Open(&_Stream, NFile::EFileOpen_Write);
				for (auto &Value : _Values)
					CDatabaseValue::fs_FeedLexicographic(Stream, Value);
			}
			_Stream << uint8(0);
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NContainer::TCSet<t_CKey, t_CCompare, t_CAllocator> &o_Values)
		{
			auto StartPos = _Stream.f_GetPosition();

			while (true)
			{
				uint8 Value;
				_Stream >> Value;
				if (!Value)
					break;
			}

			[[maybe_unused]] auto EndPos = _Stream.f_GetPosition();

			_Stream.f_SetPosition(StartPos);

			{
				NEncoding::TCBinaryStream_Bin128<> Stream(true);
				Stream.f_Open(&_Stream, NFile::EFileOpen_Read, (EndPos - StartPos) - 1);
				while (!Stream.f_IsAtEndOfStream())
				{
					t_CKey Key;
					CDatabaseValue::fs_ConsumeLexicographic(Stream, Key);
					o_Values[fg_Move(Key)];
				}
			}
			uint8 EndMarker;
			_Stream >> EndMarker;
			DMibCheck(_Stream.f_GetPosition() == EndPos);
			DMibCheck(EndMarker == 0);
		}
	};
}
