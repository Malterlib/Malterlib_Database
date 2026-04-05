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

		auto [DatabaseEnvironment, Database] = co_await
			(
				g_Dispatch(BlockingActorCheckout) / [_Directory, _MaxSize]
				{
					CFile::fs_CreateDirectory(_Directory);
					CDatabaseEnvironment DatabaseEnvironment;
					DatabaseEnvironment.f_SetMapSize(_MaxSize);
					auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
					DatabaseEnvironment.f_Open(_Directory, gc_EnvironmentFlags, Attribs);
					auto Transaction = DatabaseEnvironment.f_BeginWriteTransaction();
					auto Database = Transaction.f_OpenDatabase();
					Transaction.f_Commit();

					return fg_Tuple(fg_Move(DatabaseEnvironment), Database);
				}
			)
		;

		mp_DatabaseEnvironment = fg_Move(DatabaseEnvironment);
		mp_Database = Database;

		Cleanup.f_Clear();

		co_return {};
	}

	TCFuture<void> CDatabaseActor::f_Compact(CTransactionWrite _WriteTransaction, uint64 _MaxSize)
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		if (!_MaxSize)
			_MaxSize = mp_DatabaseEnvironment.f_SizeStatistics().m_MapSize;

		co_await
			(
				g_Dispatch(_WriteTransaction.mp_Checkout.f_Actor()) / [this, pCanDestroy = mp_pCanDestroyTracker, WriteTransaction = fg_Move(_WriteTransaction), _MaxSize]() mutable -> TCFuture<void>
				{
					CStr OriginalPath = mp_DatabaseEnvironment.f_GetPath();
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
							auto iSource = WriteTransaction.m_Transaction.f_ReadCursor();
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

					auto &OldEnvironment = mp_OldDatabaseEnvironments.f_Insert();
					OldEnvironment.m_DatabaseEnvironment = fg_Move(fg_Move(mp_DatabaseEnvironment));
					OldEnvironment.m_TempDeletePath = TempDeletePath;

					{
						auto CaptureScope = co_await g_CaptureExceptions;

						mp_DatabaseEnvironment = {};
						mp_DatabaseEnvironment.f_SetMapSize(_MaxSize);
						auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
						mp_DatabaseEnvironment.f_Open(OriginalPath, gc_EnvironmentFlags, Attribs);
						auto Transaction = mp_DatabaseEnvironment.f_BeginWriteTransaction();
						mp_Database = Transaction.f_OpenDatabase();
						Transaction.f_Commit();
					}

					++mp_CompactionSequence;

					co_return {};
				}
			)
		;

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
			if (*Environment.m_DatabaseEnvironment.f_GetReferencesPointer() != 0)
				continue;

			ToRemoveIndexes.f_Insert(iEnv);
			ToRemove.f_Insert(fg_Move(Environment));

			++iEnv;
		}

		for (auto iToRemove : ToRemoveIndexes.f_Reverse())
			mp_OldDatabaseEnvironments.f_Remove(iToRemove);

		auto BlockingActorCheckout = fg_BlockingActor();

		co_await
			(
				g_Dispatch(BlockingActorCheckout) / [ToRemove = fg_Move(ToRemove)]() mutable
				{
					for (auto &Environment : ToRemove)
					{
						Environment.m_DatabaseEnvironment.f_Close();
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

	CDatabaseSubReadTransaction CDatabaseActor::CTransactionRead::f_OpenDatabaseTransaction
		(
			ch8 const *_pName
			, EDatabaseSubFlag _Flags
			, CDatabaseSubProperties const &_Properties
		)
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

	CDatabaseSub CDatabaseActor::CTransactionWrite::f_OpenDatabase
		(
			ch8 const *_pName
			, EDatabaseSubFlag _Flags
			, CDatabaseSubProperties const &_Properties
		)
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
		co_return mp_DatabaseEnvironment.f_CheckForStaleReaders();
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

		auto SequenceSubscription = co_await mp_ReadSequencer.f_Sequence();

		auto BlockingActorCheckout = fg_BlockingActor();

		co_return co_await
			(
				g_Dispatch(BlockingActorCheckout.f_Actor()) / [this, pCanDestroy = mp_pCanDestroyTracker]() mutable -> TCFuture<CTransactionRead>
				{
					auto CaptureScope = co_await g_CaptureExceptions;

					NStorage::TCSharedPointer<CState> pState = fg_Construct();

					pState->m_MainTransaction = this->mp_DatabaseEnvironment.f_BeginReadTransaction();
					auto Transaction = this->mp_Database.f_Transaction(pState->m_MainTransaction);

					++mp_nReaders;
					auto *pReferences = mp_DatabaseEnvironment.f_GetReferencesPointer();
					++(*pReferences);

			#ifndef DMibClangAnalyzerWorkaround
					co_return CTransactionRead
						{
							g_BlockingActorSubscription / [this, pReferences, pState, pCanDestroy = fg_Move(pCanDestroy)]() mutable
							{
								pState.f_Clear();
								auto NewReaders = --mp_nReaders;
								--(*pReferences);
								if (NewReaders != 0)
									return;

								g_Dispatch(fg_ThisActor(this)) / [this, pCanDestroy = fg_Move(pCanDestroy)]() -> TCFuture<void>
									{
										if (mp_nReaders.f_Load() == 0)
										{
											for (auto &Waiting : mp_WaitingForReaders)
												Waiting.f_SetResult();

											mp_WaitingForReaders.f_Clear();
										}

										co_await fp_TryDeletePending();

										co_return {};
									}
									> fg_LogError("Database", "Failed releasing after 0 readers")
								;
							}
							, pState->m_MainTransaction
							, fg_Move(Transaction)
						}
					;
			#else
					(void)Transaction;
					co_return DMibErrorInstance("Workaround analyzer bug");
			#endif
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
		auto BlockingActorCheckout = fg_BlockingActor();
		auto BlockingActor = BlockingActorCheckout.f_Actor();

		co_return co_await
			(
				g_Dispatch(BlockingActor)
				/ [this, pCanDestroy = fg_Move(pCanDestroy), SequenceSubscription = fg_Move(SequenceSubscription), BlockingActorCheckout = fg_Move(BlockingActorCheckout)]
				() mutable -> TCFuture<CTransactionWrite>
				{
					if (mp_bDestroyed.f_Load())
						co_return DMibErrorInstance("Database actor destroyed");

					auto CaptureScope = co_await g_CaptureExceptions;
					NStorage::TCSharedPointer<CState> pState = fg_Construct();

					pState->m_MainTransaction = this->mp_DatabaseEnvironment.f_BeginWriteTransaction();
					auto Transaction = this->mp_Database.f_Transaction(pState->m_MainTransaction);

					auto *pReferences = mp_DatabaseEnvironment.f_GetReferencesPointer();
					++(*pReferences);

					auto BlockingActor = BlockingActorCheckout.f_Actor();
					auto ThisActor = fg_ThisActor(this);

#ifndef DMibClangAnalyzerWorkaround
					co_return CTransactionWrite
						{
							g_ActorSubscription(BlockingActor) / [this, ThisActor, pReferences, pState, SequenceSubscription = fg_Move(SequenceSubscription), pCanDestroy = fg_Move(pCanDestroy)]() mutable
							{
								pState.f_Clear();
								--(*pReferences);
								g_Dispatch(ThisActor) / [this, pCanDestroy = fg_Move(pCanDestroy)]() -> TCFuture<void>
									{
										if (mp_bDestroyed.f_Load())
											co_return {};

										co_await fp_TryDeletePending();

										co_return {};
									}
									> fg_LogError("Database", "Failed delete pending after write transaction")
								;
							}
							, pState->m_MainTransaction
							, fg_Move(Transaction)
							, fg_Move(BlockingActorCheckout)
						}
					;
#else
					co_return DMibErrorInstance("Workaround analyzer bug");
#endif
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
		Stats = mp_DatabaseEnvironment.f_Statistics();

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
				DMibLogWithCategory(Database, Warning, "Compacting database at '{}' due to MDB_MAP_FULL", mp_DatabaseEnvironment.f_GetPath());
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

		co_return mp_DatabaseEnvironment.f_Statistics();
	}

	TCFuture<CDatabaseSizeStatistics> CDatabaseActor::f_SizeStatistics()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_DatabaseEnvironment.f_SizeStatistics();
	}

	TCFuture<CDatabaseEnvironmentInfo> CDatabaseActor::f_Info()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_DatabaseEnvironment.f_Info();
	}

	TCFuture<CStr> CDatabaseActor::f_GetPath()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_DatabaseEnvironment.f_GetPath();
	}

	CBlockingActorCheckout const &CDatabaseActor::CTransactionWrite::f_Checkout() const
	{
		return mp_Checkout;
	}
}
