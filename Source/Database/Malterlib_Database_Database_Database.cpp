// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
 SPDX-License-Identifier: OLDAP-2.8
 SPDX-License-Identifier: ISC
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2019 Howard Chu, Symas Corp. All rights reserved.

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
#include "Malterlib_Database_Database_IDList.h"

namespace NMib::NDatabase
{
	/** @brief Open a database in the environment.
	 *
	 * A database handle denotes the name and parameters of a database,
	 * independently of whether such a database exists.
	 * The database handle may be discarded by calling #mdb_dbi_close().
	 * The old database handle is returned if the database was already open.
	 * The handle may only be closed once.
	 *
	 * The database handle will be private to the current transaction until
	 * the transaction is successfully committed. If the transaction is
	 * aborted the handle will be closed automatically.
	 * After a successful commit the handle will reside in the shared
	 * environment, and may be used by other transactions.
	 *
	 * This function must not be called from multiple concurrent
	 * transactions in the same process. A transaction that uses
	 * this function must finish (either commit or abort) before
	 * any other transaction in the process may use this function.
	 *
	 * To use named databases (with name != nullptr), #f_SetMaxDatabases()
	 * must be called before opening the environment.  Database names are
	 * keys in the unnamed database, and may be read but not written.
	 *
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] name The name of the database to open. If only a single
	 *	database is needed in the environment, this value may be nullptr.
	 * @param[in] flags Special options for this database. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * <ul>
	 *	<li>#EDatabaseSubFlag_ReverseKeys
	 *		Keys are strings to be compared in reverse order, from the end
	 *		of the strings to the beginning. By default, Keys are treated as strings and
	 *		compared from beginning to end.
	 *	<li>#EDatabaseSubFlag_SortedDuplicates
	 *		Duplicate keys may be used in the database. (Or, from another perspective,
	 *		keys may have multiple data items, stored in sorted order.) By default
	 *		keys must be unique and may have only a single data item.
	 *	<li>#EDatabaseSubFlag_IntegerKeys
	 *		Keys are binary integers in native byte order, either uint32
	 *		or CDatabaseOffset, and will be sorted as such.
	 *		(lmdb expects 32-bit int <= size_t <= 32/64-bit CDatabaseOffset.)
	 *		The keys must all be of the same size.
	 *	<li>#EDatabaseSubFlag_SortedDuplicatesFixedSize
	 *		This flag may only be used in combination with #EDatabaseSubFlag_SortedDuplicates. This option
	 *		tells the library that the data items for this database are all the same
	 *		size, which allows further optimizations in storage and retrieval. When
	 *		all data items are the same size, the #MDB_GET_MULTIPLE, #MDB_NEXT_MULTIPLE
	 *		and #MDB_PREV_MULTIPLE cursor operations may be used to retrieve multiple
	 *		items at once.
	 *	<li>#EDatabaseSubFlag_SortedDuplicatesIntegerKeys
	 *		This option specifies that duplicate data items are binary integers,
	 *		similar to #EDatabaseSubFlag_IntegerKeys keys.
	 *	<li>#EDatabaseSubFlag_SortedDuplicatesReverseKeys
	 *		This option specifies that duplicate data items should be compared as
	 *		strings in reverse order.
	 *	<li>#EDatabaseSubFlag_Create
	 *		Create the named database if it doesn't exist. This option is not
	 *		allowed in a read-only transaction or a read-only environment.
	 * </ul>
	 * @param[out] Sub database handle where the new #CDatabaseSubID handle will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_NOTFOUND - the specified database doesn't exist in the environment
	 *		and #EDatabaseSubFlag_Create was not specified.
	 *	<li>#MDB_DBS_FULL - too many databases have been opened. See #f_SetMaxDatabases().
	 * </ul>
	 */
	CDatabaseSub CDatabaseTransactionCommon::fp_OpenDatabase(const char *_pName, EDatabaseSubFlag _Flags, CDatabaseSubProperties const &_Properties)
	{
		using namespace NPrivate;
		CDatabaseValue key, data;
		CDatabaseSubID i;
		MDB_cursor mc;
		MDB_db dummy;
		int dbflag, exact;
		uint32 unused = 0, seq;
		umint len;

		if (_Flags & ~VALID_FLAGS)
			DMibError("Invalid flags specified");

		auto txn = mp_pTransaction.f_Get();

		txn->f_CheckBlocked();

		/* main DB? */
		if (!_pName)
		{
			if (_Flags & PERSISTENT_FLAGS)
			{
				uint16 f2 = _Flags & PERSISTENT_FLAGS;
				/* make sure flag changes get committed */
				if ((txn->mt_dbs[MAIN_DBI].md_flags | f2) != txn->mt_dbs[MAIN_DBI].md_flags)
				{
					txn->mt_dbs[MAIN_DBI].md_flags |= f2;
					txn->mt_flags |= MDB_TXN_DIRTY;
				}
			}
			mdb_default_cmp(txn, MAIN_DBI);
			return MAIN_DBI;
		}

		if (txn->mt_dbxs[MAIN_DBI].m_fKeyCompare.f_IsEmpty())
			mdb_default_cmp(txn, MAIN_DBI);

		/* Is the DB already open? */
		len = strlen(_pName);
		for (i=CORE_DBS; i<txn->mt_numdbs; i++)
		{
			if (!txn->mt_dbxs[i].m_DatabaseName.m_Size)
			{
				/* Remember this free slot */
				if (!unused) unused = i;
				continue;
			}
			if (len == txn->mt_dbxs[i].m_DatabaseName.m_Size && !strncmp(_pName, (char const *)txn->mt_dbxs[i].m_DatabaseName.m_pData, len))
				return i;
		}

		/* If no free slot and max hit, fail */
		if (!unused && txn->mt_numdbs >= txn->mt_env->me_maxdbs)
			DMibError("Out of database IDs");

		/* Cannot mix named databases with some mainDB flags */
		if (txn->mt_dbs[MAIN_DBI].md_flags & (EDatabaseSubFlag_SortedDuplicates|EDatabaseSubFlag_IntegerKeys))
		{
			if (_Flags & EDatabaseSubFlag_Create)
				DMibError(NStr::fg_Format("Database '{}' not found", _pName));
			else if (txn->mt_dbs[MAIN_DBI].md_flags & (EDatabaseSubFlag_SortedDuplicates))
				DMibError("Named databases not supported when main database is dupsort");
			else if (txn->mt_dbs[MAIN_DBI].md_flags & (EDatabaseSubFlag_IntegerKeys))
				DMibError("Named databases not supported when main database is integer key");
		}

		/* Find the DB info */
		dbflag = DB_NEW|DB_VALID|DB_USRVALID;
		exact = 0;
		key.m_Size = len;
		key.m_pData = (uint8 *)_pName;
		mdb_cursor_init(&mc, txn, MAIN_DBI, nullptr);
		int rc = mdb_cursor_set(&mc, &key, &data, MDB_SET, &exact);
		if (rc == MDB_SUCCESS)
		{
			/* make sure this is actually a DB */
			MDB_node *node = NODEPTR(mc.mc_pg[mc.mc_top], mc.mc_ki[mc.mc_top]);
			if ((node->mn_flags & (F_DUPDATA|F_SUBDATA)) != F_SUBDATA)
				DMibError("Named record is not a database");
		}
		else if (! (rc == MDB_NOTFOUND && (_Flags & EDatabaseSubFlag_Create)))
			DMibError(NStr::fg_Format("Error looking for database record: {}", NPrivate::mdb_strerror(rc)));

		/* Done here so we cannot fail after creating a new DB */
		char *namedup;
		umint NameLen = NStr::fg_StrLen(_pName);
		{
			namedup = (char *)NMemory::fg_Alloc(NameLen + 1);
			NStr::fg_StrCopy(namedup, _pName, NameLen + 1);
		}
		auto Cleanup = g_OnScopeExit / [&]
			{
				NMemory::fg_Free(namedup, NameLen + 1);
			}
		;

		if (rc) {
			/* MDB_NOTFOUND and EDatabaseSubFlag_Create: Create new DB */
			data.m_Size = sizeof(MDB_db);
			data.m_pData = (uint8 *)&dummy;
			memset(&dummy, 0, sizeof(dummy));
			dummy.md_root = P_INVALID;
			dummy.md_flags = _Flags & PERSISTENT_FLAGS;
			rc = mdb_cursor_put(&mc, &key, &data, (EDatabaseWriteFlag)F_SUBDATA);
			if (rc)
				DMibError(NStr::fg_Format("Error creating database record: {}", NPrivate::mdb_strerror(rc)));
			dbflag |= DB_DIRTY;
		}

		{
			/* Got info, register DBI in this txn */
			uint32 slot = unused ? unused : txn->mt_numdbs;
			txn->mt_dbxs[slot].m_DatabaseName.m_pData = (uint8 *)namedup;
			Cleanup.f_Clear();
			txn->mt_dbxs[slot].m_DatabaseName.m_Size = len;
			txn->mt_dbflags[slot] = dbflag;
			/* txn-> and env-> are the same in read txns, use
			 * tmp variable to avoid undefined assignment
			 */
			seq = ++txn->mt_env->me_dbiseqs[slot];
			txn->mt_dbiseqs[slot] = seq;

			memcpy(&txn->mt_dbs[slot], data.m_pData, sizeof(MDB_db));
			mdb_default_cmp(txn, slot);
			if (!unused)
				txn->mt_numdbs++;
			return slot;
		}
	}

