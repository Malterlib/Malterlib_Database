// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Database_DatabaseActor.h"

#include <Mib/Concurrency/ActorSubscription>
#include <Mib/Concurrency/ActorFunctor>
#include <Mib/Concurrency/LogError>

namespace NMib::NDatabase
{
	using namespace NConcurrency;
	using namespace NStorage;
	using namespace NFile;
	using namespace NStr;

	static auto gc_EnvironmentFlags = EDatabaseEnvironmentFlag_NoTls | EDatabaseEnvironmentFlag_AllowOneProcessLocking;

	TCFuture<void> CDatabaseActor::f_OpenDatabase(CStr _Directory, uint64 _MaxSize)
	{
		if (mp_bOpened)
			co_return DMibErrorInstance("Database has already been opened");

		mp_bOpened = true;

		auto Cleanup = g_OnScopeExit / [&]
			{
				mp_bOpened = false;
			}
		;

		auto BlockingActorCheckout = fg_BlockingActor();

		mp_pEnvironment = co_await
			(
				g_Dispatch(BlockingActorCheckout) / [_Directory, _MaxSize]
				{
					CFile::fs_CreateDirectory(_Directory);
					TCSharedPointer<CEnvironment> pEnvironment = fg_Construct();
					auto &DatabaseEnvironment = pEnvironment->m_DatabaseEnvironment;
					DatabaseEnvironment.f_SetMapSize(_MaxSize);
					auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
					DatabaseEnvironment.f_Open(_Directory, gc_EnvironmentFlags, Attribs);
					auto Transaction = DatabaseEnvironment.f_BeginWriteTransaction();
					pEnvironment->m_Database = Transaction.f_OpenDatabase();
					Transaction.f_Commit();

					return pEnvironment;
				}
			)
		;

		Cleanup.f_Clear();

		co_return {};
	}

	TCFuture<void> CDatabaseActor::f_Compact(CTransactionWrite _WriteTransaction, uint64 _MaxSize)
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		// The write transaction holds the write sequencer until it is destroyed at the end of this function, so the current
		// environment is the one it was opened on, and no other transaction can be opened on it after the new one is published.
		// It is shared with the compaction on the blocking actor, so it also lives until that is done if this function is aborted
		TCSharedPointer<CTransactionWrite> pWriteTransaction = fg_Construct(fg_Move(_WriteTransaction));
		auto pOldEnvironment = mp_pEnvironment;

		if (!_MaxSize)
			_MaxSize = pOldEnvironment->m_DatabaseEnvironment.f_SizeStatistics().m_MapSize;

		CStr OriginalPath = pOldEnvironment->m_DatabaseEnvironment.f_GetPath();
		auto BlockingActor = pWriteTransaction->mp_Checkout.f_Actor();

		CStr OldEnvironmentPath = co_await
			(
				g_Dispatch(BlockingActor) / [pWriteTransaction, pCanDestroy = mp_pCanDestroyTracker, OriginalPath, _MaxSize]() -> TCFuture<CStr>
				{
					CStr CompactTempPath = OriginalPath + ".compact";
					CStr TempDeletePath = OriginalPath + ".tempdelete";

					CDatabaseEnvironment NewDatabaseEnvironment;
					CDatabaseWriteTransaction OutTransaction;
					CDatabaseSub OutDatabase;
					{
						auto CaptureScope = co_await g_CaptureExceptions.f_Specific<CExceptionFile>();

						if (CFile::fs_FileExists(CompactTempPath))
							CFile::fs_DeleteDirectoryRecursive(CompactTempPath);

						umint iDisambiguate = 0;
						while (CFile::fs_FileExists(TempDeletePath))
							TempDeletePath = OriginalPath + CStr(".tempdelete{}"_f << iDisambiguate++);

						CFile::fs_CreateDirectory(CompactTempPath);

						NewDatabaseEnvironment.f_SetMapSize(_MaxSize);
						auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
						NewDatabaseEnvironment.f_Open(CompactTempPath, gc_EnvironmentFlags, Attribs);
						OutTransaction = NewDatabaseEnvironment.f_BeginWriteTransaction();
						OutDatabase = OutTransaction.f_OpenDatabase();
					}

					{
						auto CaptureScope = co_await g_CaptureExceptions;
						{
							auto Transaction = OutDatabase.f_Transaction(OutTransaction);
							auto iOut = Transaction.f_WriteCursor();
							auto iSource = pWriteTransaction->m_Transaction.f_ReadCursor();
							iSource.f_First();
							for (; iSource; ++iSource)
								iOut.f_Insert(iSource.f_Key(), iSource.f_Value());
						}

						OutTransaction.f_Commit();
					}

					NewDatabaseEnvironment.f_Close();

					{
						auto CaptureScope = co_await g_CaptureExceptions.f_Specific<CExceptionFile>();

						CFile::fs_CreateDirectory(TempDeletePath);

						CFile::fs_RenameFile(OriginalPath / "MalterlibDB.lock", TempDeletePath / "MalterlibDB.lock");
						CFile::fs_RenameFile(OriginalPath / "MalterlibDB.mibdb", TempDeletePath / "MalterlibDB.mibdb");

						CFile::fs_RenameFile(CompactTempPath / "MalterlibDB.lock", OriginalPath / "MalterlibDB.lock");
						CFile::fs_RenameFile(CompactTempPath / "MalterlibDB.mibdb", OriginalPath / "MalterlibDB.mibdb");

						CFile::fs_DeleteDirectory(CompactTempPath);
					}

					co_return fg_Move(TempDeletePath);
				}
			)
		;

