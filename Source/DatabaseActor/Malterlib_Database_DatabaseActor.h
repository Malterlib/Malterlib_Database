// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/ActorSequencerActor>
#include <Mib/Concurrency/ActorFunctorWeak>
#include <Mib/Database/Database>

namespace NMib::NDatabase
{
	struct CDatabaseActor : public NConcurrency::CActor
	{
		struct CTransactionRead
		{
			CTransactionRead(NConcurrency::CActorSubscription &&_Subscription, CDatabaseReadTransaction &_MainTransaction, CDatabaseSubReadTransaction &&_Transaction);
			~CTransactionRead();
			CTransactionRead(CTransactionRead &&_Other);
			CTransactionRead(CTransactionRead const &_Other) = delete;
			CTransactionRead &operator = (CTransactionRead &&_Other);
			CTransactionRead &operator = (CTransactionRead const &_Other) = delete;

			bool f_HasDatabases() const;
			CDatabaseSubReadTransaction f_OpenDatabaseTransaction
				(
					ch8 const *_pName = nullptr
					, EDatabaseSubFlag _Flags = EDatabaseSubFlag_None
					, CDatabaseSubProperties const &_Properties = CDatabaseSubProperties()
				)
			;
			NContainer::TCSet<NStr::CStr> f_EnumDatabases();

			template <typename tf_CFunctor, typename ...tfp_CHandlers>
			auto f_BlockingDispatch(tf_CFunctor &&_Functor, tfp_CHandlers && ...p_Handlers) && -> NConcurrency::TCUnsafeFuture
				<
					typename NConcurrency::NPrivate::TCIsFuture
					<
						typename NTraits::TCMemberFunctionPointerTraits<decltype(&NTraits::TCRemoveReference<tf_CFunctor>::operator ())>::CReturn
					>::CType
				>
			;

			CDatabaseSubReadTransaction m_Transaction; // Transaction for the unnamed main database

		private:
			friend struct CDatabaseActor;

			NConcurrency::CActorSubscription mp_Subscription;
			CDatabaseReadTransaction *mp_pMainTransaction;
		};

		struct CTransactionWrite
		{
			CTransactionWrite
				(
					NConcurrency::CActorSubscription &&_Subscription
					, CDatabaseWriteTransaction &_MainTransaction
					, CDatabaseSubWriteTransaction &&_Transaction
					, NConcurrency::CBlockingActorCheckout &&_Checkout
				)
			;
			~CTransactionWrite();
			CTransactionWrite(CTransactionWrite &&_Other);
			CTransactionWrite(CTransactionWrite const &_Other) = delete;
			CTransactionWrite &operator = (CTransactionWrite &&_Other);
			CTransactionWrite &operator = (CTransactionWrite const &_Other) = delete;

			void f_Clear();

			bool f_HasDatabases() const;
			CDatabaseSub f_OpenDatabase
				(
					ch8 const *_pName = nullptr
					, EDatabaseSubFlag _Flags = EDatabaseSubFlag_Create
					, CDatabaseSubProperties const &_Properties = CDatabaseSubProperties()
				)
			;
			NContainer::TCSet<NStr::CStr> f_EnumDatabases();
			CDatabaseWriteTransaction f_BeginChildTransaction();

			NConcurrency::CBlockingActorCheckout const &f_Checkout() const;

			static CTransactionWrite fs_Empty();

			bool f_IsEmpty() const;

			CDatabaseSubWriteTransaction m_Transaction; // Transaction for the unnamed main database

		private:
			friend struct CDatabaseActor;
			CTransactionWrite();

			NConcurrency::CActorSubscription mp_Subscription;
			NConcurrency::CBlockingActorCheckout mp_Checkout;
			CDatabaseWriteTransaction *mp_pMainTransaction = nullptr;
		};

		NConcurrency::TCFuture<void> f_OpenDatabase(NStr::CStr _Directory, uint64 _MaxSize);

		NConcurrency::TCFuture<CTransactionRead> f_OpenTransactionRead();
		NConcurrency::TCFuture<CTransactionWrite> f_OpenTransactionWrite();
		NConcurrency::TCFuture<void> f_CommitWriteTransaction(CTransactionWrite _WriteTransaction);
		NConcurrency::TCFuture<void> f_WaitForAllReaders();
		NConcurrency::TCFuture<umint> f_CheckForStaleReaders();
		NConcurrency::TCFuture<void> f_Compact(CTransactionWrite _WriteTransaction, uint64 _MaxSize);

		NConcurrency::TCFuture<CDatabaseStatistics> f_Statistics();
		NConcurrency::TCFuture<CDatabaseSizeStatistics> f_SizeStatistics();

		NConcurrency::TCFuture<CDatabaseEnvironmentInfo> f_Info();
		NConcurrency::TCFuture<NStr::CStr> f_GetPath();

		NConcurrency::TCFuture<CDatabaseStatistics> f_GetAggregateStatistics();

		NConcurrency::TCFuture<bool> f_WriteWithCompaction
			(
				NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<CTransactionWrite> (CTransactionWrite _Transaction, bool _bCompacting)> _fDoWork
			)
		;

	private:
		struct COldEnvironment
		{
			CDatabaseEnvironment m_DatabaseEnvironment;
			NStr::CStr m_TempDeletePath;
		};

		NConcurrency::TCFuture<void> fp_Destroy();

		NConcurrency::TCFuture<void> fp_TryDeletePending();

		CDatabaseEnvironment mp_DatabaseEnvironment;
		NContainer::TCVector<COldEnvironment> mp_OldDatabaseEnvironments;
		CDatabaseSub mp_Database;
		NStorage::TCSharedPointer<NConcurrency::CCanDestroyTracker> mp_pCanDestroyTracker = fg_Construct();

		NAtomic::TCAtomic<bool> mp_bDestroyed;

		NConcurrency::CSequencer mp_WriteSequencer{"DatabaseActor WriteSequencer"};
		NConcurrency::CSequencer mp_ReadSequencer{"DatabaseActor ReadSequencer"};
		NContainer::TCVector<NConcurrency::TCPromise<void>> mp_WaitingForReaders;
		umint mp_CompactionSequence = 0;
		NAtomic::TCAtomic<umint> mp_nReaders = 0;
		bool mp_bOpened = false;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase;
#endif

#include "Malterlib_Database_DatabaseActor.hpp"
