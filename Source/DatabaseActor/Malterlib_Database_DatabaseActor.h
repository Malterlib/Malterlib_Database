// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/ActorSequencerAsync>
#include <Mib/Database/Database>

namespace NMib::NDatabase
{
	struct CDatabaseActor : public NConcurrency::CActor
	{
		using CActorHolder = NConcurrency::CSeparateThreadActorHolder;

		struct CTransactionRead
		{
		private:
			friend struct CDatabaseActor;
			
			NConcurrency::CActorSubscription mp_Subscription;
			CDatabaseReadTransaction &mp_MainTransaction;
		public:
			CTransactionRead(NConcurrency::CActorSubscription &&_Subscription, CDatabaseReadTransaction &_MainTransaction, CDatabaseSubReadTransaction &&_Transaction);
			CTransactionRead(CTransactionRead &&) = default;
			CTransactionRead(CTransactionRead const &) = delete;

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
		};

		struct CTransactionWrite
		{
		private:
			friend struct CDatabaseActor;

			NConcurrency::CActorSubscription mp_Subscription;
			CDatabaseWriteTransaction &mp_MainTransaction;
		public:
			CTransactionWrite(NConcurrency::CActorSubscription &&_Subscription, CDatabaseWriteTransaction &_MainTransaction, CDatabaseSubWriteTransaction &&_Transaction);
			CTransactionWrite(CTransactionWrite &&) = default;
			CTransactionWrite(CTransactionWrite const &) = delete;

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

			CDatabaseSubWriteTransaction m_Transaction; // Transaction for the unnamed main database
		};

		NConcurrency::TCFuture<void> f_OpenDatabase(NStr::CStr const &_Directory, uint64 _MaxSize);

		NConcurrency::TCFuture<CTransactionRead> f_OpenTransactionRead();
		NConcurrency::TCFuture<CTransactionWrite> f_OpenTransactionWrite();
		NConcurrency::TCFuture<void> f_CommitWriteTransaction(CTransactionWrite &&_WriteTransaction);

		NConcurrency::TCFuture<CDatabaseStatistics> f_Statistics();
		NConcurrency::TCFuture<CDatabaseEnvironmentInfo> f_Info();
		NConcurrency::TCFuture<NStr::CStr> f_GetPath();

		NConcurrency::TCFuture<CDatabaseStatistics> f_GetAggregateStatistics();

	private:
		NConcurrency::TCFuture<void> fp_Destroy();

		CDatabaseEnvironment mp_DatabaseEnvironment;
		CDatabaseSub mp_Database;
		NStorage::TCSharedPointer<NConcurrency::CCanDestroyTracker> mp_pCanDestroyTracker = fg_Construct();
		NConcurrency::TCActorSequencerAsync<CTransactionWrite> mp_WriteSequencer;
		bool mp_bOpened = false;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase;
#endif