		auto NewEnvironment = co_await
			(
				g_Dispatch(BlockingActor) / [pCanDestroy = mp_pCanDestroyTracker, OriginalPath, _MaxSize]
				{
					TCSharedPointer<CEnvironment> pEnvironment = fg_Construct();
					auto &DatabaseEnvironment = pEnvironment->m_DatabaseEnvironment;
					DatabaseEnvironment.f_SetMapSize(_MaxSize);
					auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
					DatabaseEnvironment.f_Open(OriginalPath, gc_EnvironmentFlags, Attribs);
					auto Transaction = DatabaseEnvironment.f_BeginWriteTransaction();
					pEnvironment->m_Database = Transaction.f_OpenDatabase();
					Transaction.f_Commit();

					return pEnvironment;
				}
			)
			.f_Wrap()
		;

		// The original files have been moved to OldEnvironmentPath, so the old environment is retired even when the compacted
		// database failed to open
		auto &OldEnvironment = mp_OldDatabaseEnvironments.f_Insert();
		OldEnvironment.m_pEnvironment = fg_Move(pOldEnvironment);
		OldEnvironment.m_TempDeletePath = fg_Move(OldEnvironmentPath);

		if (!NewEnvironment)
		{
			mp_pEnvironment = fg_Construct();
			co_return NewEnvironment.f_GetException();
		}

		mp_pEnvironment = fg_Move(*NewEnvironment);
		++mp_CompactionSequence;

