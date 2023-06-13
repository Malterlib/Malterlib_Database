// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
		using CActorHolder = NConcurrency::CSeparateThreadActorHolder;

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

			CDatabaseSubReadTransaction m_Transaction; // Transaction for the unnamed main database

		private:
			friend struct CDatabaseActor;

			NConcurrency::CActorSubscription mp_Subscription;
			CDatabaseReadTransaction *mp_pMainTransaction;
		};

		struct CTransactionWrite
		{
			CTransactionWrite(NConcurrency::CActorSubscription &&_Subscription, CDatabaseWriteTransaction &_MainTransaction, CDatabaseSubWriteTransaction &&_Transaction);
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

			static CTransactionWrite fs_Empty();

			bool f_IsEmpty() const;

			CDatabaseSubWriteTransaction m_Transaction; // Transaction for the unnamed main database

		private:
			friend struct CDatabaseActor;
			CTransactionWrite();

			NConcurrency::CActorSubscription mp_Subscription;
			CDatabaseWriteTransaction *mp_pMainTransaction = nullptr;
		};

		NConcurrency::TCFuture<void> f_OpenDatabase(NStr::CStr const &_Directory, uint64 _MaxSize);

		NConcurrency::TCFuture<CTransactionRead> f_OpenTransactionRead();
		NConcurrency::TCFuture<CTransactionWrite> f_OpenTransactionWrite();
		NConcurrency::TCFuture<void> f_CommitWriteTransaction(CTransactionWrite &&_WriteTransaction);
		NConcurrency::TCFuture<void> f_WaitForAllReaders();
		NConcurrency::TCFuture<mint> f_CheckForStaleReaders();
		NConcurrency::TCFuture<void> f_Compact(CTransactionWrite &&_WriteTransaction, uint64 _MaxSize);

		NConcurrency::TCFuture<CDatabaseStatistics> f_Statistics();
		NConcurrency::TCFuture<CDatabaseSizeStatistics> f_SizeStatistics();

		NConcurrency::TCFuture<CDatabaseEnvironmentInfo> f_Info();
		NConcurrency::TCFuture<NStr::CStr> f_GetPath();

		NConcurrency::TCFuture<CDatabaseStatistics> f_GetAggregateStatistics();

		NConcurrency::TCFuture<bool> f_WriteWithCompaction
			(
				NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<CTransactionWrite> (CTransactionWrite &&_Transaction, bool _bCompacting)> &&_fDoWork
			)
		;

	private:
		struct COldEnvironment
		{
			CDatabaseEnvironment m_DatabaseEnvironment;
			NStr::CStr m_TempDeletePath;
		};

		NConcurrency::TCFuture<void> fp_Destroy();

		void fp_TryDeletePending();

		CDatabaseEnvironment mp_DatabaseEnvironment;
		NContainer::TCVector<COldEnvironment> mp_OldDatabaseEnvironments;
		CDatabaseSub mp_Database;
		NStorage::TCSharedPointer<NConcurrency::CCanDestroyTracker> mp_pCanDestroyTracker = fg_Construct();

		NAtomic::TCAtomic<bool> mp_bDestroyed;
		NConcurrency::TCActor<NConcurrency::CSeparateThreadActor> mp_WriteActor;

		NConcurrency::TCSequencer<CTransactionWrite> mp_WriteSequencer{"DatabaseActor WriteSequencer"};
		NContainer::TCVector<NConcurrency::TCPromise<void>> mp_WaitingForReaders;
		mint mp_CompactionSequence = 0;
		mint mp_nReaders = 0;
		bool mp_bOpened = false;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase;
#endif
