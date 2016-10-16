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

#include "Malterlib_Database_LowLevel.h"
#include "Malterlib_Database_LowLevel_Internal.h"
#include "Malterlib_Database_LowLevel_IDList.h"

namespace NMib
{
	namespace NDatabase
	{
		CLowLevelDatabase::CLowLevelDatabase()
			: mp_DatabaseID(TCLimitsInt<uint32>::mc_Max)
		{
		}

		CLowLevelDatabase::CLowLevelDatabase(CLowLevelDatabaseID _DatabaseID)
			: mp_DatabaseID(_DatabaseID)
		{
		}
		
		CLowLevelDatabaseReadTransaction CLowLevelDatabase::f_Transaction(CLowLevelReadTransaction const &_Transaction)
		{
			if (mp_DatabaseID == TCLimitsInt<uint32>::mc_Max)
				DMibError("Invalid database");
			return {_Transaction.mp_pTransaction.f_Get(), mp_DatabaseID};
		}
		
		CLowLevelDatabaseWriteTransaction CLowLevelDatabase::f_Transaction(CLowLevelWriteTransaction const &_Transaction)
		{
			if (mp_DatabaseID == TCLimitsInt<uint32>::mc_Max)
				DMibError("Invalid database");
			return {_Transaction.mp_pTransaction.f_Get(), mp_DatabaseID};
		}
		
		CLowLevelStatistics CLowLevelDatabaseReadTransaction::f_DatabaseStatistics()
		{
			return mp_pTransaction->f_DatabaseStatistics(mp_DatabaseID);
		}
		
		uint32 CLowLevelDatabaseReadTransaction::f_DatabaseFlags()
		{
			return mp_pTransaction->f_DatabaseFlags(mp_DatabaseID);
		}

		bool CLowLevelDatabaseReadTransaction::f_Get(CLowLevelValue const &_Key, CLowLevelValue &o_Value)
		{
			return mp_pTransaction->f_Get(mp_DatabaseID, const_cast<CLowLevelValue *>(&_Key), &o_Value);
		}
		
		int CLowLevelDatabaseReadTransaction::f_KeyCompare(CLowLevelValue const &_Left, CLowLevelValue const &_Right)
		{
			return mp_pTransaction->f_KeyCompare(mp_DatabaseID, &_Left, &_Right);
		}
		
		int CLowLevelDatabaseReadTransaction::f_DataCompare(CLowLevelValue const &_Left, CLowLevelValue const &_Right)
		{
			return mp_pTransaction->f_DataCompare(mp_DatabaseID, &_Left, &_Right);
		}

		CLowLevelCursor CLowLevelDatabaseReadTransaction::f_ReadCursor()
		{
			NPtr::TCUniquePointer<CLowLevelCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
			return fg_Move(pCursor);
		}
		
		CLowLevelCursorDuplicateData CLowLevelDatabaseReadTransaction::f_ReadCursorDuplicateData()
		{
			NPtr::TCUniquePointer<CLowLevelCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
			return fg_Move(pCursor);
		}

		CLowLevelDatabaseReadTransaction::CLowLevelDatabaseReadTransaction(CLowLevelTransactionCommon::CInternal *_pTransaction, CLowLevelDatabaseID _DatabaseID)
			: mp_pTransaction(_pTransaction)
			, mp_DatabaseID(_DatabaseID)
		{
		}

		void CLowLevelDatabaseWriteTransaction::f_DropDatabase(bool _bDelete)
		{
			return mp_pTransaction->f_DropDatabase(mp_DatabaseID, _bDelete);
		}
		
		void CLowLevelDatabaseWriteTransaction::f_Insert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend)
		{
			auto &Transaction = *mp_pTransaction;
			CLowLevelValue Key = _Key;
			CLowLevelValue Value = _Value;
			auto Flags = (_bAppend ? ELowLevelWriteFlag_Append : ELowLevelWriteFlag_None) | ELowLevelWriteFlag_NoOverwrite;
			if (Transaction.mt_dbs[mp_DatabaseID].md_flags & ELowLevelDatabaseFlag_SortedDuplicates)
				Flags |= ELowLevelWriteFlag_NoDuplicateData;
			Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
		}
		
