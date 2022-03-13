// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Storage/Variant>

namespace NMib::NDatabase
{
	template <typename t_CIndex, typename ...tp_CMembers>
	struct TCDatabaseStreamLexicographic<NStorage::TCVariantCommon<t_CIndex, tp_CMembers...>, false>
	{
		using CTypeID = typename NStorage::TCVariantCommon<t_CIndex, tp_CMembers...>::CIndexUnsignedInteger;

		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, NStorage::TCVariantCommon<t_CIndex, tp_CMembers...> const &_Variant)
		{
			CTypeID TypeID = (CTypeID)_Variant.f_GetTypeID();

			CDatabaseValue::fs_FeedLexicographic(_Stream, TypeID);
			_Variant.f_Visit
				(
					[&](auto &&_Value) -> void
					{
						CDatabaseValue::fs_FeedLexicographic(_Stream, _Value);
					}
				)
			;
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, NStorage::TCVariantCommon<t_CIndex, tp_CMembers...> &_Variant)
		{
			CTypeID TypeID;
			CDatabaseValue::fs_ConsumeLexicographic(_Stream, TypeID);

			bool bSuccess = _Variant.f_TryVisitCreate
				(
				 	TypeID
					, [&](auto &&_Value) -> void
					{
						CDatabaseValue::fs_ConsumeLexicographic(_Stream, _Value);
					}
				)
			;

			if (!bSuccess)
				DMibErrorStream(NStr::CStr::CFormat("Unsupported type '{}' in variant") << TypeID);
		}
	};
}