		co_return {};
	}

	TCFuture<void> CDatabaseActor::fp_TryDeletePending()
	{
		if (mp_OldDatabaseEnvironments.f_IsEmpty())
			co_return {};

		NContainer::TCVector<umint> ToRemoveIndexes;
		NContainer::TCVector<COldEnvironment> ToRemove;
		umint iEnv = 0;
		for (auto &Environment : mp_OldDatabaseEnvironments)
		{
			umint iThisEnv = iEnv++;

			// Only the current environment gets new references, so this can't become referenced again
			if (*Environment.m_pEnvironment->m_DatabaseEnvironment.f_GetReferencesPointer() != 0)
				continue;

			ToRemoveIndexes.f_Insert(iThisEnv);
			ToRemove.f_Insert(fg_Move(Environment));
		}

		if (ToRemove.f_IsEmpty())
			co_return {};

		for (auto iToRemove : ToRemoveIndexes.f_Reverse())
			mp_OldDatabaseEnvironments.f_Remove(iToRemove);

		auto BlockingActorCheckout = fg_BlockingActor();

		co_await
			(
				g_Dispatch(BlockingActorCheckout) / [ToRemove = fg_Move(ToRemove)]() mutable
				{
					for (auto &Environment : ToRemove)
					{
						Environment.m_pEnvironment->m_DatabaseEnvironment.f_Close();
						if (CFile::fs_FileExists(Environment.m_TempDeletePath))
						{
							try
							{
								CFile::fs_DeleteDirectoryRecursive(Environment.m_TempDeletePath);
							}
							catch (CExceptionFile const &)
							{
							}
						}
					}
				}
			)
		;

		co_return {};
	}

	TCFuture<void> CDatabaseActor::fp_TransactionClosed(bool _bReader)
	{
		if (_bReader)
		{
			if (--mp_nReaders != 0)
				co_return {};

			for (auto &Waiting : mp_WaitingForReaders)
				Waiting.f_SetResult();

			mp_WaitingForReaders.f_Clear();
		}
		else if (mp_bDestroyed.f_Load())
			co_return {};

		co_await fp_TryDeletePending();

		co_return {};
	}

	void CDatabaseActor::fsp_ReleaseEnvironment(TCWeakActor<CDatabaseActor> const &_ThisActor, CEnvironment &_Environment, bool _bReader)
	{
		--(*_Environment.m_DatabaseEnvironment.f_GetReferencesPointer());

		// A destroyed actor has nothing left waiting for readers, and deletes the pending environments when it is destroyed
		auto Actor = _ThisActor.f_Lock();
		if (!Actor)
			return;

		Actor(&CDatabaseActor::fp_TransactionClosed, _bReader) > fg_LogError("Database", "Failed releasing after transaction closed");
	}

	CDatabaseActor::CTransactionRead::CTransactionRead(CActorSubscription &&_Subscription, CDatabaseReadTransaction &_MainTransaction, CDatabaseSubReadTransaction &&_Transaction)
		: mp_Subscription(fg_Move(_Subscription))
		, mp_pMainTransaction(&_MainTransaction)
		, m_Transaction(fg_Move(_Transaction))
	{
	}

	CDatabaseActor::CTransactionRead::CTransactionRead(CTransactionRead &&_Other)
		: mp_Subscription(fg_Move(_Other.mp_Subscription))
		, mp_pMainTransaction(fg_Exchange(_Other.mp_pMainTransaction, nullptr))
		, m_Transaction(fg_Move(_Other.m_Transaction))
	{
	}

	CDatabaseActor::CTransactionRead &CDatabaseActor::CTransactionRead::operator = (CTransactionRead &&_Other)
	{
		mp_Subscription = fg_Move(_Other.mp_Subscription);
		m_Transaction = fg_Move(_Other.m_Transaction);
		mp_pMainTransaction = fg_Exchange(_Other.mp_pMainTransaction, nullptr);

		return *this;
	}

	CDatabaseActor::CTransactionRead::~CTransactionRead() = default;

	CDatabaseSubReadTransaction CDatabaseActor::CTransactionRead::f_OpenDatabaseTransaction(ch8 const *_pName, EDatabaseSubFlag _Flags, CDatabaseSubProperties const &_Properties)
	{
		return mp_pMainTransaction->f_OpenDatabaseTransaction(_pName, _Flags, _Properties);
	}

	bool CDatabaseActor::CTransactionRead::f_HasDatabases() const
	{
		return mp_pMainTransaction->f_HasDatabases();
	}

	NContainer::TCSet<CStr> CDatabaseActor::CTransactionRead::f_EnumDatabases()
	{
		return mp_pMainTransaction->f_EnumDatabases();
	}

	CDatabaseActor::CTransactionWrite::CTransactionWrite
		(
			CActorSubscription &&_Subscription
			, CDatabaseWriteTransaction &_MainTransaction
			, CDatabaseSubWriteTransaction &&_Transaction
			, CBlockingActorCheckout &&_Checkout
		)
		: mp_Subscription(fg_Move(_Subscription))
		, mp_pMainTransaction(&_MainTransaction)
		, mp_Checkout(fg_Move(_Checkout))
		, m_Transaction(fg_Move(_Transaction))
	{
	}

	CDatabaseActor::CTransactionWrite::CTransactionWrite()
		: m_Transaction(CDatabaseSubWriteTransaction::fs_Empty())
		, mp_Checkout(nullptr, nullptr)
	{
	}

	CDatabaseActor::CTransactionWrite CDatabaseActor::CTransactionWrite::fs_Empty()
	{
		return {};
	}

	bool CDatabaseActor::CTransactionWrite::f_IsEmpty() const
	{
		return mp_pMainTransaction == nullptr;
	}

	CDatabaseActor::CTransactionWrite::CTransactionWrite(CTransactionWrite &&_Other)
		: mp_Subscription(fg_Move(_Other.mp_Subscription))
		, mp_pMainTransaction(fg_Exchange(_Other.mp_pMainTransaction, nullptr))
		, m_Transaction(fg_Move(_Other.m_Transaction))
		, mp_Checkout(fg_Move(_Other.mp_Checkout))
	{
	}

	CDatabaseActor::CTransactionWrite &CDatabaseActor::CTransactionWrite::operator = (CTransactionWrite &&_Other)
	{
		mp_Subscription = fg_Move(_Other.mp_Subscription);
		m_Transaction = fg_Move(_Other.m_Transaction);
		mp_pMainTransaction = fg_Exchange(_Other.mp_pMainTransaction, nullptr);
		mp_Checkout = fg_Move(_Other.mp_Checkout);

		return *this;
	}

	void CDatabaseActor::CTransactionWrite::f_Clear()
	{
		mp_Subscription.f_Clear();
		m_Transaction.f_Clear();
		mp_Checkout.f_Clear();
		mp_pMainTransaction = nullptr;
	}

	CDatabaseActor::CTransactionWrite::~CTransactionWrite() = default;

	CDatabaseSub CDatabaseActor::CTransactionWrite::f_OpenDatabase(ch8 const *_pName, EDatabaseSubFlag _Flags, CDatabaseSubProperties const &_Properties)
	{
		return mp_pMainTransaction->f_OpenDatabase(_pName, _Flags, _Properties);
	}

	bool CDatabaseActor::CTransactionWrite::f_HasDatabases() const
	{
		return mp_pMainTransaction->f_HasDatabases();
	}

	NContainer::TCSet<CStr> CDatabaseActor::CTransactionWrite::f_EnumDatabases()
	{
		return mp_pMainTransaction->f_EnumDatabases();
	}

	CDatabaseWriteTransaction CDatabaseActor::CTransactionWrite::f_BeginChildTransaction()
	{
		return mp_pMainTransaction->f_BeginChildTransaction();
	}

	TCFuture<void> CDatabaseActor::fp_Destroy()
	{
		mp_bDestroyed.f_Exchange(true);

		CLogError LogError("Database");
		{
			TCFutureVector<void> Destroys;
			fg_Move(mp_WriteSequencer).f_Destroy() > Destroys;
			fg_Move(mp_ReadSequencer).f_Destroy() > Destroys;

			auto pTracker = fg_Move(mp_pCanDestroyTracker);
			pTracker->f_Future() > Destroys;
			pTracker.f_Clear();

			co_await fg_AllDone(Destroys).f_Wrap() > LogError.f_Warning("Failed to destroy can destroy tracker or write sequencer");
		}

		co_await fp_TryDeletePending();

		co_return {};
	}

	TCFuture<void> CDatabaseActor::f_WaitForAllReaders()
	{
		if (mp_nReaders == 0)
			co_return {};

		auto Future = mp_WaitingForReaders.f_Insert().f_Future();
		co_await fg_Move(Future);

		co_return {};
	}

	TCFuture<umint> CDatabaseActor::f_CheckForStaleReaders()
	{
		co_return mp_pEnvironment->m_DatabaseEnvironment.f_CheckForStaleReaders();
	}

	auto CDatabaseActor::f_OpenTransactionRead() -> TCFuture<CTransactionRead>
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		struct CState
		{
			CDatabaseReadTransaction m_MainTransaction;
		};

		auto pCanDestroy = mp_pCanDestroyTracker;
		auto SequenceSubscription = co_await mp_ReadSequencer.f_Sequence();

		if (mp_bDestroyed.f_Load())
			co_return DMibErrorInstance("Database actor destroyed");

		auto BlockingActorCheckout = fg_BlockingActor();

		// The reference is taken here, on this actor, so the environment can't be closed by a compaction before the
		// transaction has been opened on it
		auto pEnvironment = mp_pEnvironment;
		if (!pEnvironment->m_DatabaseEnvironment.f_IsOpen())
			co_return DMibErrorInstance("Database is not open: it failed to reopen after being compacted");

		++(*pEnvironment->m_DatabaseEnvironment.f_GetReferencesPointer());
		++mp_nReaders;

		// Taken here, on this actor, since the blocking thread must not read this actor's self while it is being
		// destroyed; weak, since an open transaction must not keep the actor alive
		auto ThisActor = fg_ThisActor(this).f_Weak();

		co_return co_await
			(
				g_Dispatch(BlockingActorCheckout.f_Actor()) / [ThisActor, pEnvironment, pCanDestroy = fg_Move(pCanDestroy)]() mutable -> TCFuture<CTransactionRead>
				{
					auto ReleaseOnError = g_OnScopeExit / [&]
						{
							fsp_ReleaseEnvironment(ThisActor, *pEnvironment, true);
						}
					;

					auto CaptureScope = co_await g_CaptureExceptions;

					NStorage::TCSharedPointer<CState> pState = fg_Construct();

					pState->m_MainTransaction = pEnvironment->m_DatabaseEnvironment.f_BeginReadTransaction();
					auto Transaction = pEnvironment->m_Database.f_Transaction(pState->m_MainTransaction);

					auto Subscription = g_BlockingActorSubscription / [ThisActor, pEnvironment, pState, pCanDestroy = fg_Move(pCanDestroy)]() mutable
						{
							pState.f_Clear();
							fsp_ReleaseEnvironment(ThisActor, *pEnvironment, true);
						}
					;

					ReleaseOnError.f_Clear();

					co_return CTransactionRead{fg_Move(Subscription), pState->m_MainTransaction, fg_Move(Transaction)};
				}
			)
		;
	}

	auto CDatabaseActor::f_OpenTransactionWrite() -> TCFuture<CTransactionWrite>
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		struct CState
		{
			CDatabaseWriteTransaction m_MainTransaction;
		};

		auto pCanDestroy = mp_pCanDestroyTracker;
		auto SequenceSubscription = co_await mp_WriteSequencer.f_Sequence();

		if (mp_bDestroyed.f_Load())
			co_return DMibErrorInstance("Database actor destroyed");

		auto BlockingActorCheckout = fg_BlockingActor();

		// The reference is taken here, on this actor, so the environment can't be closed by a compaction before the
		// transaction has been opened on it
		auto pEnvironment = mp_pEnvironment;
		if (!pEnvironment->m_DatabaseEnvironment.f_IsOpen())
			co_return DMibErrorInstance("Database is not open: it failed to reopen after being compacted");

		++(*pEnvironment->m_DatabaseEnvironment.f_GetReferencesPointer());
		auto BlockingActor = BlockingActorCheckout.f_Actor();

		// Taken here, on this actor, since the blocking thread must not read this actor's self while it is being
		// destroyed; weak, since an open transaction must not keep the actor alive
		auto ThisActor = fg_ThisActor(this).f_Weak();

		co_return co_await
			(
				g_Dispatch(BlockingActor)
				/ [ThisActor, pEnvironment, pCanDestroy = fg_Move(pCanDestroy), SequenceSubscription = fg_Move(SequenceSubscription), BlockingActorCheckout = fg_Move(BlockingActorCheckout)]
				() mutable -> TCFuture<CTransactionWrite>
				{
					auto ReleaseOnError = g_OnScopeExit / [&]
						{
							fsp_ReleaseEnvironment(ThisActor, *pEnvironment, false);
						}
					;

					auto CaptureScope = co_await g_CaptureExceptions;
					NStorage::TCSharedPointer<CState> pState = fg_Construct();

					pState->m_MainTransaction = pEnvironment->m_DatabaseEnvironment.f_BeginWriteTransaction();
					auto Transaction = pEnvironment->m_Database.f_Transaction(pState->m_MainTransaction);

					auto BlockingActor = BlockingActorCheckout.f_Actor();

					auto Subscription = g_ActorSubscription(BlockingActor)
						/ [ThisActor, pEnvironment, pState, SequenceSubscription = fg_Move(SequenceSubscription), pCanDestroy = fg_Move(pCanDestroy)]() mutable
						{
							pState.f_Clear();
							fsp_ReleaseEnvironment(ThisActor, *pEnvironment, false);
						}
					;

					ReleaseOnError.f_Clear();

					co_return CTransactionWrite{fg_Move(Subscription), pState->m_MainTransaction, fg_Move(Transaction), fg_Move(BlockingActorCheckout)};
				}
			)
		;
	}

	TCFuture<CDatabaseStatistics> CDatabaseActor::f_GetAggregateStatistics()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		auto CaptureScope = co_await g_CaptureExceptions;

		CDatabaseStatistics Stats;
		Stats = mp_pEnvironment->m_DatabaseEnvironment.f_Statistics();

		auto ReadTransaction = co_await f_OpenTransactionRead();

		if (!ReadTransaction.f_HasDatabases())
			co_return Stats;

		auto Databases = ReadTransaction.f_EnumDatabases();
		for (auto &Database : Databases)
		{
			auto DatabaseTransaction = ReadTransaction.f_OpenDatabaseTransaction(Database);
			auto DbStats = DatabaseTransaction.f_DatabaseStatistics();
			Stats.m_PageSize = fg_Max(Stats.m_PageSize, DbStats.m_PageSize);
			Stats.m_nLeafPages += DbStats.m_nLeafPages;
			Stats.m_nBranchPages += DbStats.m_nBranchPages;
			Stats.m_nOverflowPages += DbStats.m_nOverflowPages;
			Stats.m_nDataItems += DbStats.m_nDataItems;
		}

		co_return Stats;
	}

	TCFuture<bool> CDatabaseActor::f_WriteWithCompaction(TCActorFunctorWeak<TCFuture<CTransactionWrite> (CTransactionWrite _Transaction, bool _bCompacting)> _fDoWork)
	{
		auto fDoWrite = g_ActorFunctor / [OriginalCompactionSequence = mp_CompactionSequence, this, fDoWork = fg_Move(_fDoWork)](bool _bCompact) -> TCFuture<bool>
			{
				auto Transaction = co_await f_OpenTransactionWrite();
				Transaction = co_await fDoWork(fg_Move(Transaction), _bCompact);
				if (Transaction.f_IsEmpty())
					co_return false;

				if (_bCompact && OriginalCompactionSequence == mp_CompactionSequence)
					co_await f_Compact(fg_Move(Transaction), 0);
				else
					co_await f_CommitWriteTransaction(fg_Move(Transaction));

				co_return true;
			}
		;

		auto Result = co_await fDoWrite(false).f_Wrap();

		if (!Result)
		{
			if (Result.f_GetExceptionStr().f_Find("MDB_MAP_FULL") >= 0)
			{
				DMibLogWithCategory(Database, Warning, "Compacting database at '{}' due to MDB_MAP_FULL", mp_pEnvironment->m_DatabaseEnvironment.f_GetPath());
				co_return co_await fDoWrite(true);
			}
			else
				co_return Result.f_GetException();
		}

		co_return *Result;
	}

	TCFuture<void> CDatabaseActor::f_CommitWriteTransaction(CTransactionWrite _WriteTransaction)
	{
		auto BlockingActor = _WriteTransaction.mp_Checkout.f_Actor();

		co_await
			(
				g_Dispatch(BlockingActor) / [WriteTransaction = fg_Move(_WriteTransaction), pCanDestroy = mp_pCanDestroyTracker]() mutable -> TCFuture<void>
				{
					{
						auto CaptureScope = co_await g_CaptureExceptions;
						WriteTransaction.mp_pMainTransaction->f_Commit();
					}

					if (WriteTransaction.mp_Subscription)
						co_await WriteTransaction.mp_Subscription->f_Destroy();

					co_return {};
				}
			)
		;

		co_return {};
	}

	TCFuture<CDatabaseStatistics> CDatabaseActor::f_Statistics()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_pEnvironment->m_DatabaseEnvironment.f_Statistics();
	}

	TCFuture<CDatabaseSizeStatistics> CDatabaseActor::f_SizeStatistics()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_pEnvironment->m_DatabaseEnvironment.f_SizeStatistics();
	}

	TCFuture<CDatabaseEnvironmentInfo> CDatabaseActor::f_Info()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_pEnvironment->m_DatabaseEnvironment.f_Info();
	}

	TCFuture<CStr> CDatabaseActor::f_GetPath()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_pEnvironment->m_DatabaseEnvironment.f_GetPath();
	}

	CBlockingActorCheckout const &CDatabaseActor::CTransactionWrite::f_Checkout() const
	{
		return mp_Checkout;
	}
}