		CLowLevelValue CLowLevelDatabaseWriteTransaction::f_Insert(CLowLevelValue const &_Key, mint _DataSize, bool _bAppend)
		{
			auto &Transaction = *mp_pTransaction;
			CLowLevelValue Key = _Key;
			CLowLevelValue Value;
			Value.m_Size = _DataSize;
			auto Flags = (_bAppend ? ELowLevelWriteFlag_Append : ELowLevelWriteFlag_None) | ELowLevelWriteFlag_NoOverwrite | ELowLevelWriteFlag_ReserveSpace;
			Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
			return Value;
		}
		
		void CLowLevelDatabaseWriteTransaction::f_Upsert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend)
		{
			auto &Transaction = *mp_pTransaction;
			CLowLevelValue Key = _Key;
			CLowLevelValue Value = _Value;
			auto Flags = (_bAppend ? ELowLevelWriteFlag_Append : ELowLevelWriteFlag_None);
			Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
		}
		
		CLowLevelValue CLowLevelDatabaseWriteTransaction::f_Upsert(CLowLevelValue const &_Key, mint _DataSize, bool _bAppend)
		{
			auto &Transaction = *mp_pTransaction;
			CLowLevelValue Key = _Key;
			CLowLevelValue Value;
			Value.m_Size = _DataSize;
			auto Flags = (_bAppend ? ELowLevelWriteFlag_Append : ELowLevelWriteFlag_None) | ELowLevelWriteFlag_ReserveSpace;
			Transaction.f_Put(mp_DatabaseID, &Key, &Value, Flags);
			return Value;
		}
		
		void CLowLevelDatabaseWriteTransaction::f_Delete(CLowLevelValue const &_Key)
		{
			return mp_pTransaction->f_Delete(mp_DatabaseID, &_Key, nullptr);
		}
		
		void CLowLevelDatabaseWriteTransaction::f_DeleteData(CLowLevelValue const &_Key, CLowLevelValue const &_Data)
		{
			return mp_pTransaction->f_Delete(mp_DatabaseID, &_Key, &_Data);
		}
		
		CLowLevelDatabaseWriteTransaction::CLowLevelDatabaseWriteTransaction(CLowLevelTransactionCommon::CInternal *_pTransaction, CLowLevelDatabaseID _DatabaseID)
			: CLowLevelDatabaseReadTransaction(_pTransaction, _DatabaseID)
		{
		}
		
		CLowLevelCursorWrite CLowLevelDatabaseWriteTransaction::f_WriteCursor()
		{
			NPtr::TCUniquePointer<CLowLevelCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
			return fg_Move(pCursor);
		}
		
		CLowLevelCursorDuplicateDataWrite CLowLevelDatabaseWriteTransaction::f_WriteCursorDuplicateData()
		{
			NPtr::TCUniquePointer<CLowLevelCursorCommon::CInternal> pCursor = fg_Explicit(mp_pTransaction->f_Cursor(mp_DatabaseID));
			return fg_Move(pCursor);
		}
	}
	
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
	 * 	database is needed in the environment, this value may be nullptr.
	 * @param[in] flags Special options for this database. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * <ul>
	 *	<li>#ELowLevelDatabaseFlag_ReverseKeys
	 *		Keys are strings to be compared in reverse order, from the end
	 *		of the strings to the beginning. By default, Keys are treated as strings and
	 *		compared from beginning to end.
	 *	<li>#ELowLevelDatabaseFlag_SortedDuplicates
	 *		Duplicate keys may be used in the database. (Or, from another perspective,
	 *		keys may have multiple data items, stored in sorted order.) By default
	 *		keys must be unique and may have only a single data item.
	 *	<li>#ELowLevelDatabaseFlag_IntegerKeys
	 *		Keys are binary integers in native byte order, either uint32
	 *		or mint, and will be sorted as such.
	 *		The keys must all be of the same size.
	 *	<li>#ELowLevelDatabaseFlag_SortedDuplicatesFixedSize
	 *		This flag may only be used in combination with #ELowLevelDatabaseFlag_SortedDuplicates. This option
	 *		tells the library that the data items for this database are all the same
	 *		size, which allows further optimizations in storage and retrieval. When
	 *		all data items are the same size, the #MDB_GET_MULTIPLE and #MDB_NEXT_MULTIPLE
	 *		cursor operations may be used to retrieve multiple items at once.
	 *	<li>#ELowLevelDatabaseFlag_SortedDuplicatesIntegerKeys
	 *		This option specifies that duplicate data items are binary integers,
	 *		similar to #ELowLevelDatabaseFlag_IntegerKeys keys.
	 *	<li>#ELowLevelDatabaseFlag_SortedDuplicatesReverseKeys
	 *		This option specifies that duplicate data items should be compared as
	 *		strings in reverse order.
	 *	<li>#ELowLevelDatabaseFlag_Create
	 *		Create the named database if it doesn't exist. This option is not
	 *		allowed in a read-only transaction or a read-only environment.
	 * </ul>
	 * @param[out] dbi Address where the new #CLowLevelDatabaseID handle will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_NOTFOUND - the specified database doesn't exist in the environment
	 *		and #ELowLevelDatabaseFlag_Create was not specified.
	 *	<li>#MDB_DBS_FULL - too many databases have been opened. See #f_SetMaxDatabases().
	 * </ul>
	 */
	NDatabase::CLowLevelDatabase NDatabase::CLowLevelTransactionCommon::fp_OpenDatabase(const char *_pName, ELowLevelDatabaseFlag _Flags, CDatabaseProperties const &_Properties)
	{
		using namespace NPrivate;
		CLowLevelValue key, data;
		CLowLevelDatabaseID i;
		MDB_cursor mc;
		MDB_db dummy;
		int dbflag, exact;
		uint32 unused = 0, seq;
		mint len;

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
		if (txn->mt_dbs[MAIN_DBI].md_flags & (ELowLevelDatabaseFlag_SortedDuplicates|ELowLevelDatabaseFlag_IntegerKeys))
		{
			if (_Flags & ELowLevelDatabaseFlag_Create)
				DMibError(NStr::fg_Format("Database '{}' not found", _pName));
			else if (txn->mt_dbs[MAIN_DBI].md_flags & (ELowLevelDatabaseFlag_SortedDuplicates))
				DMibError("Named databases not supported when main database is dupsort");
			else if (txn->mt_dbs[MAIN_DBI].md_flags & (ELowLevelDatabaseFlag_IntegerKeys))
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
		else if (! (rc == MDB_NOTFOUND && (_Flags & ELowLevelDatabaseFlag_Create))) 
			DMibError(NStr::fg_Format("Error looking for database record: {}", NPrivate::mdb_strerror(rc)));

		/* Done here so we cannot fail after creating a new DB */
		char *namedup;
		{
			mint NameLen = NStr::fg_StrLen(_pName);
			namedup = (char *)NMem::fg_Alloc(NameLen);
			NStr::fg_StrCopy(namedup, _pName, NameLen + 1);
		}
		auto Cleanup = g_OnScopeExit > [&]
			{
				NMem::fg_Free(namedup);
			}
		;

		if (rc) {
			/* MDB_NOTFOUND and ELowLevelDatabaseFlag_Create: Create new DB */
			data.m_Size = sizeof(MDB_db);
			data.m_pData = (uint8 *)&dummy;
			memset(&dummy, 0, sizeof(dummy));
			dummy.md_root = P_INVALID;
			dummy.md_flags = _Flags & PERSISTENT_FLAGS;
			rc = mdb_cursor_put(&mc, &key, &data, (ELowLevelWriteFlag)F_SUBDATA);
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
	
	NDatabase::CLowLevelDatabaseReadTransaction NDatabase::CLowLevelReadTransaction::f_OpenDatabaseTransaction
		(
			const char *_pName
			, ELowLevelDatabaseFlag _Flags
			, CDatabaseProperties const &_Properties
		)
	{
		auto Database = fp_OpenDatabase(_pName, _Flags, _Properties);
		return Database.f_Transaction(*this);
	}
	
	NContainer::TCSet<NStr::CStr> NDatabase::CLowLevelReadTransaction::f_EnumDatabases()
	{
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
	
	NContainer::TCSet<NStr::CStr> NDatabase::CLowLevelWriteTransaction::f_EnumDatabases()
	{
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

	NDatabase::CLowLevelDatabase NDatabase::CLowLevelWriteTransaction::f_OpenDatabase(ch8 const *_pName, ELowLevelDatabaseFlag _Flags, CDatabaseProperties const &_Properties)
	{
		return fp_OpenDatabase(_pName, _Flags, _Properties);
	}

	/** @brief Retrieve statistics for a database.
	 *
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[out] stat The address of an #CLowLevelStatistics structure
	 * 	where the statistics will be copied
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	NDatabase::CLowLevelStatistics DMibDatabaseInfrequentSection NDatabase::CLowLevelTransactionCommon::CInternal::f_DatabaseStatistics(CLowLevelDatabaseID dbi)
	{
		using namespace NPrivate;
		if (!TXN_DBI_EXIST(this, dbi, DB_VALID))
			DMibError("Database does not exist");

		f_CheckBlocked();

		if (mt_dbflags[dbi] & DB_STALE) {
			MDB_cursor mc;
			MDB_xcursor mx;
			/* Stale, must read the DB's root. cursor_init does it for us. */
			mdb_cursor_init(&mc, this, dbi, &mx);
		}
		return mt_env->f_DatabaseStatistics(&mt_dbs[dbi]);
	}
	
	/** @brief Retrieve the DB flags for a database handle.
	 *
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[out] flags Address where the flags will be returned.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	uint32 NDatabase::CLowLevelTransactionCommon::CInternal::f_DatabaseFlags(CLowLevelDatabaseID dbi)
	{
		/* We could return the flags for the FREE_DBI too but what's the point? */
		if (!TXN_DBI_EXIST(this, dbi, DB_USRVALID))
			DMibError("Database does not exists");
		return mt_dbs[dbi].md_flags & PERSISTENT_FLAGS;
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
	 * @param[in] env An environment handle returned by #CLowLevelEnvironment::CLowLevelEnvironment()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 */
	void NDatabase::CLowLevelEnvironment::fp_CloseDatabase(CLowLevelDatabaseID dbi)
	{
		auto &Environment = *mp_pEnvironment;
		char *ptr;
		if (dbi < CORE_DBS || dbi >= Environment.me_maxdbs)
			return;
		ptr = (char *)Environment.m_StaticDasabaseInfo[dbi].m_DatabaseName.m_pData;
		/* If there was no name, this was already closed */
		if (ptr) {
			Environment.m_StaticDasabaseInfo[dbi].m_DatabaseName.m_pData = nullptr;
			Environment.m_StaticDasabaseInfo[dbi].m_DatabaseName.m_Size = 0;
			Environment.me_dbflags[dbi] = 0;
			Environment.me_dbiseqs[dbi]++;
			NMem::fg_Free(ptr);
		}
	}
	
	/** @brief Empty or delete+close a database.
	 *
	 * See #fp_CloseDatabase() for restrictions about closing the DB handle.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] del 0 to empty the DB, 1 to delete it from the
	 * environment and close the DB handle.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	void NDatabase::CLowLevelTransactionCommon::CInternal::f_DropDatabase(CLowLevelDatabaseID dbi, bool _bDelete)
	{
		using namespace NPrivate;			
		MDB_cursor *mc, *m2;

		if (!TXN_DBI_EXIST(this, dbi, DB_USRVALID))
			DMibError("Database does not exists");

		if (F_ISSET(mt_flags, MDB_TXN_RDONLY))
			DMibError("Cannot drop database with read only transaction");

		if (TXN_DBI_CHANGED(this, dbi))
			DMibError("Stale write transaction");

		mc = f_Cursor(dbi);
		
		auto Cleanup = g_OnScopeExit > [&]()
			{
				mdb_cursor_close(mc);				
			}
		;

		{
			int rc = mdb_drop0(mc, mc->mc_db->md_flags & ELowLevelDatabaseFlag_SortedDuplicates);
			/* Invalidate the dropped DB's cursors */
			for (m2 = mt_cursors[dbi]; m2; m2 = m2->mc_next)
				m2->mc_flags &= ~(C_INITIALIZED|C_EOF);
			if (rc)
				DMibError(NStr::fg_Format("Error dropping database: {}", mdb_strerror(rc)));
		}

		/* Can't delete the main DB */
		if (_bDelete && dbi >= CORE_DBS) 
		{
			int rc = mdb_del0(this, MAIN_DBI, &mc->mc_dbx->m_DatabaseName, nullptr, (ELowLevelWriteFlag)F_SUBDATA);
			if (!rc) 
			{
				mt_dbflags[dbi] = DB_STALE;
				mt_env->mdb_dbi_close(dbi);
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
			mt_dbflags[dbi] |= DB_DIRTY;
			mt_dbs[dbi].md_depth = 0;
			mt_dbs[dbi].md_branch_pages = 0;
			mt_dbs[dbi].md_leaf_pages = 0;
			mt_dbs[dbi].md_overflow_pages = 0;
			mt_dbs[dbi].md_entries = 0;
			mt_dbs[dbi].md_root = P_INVALID;

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
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] cmp A #FLowLevelCompare function
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void NDatabase::CLowLevelTransactionCommon::CInternal::f_SetDatabaseKeyCompare(CLowLevelDatabaseID dbi, FLowLevelCompare &&_fCompare)
	{
		if (!TXN_DBI_EXIST(this, dbi, DB_USRVALID))
			DMibError("Database does not exists");

		mt_dbxs[dbi].m_fKeyCompare = fg_Move(_fCompare);
	}

	/** @brief Set a custom data comparison function for a #ELowLevelDatabaseFlag_SortedDuplicates database.
	 *
	 * This comparison function is called whenever it is necessary to compare a data
	 * item specified by the application with a data item currently stored in the database.
	 * This function only takes effect if the database was opened with the #ELowLevelDatabaseFlag_SortedDuplicates
	 * flag.
	 * If no comparison function is specified, and no special key flags were specified
	 * with #f_OpenDatabase(), the data items are compared lexically, with shorter items collating
	 * before longer items.
	 * @warning This function must be called before any data access functions are used,
	 * otherwise data corruption may occur. The same comparison function must be used by every
	 * program accessing the database, every time the database is used.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @param[in] dbi A database handle returned by #f_OpenDatabase()
	 * @param[in] cmp A #FLowLevelCompare function
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	void NDatabase::CLowLevelTransactionCommon::CInternal::f_SetDatabaseDataCompare(CLowLevelDatabaseID dbi, FLowLevelCompare &&_fCompare)
	{
		if (!TXN_DBI_EXIST(this, dbi, DB_USRVALID))
			DMibError("Database does not exists");

		mt_dbxs[dbi].m_fDataCompare = fg_Move(_fCompare);
	}
	
	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------
}