	CDatabaseSubReadTransaction CDatabaseReadTransaction::f_OpenDatabaseTransaction
		(
			const char *_pName
			, EDatabaseSubFlag _Flags
			, CDatabaseSubProperties const &_Properties
		)
	{
		if (_pName && !f_HasDatabases())
			DMibError("The database doesn't support sub-databases");

		auto Database = fp_OpenDatabase(_pName, _Flags, _Properties);
		return Database.f_Transaction(*this);
	}

	NContainer::TCSet<NStr::CStr> CDatabaseReadTransaction::f_EnumDatabases()
	{
		if (!f_HasDatabases())
			DMibError("The database doesn't support sub-databases");

		auto DatabaseTransaction = f_OpenDatabaseTransaction();
		auto iCursor = DatabaseTransaction.f_ReadCursor();
		iCursor.f_First();
		NContainer::TCSet<NStr::CStr> Databases;
		while (iCursor)
		{
			auto &Key = iCursor.f_Key();

			NStr::CStr DatabaseName;
			DatabaseName.f_AddStr((ch8 const *)Key.m_pData, Key.m_Size);
			Databases[DatabaseName];

			++iCursor;
		}
		return Databases;
	}

	NContainer::TCSet<NStr::CStr> CDatabaseWriteTransaction::f_EnumDatabases()
	{
		if (!f_HasDatabases())
			DMibError("The database doesn't support sub-databases");

		auto DatabaseTransaction = f_OpenDatabase().f_Transaction(*this);
		auto iCursor = DatabaseTransaction.f_ReadCursor();
		iCursor.f_First();
		NContainer::TCSet<NStr::CStr> Databases;
		while (iCursor)
		{
			auto &Key = iCursor.f_Key();

			NStr::CStr DatabaseName;
			DatabaseName.f_AddStr((ch8 const *)Key.m_pData, Key.m_Size);
			Databases[DatabaseName];

			++iCursor;
		}
		return Databases;
	}

