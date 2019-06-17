// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Database/LowLevel>

namespace NMib::NDatabase
{
	struct CDatabaseLowLevelActor : public NConcurrency::CActor
	{
		using CActorHolder = NConcurrency::CSeparateThreadActorHolder;

		struct CTransactionRead
		{
			CTransactionRead(CLowLevelReadTransaction &&_MainTransaction, CLowLevelDatabaseReadTransaction &&_Transaction);

			CLowLevelReadTransaction m_MainTransaction;
			CLowLevelDatabaseReadTransaction m_Transaction;
		};

		struct CTransactionWrite
		{
			CTransactionWrite(CLowLevelWriteTransaction &&_MainTransaction, CLowLevelDatabaseWriteTransaction &&_Transaction);

			CLowLevelWriteTransaction m_MainTransaction;
			CLowLevelDatabaseWriteTransaction m_Transaction;
		};

		NConcurrency::TCFuture<void> f_OpenDatabase(NStr::CStr const &_Directory, uint64 _MaxSize);

		NConcurrency::TCFuture<CTransactionRead> f_OpenTransactionRead();
		NConcurrency::TCFuture<CTransactionWrite> f_OpenTransactionWrite();

	private:
		CLowLevelEnvironment mp_DatabaseEnvironment;
		CLowLevelDatabase mp_Database;
		bool mp_bOpened = false;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase;
#endif
