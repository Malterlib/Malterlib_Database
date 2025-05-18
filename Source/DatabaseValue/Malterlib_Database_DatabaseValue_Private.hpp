// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase::NPrivate
{
	template <typename ...tfp_CValue, mint ...tfp_Indices>
	NStorage::TCTuple<typename TCGetDescending<NTraits::TCRemoveQualifiers<tfp_CValue>>::CType...> fg_ConsumeLexicographicByteArrayImpl
		(
			CDatabaseValue const &_Value
			, NMeta::TCIndices<tfp_Indices...> _Dummy
		)
	{
		NStream::CBinaryStreamMemoryPtr<NStream::CBinaryStreamBigEndian> Stream;
		Stream.f_OpenRead(_Value.m_pData, _Value.m_Size);
		NStorage::TCTuple<typename TCGetDescending<NTraits::TCRemoveQualifiers<tfp_CValue>>::CType...> Values;

		(
			[&]
			{
				using CDecodedDescending = TCGetDescending<NTraits::TCRemoveQualifiers<tfp_CValue>>;
				CDatabaseValue::fs_ConsumeLexicographic(Stream, fg_Descending<CDecodedDescending::mc_bDescending>(fg_Get<tfp_Indices>(Values)));
			}
			()
			, ...
		);

		return Values;
	}
}
