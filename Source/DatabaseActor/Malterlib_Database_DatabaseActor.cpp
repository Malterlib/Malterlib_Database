// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Database_DatabaseActor.h"

#include <Mib/Concurrency/ActorSubscription>
#include <Mib/Concurrency/ActorFunctor>

namespace NMib::NDatabase
{
	using namespace NConcurrency;
	using namespace NStorage;
	using namespace NFile;

	TCFuture<void> CDatabaseActor::f_OpenDatabase(NStr::CStr const &_Directory, uint64 _MaxSize)
	{
		return TCFuture<void>::fs_RunProtected() / [&]
			{
				if (mp_bOpened)
					DMibError("Database has already been opened");
				CFile::fs_CreateDirectory(_Directory);
				mp_DatabaseEnvironment.f_SetMapSize(_MaxSize);
				auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
				mp_DatabaseEnvironment.f_Open(_Directory, EDatabaseEnvironmentFlag_NoTls, Attribs);
				auto Transaction = mp_DatabaseEnvironment.f_BeginWriteTransaction();
				mp_Database = Transaction.f_OpenDatabase();
				Transaction.f_Commit();
				mp_bOpened = true;
			}
		;
	}

	NConcurrency::TCFuture<void> CDatabaseActor::f_Compact(CTransactionWrite &&_WriteTransaction, uint64 _MaxSize)
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		using namespace NStr;

		CStr OriginalPath = mp_DatabaseEnvironment.f_GetPath();
		CStr CompactTempPath = OriginalPath + ".compact";
		CStr TempDeletePath = OriginalPath + ".tempdelete";

		if (!_MaxSize)
			_MaxSize = mp_DatabaseEnvironment.f_SizeStatistics().m_MapSize;

		CDatabaseEnvironment NewDatabaseEnvironment;
		CDatabaseWriteTransaction OutTransaction;
		CDatabaseSub OutDatabase;
		try
		{
			if (CFile::fs_FileExists(CompactTempPath))
				CFile::fs_DeleteDirectoryRecursive(CompactTempPath);

			mint iDisambiguate = 0;
			while (CFile::fs_FileExists(TempDeletePath))
				TempDeletePath = OriginalPath + CStr(".tempdelete{}"_f << iDisambiguate++);

			CFile::fs_CreateDirectory(CompactTempPath);

			NewDatabaseEnvironment.f_SetMapSize(_MaxSize);
			auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
			NewDatabaseEnvironment.f_Open(CompactTempPath, EDatabaseEnvironmentFlag_NoTls, Attribs);
			OutTransaction = NewDatabaseEnvironment.f_BeginWriteTransaction();
			OutDatabase = OutTransaction.f_OpenDatabase();

		}
		catch (CExceptionFile const &_Exception)
		{
			co_return _Exception.f_ExceptionPointer();
		}

		try
		{
			{
				auto Transaction = OutDatabase.f_Transaction(OutTransaction);
				auto iOut = Transaction.f_WriteCursor();
				auto iSource = _WriteTransaction.m_Transaction.f_ReadCursor();
				iSource.f_First();
				for (; iSource; ++iSource)
					iOut.f_Insert(iSource.f_Key(), iSource.f_Value());
			}

			OutTransaction.f_Commit();
		}
		catch (NException::CException const &_Exception)
		{
			co_return _Exception.f_ExceptionPointer();
		}
		NewDatabaseEnvironment.f_Close();

		try
		{
			CFile::fs_CreateDirectory(TempDeletePath);

			CFile::fs_RenameFile(OriginalPath / "MalterlibDB.lock", TempDeletePath / "MalterlibDB.lock");
			CFile::fs_RenameFile(OriginalPath / "MalterlibDB.mibdb", TempDeletePath / "MalterlibDB.mibdb");

			CFile::fs_RenameFile(CompactTempPath / "MalterlibDB.lock", OriginalPath / "MalterlibDB.lock");
			CFile::fs_RenameFile(CompactTempPath / "MalterlibDB.mibdb", OriginalPath / "MalterlibDB.mibdb");

			CFile::fs_DeleteDirectory(CompactTempPath);
		}
		catch (CExceptionFile const &_Exception)
		{
			co_return _Exception.f_ExceptionPointer();
		}

		auto &OldEnvironment = mp_OldDatabaseEnvironments.f_Insert();
		OldEnvironment.m_DatabaseEnvironment = fg_Move(fg_Move(mp_DatabaseEnvironment));
		OldEnvironment.m_TempDeletePath = TempDeletePath;

