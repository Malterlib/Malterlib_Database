// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Storage/Optional>

#include "Malterlib_Database_DatabaseValue_Variant.hpp"

namespace NMib::NDatabase
{
	template <typename t_CType>
	struct TCDatabaseStreamLexicographic<NStorage::TCOptional<t_CType>, false> : public TCDatabaseStreamLexicographic<typename NStorage::TCOptional<t_CType>::CVariant, false>
	{
	};

	template <>
	struct TCDatabaseStreamLexicographic<CVoidTag, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, CVoidTag const &_Values)
		{
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, CVoidTag &o_Values)
		{
		}
	};
}
