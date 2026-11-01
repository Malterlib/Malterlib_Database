// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
SPDX-License-Identifier: OLDAP-2.8
SPDX-License-Identifier: ISC
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
	struct CDatabaseCursorDuplicateData : public CDatabaseCursorCommon
	{
		CDatabaseOffset f_DataCount();
		bool f_NextData(); // Returns 'true' if new data is available
		bool f_PrevData();

		bool f_FirstData();
		bool f_LastData();

		bool f_FindEqual(CDatabaseValue const &_Key, CDatabaseValue const &_Value);
		bool f_FindLowerBound(CDatabaseValue const &_Key, CDatabaseValue const &_Value);

		template <typename tf_CKey, typename tf_CValue>
		bool f_FindEqual(tf_CKey const &_Key, tf_CValue const &_Value);
		template <typename tf_CKey, typename tf_CValue>
		bool f_FindLowerBound(tf_CKey const &_Key, tf_CValue const &_Value);

		CDatabaseValue const &f_Value() const;

		template <typename tf_CValue>
		auto f_Value() const;
		template <typename ...tfp_CValue>
		auto f_ValueTuple() const;

	protected:
		friend struct CDatabaseSubReadTransaction;
		bool fp_FindLowerBoundIgnoringPrefix(CDatabaseValue const &_Key, CDatabaseValue const &_Value);

		CDatabaseCursorDuplicateData(NStorage::TCUniquePointer<CInternal> &&_pInternal);
	};

	struct CDatabaseCursorDuplicateDataWrite : public CDatabaseCursorDuplicateData
	{
		void f_SetValue(CDatabaseValue const &_Value, bool _bAppend = false);
		void f_Insert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend = false);
		void f_Upsert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend = false);

		void f_DeleteKey();
		void f_DeleteData();

		template <typename tf_CValue>
		void f_SetValue(tf_CValue const &_Value, bool _bAppend = false);

		template <typename tf_CKey, typename tf_CValue>
		void f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false);

		template <typename tf_CKey, typename tf_CValue>
		void f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false);

	protected:
		friend struct CDatabaseSubWriteTransaction;

		CDatabaseCursorDuplicateDataWrite(NStorage::TCUniquePointer<CInternal> &&_pInternal);
	};
}
