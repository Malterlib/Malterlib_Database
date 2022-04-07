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
	struct CDatabaseSubReadTransaction
	{
		CDatabaseStatistics f_DatabaseStatistics();
		CDatabaseSizeStatistics f_SizeStatistics();
		CDatabaseEnvironmentInfo f_EnvironmentInfo();
		uint32 f_DatabaseFlags();

		bool f_Get(CDatabaseValue const &_Key, CDatabaseValue &o_Value);
		template <typename tf_CKey, typename tf_CValue>
		bool f_Get(tf_CKey const &_Key, tf_CValue &o_Value);
		template <typename tf_CKey, typename tf_CValue>
		bool f_GetDuplicateData(tf_CKey const &_Key, tf_CValue &o_Value);

		bool f_Exists(CDatabaseValue const &_Key);
		template <typename tf_CKey>
		bool f_Exists(tf_CKey const &_Key);

		int f_KeyCompare(CDatabaseValue const &_Left, CDatabaseValue const &_Right);
		int f_DataCompare(CDatabaseValue const &_Left, CDatabaseValue const &_Right);

		CDatabaseCursor f_ReadCursor();
		CDatabaseCursorDuplicateData f_ReadCursorDuplicateData();

		CDatabaseCursor f_ReadCursor(CDatabaseValue const &_Prefix);
		CDatabaseCursorDuplicateData f_ReadCursorDuplicateData(CDatabaseValue const &_Prefix);

		template <typename ...tfp_CKeys>
		CDatabaseCursor f_ReadCursor(tfp_CKeys const & ...p_Prefixes);
		template <typename ...tfp_CKeys>
		CDatabaseCursorDuplicateData f_ReadCursorDuplicateData(tfp_CKeys const & ...p_Prefixes);

		void f_Clear();

	protected:
		friend struct CDatabaseSub;

		CDatabaseSubReadTransaction(CDatabaseTransactionCommon::CInternal *_pTransaction, CDatabaseSubID _DatabaseID);

		CDatabaseTransactionCommon::CInternal *mp_pTransaction;
		CDatabaseSubID mp_DatabaseID;
	};

	struct CDatabaseSubWriteTransaction : public CDatabaseSubReadTransaction
	{
		void f_DropDatabase(bool _bDelete);

		void f_Insert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend = false);
		CDatabaseValue f_InsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend = false);

		void f_Upsert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend = false);
		CDatabaseValue f_UpsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend = false);

		void f_Delete(CDatabaseValue const &_Key);
		void f_DeleteData(CDatabaseValue const &_Key, CDatabaseValue const &_Data); // Only for duplicate data databases

		template <typename tf_CKey, typename tf_CValue>
		void f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false);
		template <typename tf_CKey, typename tf_CValue>
		void f_InsertDuplicateData(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false); // Only for duplicate data databases
		template <typename tf_CKey>
		CDatabaseValue f_InsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend = false);

		template <typename tf_CKey, typename tf_CValue>
		void f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false);
		template <typename tf_CKey, typename tf_CValue>
		void f_UpsertDuplicateData(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false); // Only for duplicate data databases
		template <typename tf_CKey>
		CDatabaseValue f_UpsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend = false);

		template <typename tf_CKey>
		void f_Delete(tf_CKey const &_Key);
		template <typename tf_CKey, typename tf_CValue>
		void f_DeleteData(tf_CKey const &_Key, tf_CValue const &_Data); // Only for duplicate data databases

		CDatabaseCursorWrite f_WriteCursor();
		CDatabaseCursorDuplicateDataWrite f_WriteCursorDuplicateData();

		CDatabaseCursorWrite f_WriteCursor(CDatabaseValue const &_Prefix);
		CDatabaseCursorDuplicateDataWrite f_WriteCursorDuplicateData(CDatabaseValue const &_Prefix);

		template <typename ...tfp_CKeys>
		CDatabaseCursorWrite f_WriteCursor(tfp_CKeys const & ...p_Prefixes);
		template <typename ...tfp_CKeys>
		CDatabaseCursorDuplicateDataWrite f_WriteCursorDuplicateData(tfp_CKeys const & ...p_Prefixes);

	private:
		friend struct CDatabaseSub;

		CDatabaseSubWriteTransaction(CDatabaseTransactionCommon::CInternal *_pTransaction, CDatabaseSubID _DatabaseID);
	};
}
