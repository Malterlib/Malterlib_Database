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
	 * The database handle may be discarded by calling #f_DbiClose().
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
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
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
	 *		all data items are the same size, the #EDatabaseCursorGet_GetMultiple, #EDatabaseCursorGet_NextMultiple
	 *		and #EDatabaseCursorGet_PrevMultiple cursor operations may be used to retrieve multiple
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
	 *	<li>#DMdbNotFound - the specified database doesn't exist in the environment
	 *		and #EDatabaseSubFlag_Create was not specified.
	 *	<li>#DMdbDbsFull - too many databases have been opened. See #f_SetMaxDatabases().
	 * </ul>
	 */
	CDatabaseSub CDatabaseTransactionCommon::fp_OpenDatabase(char const *_pName, EDatabaseSubFlag _Flags, CDatabaseSubProperties const &_Properties)
	{
		using namespace NPrivate;
		CDatabaseValue Key, Data;
		CDatabaseSubID iDatabase;
		CMdbCursor Cursor;
		CMdbDb DummyDatabase;
		int DatabaseFlag, bExact;
		uint32 iUnusedSlot = 0, Sequence;
		umint NameLength;

		if (_Flags & ~DValidFlags)
			DMibError("Invalid flags specified");

		auto pTransaction = mp_pTransaction.f_Get();

		pTransaction->f_CheckBlocked();

		/* main DB? */
		if (!_pName)
		{
			if (_Flags & DPersistentFlags)
			{
				uint16 PersistentFlags = _Flags & DPersistentFlags;
				/* make sure flag changes get committed */
				if ((pTransaction->m_pDatabases[DMainDbi].m_Flags | PersistentFlags) != pTransaction->m_pDatabases[DMainDbi].m_Flags)
				{
					pTransaction->m_pDatabases[DMainDbi].m_Flags |= PersistentFlags;
					pTransaction->m_Flags |= DMdbTxnDirty;
				}
			}
			fg_DefaultCmp(pTransaction, DMainDbi);
			return DMainDbi;
		}

		if (pTransaction->m_pDatabaseInfos[DMainDbi].m_fKeyCompare.f_IsEmpty())
			fg_DefaultCmp(pTransaction, DMainDbi);

		/* Is the DB already open? */
		NameLength = strlen(_pName);
		for (iDatabase = DCoreDbs; iDatabase < pTransaction->m_nDatabases; iDatabase++)
		{
			if (!pTransaction->m_pDatabaseInfos[iDatabase].m_DatabaseName.m_Size)
			{
				/* Remember this free slot */
				if (!iUnusedSlot)
					iUnusedSlot = iDatabase;
				continue;
			}
			if
			(
				NameLength == pTransaction->m_pDatabaseInfos[iDatabase].m_DatabaseName.m_Size
				&& !strncmp(_pName, (char const *)pTransaction->m_pDatabaseInfos[iDatabase].m_DatabaseName.m_pData, NameLength)
			)
			{
				return iDatabase;
			}
		}

		/* If no free slot and max hit, fail */
		if (!iUnusedSlot && pTransaction->m_nDatabases >= pTransaction->m_pEnvironment->m_MaxDatabases)
			DMibError("Out of database IDs");

		/* Cannot mix named databases with some mainDB flags */
		if (pTransaction->m_pDatabases[DMainDbi].m_Flags & (EDatabaseSubFlag_SortedDuplicates | EDatabaseSubFlag_IntegerKeys))
		{
			if (_Flags & EDatabaseSubFlag_Create)
				DMibError(NStr::fg_Format("Database '{}' not found", _pName));
			else if (pTransaction->m_pDatabases[DMainDbi].m_Flags & (EDatabaseSubFlag_SortedDuplicates))
				DMibError("Named databases not supported when main database is dupsort");
			else if (pTransaction->m_pDatabases[DMainDbi].m_Flags & (EDatabaseSubFlag_IntegerKeys))
				DMibError("Named databases not supported when main database is integer key");
		}

		/* Find the DB info */
		DatabaseFlag = DDbNew | DDbValid | DDbUserValid;
		bExact = 0;
		Key.m_Size = NameLength;
		Key.m_pData = (uint8 *)_pName;
		fg_CursorInit(&Cursor, pTransaction, DMainDbi, nullptr);
		int Result = fg_CursorSet(&Cursor, &Key, &Data, EDatabaseCursorGet_Set, &bExact);
		if (Result == DMdbSuccess)
		{
			/* make sure this is actually a DB */
			CMdbNode *pNode = DNodePtr(Cursor.m_Pages[Cursor.m_iTop], Cursor.m_NodeIndices[Cursor.m_iTop]);
			if ((pNode->m_Flags & (DFDupData | DFSubData)) != DFSubData)
				DMibError("Named record is not a database");
		}
		else if (!(Result == DMdbNotFound && (_Flags & EDatabaseSubFlag_Create)))
			DMibError(NStr::fg_Format("Error looking for database record: {}", NPrivate::fg_StrError(Result)));

		/* Done here so we cannot fail after creating a new DB */
		char *pNameCopy;
		umint NameLen = NStr::fg_StrLen(_pName);
		{
			pNameCopy = (char *)NMemory::fg_Alloc(NameLen + 1);
			NStr::fg_StrCopy(pNameCopy, _pName, NameLen + 1);
		}
		auto Cleanup = g_OnScopeExit / [&]
			{
				NMemory::fg_Free(pNameCopy, NameLen + 1);
			}
		;

		if (Result)
		{
			/* DMdbNotFound and EDatabaseSubFlag_Create: Create new DB */
			Data.m_Size = sizeof(CMdbDb);
			Data.m_pData = (uint8 *)&DummyDatabase;
			memset(&DummyDatabase, 0, sizeof(DummyDatabase));
			DummyDatabase.m_RootPage = DPInvalid;
			DummyDatabase.m_Flags = _Flags & DPersistentFlags;
			Result = fg_CursorPut(&Cursor, &Key, &Data, (EDatabaseWriteFlag)DFSubData);
			if (Result)
				DMibError(NStr::fg_Format("Error creating database record: {}", NPrivate::fg_StrError(Result)));
			DatabaseFlag |= DDbDirty;
		}

		{
			/* Got info, register DBI in this txn */
			uint32 iSlot = iUnusedSlot ? iUnusedSlot : pTransaction->m_nDatabases;
			pTransaction->m_pDatabaseInfos[iSlot].m_DatabaseName.m_pData = (uint8 *)pNameCopy;
			Cleanup.f_Clear();
			pTransaction->m_pDatabaseInfos[iSlot].m_DatabaseName.m_Size = NameLength;
			pTransaction->m_pDatabaseFlags[iSlot] = DatabaseFlag;
			/* pTransaction-> and env-> are the same in read txns, use
			 * tmp variable to avoid undefined assignment
			 */
			Sequence = ++pTransaction->m_pEnvironment->m_pDatabaseSequences[iSlot];
			pTransaction->m_pDatabaseSequences[iSlot] = Sequence;

			memcpy(&pTransaction->m_pDatabases[iSlot], Data.m_pData, sizeof(CMdbDb));
			fg_DefaultCmp(pTransaction, iSlot);
			if (!iUnusedSlot)
				pTransaction->m_nDatabases++;
			return iSlot;
		}
	}

	CDatabaseSubReadTransaction CDatabaseReadTransaction::f_OpenDatabaseTransaction(char const *_pName, EDatabaseSubFlag _Flags, CDatabaseSubProperties const &_Properties)
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
		if (!DTxnDbiExist(this, _DatabaseID, DDbValid))
			DMibError("Database does not exist");

		f_CheckBlocked();

		if (m_pDatabaseFlags[_DatabaseID] & DDbStale)
		{
			CMdbCursor Cursor;
			CMdbXCursor XCursor;
			/* Stale, must read the DB's root. cursor_init does it for us. */
			fg_CursorInit(&Cursor, this, _DatabaseID, &XCursor);
		}
	}

	/** @brief Retrieve statistics for a database.
	 *
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
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

		return m_pEnvironment->f_DatabaseStatistics(&m_pDatabases[_DatabaseID]);
	}

	CDatabaseSizeStatistics DMibDatabaseInfrequentSection CDatabaseTransactionCommon::CInternal::f_SizeStatistics(CDatabaseSubID _DatabaseID)
	{
		f_PrepareStatisticsQuery(_DatabaseID);

		return m_pEnvironment->f_SizeStatistics(&m_pDatabases[_DatabaseID]);
	}

	CDatabaseEnvironmentInfo DMibDatabaseInfrequentSection CDatabaseTransactionCommon::CInternal::f_EnvironmentInfo()
	{
		return m_pEnvironment->f_EnvironmentInfo();
	}

	/** @brief Retrieve the DB flags for a database handle.
	 *
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[out] flags Address where the flags will be returned.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	uint32 CDatabaseTransactionCommon::CInternal::f_DatabaseFlags(CDatabaseSubID _DatabaseID)
	{
		/* We could return the flags for the DFreeDbi too but what's the point? */
		if (!DTxnDbiExist(this, _DatabaseID, DDbUserValid))
			DMibError("Database does not exists");
		return m_pDatabases[_DatabaseID].m_Flags & DPersistentFlags;
	}

	/** @brief Close a database handle. Normally unnecessary. Use with care:
	 *
	 * This call is not mutex protected. Handles should only be closed by
	 * a single thread, and only if no other threads are going to reference
	 * the database handle or one of its cursors any further. Do not close
	 * a handle if an existing transaction has modified its database.
	 * Doing so can cause misbehavior from database corruption to errors
	 * like DMdbBadValsize (since the DB name is gone).
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
		if (_DatabaseID < DCoreDbs || _DatabaseID >= Environment.m_MaxDatabases)
			return;
		auto &StaticDatabaseInfo = Environment.m_StaticDasabaseInfo[_DatabaseID];
		char *pNamePointer = (char *)StaticDatabaseInfo.m_DatabaseName.m_pData;
		umint NameSize = StaticDatabaseInfo.m_DatabaseName.m_Size;
		/* If there was no name, this was already closed */
		if (pNamePointer)
		{
			StaticDatabaseInfo.m_DatabaseName.m_pData = nullptr;
			StaticDatabaseInfo.m_DatabaseName.m_Size = 0;
			Environment.m_pDatabaseFlags[_DatabaseID] = 0;
			Environment.m_pDatabaseSequences[_DatabaseID]++;
			NMemory::fg_Free(pNamePointer, NameSize + 1);
		}
	}

	/** @brief Empty or delete+close a database.
	 *
	 * See #fp_CloseDatabase() for restrictions about closing the DB handle.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @param[in] _DatabaseID A database handle returned by #f_OpenDatabase()
	 * @param[in] del 0 to empty the DB, 1 to delete it from the
	 * environment and close the DB handle.
	 * @return A non-zero error value on failure and 0 on success.
	 */
	void CDatabaseTransactionCommon::CInternal::f_DropDatabase(CDatabaseSubID _DatabaseID, bool _bDelete)
	{
		using namespace NPrivate;
		CMdbCursor *pCursor, *pOther;

		if (!DTxnDbiExist(this, _DatabaseID, DDbUserValid))
			DMibError("Database does not exists");

		if (DFIsSet(m_Flags, DMdbTxnReadOnly))
			DMibError("Cannot drop database with read only transaction");

		if (DTxnDbiChanged(this, _DatabaseID))
			DMibError("Stale write transaction");

		pCursor = f_Cursor(_DatabaseID);

		auto Cleanup = g_OnScopeExit / [&]()
			{
				fg_CursorClose(pCursor);
			}
		;

		{
			int Result = fg_Drop0(pCursor, pCursor->m_pDatabase->m_Flags & EDatabaseSubFlag_SortedDuplicates);
			/* Invalidate the dropped DB's cursors */
			for (pOther = m_pCursors[_DatabaseID]; pOther; pOther = pOther->m_pNext)
				pOther->m_Flags &= ~(DCInitialized | DCEof);
			if (Result)
				DMibError(NStr::fg_Format("Error dropping database: {}", fg_StrError(Result)));
		}

		/* Can't delete the main DB */
		if (_bDelete && _DatabaseID >= DCoreDbs)
		{
			int Result = fg_Del0(this, DMainDbi, &pCursor->m_pDatabaseInfo->m_DatabaseName, nullptr, (EDatabaseWriteFlag)DFSubData);
			if (!Result)
			{
				m_pDatabaseFlags[_DatabaseID] = DDbStale;
				m_pEnvironment->f_DbiClose(_DatabaseID);
			}
			else
			{
				m_Flags |= DMdbTxnError;
				DMibError(NStr::fg_Format("Error deleting database: {}", fg_StrError(Result)));
			}
		}
		else
		{
			/* reset the DB record, mark it dirty */
			m_pDatabaseFlags[_DatabaseID] |= DDbDirty;
			m_pDatabases[_DatabaseID].m_Depth = 0;
			m_pDatabases[_DatabaseID].m_nBranchPages = 0;
			m_pDatabases[_DatabaseID].m_nLeafPages = 0;
			m_pDatabases[_DatabaseID].m_nOverflowPages = 0;
			m_pDatabases[_DatabaseID].m_nEntries = 0;
			m_pDatabases[_DatabaseID].m_RootPage = DPInvalid;

			m_Flags |= DMdbTxnDirty;
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
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
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
		if (!DTxnDbiExist(this, _DatabaseID, DDbUserValid))
			DMibError("Database does not exists");

		m_pDatabaseInfos[_DatabaseID].m_fKeyCompare = fg_Move(_fCompare);
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
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
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
		if (!DTxnDbiExist(this, _DatabaseID, DDbUserValid))
			DMibError("Database does not exists");

		m_pDatabaseInfos[_DatabaseID].m_fDataCompare = fg_Move(_fCompare);
	}
}
