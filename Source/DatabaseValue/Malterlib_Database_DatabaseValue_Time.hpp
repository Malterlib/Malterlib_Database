// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Time/Time>

namespace NMib::NDatabase
{
	template <>
	struct TCDatabaseStreamLexicographic<NTime::CTime, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NTime::CTime const &_Value)
		{
			CDatabaseValue::fs_FeedLexicographic(_Stream, _Value.f_GetSeconds());
			CDatabaseValue::fs_FeedLexicographic(_Stream, _Value.f_GetFractionInt());
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NTime::CTime &o_Value)
		{
			uint64 Seconds;
			CDatabaseValue::fs_ConsumeLexicographic(_Stream, Seconds);
			o_Value.f_SetSecondsNoFraction(Seconds);

			uint64 Fraction;
			CDatabaseValue::fs_ConsumeLexicographic(_Stream, Fraction);
			o_Value.f_SetFractionInt(Fraction);
		}
	};

	template <>
	struct TCDatabaseStreamLexicographic<NTime::CTimeSpan, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NTime::CTimeSpan const &_Value)
		{
			CDatabaseValue::fs_FeedLexicographic(_Stream, _Value.f_GetSeconds());
			CDatabaseValue::fs_FeedLexicographic(_Stream, _Value.f_GetFractionInt());
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NTime::CTimeSpan &o_Value)
		{
			int64 Seconds;
			CDatabaseValue::fs_ConsumeLexicographic(_Stream, Seconds);
			o_Value.f_SetSecondsNoFraction(Seconds);

			uint64 Fraction;
			CDatabaseValue::fs_ConsumeLexicographic(_Stream, Fraction);
			o_Value.f_SetFractionInt(Fraction);
		}
	};
}
