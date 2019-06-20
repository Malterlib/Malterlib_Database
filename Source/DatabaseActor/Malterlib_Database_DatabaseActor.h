// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Database/Database>

namespace NMib::NDatabase
{
	struct CDatabaseActor : public NConcurrency::CActor
	{
		using CActorHolder = NConcurrency::CSeparateThreadActorHolder;

		struct CTransactionRead
		{
			CTransactionRead(CDatabaseReadTransaction &&_MainTransaction, CDatabaseSubReadTransaction &&_Transaction);

			CDatabaseReadTransaction m_MainTransaction;
			CDatabaseSubReadTransaction m_Transaction;
		};

		struct CTransactionWrite
		{
			CTransactionWrite(CDatabaseWriteTransaction &&_MainTransaction, CDatabaseSubWriteTransaction &&_Transaction);

			CDatabaseWriteTransaction m_MainTransaction;
			CDatabaseSubWriteTransaction m_Transaction;
		};

		NConcurrency::TCFuture<void> f_OpenDatabase(NStr::CStr const &_Directory, uint64 _MaxSize);

		NConcurrency::TCFuture<CTransactionRead> f_OpenTransactionRead();
		NConcurrency::TCFuture<CTransactionWrite> f_OpenTransactionWrite();

	private:
		CDatabaseEnvironment mp_DatabaseEnvironment;
		CDatabaseSub mp_Database;
		bool mp_bOpened = false;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase;
#endif
