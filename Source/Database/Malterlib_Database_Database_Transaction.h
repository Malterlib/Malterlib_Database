// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2019 Howard Chu, Symas Corp.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted only as authorized by the OpenLDAP
 Public License.

 A copy of this license is available in the file LICENSE.OpenLDAP in the
 top-level directory of the distribution or, alternatively, at
 <http://www.OpenLDAP.org/license.html>.

 This code is derived from btree.c written by Martin Hedenfalk.

 Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.

 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#pragma once

namespace NMib::NDatabase
{
	/// \enum Write transaction flags
	enum EDatabaseWriteTrasactionFlag
	{
		EDatabaseWriteTrasactionFlag_None
		, EDatabaseWriteTrasactionFlag_NoMetaSync = 0x40000	///< Flush system buffers but omit metadata flush when committing this transaction.
		, EDatabaseWriteTrasactionFlag_NoSync = 0x10000		///< Don't flush system buffers to disk when committing this transaction.
	};

	struct CDatabaseTransactionCommon
	{
		struct CInternal;

		CDatabaseTransactionCommon();
		~CDatabaseTransactionCommon();
		CDatabaseTransactionCommon(CDatabaseTransactionCommon &&_Other);
		CDatabaseTransactionCommon &operator =(CDatabaseTransactionCommon &&_Other);

		CDatabaseOffset f_TransactionID();

	protected:
		friend struct CDatabaseEnvironment_Internal;
		friend struct CDatabaseSub;
		friend struct CDatabaseEnvironment;
		friend struct CInternal;

		CDatabaseTransactionCommon(NStorage::TCUniquePointer<CInternal> &&_pTransaction);

		CDatabaseSub fp_OpenDatabase(ch8 const *_pName, EDatabaseSubFlag _Flags, CDatabaseSubProperties const &_Properties);
		CInternal *fp_GetInternal();

		NStorage::TCUniquePointer<CInternal> mp_pTransaction;
	};

	struct CDatabaseResetReadTransaction
	{
		CDatabaseReadTransaction f_Renew();

	private:
		friend struct CDatabaseReadTransaction;

		CDatabaseResetReadTransaction(NStorage::TCUniquePointer<CDatabaseTransactionCommon::CInternal> &&_pTransaction);

		NStorage::TCUniquePointer<CDatabaseTransactionCommon::CInternal> mp_pTransaction;
	};

	struct CDatabaseReadTransaction : protected CDatabaseTransactionCommon
	{
		CDatabaseReadTransaction();
		CDatabaseReadTransaction(CDatabaseReadTransaction &&_Other);
		CDatabaseReadTransaction &operator =(CDatabaseReadTransaction &&_Other);

		CDatabaseResetReadTransaction f_Reset();
		CDatabaseSubReadTransaction f_OpenDatabaseTransaction
			(
				ch8 const *_pName = nullptr
				, EDatabaseSubFlag _Flags = EDatabaseSubFlag_None
				, CDatabaseSubProperties const &_Properties = CDatabaseSubProperties()
			)
		;

		NContainer::TCSet<NStr::CStr> f_EnumDatabases();

	private:
		friend struct CDatabaseResetReadTransaction;
		friend struct CDatabaseEnvironment;
		friend struct CDatabaseSub;

		CDatabaseReadTransaction(NStorage::TCUniquePointer<CInternal> &&_pTransaction);
		CDatabaseReadTransaction(CDatabaseTransactionCommon &&_Transaction);
	};

	struct CDatabaseWriteTransaction : protected CDatabaseTransactionCommon
	{
		CDatabaseWriteTransaction();
		CDatabaseWriteTransaction(CDatabaseWriteTransaction &&_Other);
		CDatabaseWriteTransaction &operator =(CDatabaseWriteTransaction &&_Other);

		void f_Commit();
		CDatabaseSub f_OpenDatabase
			(
				ch8 const *_pName = nullptr
				, EDatabaseSubFlag _Flags = EDatabaseSubFlag_Create
				, CDatabaseSubProperties const &_Properties = CDatabaseSubProperties()
			)
		;

		NContainer::TCSet<NStr::CStr> f_EnumDatabases();

		CDatabaseWriteTransaction f_BeginChildTransaction();
	private:
		friend struct CDatabaseEnvironment;
		friend struct CDatabaseSub;

		CDatabaseWriteTransaction(CDatabaseTransactionCommon &&_Transaction);
	};
}