		try
		{
			mp_DatabaseEnvironment = {};
			mp_DatabaseEnvironment.f_SetMapSize(_MaxSize);
			auto Attribs = EFileAttrib_UserWrite | EFileAttrib_UserRead | EFileAttrib_UnixAttributesValid;
			mp_DatabaseEnvironment.f_Open(OriginalPath, EDatabaseEnvironmentFlag_NoTls, Attribs);
			auto Transaction = mp_DatabaseEnvironment.f_BeginWriteTransaction();
			mp_Database = Transaction.f_OpenDatabase();
			Transaction.f_Commit();
		}
		catch (NException::CException const &_Exception)
		{
			co_return _Exception.f_ExceptionPointer();
		}

		++mp_CompactionSequence;

		co_return {};
	}

	void CDatabaseActor::fp_TryDeletePending()
	{
		if (mp_OldDatabaseEnvironments.f_IsEmpty())
			return;

		NContainer::TCSet<mint> ToRemove;
		mint iEnv = 0;
		for (auto &Enviroment : mp_OldDatabaseEnvironments)
		{
			if (*Enviroment.m_DatabaseEnvironment.f_GetReferencesPointer() != 0)
				continue;

			Enviroment.m_DatabaseEnvironment.f_Close();
			ToRemove[iEnv];
			if (CFile::fs_FileExists(Enviroment.m_TempDeletePath))
			{
				try
				{
					CFile::fs_DeleteDirectoryRecursive(Enviroment.m_TempDeletePath);
				}
				catch (CExceptionFile const &)
				{
				}
			}

			++iEnv;
		}

		for (auto iToRemove = ToRemove.f_GetIteratorBidirectionalReverse(); iToRemove; ++iToRemove)
			mp_OldDatabaseEnvironments.f_Remove(*iToRemove);
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

	NContainer::TCSet<NStr::CStr> CDatabaseActor::CTransactionRead::f_EnumDatabases()
	{
		return mp_pMainTransaction->f_EnumDatabases();
	}

	CDatabaseActor::CTransactionWrite::CTransactionWrite(CActorSubscription &&_Subscription, CDatabaseWriteTransaction &_MainTransaction, CDatabaseSubWriteTransaction &&_Transaction)
		: mp_Subscription(fg_Move(_Subscription))
		, mp_pMainTransaction(&_MainTransaction)
		, m_Transaction(fg_Move(_Transaction))
	{
	}

	CDatabaseActor::CTransactionWrite::CTransactionWrite()
		: m_Transaction(CDatabaseSubWriteTransaction::fs_Empty())
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
	{
	}

	CDatabaseActor::CTransactionWrite &CDatabaseActor::CTransactionWrite::operator = (CTransactionWrite &&_Other)
	{
		mp_Subscription = fg_Move(_Other.mp_Subscription);
		m_Transaction = fg_Move(_Other.m_Transaction);
		mp_pMainTransaction = fg_Exchange(_Other.mp_pMainTransaction, nullptr);

		return *this;
	}

	void CDatabaseActor::CTransactionWrite::f_Clear()
	{
		mp_Subscription.f_Clear();
		m_Transaction.f_Clear();
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

	NContainer::TCSet<NStr::CStr> CDatabaseActor::CTransactionWrite::f_EnumDatabases()
	{
		return mp_pMainTransaction->f_EnumDatabases();
	}

	TCFuture<void> CDatabaseActor::fp_Destroy()
	{
		TCActorResultVector<void> Destroys;
		mp_WriteSequencer.f_Abort() > Destroys.f_AddResult();

		auto pTracker = fg_Move(mp_pCanDestroyTracker);
		pTracker->f_Future() > Destroys.f_AddResult();
		pTracker.f_Clear();

		co_await Destroys.f_GetResults();

		fp_TryDeletePending();

		co_return {};
	}

	NConcurrency::TCFuture<void> CDatabaseActor::f_WaitForAllReaders()
	{
		if (mp_nReaders == 0)
			co_return {};

		auto Future = mp_WaitingForReaders.f_Insert().f_Future();
		co_await fg_Move(Future);

		co_return {};
	}

	NConcurrency::TCFuture<mint> CDatabaseActor::f_CheckForStaleReaders()
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

		try
		{
			NStorage::TCSharedPointer<CState> pState = fg_Construct();

			pState->m_MainTransaction = this->mp_DatabaseEnvironment.f_BeginReadTransaction();
			auto Transaction = this->mp_Database.f_Transaction(pState->m_MainTransaction);

			++mp_nReaders;
			auto *pReferences = mp_DatabaseEnvironment.f_GetReferencesPointer();
			++(*pReferences);

#ifndef DMibClangAnalyzerWorkaround
			co_return CTransactionRead
				{
					g_ActorSubscription / [this, pReferences, pState, pCanDestroy = mp_pCanDestroyTracker]() mutable
					{
						pState.f_Clear();
						--mp_nReaders;
						--(*pReferences);
						if (mp_nReaders == 0)
						{
							for (auto &Waiting : mp_WaitingForReaders)
								Waiting.f_SetResult();

							mp_WaitingForReaders.f_Clear();
							fp_TryDeletePending();
						}
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
		catch (NException::CException const &)
		{
			co_return NException::fg_CurrentException();
		}
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

		co_return co_await
			(
			 	mp_WriteSequencer / [this](CActorSubscription &&_SequencerSubscription) -> TCFuture<CTransactionWrite>
				{
					if (f_IsDestroyed())
						co_return DMibErrorInstance("Database actor destroyed");

					try
					{
						NStorage::TCSharedPointer<CState> pState = fg_Construct();

						pState->m_MainTransaction = this->mp_DatabaseEnvironment.f_BeginWriteTransaction();
						auto Transaction = this->mp_Database.f_Transaction(pState->m_MainTransaction);

						auto *pReferences = mp_DatabaseEnvironment.f_GetReferencesPointer();
						++(*pReferences);

#ifndef DMibClangAnalyzerWorkaround
						co_return CTransactionWrite
							{
								g_ActorSubscription / [this, pReferences, pState, SequencerSubscription = fg_Move(_SequencerSubscription), pCanDestroy = mp_pCanDestroyTracker]() mutable
								{
									pState.f_Clear();
									--(*pReferences);
									fp_TryDeletePending();
								}
								, pState->m_MainTransaction
								, fg_Move(Transaction)
							}
						;
#else
						co_return DMibErrorInstance("Workaround analyzer bug");
#endif
					}
					catch (NException::CException const &)
					{
						co_return NException::fg_CurrentException();
					}
				}
			)
		;
	}

	NConcurrency::TCFuture<CDatabaseStatistics> CDatabaseActor::f_GetAggregateStatistics()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		try
		{
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
		catch (NException::CException const &_Exception)
		{
			co_return _Exception.f_ExceptionPointer();
		}
	}

	NConcurrency::TCFuture<bool> CDatabaseActor::f_WriteWithCompaction
		(
			NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<CTransactionWrite> (CTransactionWrite &&_Transaction, bool _bCompacting)> &&_fDoWork
		)
	{
		auto fDoWrite = g_ActorFunctor / [OriginalCompactionSequence = mp_CompactionSequence, this, fDoWork = fg_Move(_fDoWork)](bool _bCompact) -> TCFuture<bool>
			{
				auto Transaction = co_await self(&CDatabaseActor::f_OpenTransactionWrite);
				Transaction = co_await fDoWork(fg_Move(Transaction), _bCompact);
				if (Transaction.f_IsEmpty())
					co_return false;

				if (_bCompact && OriginalCompactionSequence == mp_CompactionSequence)
					co_await self(&CDatabaseActor::f_Compact, fg_Move(Transaction), 0);
				else
					co_await self(&CDatabaseActor::f_CommitWriteTransaction, fg_Move(Transaction));

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

	NConcurrency::TCFuture<void> CDatabaseActor::f_CommitWriteTransaction(CTransactionWrite &&_WriteTransaction)
	{
		try
		{
			_WriteTransaction.mp_pMainTransaction->f_Commit();
		}
		catch (NException::CException const &_Exception)
		{
			co_return _Exception.f_ExceptionPointer();
		}

		if (_WriteTransaction.mp_Subscription)
			co_await _WriteTransaction.mp_Subscription->f_Destroy();

		co_return {};
	}

	NConcurrency::TCFuture<CDatabaseStatistics> CDatabaseActor::f_Statistics()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_DatabaseEnvironment.f_Statistics();
	}

	NConcurrency::TCFuture<CDatabaseSizeStatistics> CDatabaseActor::f_SizeStatistics()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_DatabaseEnvironment.f_SizeStatistics();
	}

	NConcurrency::TCFuture<CDatabaseEnvironmentInfo> CDatabaseActor::f_Info()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");

		co_return mp_DatabaseEnvironment.f_Info();
	}

	NConcurrency::TCFuture<NStr::CStr> CDatabaseActor::f_GetPath()
	{
		if (!mp_bOpened)
			co_return DMibErrorInstance("Database has not been opened");

		if (f_IsDestroyed())
			co_return DMibErrorInstance("Database actor destroyed");
		
		co_return mp_DatabaseEnvironment.f_GetPath();
	}
}
