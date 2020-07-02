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
	struct CDatabaseCursorCommon
	{
		CDatabaseCursorCommon();
		~CDatabaseCursorCommon();
		CDatabaseCursorCommon(CDatabaseCursorCommon &&_Other);
		CDatabaseCursorCommon &operator =(CDatabaseCursorCommon &&_Other);

		struct CInternal;

		operator bool() const;

		CDatabaseValue const &f_Key() const;

		bool f_First();
		bool f_Last();

		bool f_FindEqual(CDatabaseValue const &_Key);
		bool f_FindLowerBound(CDatabaseValue const &_Key);

		void f_SetPrefix(CDatabaseValue const &_Prefix);
		template <typename ...tfp_CKey>
		void f_SetPrefix(tfp_CKey const & ...p_Prefixes);

		template <typename tf_CKey>
		auto f_Key() const;
		template <typename ...tfp_CKeys>
		auto f_KeyTuple() const;

		template <typename ...tfp_CKeys>
		bool f_FindEqual(tfp_CKeys const & ...p_Keys);
		template <typename ...tfp_CKeys>
		bool f_FindLowerBound(tfp_CKeys const & ...p_Keys);

		void operator ++();
		void operator --();

	protected:
		CDatabaseCursorCommon(NStorage::TCUniquePointer<CInternal> &&_pInternal);

		void fp_SetToCurrent();
		bool fp_FindLowerBoundIgnoringPrefix(CDatabaseValue const &_Key);
		bool fp_CheckValidity(int _Result, ch8 const *_pErrorFormat);
		bool fp_CheckValidityIgnoringPrefix(int _Result, ch8 const *_pErrorFormat);

		CDatabaseValue mp_Key;
		CDatabaseValue mp_Value;

		CDatabaseValue mp_Prefix;

		CDatabaseValueCache mp_PrefixCache;
		CDatabaseValueCache mp_PrefixReverseCache;
		CDatabaseValueCache mp_KeyCache;

		NStorage::TCUniquePointer<CInternal> mp_pInternal;

		bool mp_bValid = false;
	};

	struct CDatabaseCursor : public CDatabaseCursorCommon
	{
		CDatabaseValue const &f_Value() const;

		template <typename tf_CValue>
		auto f_Value() const;
		template <typename ...tfp_CValue>
		auto f_ValueTuple() const;

	protected:
		friend struct CDatabaseSubReadTransaction;

		CDatabaseCursor(NStorage::TCUniquePointer<CInternal> &&_pInternal);
	};

	struct CDatabaseCursorWrite : public CDatabaseCursor
	{
		void f_SetValue(CDatabaseValue const &_Value, bool _bAppend = false);
		CDatabaseValue f_SetValueInplace(mint _DataSize, bool _bAppend = false);

		void f_Insert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend = false);
		CDatabaseValue f_InsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend = false);

		void f_Upsert(CDatabaseValue const &_Key, CDatabaseValue const &_Value, bool _bAppend = false);
		CDatabaseValue f_UpsertInplace(CDatabaseValue const &_Key, mint _DataSize, bool _bAppend = false);

		void f_Delete();

		template <typename tf_CValue>
		void f_SetValue(tf_CValue const &_Value, bool _bAppend = false);

		template <typename tf_CKey, typename tf_CValue>
		void f_Insert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false);
		template <typename tf_CKey>
		CDatabaseValue f_InsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend = false);

		template <typename tf_CKey, typename tf_CValue>
		void f_Upsert(tf_CKey const &_Key, tf_CValue const &_Value, bool _bAppend = false);
		template <typename tf_CKey>
		CDatabaseValue f_UpsertInplace(tf_CKey const &_Key, mint _DataSize, bool _bAppend = false);

	protected:
		friend struct CDatabaseSubWriteTransaction;

		CDatabaseCursorWrite(NStorage::TCUniquePointer<CInternal> &&_pInternal);
	};
}
