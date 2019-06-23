// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Database_DatabaseActor.h"
#include <Mib/Concurrency/ActorSubscription>

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

	CDatabaseActor::CTransactionRead::CTransactionRead(CActorSubscription &&_Subscription, CDatabaseReadTransaction &_MainTransaction, CDatabaseSubReadTransaction &&_Transaction)
		: mp_Subscription(fg_Move(_Subscription))
		, mp_MainTransaction(_MainTransaction)
		, m_Transaction(fg_Move(_Transaction))
	{
	}

	CDatabaseSubReadTransaction CDatabaseActor::CTransactionRead::f_OpenDatabaseTransaction
		(
			ch8 const *_pName
			, EDatabaseSubFlag _Flags
			, CDatabaseSubProperties const &_Properties
		)
	{
		return mp_MainTransaction.f_OpenDatabaseTransaction(_pName, _Flags, _Properties);
	}

	NContainer::TCSet<NStr::CStr> CDatabaseActor::CTransactionRead::f_EnumDatabases()
	{
		return mp_MainTransaction.f_EnumDatabases();
	}

	CDatabaseActor::CTransactionWrite::CTransactionWrite(CActorSubscription &&_Subscription, CDatabaseWriteTransaction &_MainTransaction, CDatabaseSubWriteTransaction &&_Transaction)
		: mp_Subscription(fg_Move(_Subscription))
		, mp_MainTransaction(_MainTransaction)
		, m_Transaction(fg_Move(_Transaction))
	{
	}

	CDatabaseSub CDatabaseActor::CTransactionWrite::f_OpenDatabase
		(
			ch8 const *_pName
			, EDatabaseSubFlag _Flags
			, CDatabaseSubProperties const &_Properties
		)
	{
		return mp_MainTransaction.f_OpenDatabase(_pName, _Flags, _Properties);
	}

	NContainer::TCSet<NStr::CStr> CDatabaseActor::CTransactionWrite::f_EnumDatabases()
	{
		return mp_MainTransaction.f_EnumDatabases();
	}

	TCFuture<void> CDatabaseActor::fp_Destroy()
	{
		TCActorResultVector<void> Destroys;
		mp_WriteSequencer.f_Abort() > Destroys.f_AddResult();

		auto pTracker = fg_Move(mp_pCanDestroyTracker);
		pTracker->f_Future() > Destroys.f_AddResult();
		pTracker.f_Clear();

		co_await Destroys.f_GetResults();

		co_return {};
	}

	auto CDatabaseActor::f_OpenTransactionRead() -> TCFuture<CTransactionRead>
	{
		if (!mp_bOpened)
			return DMibErrorInstance("Database has not been opened");
		if (mp_bDestroyed)
			return DMibErrorInstance("Database actor destroyed");

		struct CState
		{
			CDatabaseReadTransaction m_MainTransaction;
		};

		try
		{
			NStorage::TCSharedPointer<CState> pState = fg_Construct();

			pState->m_MainTransaction = this->mp_DatabaseEnvironment.f_BeginReadTransaction();
			auto Transaction = this->mp_Database.f_Transaction(pState->m_MainTransaction);

			return fg_Explicit
				(
					CTransactionRead
					{
						g_ActorSubscription / [pState, pCanDestroy = mp_pCanDestroyTracker]() mutable
						{
							pState.f_Clear();
						}
						, pState->m_MainTransaction
						, fg_Move(Transaction)
					}
				)
			;
		}
		catch (NException::CException const &_Exception)
		{
			return _Exception;
		}
	}

	auto CDatabaseActor::f_OpenTransactionWrite() -> TCFuture<CTransactionWrite>
	{
		if (!mp_bOpened)
			return DMibErrorInstance("Database has not been opened");
		if (mp_bDestroyed)
			return DMibErrorInstance("Database actor destroyed");

		struct CState
		{
			CDatabaseWriteTransaction m_MainTransaction;
		};

		return mp_WriteSequencer / [this](CActorSubscription &&_SequencerSubscription) -> TCFuture<CTransactionWrite>
			{
				try
				{
					NStorage::TCSharedPointer<CState> pState = fg_Construct();

					pState->m_MainTransaction = this->mp_DatabaseEnvironment.f_BeginWriteTransaction();
					auto Transaction = this->mp_Database.f_Transaction(pState->m_MainTransaction);

					return fg_Explicit
						(
							CTransactionWrite
							{
								g_ActorSubscription / [pState, SequencerSubscription = fg_Move(_SequencerSubscription), pCanDestroy = mp_pCanDestroyTracker]() mutable
								{
									pState.f_Clear();
								}
								, pState->m_MainTransaction
								, fg_Move(Transaction)
							}
						)
					;
				}
				catch (NException::CException const &_Exception)
				{
					return _Exception;
				}
			}
		;
	}

	NConcurrency::TCFuture<void> CDatabaseActor::f_CommitWriteTransaction(CTransactionWrite &&_WriteTransaction)
	{
		return TCFuture<void>::fs_RunProtected() / [&]
			{
				_WriteTransaction.mp_MainTransaction.f_Commit();
			}
		;
	}
}
