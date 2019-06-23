// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Encoding/Bin128>

namespace NMib::NDatabase
{
	template <typename t_CData, typename t_CAllocator, typename t_COptions>
	struct TCDatabaseStreamLexicographic<NContainer::TCVector<t_CData, t_CAllocator, t_COptions>, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NContainer::TCVector<t_CData, t_CAllocator, t_COptions> const &_Values)
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
		static void fs_Decode(tf_CStream &_Stream, NContainer::TCVector<t_CData, t_CAllocator, t_COptions> &o_Values)
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
					CDatabaseValue::fs_ConsumeLexicographic(Stream, o_Values.f_Insert());
			}
			uint8 EndMarker;
			_Stream >> EndMarker;
			DMibCheck(_Stream.f_GetPosition() == EndPos);
			DMibCheck(EndMarker == 0);
		}
	};

	template <typename t_CAllocator, typename t_COptions>
	struct TCDatabaseStreamLexicographic<NContainer::TCVector<uint8, t_CAllocator, t_COptions>, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NContainer::TCVector<uint8, t_CAllocator, t_COptions> const &_Values)
		{
			{
				NEncoding::TCBinaryStream_Bin128<> Stream(true);
				Stream.f_Open(&_Stream, NFile::EFileOpen_Write);
				Stream.f_FeedBytes(_Values.f_GetArray(), _Values.f_GetLen());
			}
			_Stream << uint8(0);
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NContainer::TCVector<uint8, t_CAllocator, t_COptions> &o_Values)
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
				auto Length = Stream.f_GetLength();
				Stream.f_ConsumeBytes(o_Values.f_GetArray(Length), Length);
			}
			uint8 EndMarker;
			_Stream >> EndMarker;
			DMibCheck(_Stream.f_GetPosition() == EndPos);
			DMibCheck(EndMarker == 0);
		}
	};

	template <>
	struct TCDatabaseStreamLexicographic<NContainer::CByteVector, false> : public TCDatabaseStreamLexicographic<NContainer::TCVector<uint8>, false>
	{
	};

	template <>
	struct TCDatabaseStreamLexicographic<NContainer::CSecureByteVector, false> : public TCDatabaseStreamLexicographic<NContainer::TCVector<uint8, NMemory::CAllocator_HeapSecure>, false>
	{
	};
}
