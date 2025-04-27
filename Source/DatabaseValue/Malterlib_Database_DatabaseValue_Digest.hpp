// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Cryptography/Hash>

namespace NMib::NDatabase
{
	template <mint t_Size, typename t_CHash>
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
