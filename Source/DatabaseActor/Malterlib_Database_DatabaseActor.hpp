// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename tf_CFunctor, typename ...tfp_CHandlers>
	auto CDatabaseActor::CTransactionRead::f_BlockingDispatch(tf_CFunctor &&_Functor, tfp_CHandlers && ...p_Handlers) && -> NConcurrency::TCFuture
		<
			typename NConcurrency::NPrivate::TCIsFuture
			<
				typename NTraits::TCMemberFunctionPointerTraits<decltype(&NTraits::TCRemoveReference<tf_CFunctor>::CType::operator ())>::CReturn
			>::CType
		>
	{
		co_await NConcurrency::ECoroutineFlag_AllowReferences;
		auto BlockingActorCheckout = NConcurrency::fg_BlockingActor();
		co_return co_await
			(
				(
					NConcurrency::fg_Dispatch(BlockingActorCheckout.f_Actor(), fg_Move(_Functor), fg_Move(*this))
					% ... % p_Handlers
				)
			)
		;
	}
}
