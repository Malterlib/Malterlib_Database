// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Encoding/Bin128>

namespace NMib::NDatabase
{
	template <typename ...tp_CTypes>
	struct TCDatabaseStreamLexicographic<NStorage::TCTuple<tp_CTypes...>, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NStorage::TCTuple<tp_CTypes...> const &_Values)
		{
			NStorage::fg_TupleApply
				(
					[&](auto && ...p_Values) -> void
					{
						(
							[&]
							{
								CDatabaseValue::fs_FeedLexicographic(_Stream, p_Values);
							}
							()
							, ...
						);
					}
					, _Values
				)
			;
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NStorage::TCTuple<tp_CTypes...> &o_Values)
		{
			NStorage::fg_TupleApply
				(
					[&](auto && ...p_Values) -> void
					{
						(
							[&]
							{
								CDatabaseValue::fs_ConsumeLexicographic(_Stream, p_Values);
							}
							()
							, ...
						);
					}
					, o_Values
				)
			;
		}
	};
}