	CDatabaseSub CDatabaseWriteTransaction::f_OpenDatabase(ch8 const *_pName, EDatabaseSubFlag _Flags, CDatabaseSubProperties const &_Properties)
	{
		if (_pName && !f_HasDatabases())
			DMibError("The database doesn't support sub-databases");

		return fp_OpenDatabase(_pName, _Flags, _Properties);
	}

	void DMibDatabaseInfrequentSection CDatabaseTransactionCommon::CInternal::f_PrepareStatisticsQuery(CDatabaseSubID _DatabaseID)
	{
		using namespace NPrivate;
		if (!TXN_DBI_EXIST(this, _DatabaseID, DB_VALID))
			DMibError("Database does not exist");

		f_CheckBlocked();

		if (mt_dbflags[_DatabaseID] & DB_STALE)
		{
			MDB_cursor mc;
			MDB_xcursor mx;
			/* Stale, must read the DB's root. cursor_init does it for us. */
			mdb_cursor_init(&mc, this, _DatabaseID, &mx);
		}
	}

	/** @brief Retrieve statistics for a database.
	 *
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[out] stat The address of an #CDatabaseStatistics structure
	 *	where the statistics will be copied
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	CDatabaseStatistics DMibDatabaseInfrequentSection CDatabaseTransactionCommon::CInternal::f_DatabaseStatistics(CDatabaseSubID _DatabaseID)
	{
		f_PrepareStatisticsQuery(_DatabaseID);

		return mt_env->f_DatabaseStatistics(&mt_dbs[_DatabaseID]);
	}

	CDatabaseSizeStatistics DMibDatabaseInfrequentSection CDatabaseTransactionCommon::CInternal::f_SizeStatistics(CDatabaseSubID _DatabaseID)
	{
		f_PrepareStatisticsQuery(_DatabaseID);

		return mt_env->f_SizeStatistics(&mt_dbs[_DatabaseID]);
	}

	CDatabaseEnvironmentInfo DMibDatabaseInfrequentSection CDatabaseTransactionCommon::CInternal::f_EnvironmentInfo()
	{
		return mt_env->f_EnvironmentInfo();
	}

	/** @brief Retrieve the DB flags for a database handle.
	 *
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[out] flags Address where the flags will be returned.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	uint32 CDatabaseTransactionCommon::CInternal::f_DatabaseFlags(CDatabaseSubID _DatabaseID)
	{
		/* We could return the flags for the FREE_DBI too but what's the point? */
		if (!TXN_DBI_EXIST(this, _DatabaseID, DB_USRVALID))
			DMibError("Database does not exists");
		return mt_dbs[_DatabaseID].md_flags & PERSISTENT_FLAGS;
	}

	/** @brief Close a database handle. Normally unnecessary. Use with care:
	 *
	 * This call is not mutex protected. Handles should only be closed by
	 * a single thread, and only if no other threads are going to reference
	 * the database handle or one of its cursors any further. Do not close
	 * a handle if an existing transaction has modified its database.
	 * Doing so can cause misbehavior from database corruption to errors
	 * like MDB_BAD_VALSIZE (since the DB name is gone).
	 *
	 * Closing a database handle is not necessary, but lets #f_OpenDatabase()
	 * reuse the handle value.  Usually it's better to set a bigger
	 * #f_SetMaxDatabases(), unless that value would be large.
	 *
	 * @param[in] env An environment handle returned by #CDatabaseEnvironment::CDatabaseEnvironment()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 */
	void CDatabaseEnvironment::fp_CloseDatabase(CDatabaseSubID _DatabaseID)
	{
		auto &Environment = *mp_pEnvironment;
		if (_DatabaseID < CORE_DBS || _DatabaseID >= Environment.me_maxdbs)
			return;
		auto &StaticDatabaseInfo = Environment.m_StaticDasabaseInfo[_DatabaseID];
		char *pNamePointer = (char *)StaticDatabaseInfo.m_DatabaseName.m_pData;
		umint NameSize = StaticDatabaseInfo.m_DatabaseName.m_Size;
		/* If there was no name, this was already closed */
		if (pNamePointer) {
			StaticDatabaseInfo.m_DatabaseName.m_pData = nullptr;
			StaticDatabaseInfo.m_DatabaseName.m_Size = 0;
			Environment.me_dbflags[_DatabaseID] = 0;
			Environment.me_dbiseqs[_DatabaseID]++;
			NMemory::fg_Free(pNamePointer, NameSize + 1);
		}
	}

	/** @brief Empty or delete+close a database.
	 *
	 * See #fp_CloseDatabase() for restrictions about closing the DB handle.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] del 0 to empty the DB, 1 to delete it from the
	 * environment and close the DB handle.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	void CDatabaseTransactionCommon::CInternal::f_DropDatabase(CDatabaseSubID _DatabaseID, bool _bDelete)
	{
		using namespace NPrivate;
		MDB_cursor *mc, *m2;

		if (!TXN_DBI_EXIST(this, _DatabaseID, DB_USRVALID))
			DMibError("Database does not exists");

		if (F_ISSET(mt_flags, MDB_TXN_RDONLY))
			DMibError("Cannot drop database with read only transaction");

		if (TXN_DBI_CHANGED(this, _DatabaseID))
			DMibError("Stale write transaction");

		mc = f_Cursor(_DatabaseID);

		auto Cleanup = g_OnScopeExit / [&]()
			{
				mdb_cursor_close(mc);
			}
		;

		{
			int rc = mdb_drop0(mc, mc->mc_db->md_flags & EDatabaseSubFlag_SortedDuplicates);
			/* Invalidate the dropped DB's cursors */
			for (m2 = mt_cursors[_DatabaseID]; m2; m2 = m2->mc_next)
				m2->mc_flags &= ~(C_INITIALIZED|C_EOF);
			if (rc)
				DMibError(NStr::fg_Format("Error dropping database: {}", mdb_strerror(rc)));
		}

		/* Can't delete the main DB */
		if (_bDelete && _DatabaseID >= CORE_DBS)
		{
			int rc = mdb_del0(this, MAIN_DBI, &mc->mc_dbx->m_DatabaseName, nullptr, (EDatabaseWriteFlag)F_SUBDATA);
			if (!rc)
			{
				mt_dbflags[_DatabaseID] = DB_STALE;
				mt_env->mdb_dbi_close(_DatabaseID);
			}
			else
			{
				mt_flags |= MDB_TXN_ERROR;
				DMibError(NStr::fg_Format("Error deleting database: {}", mdb_strerror(rc)));
			}
		}
		else
		{
			/* reset the DB record, mark it dirty */
			mt_dbflags[_DatabaseID] |= DB_DIRTY;
			mt_dbs[_DatabaseID].md_depth = 0;
			mt_dbs[_DatabaseID].md_branch_pages = 0;
			mt_dbs[_DatabaseID].md_leaf_pages = 0;
			mt_dbs[_DatabaseID].md_overflow_pages = 0;
			mt_dbs[_DatabaseID].md_entries = 0;
			mt_dbs[_DatabaseID].md_root = P_INVALID;

			mt_flags |= MDB_TXN_DIRTY;
		}
	}

	/** @brief Set a custom key comparison function for a database.
	 *
	 * The comparison function is called whenever it is necessary to compare a
	 * key specified by the application with a key currently stored in the database.
	 * If no comparison function is specified, and no special key flags were specified
	 * with #f_OpenDatabase(), the keys are compared lexically, with shorter keys collating
	 * before longer keys.
	 * @warning This function must be called before any data access functions are used,
	 * otherwise data corruption may occur. The same comparison function must be used by every
	 * program accessing the database, every time the database is used.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] cmp A #FDatabaseCompare function
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void CDatabaseTransactionCommon::CInternal::f_SetDatabaseKeyCompare(CDatabaseSubID _DatabaseID, FDatabaseCompare &&_fCompare)
	{
		if (!TXN_DBI_EXIST(this, _DatabaseID, DB_USRVALID))
			DMibError("Database does not exists");

		mt_dbxs[_DatabaseID].m_fKeyCompare = fg_Move(_fCompare);
	}

	/** @brief Set a custom data comparison function for a #EDatabaseSubFlag_SortedDuplicates database.
	 *
	 * This comparison function is called whenever it is necessary to compare a data
	 * item specified by the application with a data item currently stored in the database.
	 * This function only takes effect if the database was opened with the #EDatabaseSubFlag_SortedDuplicates
	 * flag.
	 * If no comparison function is specified, and no special key flags were specified
	 * with #f_OpenDatabase(), the data items are compared lexically, with shorter items collating
	 * before longer items.
	 * @warning This function must be called before any data access functions are used,
	 * otherwise data corruption may occur. The same comparison function must be used by every
	 * program accessing the database, every time the database is used.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] cmp A #FDatabaseCompare function
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void CDatabaseTransactionCommon::CInternal::f_SetDatabaseDataCompare(CDatabaseSubID _DatabaseID, FDatabaseCompare &&_fCompare)
	{
		if (!TXN_DBI_EXIST(this, _DatabaseID, DB_USRVALID))
			DMibError("Database does not exists");

		mt_dbxs[_DatabaseID].m_fDataCompare = fg_Move(_fCompare);
	}
}
