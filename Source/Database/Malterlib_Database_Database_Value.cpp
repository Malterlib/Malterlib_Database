// Copyright © 2016 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2016 Howard Chu, Symas Corp. All rights reserved.

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

#include "Malterlib_Database_Database.h"
#include "Malterlib_Database_Database_Internal.h"

namespace NMib::NDatabase
{
	/** @brief Get items from a database.
	 *
	 * This function retrieves key/data pairs from the database. The address
	 * and length of the data associated with the specified \b key are returned
	 * in the structure to which \b data refers.
	 * If the database supports duplicate keys (#EDatabaseSubFlag_SortedDuplicates) then the
	 * first data item for the key will be returned. Retrieval of other
	 * items requires the use of #mdb_cursor_get().
	 *
	 * @note The memory pointed to by the returned values is owned by the
	 * database. The caller need not dispose of the memory, and may not
	 * modify it in any way. For values returned in a read-only transaction
	 * any modification attempts will cause a SIGSEGV.
	 * @note Values returned from the database are valid only until a
	 * subsequent update operation, or the end of the transaction.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] key The key to search for in the database
	 * @param[out] data The data corresponding to the key
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_NOTFOUND - the key was not in the database.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	bool CDatabaseTransactionCommon::CInternal::f_Get(CDatabaseSubID dbi, CDatabaseValue *key, CDatabaseValue *data)
	{
		DMibRequire(key);
		DMibRequire(data);

		using namespace NPrivate;
		MDB_cursor	mc;
		MDB_xcursor	mx;
		int exact = 0;
		DKBUF;

		DMibDatabaseTrace("===> get db {} key [{}]", dbi, DKEY(key));

		if (!TXN_DBI_EXIST(this, dbi, DB_USRVALID))
			DMibError("Database does not exists");

		f_CheckBlocked();

		mdb_cursor_init(&mc, this, dbi, &mx);
		{
			int rc = mdb_cursor_set(&mc, key, data, MDB_SET, &exact);
			if (rc)
			{
				if (rc == MDB_NOTFOUND)
					return false;
				DMibError(NStr::fg_Format("Error setting data: {}", mdb_strerror(rc)));
			}
		}
		/* unref all the pages when DMibDatabase_Always64BitSupport - caller must copy the data
		 * before doing anything else
		 */
		MDB_CURSOR_UNREF(&mc, 1);
		return true;
	}

	/** @brief Store items into a database.
	 *
	 * This function stores key/data pairs in the database. The default behavior
	 * is to enter the new key/data pair, replacing any previously existing key
	 * if duplicates are disallowed, or adding a duplicate data item if
	 * duplicates are allowed (#EDatabaseSubFlag_SortedDuplicates).
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] key The key to store in the database
	 * @param[in,out] data The data to store
	 * @param[in] flags Special options for this operation. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * <ul>
	 *	<li>#EDatabaseWriteFlag_NoDuplicateData - enter the new key/data pair only if it does not
	 *		already appear in the database. This flag may only be specified
	 *		if the database was opened with #EDatabaseSubFlag_SortedDuplicates. The function will
	 *		return #MDB_KEYEXIST if the key/data pair already appears in the
	 *		database.
	 *	<li>#EDatabaseWriteFlag_NoOverwrite - enter the new key/data pair only if the key
	 *		does not already appear in the database. The function will return
	 *		#MDB_KEYEXIST if the key already appears in the database, even if
	 *		the database supports duplicates (#EDatabaseSubFlag_SortedDuplicates). The \b data
	 *		parameter will be set to point to the existing item.
	 *	<li>#EDatabaseWriteFlag_ReserveSpace - reserve space for data of the given size, but
	 *		don't copy the given data. Instead, return a pointer to the
	 *		reserved space, which the caller can fill in later - before
	 *		the next update operation or the transaction ends. This saves
	 *		an extra memcpy if the data is being generated later.
	 *		LMDB does nothing else with this memory, the caller is expected
	 *		to modify all of the space requested. This flag must not be
	 *		specified if the database was opened with #EDatabaseSubFlag_SortedDuplicates.
	 *	<li>#EDatabaseWriteFlag_Append - append the given key/data pair to the end of the
	 *		database. This option allows fast bulk loading when keys are
	 *		already known to be in the correct order. Loading unsorted keys
	 *		with this flag will cause a #MDB_KEYEXIST error.
	 *	<li>#EDatabaseWriteFlag_AppendDuplicateData - as above, but for sorted dup data.
	 * </ul>
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_MAP_FULL - the database is full, see #f_SetMapSize().
	 *	<li>#MDB_TXN_FULL - the transaction has too many dirty pages.
	 *	<li>EACCES - an attempt was made to write in a read-only transaction.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void CDatabaseTransactionCommon::CInternal::f_Put(CDatabaseSubID dbi, CDatabaseValue *key, CDatabaseValue *data, EDatabaseWriteFlag _WriteFlags)
	{
		DMibRequire(key);
		DMibRequire(data);
		using namespace NPrivate;

		if (!TXN_DBI_EXIST(this, dbi, DB_USRVALID))
			DMibError("Database does not exists");

		if (_WriteFlags & ~(EDatabaseWriteFlag_NoOverwrite|EDatabaseWriteFlag_NoDuplicateData|EDatabaseWriteFlag_ReserveSpace|EDatabaseWriteFlag_Append|EDatabaseWriteFlag_AppendDuplicateData|EDatabaseWriteFlag_OverwriteAtCurrentCursorPos))
			DMibError("Invalid flags");

		f_CheckBlocked();

		if (mt_flags & (MDB_TXN_RDONLY))
			DMibError("Trying to write to read only transaction");

		MDB_cursor mc;
		MDB_xcursor mx;

		mdb_cursor_init(&mc, this, dbi, &mx);
		mc.mc_next = mt_cursors[dbi];
		mt_cursors[dbi] = &mc;
		int rc = mdb_cursor_put(&mc, key, data, _WriteFlags);
		mt_cursors[dbi] = mc.mc_next;
		if (rc)
			DMibError(NStr::fg_Format("Error putting data: {}", mdb_strerror(rc)));
	}

	/** @brief Delete items from a database.
	 *
	 * This function removes key/data pairs from the database.
	 * If the database does not support sorted duplicate data items
	 * (#EDatabaseSubFlag_SortedDuplicates) the data parameter is ignored.
	 * If the database supports sorted duplicates and the data parameter
	 * is NULL, all of the duplicate data items for the key will be
	 * deleted. Otherwise, if the data parameter is non-NULL
	 * only the matching data item will be deleted.
	 * This function will return #MDB_NOTFOUND if the specified key/data
	 * pair is not in the database.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] key The key to delete from the database
	 * @param[in] data The data to delete
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EACCES - an attempt was made to write in a read-only transaction.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void CDatabaseTransactionCommon::CInternal::f_Delete(CDatabaseSubID dbi, CDatabaseValue const *key, CDatabaseValue const *data)
	{
		DMibRequire(key);
		using namespace NPrivate;

		if (!TXN_DBI_EXIST(this, dbi, DB_USRVALID))
			DMibError("Database does not exists");

		f_CheckBlocked();

		if (mt_flags & (MDB_TXN_RDONLY))
			DMibError("Trying to write to read only transaction");

		if (!F_ISSET(mt_dbs[dbi].md_flags, EDatabaseSubFlag_SortedDuplicates)) {
			/* must ignore any data */
			data = nullptr;
		}

		int rc = mdb_del0(this, dbi, key, data, EDatabaseWriteFlag_None);
		if (rc)
			DMibError(NStr::fg_Format("Error deleting: {}", mdb_strerror(rc)));
	}

	/** @brief Compare two data items according to a particular database.
	 *
	 * This returns a comparison as if the two data items were keys in the
	 * specified database.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] a The first item to compare
	 * @param[in] b The second item to compare
	 * @return < 0 if a < b, 0 if a == b, > 0 if a > b
	 */
	int CDatabaseTransactionCommon::CInternal::f_KeyCompare(CDatabaseSubID dbi, const CDatabaseValue *a, const CDatabaseValue *b)
	{
		return mt_dbxs[dbi].m_fKeyCompare(*a, *b);
	}

	/** @brief Compare two data items according to a particular database.
	 *
	 * This returns a comparison as if the two items were data items of
	 * the specified database. The database must have the #EDatabaseSubFlag_SortedDuplicates flag.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] a The first item to compare
	 * @param[in] b The second item to compare
	 * @return < 0 if a < b, 0 if a == b, > 0 if a > b
	 */
	int CDatabaseTransactionCommon::CInternal::f_DataCompare(CDatabaseSubID dbi, const CDatabaseValue *a, const CDatabaseValue *b)
	{
		FDatabaseCompare const &DataCompare = mt_dbxs[dbi].m_fDataCompare;
	#if DMibPPtrBits > 32 || defined(DMibDatabase_Always64BitSupport)
		if (a->m_Size == sizeof(CDatabaseOffset) && DataCompare == NPrivate::g_mdb_cmp_int)
			return NPrivate::g_mdb_cmp_clong(*a, *b);
	#endif
		return DataCompare(*a, *b);
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

	int NPrivate::mdb_del0(MDB_txn *txn, CDatabaseSubID dbi, CDatabaseValue const *key, CDatabaseValue const *data, EDatabaseWriteFlag _Flags)
	{
		MDB_cursor mc;
		MDB_xcursor mx;
		EDatabaseCursorGet op;
		CDatabaseValue rdata, *xdata;
		int		 rc, exact = 0;
		DKBUF;

		DMibDatabaseTrace("====> delete db {} key [{}]", dbi, DKEY(key));

		mdb_cursor_init(&mc, txn, dbi, &mx);

		if (data) {
			op = MDB_GET_BOTH;
			rdata = *data;
			xdata = &rdata;
		} else {
			op = MDB_SET;
			xdata = nullptr;
			_Flags |= EDatabaseWriteFlag_NoDuplicateData;
		}
		auto Key = *key;
		rc = mdb_cursor_set(&mc, &Key, xdata, op, &exact);
		if (rc == 0) {
			/* let mdb_page_split know about this cursor if needed:
			 * delete will trigger a rebalance; if it needs to move
			 * a node from one page to another, it will have to
			 * update the parent's separator key(s). If the new sepkey
			 * is larger than the current one, the parent page may
			 * run out of space, triggering a split. We need this
			 * cursor to be consistent until the end of the rebalance.
			 */
			mc.mc_next = txn->mt_cursors[dbi];
			txn->mt_cursors[dbi] = &mc;
			rc = mdb_cursor_del(&mc, _Flags);
			txn->mt_cursors[dbi] = mc.mc_next;
		}
		return rc;
	}
}
