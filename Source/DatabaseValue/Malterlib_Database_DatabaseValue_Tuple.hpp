// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Encoding/Bin128>

namespace NMib::NDatabase
{
	template <typename... tp_CTypes>
	struct TCDatabaseStreamLexicographic<NStorage::TCTuple<tp_CTypes...>, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NStorage::TCTuple<tp_CTypes...> const &_Values)
		{
			NStorage::fg_TupleApply
				(
					[&](auto && ...p_Values) -> void
					{
						[[maybe_unused]] TCInitializerList<bool> Dummy =
							{
								[&]
								{
									CDatabaseValue::fs_FeedLexicographic(_Stream, p_Values);
									return true;
								}()...
							}
						;
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
						[[maybe_unused]] TCInitializerList<bool> Dummy =
							{
								[&]
								{
									CDatabaseValue::fs_ConsumeLexicographic(_Stream, p_Values);
									return true;
								}()...
							}
						;
					}
					, o_Values
				)
			;
		}
	};
}
