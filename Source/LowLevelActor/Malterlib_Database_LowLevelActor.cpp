// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Database_LowLevelActor.h"

namespace NMib::NDatabase
{
	NConcurrency::TCFuture<void> CDatabaseLowLevelActor::f_OpenDatabase(NStr::CStr const &_Directory, uint64 _MaxSize)
	{
		return NConcurrency::TCFuture<void>::fs_RunProtected() / [&]
			{
				if (mp_bOpened)
					DMibError("Database has already been opened");
				NFile::CFile::fs_CreateDirectory(_Directory);
				mp_DatabaseEnvironment.f_SetMapSize(_MaxSize);
				auto Attribs = NFile::EFileAttrib_UserWrite | NFile::EFileAttrib_UserRead | NFile::EFileAttrib_UnixAttributesValid;
				mp_DatabaseEnvironment.f_Open(_Directory, ELowLevelEnvironmentFlag_NoTls, Attribs);
				auto Transaction = mp_DatabaseEnvironment.f_BeginWriteTransaction();
				mp_Database = Transaction.f_OpenDatabase();
				Transaction.f_Commit();
				mp_bOpened = true;
			}
		;
	}

	CDatabaseLowLevelActor::CTransactionRead::CTransactionRead(CLowLevelReadTransaction &&_MainTransaction, CLowLevelDatabaseReadTransaction &&_Transaction)
		: m_MainTransaction(fg_Move(_MainTransaction))
		, m_Transaction(fg_Move(_Transaction))
	{
	}

	CDatabaseLowLevelActor::CTransactionWrite::CTransactionWrite(CLowLevelWriteTransaction &&_MainTransaction, CLowLevelDatabaseWriteTransaction &&_Transaction)
		: m_MainTransaction(fg_Move(_MainTransaction))
		, m_Transaction(fg_Move(_Transaction))
	{
	}

	auto CDatabaseLowLevelActor::f_OpenTransactionRead() -> NConcurrency::TCFuture<CTransactionRead>
	{
		if (!mp_bOpened)
			return DMibErrorInstance("Database has not been opened");

		auto MainTransaction = this->mp_DatabaseEnvironment.f_BeginReadTransaction();
		auto Transaction = this->mp_Database.f_Transaction(MainTransaction);
		return fg_Explicit(CTransactionRead{fg_Move(MainTransaction), fg_Move(Transaction)});
	}

	auto CDatabaseLowLevelActor::f_OpenTransactionWrite() -> NConcurrency::TCFuture<CTransactionWrite>
	{
		if (!mp_bOpened)
			return DMibErrorInstance("Database has not been opened");

		auto MainTransaction = this->mp_DatabaseEnvironment.f_BeginWriteTransaction();
		auto Transaction = this->mp_Database.f_Transaction(MainTransaction);
		return fg_Explicit(CTransactionWrite{fg_Move(MainTransaction), fg_Move(Transaction)});
	}
}
