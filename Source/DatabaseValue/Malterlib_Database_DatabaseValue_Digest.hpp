// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Cryptography/Hash>

namespace NMib::NDatabase
{
	template <umint t_Size, typename t_CHash>
	struct TCDatabaseStreamLexicographic<NCryptography::TCMessageDigest<t_Size, t_CHash>, false>
	{
		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NCryptography::TCMessageDigest<t_Size, t_CHash> const &_Value)
		{
			_Stream.f_FeedBytes(_Value.f_GetData(), _Value.mc_Size);
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NCryptography::TCMessageDigest<t_Size, t_CHash> &o_Value)
		{
			_Stream.f_ConsumeBytes(o_Value.f_GetData(), o_Value.mc_Size);
		}
	};
}
