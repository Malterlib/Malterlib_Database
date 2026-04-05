// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDatabase
{
	template <typename tf_CFunctor, typename ...tfp_CHandlers>
	auto CDatabaseActor::CTransactionRead::f_BlockingDispatch(tf_CFunctor &&_Functor, tfp_CHandlers && ...p_Handlers) && -> NConcurrency::TCUnsafeFuture
		<
			typename NConcurrency::NPrivate::TCIsFuture
			<
				typename NTraits::TCMemberFunctionPointerTraits<decltype(&NTraits::TCRemoveReference<tf_CFunctor>::operator ())>::CReturn
			>::CType
		>
	{
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
