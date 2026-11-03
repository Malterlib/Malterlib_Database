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

namespace NMib::NDatabase
{
	CDatabaseTransactionCommon::CDatabaseTransactionCommon() = default;

	CDatabaseTransactionCommon::~CDatabaseTransactionCommon()
	{
		if (mp_pTransaction)
			NPrivate::fg_TxnAbort(mp_pTransaction.f_Detach());
	}

	CDatabaseTransactionCommon::CDatabaseTransactionCommon(CDatabaseTransactionCommon &&_Other) = default;

	CDatabaseTransactionCommon::CDatabaseTransactionCommon(NStorage::TCUniquePointer<CInternal> &&_pTransaction)
		: mp_pTransaction(fg_Move(_pTransaction))
	{
	}

	CDatabaseTransactionCommon &CDatabaseTransactionCommon::operator = (CDatabaseTransactionCommon &&_Other) = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction() = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction(CDatabaseWriteTransaction &&_Other) = default;

	CDatabaseWriteTransaction &CDatabaseWriteTransaction::operator = (CDatabaseWriteTransaction &&_Other) = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction(CDatabaseTransactionCommon &&_Transaction)
		: CDatabaseTransactionCommon(fg_Move(_Transaction))
	{
	}

	void CDatabaseWriteTransaction::f_Commit()
	{
		if (mp_pTransaction.f_Get()->m_pChild)
			DMibError("Transaction still has a child transaction");

		NPrivate::fg_TxnCommit(mp_pTransaction.f_Detach());
	}

	CDatabaseReadTransaction::CDatabaseReadTransaction() = default;

	CDatabaseReadTransaction::CDatabaseReadTransaction(CDatabaseReadTransaction &&_Other) = default;

	CDatabaseReadTransaction &CDatabaseReadTransaction::operator = (CDatabaseReadTransaction &&_Other) = default;

	CDatabaseReadTransaction::CDatabaseReadTransaction(NStorage::TCUniquePointer<CDatabaseTransactionCommon::CInternal> &&_pTransaction)
		: CDatabaseTransactionCommon(fg_Move(_pTransaction))
	{
	}

	CDatabaseReadTransaction::CDatabaseReadTransaction(CDatabaseTransactionCommon &&_Transaction)
		: CDatabaseTransactionCommon(fg_Move(_Transaction))
	{
	}

	CDatabaseResetReadTransaction::CDatabaseResetReadTransaction(NStorage::TCUniquePointer<CDatabaseTransactionCommon::CInternal> &&_pTransaction)
		: mp_pTransaction(fg_Move(_pTransaction))
	{
	}

	/** @brief Create a transaction for use with the environment.
	 *
	 * The transaction handle may be discarded using #fg_TxnAbort() or #f_Commit().
	 * @note A transaction and its cursors must only be used by a single
	 * thread, and a thread may only have a single transaction at a time.
	 * If #EDatabaseEnvironmentFlag_NoTls is in use, this does not apply to read-only transactions.
	 * @note Cursors may not span transactions.
	 * @param[in] env An environment handle returned by #CDatabaseEnvironment::CDatabaseEnvironment()
	 * @param[in] parent If this parameter is non-NULL, the new transaction
	 * will be a nested transaction, with the transaction indicated by \b parent
	 * as its parent. Transactions may be nested to any level. A parent
	 * transaction and its cursors may not issue any other operations than
	 * f_Commit and fg_TxnAbort while it has active child transactions.
	 * @param[in] flags Special options for this transaction. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * <ul>
	 *	<li>#EDatabaseEnvironmentFlag_ReadOnly
	 *		This transaction will not perform any write operations.
	 *	<li>#EDatabaseEnvironmentFlag_NoSync
	 *		Don't flush system buffers to disk when committing this transaction.
	 *	<li>#EDatabaseEnvironmentFlag_NoMetaSync
	 *		Flush system buffers but omit metadata flush when committing this transaction.
	 * </ul>
	 * @param[out] txn Address where the new #CMdbTxn handle will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#DMdbPanic - a fatal error occurred earlier and the environment
	 *		must be shut down.
	 *	<li>#DMdbMapResized - another process wrote data beyond this CMdbEnv's
	 *		mapsize and this environment's map must be resized as well.
	 *		See #f_SetMapSize().
	 *	<li>#DMdbReadersFull - a read-only transaction was requested and
	 *		the reader lock table is full. See #f_SetMaxReaders().
	 *	<li>ENOMEM - out of memory.
	 * </ul>
	 */
	CDatabaseReadTransaction CDatabaseEnvironment::f_BeginReadTransaction()
	{
		fp_CheckOpened();

		auto Transaction = mp_pEnvironment->f_TxnBegin(nullptr, EDatabaseEnvironmentFlag_ReadOnly);
		return fg_Move(Transaction);
	}

	CDatabaseWriteTransaction CDatabaseEnvironment::f_BeginWriteTransaction(EDatabaseWriteTrasactionFlag _Flags)
	{
		fp_CheckOpened();

		static_assert(uint32(EDatabaseWriteTrasactionFlag_NoMetaSync) == uint32(EDatabaseEnvironmentFlag_NoMetaSync));
		static_assert(uint32(EDatabaseWriteTrasactionFlag_NoSync) == uint32(EDatabaseEnvironmentFlag_NoSync));

		DMibRequire((_Flags & ~(EDatabaseWriteTrasactionFlag_NoMetaSync | EDatabaseWriteTrasactionFlag_NoSync)) == 0);

		auto Transaction = mp_pEnvironment->f_TxnBegin(nullptr, fg_AutoStaticCast(_Flags));
		return fg_Move(Transaction);
	}

	CDatabaseWriteTransaction CDatabaseWriteTransaction::f_BeginChildTransaction()
	{
		auto pParentTransaction = mp_pTransaction.f_Get();
		DMibRequire(pParentTransaction);
		auto Transaction = pParentTransaction->m_pEnvironment->f_TxnBegin(mp_pTransaction.f_Get(), 0);
		return fg_Move(Transaction);
	}

	void CDatabaseTransactionCommon::CInternal::fs_CheckBlocked(uint32 _Flags)
	{
		if (_Flags & DMdbTxnFinished)
			DMibError("Transaction already finished");
		if (_Flags & DMdbTxnError)
			DMibError("Transaction in error state");
		if (_Flags & DMdbTxnHasChild)
			DMibError("Transtion has a child");
	}

	void CDatabaseTransactionCommon::CInternal::f_CheckBlocked() const
	{
		fs_CheckBlocked(m_Flags);
	}

	void CDatabaseEnvironment_Internal::f_DbiClose(CDatabaseSubID _DatabaseID)
	{
		void *pName;
		if (_DatabaseID < DCoreDbs || _DatabaseID >= m_MaxDatabases)
			return;

		auto pStaticDasabaseInfo = m_StaticDasabaseInfo.f_GetArray();

		auto &StaticDasabaseInfo = pStaticDasabaseInfo[_DatabaseID];

		pName = (ch8 *)StaticDasabaseInfo.m_DatabaseName.m_pData;
		auto Size = StaticDasabaseInfo.m_DatabaseName.m_Size;
		/* If there was no name, this was already closed */
		if (pName)
		{
			StaticDasabaseInfo.m_DatabaseName.m_pData = NULL;
			StaticDasabaseInfo.m_DatabaseName.m_Size = 0;
			m_pDatabaseFlags[_DatabaseID] = 0;
			m_pDatabaseSequences[_DatabaseID]++;
			NMemory::fg_Free(pName, Size + 1);
		}
	}

	void CDatabaseEnvironment_Internal::f_TxnFree(NPrivate::CMdbTxn *_pTransaction)
	{
		using namespace NPrivate;

		umint Size = 0;
		if (_pTransaction->m_pParent)
		{
			Size = m_MaxDatabases * (sizeof(CMdbDb) + sizeof(CMdbCursor *) + 1);
			Size += sizeof(CMdbNTxn);
		}
		else if (_pTransaction->m_Flags & EDatabaseEnvironmentFlag_ReadOnly)
		{
			Size = m_MaxDatabases * (sizeof(CMdbDb) + 1);
			Size += sizeof(CMdbTxn);
		}
		else
			DMibNeverGetHere;

		NMemory::fg_Free(_pTransaction, Size);
	}

	CDatabaseTransactionCommon CDatabaseEnvironment_Internal::f_TxnBegin(NPrivate::CMdbTxn *_pParent, uint32 _Flags)
	{
		using namespace NPrivate;
		CMdbTxn *pTransaction = nullptr;
		int Size, TransactionSize;

		_Flags &= DMdbTxnBeginFlags;
		_Flags |= m_Flags & EDatabaseEnvironmentFlag_WriteMap;

		if (m_Flags & EDatabaseEnvironmentFlag_ReadOnly & ~_Flags) // write txn in RDONLY env
			DMibError("Cannot open write transaction in read only environment");

		if (_pParent)
		{
			/* Nested transactions: Max 1 child, write txns only, no writemap */
			_Flags |= _pParent->m_Flags;
			if (_pParent->m_Flags & DMdbTxnReadOnly)
				DMibError("Cannot create child transaction for read only transaction");
			if (_Flags & DMdbTxnReadOnly)
				DMibError("Child transactions cannot be read only");
			if (_Flags & EDatabaseEnvironmentFlag_WriteMap)
				DMibError("Child transactions are not supported for write map");

			CDatabaseTransactionCommon::CInternal::fs_CheckBlocked(_Flags);

			/* Child txns save CMdbPgState and use own copy of cursors */
			Size = m_MaxDatabases * (sizeof(CMdbDb) + sizeof(CMdbCursor *) + 1);
			Size += TransactionSize = sizeof(CMdbNTxn);
		}
		else if (_Flags & EDatabaseEnvironmentFlag_ReadOnly)
		{
			Size = m_MaxDatabases * (sizeof(CMdbDb) + 1);
			Size += TransactionSize = sizeof(CMdbTxn);
		}
		else
		{
			/* Reuse preallocated write txn. However, do not touch it until
			 * fg_TxnRenew0() succeeds, since it currently may be active.
			 */
			pTransaction = m_pPreallocatedTransaction;
		}
		auto Cleanup = g_OnScopeExit / [&]
			{
				if (pTransaction && pTransaction != m_pPreallocatedTransaction)
					NMemory::fg_Free(pTransaction, Size);
			}
		;

		if (!pTransaction)
		{
			pTransaction = (CMdbTxn *)NMemory::fg_AllocInitZero(Size);

#ifdef DMibDatabase_Always64BitSupport
			if (!_pParent)
			{
				pTransaction->m_pReadOnlyPages = (NPrivate::MDB_ID3L)NMemory::fg_Alloc(DMdbTrpageSize * sizeof(MDB_ID3));
				pTransaction->m_pReadOnlyPages[0].m_ID = 0;
				pTransaction->m_ReadOnlyPageCheck = DMdbTrpageSize / 2;
			}
#endif
			pTransaction->m_pDatabaseInfos = m_StaticDasabaseInfo.f_GetArray(); // static
			pTransaction->m_pDatabases = (CMdbDb *)((char *)pTransaction + TransactionSize);
			pTransaction->m_pDatabaseFlags = (unsigned char *)pTransaction + Size - m_MaxDatabases;
			pTransaction->m_Flags = _Flags;
			pTransaction->m_pEnvironment = this;
		}

#ifdef DMibDatabase_Always64BitSupport
		auto Cleanup2 = g_OnScopeExit / [&]
			{
				if (pTransaction->m_pReadOnlyPages)
					NMemory::fg_Free(pTransaction->m_pReadOnlyPages, DMdbTrpageSize * sizeof(MDB_ID3));
			}
		;
#endif
		if (_pParent)
		{
			uint32 iDatabase;
			pTransaction->m_pCursors = (CMdbCursor **)(pTransaction->m_pDatabases + m_MaxDatabases);
			pTransaction->m_pDatabaseSequences = _pParent->m_pDatabaseSequences;
			pTransaction->m_pDirtyList = (CMdbId2L)NMemory::fg_Alloc(sizeof(CMdbId2) * DMdbIdlUmSize);
			auto Cleanup3 = g_OnScopeExit / [&]
				{
					NMemory::fg_Free(pTransaction->m_pDirtyList, sizeof(CMdbId2) * DMdbIdlUmSize);
				}
			;
			pTransaction->m_pFreePages = fg_MidlAlloc(DMdbIdlUmMax);
			auto Cleanup4 = g_OnScopeExit / [&]
				{
					fg_TxnEnd(pTransaction, gc_MDB_END_FAIL_BEGINCHILD);
				}
			;
			Cleanup3.f_Clear();
			pTransaction->m_TransactionID = _pParent->m_TransactionID;
			pTransaction->m_DirtyRoom = _pParent->m_DirtyRoom;
			pTransaction->m_pDirtyList[0].m_ID = 0;
			pTransaction->m_pSpillPages = nullptr;
			pTransaction->m_NextPageNumber = _pParent->m_NextPageNumber;
			_pParent->m_Flags |= DMdbTxnHasChild;
			_pParent->m_pChild = pTransaction;
			pTransaction->m_pParent = _pParent;
			pTransaction->m_nDatabases = _pParent->m_nDatabases;
#ifdef DMibDatabase_Always64BitSupport
			pTransaction->m_pReadOnlyPages = _pParent->m_pReadOnlyPages;
#endif
			memcpy(pTransaction->m_pDatabases, _pParent->m_pDatabases, pTransaction->m_nDatabases * sizeof(CMdbDb));
			/* Copy _pParent->m_pDatabaseFlags, but clear DDbNew */
			for (iDatabase = 0; iDatabase < pTransaction->m_nDatabases; iDatabase++)
				pTransaction->m_pDatabaseFlags[iDatabase] = _pParent->m_pDatabaseFlags[iDatabase] & ~DDbNew;
			NPrivate::CMdbNTxn *pNestedTransaction = (CMdbNTxn *)pTransaction;
			pNestedTransaction->m_PageState = m_PageState; // save parent m_PageState.m_pReclaimedPages & co
			if (m_PageState.m_pReclaimedPages)
			{
				umint Size = DMdbIdlSizeof(m_PageState.m_pReclaimedPages);
				m_PageState.m_pReclaimedPages = fg_MidlAlloc(m_PageState.m_pReclaimedPages[0]);
				memcpy(m_PageState.m_pReclaimedPages, pNestedTransaction->m_PageState.m_pReclaimedPages, Size);
			}
			NPrivate::fg_CursorShadow(_pParent, pTransaction);

			Cleanup4.f_Clear();
			Cleanup3.f_Clear();
		}
		else
		{ // EDatabaseEnvironmentFlag_ReadOnly
			if (pTransaction != m_pPreallocatedTransaction)
				pTransaction->m_pDatabaseSequences = m_pDatabaseSequences;
			fg_TxnRenew0(pTransaction);
		}
#ifdef DMibDatabase_Always64BitSupport
		Cleanup2.f_Clear();
#endif
		Cleanup.f_Clear();
		pTransaction->m_Flags |= _Flags; // could not change pTransaction=m_pPreallocatedTransaction earlier
		DMibDatabaseTrace
			(
				"begin txn {}{} {} on mdbenv {}, root page {}"
				, pTransaction->m_TransactionID
				, (_Flags & EDatabaseEnvironmentFlag_ReadOnly) ? "r" : "w"
				, (void *)pTransaction
				, (void *)this
				, pTransaction->m_pDatabases[DMainDbi].m_RootPage
			)
		;

		CDatabaseTransactionCommon Transaction;
		Transaction.mp_pTransaction = fg_Explicit(pTransaction);
		return Transaction;
	}

	/** @brief Return the transaction's ID.
	 *
	 * This returns the identifier associated with this transaction. For a
	 * read-only transaction, this corresponds to the snapshot being read;
	 * concurrent readers will frequently have the same transaction ID.
	 *
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @return A transaction ID, valid if input is an active transaction.
	 */
	CDatabaseOffset CDatabaseTransactionCommon::f_TransactionID()
	{
		return mp_pTransaction->m_TransactionID;
	}

	bool CDatabaseTransactionCommon::f_HasDatabases() const
	{
		auto &Environment = *this->mp_pTransaction->m_pEnvironment;
		return Environment.m_MaxDatabases > DCoreDbs;
	}

	CDatabaseTransactionCommon::CInternal *CDatabaseTransactionCommon::fp_GetInternal()
	{
		return mp_pTransaction.f_Get();
	}

	/** @brief Commit all the operations of a transaction into the database.
	 *
	 * The transaction handle is freed. It and its cursors must not be used
	 * again after this call.
	 * @note Earlier documentation incorrectly said all cursors would be freed.
	 * Only write-transactions free cursors.
	 * @param[in] _pTransaction A transaction handle returned by #f_TxnBegin()
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 *	<li>ENOSPC - no more disk space.
	 *	<li>EIO - a low-level I/O error occurred while writing.
	 *	<li>ENOMEM - out of memory.
	 * </ul>
	 */
	void NPrivate::fg_TxnCommit(CMdbTxn *_pTransaction)
	{
		DMibRequire(_pTransaction);
		using namespace NPrivate;
		uint32 iIndex, EndMode;
		CMdbEnv *pEnvironment;

		/* fg_TxnEnd() mode for a commit which writes nothing */
		EndMode = NPrivate::gc_MDB_END_EMPTY_COMMIT | DMdbEndUpdate | DMdbEndSlot | DMdbEndFree;

		if (_pTransaction->m_pChild)
			DMibError("Transaction still has a child transaction");

		auto Cleanup = g_OnScopeExit / [&]
			{
				NPrivate::fg_TxnAbort(_pTransaction);
			}
		;

		pEnvironment = _pTransaction->m_pEnvironment;

		if (DFIsSet(_pTransaction->m_Flags, DMdbTxnReadOnly))
			goto done;

		if (_pTransaction->m_Flags & (DMdbTxnFinished | DMdbTxnError))
		{
			if (_pTransaction->m_pParent)
				_pTransaction->m_pParent->m_Flags |= DMdbTxnError;
			if (_pTransaction->m_Flags & DMdbTxnFinished)
				DMibError("Transaction has already finished");
			if (_pTransaction->m_Flags & DMdbTxnError)
				DMibError("Transaction has already failed");
		}

		if (_pTransaction->m_pParent)
		{
			CMdbTxn *pParent = _pTransaction->m_pParent;
			CMdbPage **pLooseLink;
			CMdbId2L pDestination, pSource;
			CMdbIdl pParentSpill;
			unsigned iDestination, iSource, Length, nParentSpill;

			/* Append our free list to parent's */
			fg_MidlAppendList(&pParent->m_pFreePages, _pTransaction->m_pFreePages);
			fg_MidlFree(_pTransaction->m_pFreePages);
			/* Failures after this must either undo the changes
			 * to the parent or set DMdbTxnError in the parent.
			 */

			pParent->m_NextPageNumber = _pTransaction->m_NextPageNumber;
			pParent->m_Flags = _pTransaction->m_Flags;

			/* Merge our cursors into parent's and close them */
			fg_CursorsClose(_pTransaction, 1);

			/* Update parent's DB table. */
			memcpy(pParent->m_pDatabases, _pTransaction->m_pDatabases, _pTransaction->m_nDatabases * sizeof(CMdbDb));
			pParent->m_nDatabases = _pTransaction->m_nDatabases;
			pParent->m_pDatabaseFlags[DFreeDbi] = _pTransaction->m_pDatabaseFlags[DFreeDbi];
			pParent->m_pDatabaseFlags[DMainDbi] = _pTransaction->m_pDatabaseFlags[DMainDbi];
			for (iIndex = DCoreDbs; iIndex < _pTransaction->m_nDatabases; iIndex++)
			{
				/* preserve parent's DDbNew status */
				iDestination = pParent->m_pDatabaseFlags[iIndex] & DDbNew;
				pParent->m_pDatabaseFlags[iIndex] = _pTransaction->m_pDatabaseFlags[iIndex] | iDestination;
			}

			pDestination = pParent->m_pDirtyList;
			pSource = _pTransaction->m_pDirtyList;
			/* Remove anything in our dirty list from parent's spill list */
			if ((pParentSpill = pParent->m_pSpillPages) && (nParentSpill = pParentSpill[0]))
			{
				iDestination = iSource = nParentSpill;
				pParentSpill[0] = (CPgno)-1;
				/* Mark our dirty pages as deleted in parent spill list */
				for (iIndex = 0, Length = pSource[0].m_ID; ++iIndex <= Length;)
				{
					CMdbId ShiftedPageNumber = pSource[iIndex].m_ID << 1;
					while (ShiftedPageNumber > pParentSpill[iDestination])
						iDestination--;
					if (ShiftedPageNumber == pParentSpill[iDestination])
					{
						pParentSpill[iDestination] = 1;
						iSource = --iDestination;
					}
				}
				/* Squash deleted pagenums if we deleted any */
				for (iDestination = iSource; ++iDestination <= nParentSpill;)
				{
					if (!(pParentSpill[iDestination] & 1))
						pParentSpill[++iSource] = pParentSpill[iDestination];
				}
				pParentSpill[0] = iSource;
			}

			/* Remove anything in our spill list from parent's dirty list */
			if (_pTransaction->m_pSpillPages && _pTransaction->m_pSpillPages[0])
			{
				for (iIndex = 1; iIndex <= _pTransaction->m_pSpillPages[0]; iIndex++)
				{
					CMdbId ShiftedPageNumber = _pTransaction->m_pSpillPages[iIndex];
					if (ShiftedPageNumber & 1)
						continue; // deleted spillpg
					ShiftedPageNumber >>= 1;
					iSource = fg_Mid2lSearch(pDestination, ShiftedPageNumber);
					if (iSource <= pDestination[0].m_ID && pDestination[iSource].m_ID == ShiftedPageNumber)
					{
						pEnvironment->f_DPageFreeMemory((CMdbPage *)pDestination[iSource].m_pValue);
						while (iSource < pDestination[0].m_ID)
						{
							pDestination[iSource] = pDestination[iSource + 1];
							iSource++;
						}
						pDestination[0].m_ID--;
					}
				}
			}

			/* Find len = length of merging our dirty list with parent's */
			iDestination = pDestination[0].m_ID;
			pDestination[0].m_ID = 0; // simplify loops
			if (pParent->m_pParent)
			{
				Length = iDestination + pSource[0].m_ID;
				iSource = fg_Mid2lSearch(pSource, pDestination[iDestination].m_ID + 1) - 1;
				for (iIndex = iDestination; iSource && iIndex; iSource--)
				{
					CPgno SourcePageNumber = pSource[iSource].m_ID;
					while (SourcePageNumber < pDestination[iIndex].m_ID)
						iIndex--;
					if (SourcePageNumber == pDestination[iIndex].m_ID)
					{
						iIndex--;
						Length--;
					}
				}
			}
			else
			{ // Simplify the above for single-ancestor case
				Length = DMdbIdlUmMax - _pTransaction->m_DirtyRoom;
			}

			/* Merge our dirty list with parent's */
			iSource = pSource[0].m_ID;
			for (iIndex = Length; iSource; pDestination[iIndex--] = pSource[iSource--])
			{
				CPgno SourcePageNumber = pSource[iSource].m_ID;
				while (SourcePageNumber < pDestination[iDestination].m_ID)
					pDestination[iIndex--] = pDestination[iDestination--];
				if (SourcePageNumber == pDestination[iDestination].m_ID)
					pEnvironment->f_DPageFreeMemory((CMdbPage *)pDestination[iDestination--].m_pValue);
			}
			DMibCheck(iIndex == iDestination);
			pDestination[0].m_ID = Length;
			NMemory::fg_Free(_pTransaction->m_pDirtyList, sizeof(CMdbId2) * DMdbIdlUmSize);
			pParent->m_DirtyRoom = _pTransaction->m_DirtyRoom;
			if (_pTransaction->m_pSpillPages)
			{
				if (pParent->m_pSpillPages)
				{
					/* TODO: Prevent failure here, so parent does not fail */
					{
						auto Cleanup = g_OnScopeExit / [&]()
							{
								pParent->m_Flags |= DMdbTxnError;
							}
						;
						fg_MidlAppendList(&pParent->m_pSpillPages, _pTransaction->m_pSpillPages);
						Cleanup.f_Clear();
					}
					fg_MidlFree(_pTransaction->m_pSpillPages);
					fg_MidlSort(pParent->m_pSpillPages);
				}
				else
					pParent->m_pSpillPages = _pTransaction->m_pSpillPages;
			}

			/* Append our loose page list to parent's */
			for (pLooseLink = &pParent->m_pLoosePages; *pLooseLink; pLooseLink = &DNextLoosePage(*pLooseLink))
				;
			*pLooseLink = _pTransaction->m_pLoosePages;
			pParent->m_nLoosePages += _pTransaction->m_nLoosePages;

			pParent->m_pChild = nullptr;
			fg_MidlFree(((CMdbNTxn *)_pTransaction)->m_PageState.m_pReclaimedPages);
			pEnvironment->f_TxnFree(_pTransaction);
			Cleanup.f_Clear();
			return;
		}

		if (_pTransaction != pEnvironment->m_pTransaction)
			DMibError("Trying to commit write transaction that is not the shared write transaction");

		fg_CursorsClose(_pTransaction, 0);

		if (!_pTransaction->m_pDirtyList[0].m_ID && !(_pTransaction->m_Flags & (DMdbTxnDirty | DMdbTxnSpills)))
			goto done;

		DMibDatabaseTrace
			(
				"committing txn {} {} on mdbenv {}, root page {}"
				, _pTransaction->m_TransactionID
				, (void *)_pTransaction
				, (void *)pEnvironment
				, _pTransaction->m_pDatabases[DMainDbi].m_RootPage
			)
		;

		/* Update DB root pointers */
		if (_pTransaction->m_nDatabases > DCoreDbs)
		{
			CMdbCursor Cursor;
			CDatabaseSubID iDatabase;
			CDatabaseValue Data;
			Data.m_Size = sizeof(CMdbDb);

			NPrivate::fg_CursorInit(&Cursor, _pTransaction, DMainDbi, nullptr);
			for (iDatabase = DCoreDbs; iDatabase < _pTransaction->m_nDatabases; iDatabase++)
			{
				if (_pTransaction->m_pDatabaseFlags[iDatabase] & DDbDirty)
				{
					if (DTxnDbiChanged(_pTransaction, iDatabase))
						DMibError("Stale write transaction");
					Data.m_pData = (uint8 *)&_pTransaction->m_pDatabases[iDatabase];
					int Result = fg_CursorPut(&Cursor, &_pTransaction->m_pDatabaseInfos[iDatabase].m_DatabaseName, &Data, (EDatabaseWriteFlag)DFSubData);
					if (Result)
						DMibError(NStr::fg_Format("Error updating root pointers: {}", fg_StrError(Result)));
				}
			}
		}

		{
			int Result = fg_FreelistSave(_pTransaction);
			if (Result)
				DMibError(NStr::fg_Format("Error saving free list: {}", fg_StrError(Result)));
		}

		fg_MidlFree(pEnvironment->m_PageState.m_pReclaimedPages);
		pEnvironment->m_PageState.m_pReclaimedPages = nullptr;
		fg_MidlShrink(&_pTransaction->m_pFreePages);

#if (DMibDatabaseDebug) > 2
		fg_Audit(_pTransaction);
#endif

		if (int Result = fg_PageFlush(_pTransaction, 0))
			DMibError(NStr::fg_Format("Error flushing pages: {}", fg_StrError(Result)));
		if (!DFIsSet(_pTransaction->m_Flags, DMdbTxnNoSync))
			pEnvironment->f_Sync0(0, _pTransaction->m_NextPageNumber);
		if (int Result = NPrivate::fg_EnvWriteMeta(_pTransaction))
			DMibError(NStr::fg_Format("Error writing metadata: {}", fg_StrError(Result)));
		EndMode = gc_MDB_END_COMMITTED | DMdbEndUpdate;
		if (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_PreviousSnapshot)
		{
			if (!(pEnvironment->m_Flags & EDatabaseEnvironmentFlag_NoLock))
			{
				int Exclusive;
				if (int Result = pEnvironment->f_ShareLocks(&Exclusive))
					DMibError(NStr::fg_Format("Error sharing locks: {}", fg_StrError(Result)));
			}
			pEnvironment->m_Flags ^= EDatabaseEnvironmentFlag_PreviousSnapshot;
		}

	done:
		Cleanup.f_Clear();
		fg_TxnEnd(_pTransaction, EndMode);
	}

	/** @brief Abandon all the operations of the transaction instead of saving them.
	 *
	 * The transaction handle is freed. It and its cursors must not be used
	 * again after this call.
	 * @note Earlier documentation incorrectly said all cursors would be freed.
	 * Only write-transactions free cursors.
	 * @param[in] _pTransaction A transaction handle returned by #f_TxnBegin()
	 */
	void NPrivate::fg_TxnAbort(CMdbTxn *_pTransaction)
	{
		if (_pTransaction == nullptr)
			return;

		if (_pTransaction->m_pChild)
			fg_TxnAbort(_pTransaction->m_pChild);

		fg_TxnEnd(_pTransaction, unsigned(NPrivate::gc_MDB_END_ABORT) | DMdbEndSlot | DMdbEndFree);
	}

	/** @brief Reset a read-only transaction.
	 *
	 * Abort the transaction like #fg_TxnAbort(), but keep the transaction
	 * handle. #f_Renew() may reuse the handle. This saves allocation
	 * overhead if the process will start a new read-only transaction soon,
	 * and also locking overhead if #EDatabaseEnvironmentFlag_NoTls is in use. The reader table
	 * lock is released, but the table slot stays tied to its thread or
	 * #CMdbTxn. Use fg_TxnAbort() to discard a reset handle, and to free
	 * its lock table slot if EDatabaseEnvironmentFlag_NoTls is in use.
	 * Cursors opened within the transaction must not be used
	 * again after this call.
	 * Reader locks generally don't interfere with writers, but they keep old
	 * versions of database pages allocated. Thus they prevent the old pages
	 * from being reused when writers commit new data, and so under heavy load
	 * the database size may grow much more rapidly than otherwise.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 */
	CDatabaseResetReadTransaction CDatabaseReadTransaction::f_Reset()
	{
		auto pTransaction = mp_pTransaction.f_Get();
		/* This call is only valid for read-only txns */
		if (!(pTransaction->m_Flags & DMdbTxnReadOnly))
			DMibError("You cannot reset a write rtransaction");

		NPrivate::fg_TxnEnd(pTransaction, NPrivate::gc_MDB_END_RESET);

		return fg_Move(mp_pTransaction);
	}

	/** @brief Renew a read-only transaction.
	 *
	 * This acquires a new reader lock for a transaction handle that had been
	 * released by #f_Reset(). It must be called before a reset transaction
	 * may be used again.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#DMdbPanic - a fatal error occurred earlier and the environment
	 *		must be shut down.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	CDatabaseReadTransaction CDatabaseResetReadTransaction::f_Renew()
	{
		auto pTransaction = mp_pTransaction.f_Get();

		if (!(pTransaction->m_Flags & DMdbTxnReadOnly))
			DMibError("Only read only transactions can be reused");
		if (!(pTransaction->m_Flags & DMdbTxnFinished))
			DMibError("Only finished transactions can be reused");

		NPrivate::fg_TxnRenew0(pTransaction);
		DMibDatabaseTrace
			(
				"renew txn {}{} {} on mdbenv {}, root page {}"
				, pTransaction->m_TransactionID
				, (pTransaction->m_Flags & DMdbTxnReadOnly) ? "r" : "w"
				, (void *)pTransaction
				, (void *)pTransaction->m_pEnvironment
				, pTransaction->m_pDatabases[DMainDbi].m_RootPage
			)
		;
		return fg_Move(mp_pTransaction);
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

	/** Common code for #f_TxnBegin() and #f_Renew().
	 * @param[in] _pTransaction the transaction handle to initialize
	 * @return 0 on success, non-zero on failure.
	 */
	void NPrivate::fg_TxnRenew0(CMdbTxn *_pTransaction)
	{
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		CLocksMemoryMap *pLocksMemoryMap = pEnvironment->m_pLocksMemoryMap;
		CMdbMeta *pMeta;
		uint32 iIndex, nReaders, Flags = _pTransaction->m_Flags;
		uint16 DatabaseFlags;
		int NewNoTlsFlag = 0;

		if ((Flags &= DMdbTxnReadOnly) != 0)
		{
			if (!pLocksMemoryMap)
			{
				pMeta = pEnvironment->f_PickMeta();
				_pTransaction->m_TransactionID = pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
				_pTransaction->m_pReader = nullptr;
			}
			else
			{
				CMdbReader *pReader = (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_NoTls) ? _pTransaction->m_pReader : (CMdbReader *)pthread_getspecific(pEnvironment->m_ThreadKey);
				if (pReader)
				{
					if
					(
						pReader->m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) != pEnvironment->m_ProcessID
						|| pReader->m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) != (CTransactionID)-1
					)
					{
						DMibError("Invalid reuse of reader locktable slot");
					}
				}
				else
				{
					CProcessID ProcessID = pEnvironment->m_ProcessID;
					CThreadID ThreadID = pthread_self(); // NOLINT(malterlib-conceptual-prefix): pthread_t is a pointer on macOS and an integer on Linux
					CMdbMutexRef pReaderMutex = pEnvironment->m_ReaderMutex;

					if (!pEnvironment->m_bLiveReader)
					{
						int Result = pEnvironment->f_ReaderPid(EPidLockOp_Set, ProcessID);
						if (Result)
							DMibError(NStr::fg_Format("Failed reader pid: {}", fg_StrError(Result)));
						pEnvironment->m_bLiveReader = 1;
					}

					{
						int Result;
						if (DLockMutex(Result, pEnvironment, pReaderMutex))
							DMibError(NStr::fg_Format("Read mutex lock failed: {}", fg_StrError(Result)));
					}
					auto Unlock = g_OnScopeExit / [&]()
						{
							DUnlockMutex(pReaderMutex);
						}
					;
					nReaders = pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
					for (iIndex = 0; iIndex < nReaders; iIndex++)
					{
						if (pLocksMemoryMap->m_Readers[iIndex].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Acquire) == 0)
							break;
					}
					if (iIndex == pEnvironment->m_MaxReaders)
						DMibError("Readers full");
					pReader = &pLocksMemoryMap->m_Readers[iIndex];
					/* Claim the reader slot, carefully since other code
					 * uses the reader table un-mutexed: First reset the
					 * slot, next publish it in m_TransactionBody.m_NumReaders.  After
					 * that, it is safe for f_Close() to touch it.
					 * When it will be closed, we can finally claim it.
					 */
					pReader->m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
					pReader->m_BeginReadTransactionID.f_Store((CTransactionID)-1, NAtomic::gc_MemoryOrder_Relaxed);
					pReader->m_OwningThreadID.f_Store(ThreadID, NAtomic::gc_MemoryOrder_Relaxed);
					if (iIndex == nReaders)
						pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Store(++nReaders, NAtomic::gc_MemoryOrder_Relaxed);
					pEnvironment->m_nCloseReaders.f_Store(nReaders, NAtomic::gc_MemoryOrder_Relaxed);
					pReader->m_OwningProcessID.f_Store(ProcessID, NAtomic::gc_MemoryOrder_Release);

					NewNoTlsFlag = (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_NoTls);
					{
						int Result;
						if (!NewNoTlsFlag && (Result = pthread_setspecific(pEnvironment->m_ThreadKey, pReader)))
						{
							pReader->m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
							DMibError(NStr::fg_Format("pthread_setspecific failed: {}", fg_StrError(Result)));
						}
					}
				}
				do // LY: Retry on a race, ITS#7970.
					pReader->m_BeginReadTransactionID.f_Store(pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire), NAtomic::gc_MemoryOrder_Release);
				while
				(
					pReader->m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire) != pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire)
				);
				_pTransaction->m_TransactionID = pReader->m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
				_pTransaction->m_pReader = pReader;
				pMeta = pEnvironment->m_Metas[_pTransaction->m_TransactionID & 1];
			}
		}
		else
		{
			/* Not yet touching _pTransaction == pEnvironment->m_pPreallocatedTransaction, it may be active */
			if (pLocksMemoryMap)
			{
				{
					int Result;
					if (DLockMutex(Result, pEnvironment, pEnvironment->m_WriterMutex))
						DMibError(NStr::fg_Format("Write mutex lock failed: {}", fg_StrError(Result)));
					pEnvironment->m_LockedThreadID = NSys::fg_Thread_GetCurrentUID();
				}
				_pTransaction->m_TransactionID = pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire);
				pMeta = pEnvironment->m_Metas[_pTransaction->m_TransactionID & 1];
			}
			else
			{
				pMeta = pEnvironment->f_PickMeta();
				_pTransaction->m_TransactionID = pMeta->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			}
			_pTransaction->m_TransactionID++;
#if DMibDatabaseDebug
			if (_pTransaction->m_TransactionID == g_DebugStartTransactionID)
				g_bDebug = 1;
#endif
			_pTransaction->m_pChild = nullptr;
			_pTransaction->m_pLoosePages = nullptr;
			_pTransaction->m_nLoosePages = 0;
			_pTransaction->m_DirtyRoom = DMdbIdlUmMax;
			_pTransaction->m_pDirtyList = pEnvironment->m_pDirtyList;
			_pTransaction->m_pDirtyList[0].m_ID = 0;
			_pTransaction->m_pFreePages = pEnvironment->m_pFreePages;
			_pTransaction->m_pFreePages[0] = 0;
			_pTransaction->m_pSpillPages = nullptr;
			pEnvironment->m_pTransaction = _pTransaction;
			memcpy(_pTransaction->m_pDatabaseSequences, pEnvironment->m_pDatabaseSequences, pEnvironment->m_MaxDatabases * sizeof(uint32));
		}

		/* Copy the DB info and flags */
		memcpy(_pTransaction->m_pDatabases, pMeta->m_Databases, DCoreDbs * sizeof(CMdbDb));

		/* Moved to here to avoid a data race in read TXNs */
		_pTransaction->m_NextPageNumber = pMeta->m_LastPageNumber + 1;
#ifdef DMibDatabase_Always64BitSupport
		_pTransaction->m_LastPageNumber = _pTransaction->m_NextPageNumber - 1;
#endif

		_pTransaction->m_Flags = Flags;

		/* Setup db info */
		_pTransaction->m_nDatabases = pEnvironment->m_nDatabases;
		for (iIndex = DCoreDbs; iIndex < _pTransaction->m_nDatabases; iIndex++)
		{
			DatabaseFlags = pEnvironment->m_pDatabaseFlags[iIndex];
			_pTransaction->m_pDatabases[iIndex].m_Flags = DatabaseFlags & DPersistentFlags;
			_pTransaction->m_pDatabaseFlags[iIndex] = (DatabaseFlags & DMdbValid) ? DDbValid | DDbUserValid | DDbStale : 0;
		}
		_pTransaction->m_pDatabaseFlags[DMainDbi] = DDbValid | DDbUserValid;
		_pTransaction->m_pDatabaseFlags[DFreeDbi] = DDbValid;

		auto Cleanup = g_OnScopeExit / [&]
			{
				NPrivate::fg_TxnEnd(_pTransaction, NewNoTlsFlag /*0 or DMdbEndSlot*/ | NPrivate::gc_MDB_END_FAIL_BEGIN);
			}
		;

		if (pEnvironment->m_Flags & DMdbFatalError)
			DMibError("Database environment had fatal error, must shutdown!");
		else if (pEnvironment->m_nMaxPages < _pTransaction->m_NextPageNumber)
			DMibError("Database size changed");
		else
			Cleanup.f_Clear();
	}

	/** Export or close DBI handles opened in this txn. */
	void NPrivate::fg_DbisUpdate(CMdbTxn *_pTransaction, int _bKeep)
	{
		int iDatabase;
		CDatabaseSubID nDatabases = _pTransaction->m_nDatabases;
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		unsigned char *pDatabaseFlags = _pTransaction->m_pDatabaseFlags;

		auto pStaticDatabasesInfos = pEnvironment->m_StaticDasabaseInfo.f_GetArray();
		for (iDatabase = nDatabases; --iDatabase >= DCoreDbs;)
		{
			if (pDatabaseFlags[iDatabase] & DDbNew)
			{
				if (_bKeep)
					pEnvironment->m_pDatabaseFlags[iDatabase] = _pTransaction->m_pDatabases[iDatabase].m_Flags | DMdbValid;
				else
				{
					auto &StaticDatabaseInfo = pStaticDatabasesInfos[iDatabase];
					char *pName = (char *)StaticDatabaseInfo.m_DatabaseName.m_pData;
					umint Size = StaticDatabaseInfo.m_DatabaseName.m_Size;
					if (pName)
					{
						StaticDatabaseInfo.m_DatabaseName.m_pData = nullptr;
						StaticDatabaseInfo.m_DatabaseName.m_Size = 0;
						pEnvironment->m_pDatabaseFlags[iDatabase] = 0;
						pEnvironment->m_pDatabaseSequences[iDatabase]++;
						NMemory::fg_Free(pName, Size + 1);
					}
				}
			}
		}
		if (_bKeep && pEnvironment->m_nDatabases < nDatabases)
			pEnvironment->m_nDatabases = nDatabases;
	}

	/** End a transaction, except successful commit of a nested transaction.
	 * May be called twice for readonly txns: First reset it, then abort.
	 * @param[in] _pTransaction the transaction handle to end
	 * @param[in] _Mode why and how to end the transaction
	 */
	void NPrivate::fg_TxnEnd(CMdbTxn *_pTransaction, unsigned _Mode)
	{
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
#if DMibDatabaseDebug
		static char const * const s_EndNames[] = DMdbEndNames;
#endif

		/* Export or close DBI handles opened in this txn */
		fg_DbisUpdate(_pTransaction, _Mode & DMdbEndUpdate);

		DMibDatabaseTrace
			(
				"{} txn {}{} {} on mdbenv {}, root page {}"
				, s_EndNames[_Mode & DMdbEndOpmask]
				, _pTransaction->m_TransactionID
				, (_pTransaction->m_Flags & DMdbTxnReadOnly) ? "r" : "w"
				, (void *)_pTransaction
				, (void *)pEnvironment
				, _pTransaction->m_pDatabases[DMainDbi].m_RootPage
			)
		;

		if (DFIsSet(_pTransaction->m_Flags, DMdbTxnReadOnly))
		{
			if (_pTransaction->m_pReader)
			{
				_pTransaction->m_pReader->m_BeginReadTransactionID.f_Store((CTransactionID)-1, NAtomic::gc_MemoryOrder_Release);
				if (!(pEnvironment->m_Flags & EDatabaseEnvironmentFlag_NoTls))
					_pTransaction->m_pReader = nullptr; // _pTransaction does not own reader
				else if (_Mode & DMdbEndSlot)
				{
					_pTransaction->m_pReader->m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
					_pTransaction->m_pReader = nullptr;
				} // else _pTransaction owns the slot until it does DMdbEndSlot
			}
			_pTransaction->m_nDatabases = 0; // prevent further DBI activity
			_pTransaction->m_Flags |= DMdbTxnFinished;
		}
		else if (!DFIsSet(_pTransaction->m_Flags, DMdbTxnFinished))
		{
			CPgno *pReclaimedPages = pEnvironment->m_PageState.m_pReclaimedPages;

			if (!(_Mode & DMdbEndUpdate)) // !(already closed cursors)
				fg_CursorsClose(_pTransaction, 0);
			if (!(pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMap))
				fg_DListFree(_pTransaction);

			_pTransaction->m_nDatabases = 0;
			_pTransaction->m_Flags = DMdbTxnFinished;

			if (!_pTransaction->m_pParent)
			{
				fg_MidlShrink(&_pTransaction->m_pFreePages);
				pEnvironment->m_pFreePages = _pTransaction->m_pFreePages;
				/* m_PageState: */
				pEnvironment->m_PageState.m_pReclaimedPages = nullptr;
				pEnvironment->m_PageState.m_LastRecordID = 0;

				pEnvironment->m_pTransaction = nullptr;
				_Mode = 0; // _pTransaction == pEnvironment->m_pPreallocatedTransaction, do not free() it

				/* The writer mutex was locked in f_TxnBegin. */
				if (pEnvironment->m_pLocksMemoryMap)
				{
					DMibCheck(pEnvironment->m_LockedThreadID == NSys::fg_Thread_GetCurrentUID())(pEnvironment->m_LockedThreadID)(NSys::fg_Thread_GetCurrentUID());
					DUnlockMutex(pEnvironment->m_WriterMutex);
				}
			}
			else
			{
				_pTransaction->m_pParent->m_pChild = nullptr;
				_pTransaction->m_pParent->m_Flags &= ~DMdbTxnHasChild;
				pEnvironment->m_PageState = ((CMdbNTxn *)_pTransaction)->m_PageState;
				fg_MidlFree(_pTransaction->m_pFreePages);
				fg_MidlFree(_pTransaction->m_pSpillPages);
				NMemory::fg_Free(_pTransaction->m_pDirtyList, sizeof(CMdbId2) * DMdbIdlUmSize);
			}

			fg_MidlFree(pReclaimedPages);
		}

#ifdef DMibDatabase_Always64BitSupport
		if (!_pTransaction->m_pParent)
		{
			MDB_ID3L el = pEnvironment->m_pReadOnlyPages, tl = _pTransaction->m_pReadOnlyPages;
			unsigned i, x, n = tl[0].m_ID;
			pthread_mutex_lock(&pEnvironment->m_ReadOnlyPagesMutex);
			for (i = 1; i <= n; i++)
			{
				if (tl[i].m_ID & (DMdbRpageChunk - 1))
				{
					/* tmp overflow pages that we didn't share in env */
					munmap(tl[i].m_pValue, tl[i].m_nPages * pEnvironment->m_PageSize);
				}
				else
				{
					x = fg_Mid3lSearch(el, tl[i].m_ID);
					if (tl[i].m_pValue == el[x].m_pValue)
						el[x].m_nReferences--;
					else
					{
						/* another tmp overflow page */
						munmap(tl[i].m_pValue, tl[i].m_nPages * pEnvironment->m_PageSize);
					}
				}
			}
			pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
			tl[0].m_ID = 0;
			if (_Mode & DMdbEndFree)
				NMemory::fg_Free(tl, DMdbTrpageSize * sizeof(MDB_ID3));
		}
#endif
		if (_Mode & DMdbEndFree)
			pEnvironment->f_TxnFree(_pTransaction);
	}

	/** Save the freelist as of this transaction to the freeDB.
	 * This changes the freelist. Keep trying until it stabilizes.
	 *
	 * When (DMdbDevel) & 2, the changes do not affect #fg_PageAlloc(),
	 * it then uses the transaction's original snapshot of the freeDB.
	 */
	int NPrivate::fg_FreelistSave(CMdbTxn *_pTransaction)
	{
#ifndef DMibClangAnalyzerWorkaround
		/* env->m_PageState.m_pReclaimedPages[] can grow and shrink during this call.
		 * env->m_PageState.m_LastRecordID and _pTransaction->m_pFreePages[] can only grow.
		 * Page numbers cannot disappear from _pTransaction->m_pFreePages[].
		 */
		CMdbCursor Cursor;
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		int Result, nMaxFreePerPage = pEnvironment->m_nMaxFreePerPage, nRetriesLeft = 1;
		CTransactionID LastHeadID = 0, HeadID = 0;
		CPgno nFree = 0, *pFreePages, *pReclaimedPages;
		smint HeadRoom = 0, TotalRoom = 0, nReclaimed, CleanLimit;

		NPrivate::fg_CursorInit(&Cursor, _pTransaction, DFreeDbi, nullptr);

		if (pEnvironment->m_PageState.m_pReclaimedPages)
		{
			/* Make sure first page of freeDB is touched and on freelist */
			Result = NPrivate::fg_PageSearch(&Cursor, nullptr, DMdbPsFirst | DMdbPsModify);
			if (Result && Result != DMdbNotFound)
				return Result;
		}

		if (!pEnvironment->m_PageState.m_pReclaimedPages && _pTransaction->m_pLoosePages)
		{
			/* Put loose page numbers in m_pFreePages, since
			 * we may be unable to return them to m_PageState.m_pReclaimedPages.
			 */
			CMdbPage *pPage = _pTransaction->m_pLoosePages;
			CMdbId2 *pDirtyList = _pTransaction->m_pDirtyList;
			unsigned iDirty;
			fg_MidlNeed(&_pTransaction->m_pFreePages, _pTransaction->m_nLoosePages);

			for (; pPage; pPage = DNextLoosePage(pPage))
			{
				fg_MidlXAppend(_pTransaction->m_pFreePages, pPage->m_PageNumber);
				/* must also remove from dirty list */
				if (_pTransaction->m_Flags & DMdbTxnWriteMap)
				{
					for (iDirty = 1; iDirty <= pDirtyList[0].m_ID; iDirty++)
					{
						if (pDirtyList[iDirty].m_ID == pPage->m_PageNumber)
							break;
					}
					DMibCheck(iDirty <= pDirtyList[0].m_ID);
				}
				else
				{
					iDirty = fg_Mid2lSearch(pDirtyList, pPage->m_PageNumber);
					DMibCheck(pDirtyList[iDirty].m_ID == pPage->m_PageNumber);
					pEnvironment->f_DPageFree(pPage);
				}
				pDirtyList[iDirty].m_pValue = NULL;
			}
			{
				/* squash freed slots out of the dirty list */
				unsigned iNext;
				for (iNext = 1; pDirtyList[iNext].m_pValue && iNext <= pDirtyList[0].m_ID; iNext++);
				if (iNext <= pDirtyList[0].m_ID)
				{
					for (iDirty = iNext, iNext++;;)
					{
						while (!pDirtyList[iNext].m_pValue && iNext <= pDirtyList[0].m_ID)
							iNext++;
						if (iNext > pDirtyList[0].m_ID)
							break;
						pDirtyList[iDirty++] = pDirtyList[iNext++];
					}
					pDirtyList[0].m_ID = iDirty - 1;
				}
				else
				{
					/* all slots freed */
					pDirtyList[0].m_ID = 0;
				}
			}
			_pTransaction->m_pLoosePages = nullptr;
			_pTransaction->m_nLoosePages = 0;
		}

		/* EDatabaseWriteFlag_ReserveSpace cancels meminit in ovpage NMemory::fg_Alloc (when no WRITEMAP) */
		CleanLimit = (pEnvironment->m_Flags & (EDatabaseEnvironmentFlag_NoMemoryInit | EDatabaseEnvironmentFlag_WriteMap)) ? SSIZE_MAX : nMaxFreePerPage;

		for (;;)
		{
			/* Come back here after each Put() in case freelist changed */
			CDatabaseValue Key, Data;
			CPgno *pPages;
			smint iClean;

			/* If using records from freeDB which we have not yet
			 * deleted, delete them and any we reserved for m_PageState.m_pReclaimedPages.
			 */
			while (LastHeadID < pEnvironment->m_PageState.m_LastRecordID)
			{
				Result = NPrivate::fg_CursorFirst(&Cursor, &Key, nullptr);
				if (Result)
					return Result;
				LastHeadID = HeadID = *(CTransactionID *)Key.m_pData;
				TotalRoom = HeadRoom = 0;
				DMibCheck(LastHeadID <= pEnvironment->m_PageState.m_LastRecordID);
				Result = fg_CursorDel(&Cursor, EDatabaseWriteFlag_None);
				if (Result)
					return Result;
			}

			/* Save the IDL of pages freed by this txn, to a single record */
			if (nFree < _pTransaction->m_pFreePages[0])
			{
				if (!nFree)
				{
					/* Make sure last page of freeDB is touched and on freelist */
					Result = NPrivate::fg_PageSearch(&Cursor, nullptr, DMdbPsLast | DMdbPsModify);
					if (Result && Result != DMdbNotFound)
						return Result;
				}
				pFreePages = _pTransaction->m_pFreePages;
				/* Write to last page of freeDB */
				Key.m_Size = sizeof(_pTransaction->m_TransactionID);
				Key.m_pData = (uint8 *)&_pTransaction->m_TransactionID;
				do
				{
					nFree = pFreePages[0];
					Data.m_Size = DMdbIdlSizeof(pFreePages);
					Result = fg_CursorPut(&Cursor, &Key, &Data, EDatabaseWriteFlag_ReserveSpace);
					if (Result)
						return Result;
					/* Retry if m_pFreePages[] grew during the Put() */
					pFreePages = _pTransaction->m_pFreePages;
				}
				while (nFree < pFreePages[0]);
				fg_MidlSort(pFreePages);
				memcpy((ch8 *)Data.m_pData, pFreePages, Data.m_Size);
#	if (DMibDatabaseDebug) > 1
				{
					uint32 iPage = pFreePages[0];
					DMibDatabaseTrace("IDL write txn {} root {} num {}", _pTransaction->m_TransactionID, _pTransaction->m_pDatabases[DFreeDbi].m_RootPage, iPage);
					for (; iPage; iPage--)
						DMibDatabaseTrace("IDL {}", pFreePages[iPage]);
				}
#	endif
				continue;
			}

			pReclaimedPages = pEnvironment->m_PageState.m_pReclaimedPages;
			nReclaimed = (pReclaimedPages ? pReclaimedPages[0] : 0) + _pTransaction->m_nLoosePages;

			/* Reserve records for m_PageState.m_pReclaimedPages[]. Split it if multi-page,
			 * to avoid searching freeDB for a page range. Use keys in
			 * range [1,m_PageState.m_LastRecordID]: Smaller than txnid of oldest reader.
			 */
			if (TotalRoom >= nReclaimed)
			{
				if (TotalRoom == nReclaimed || --nRetriesLeft < 0)
					break;
			}
			else if (HeadRoom >= nMaxFreePerPage && HeadID > 1)
			{
				/* Keep current record (overflow page), add a new one */
				HeadID--;
				HeadRoom = 0;
			}
			/* (Re)write {Key = HeadID, IDL length = HeadRoom} */
			TotalRoom -= HeadRoom;
			HeadRoom = nReclaimed - TotalRoom;
			if (HeadRoom > nMaxFreePerPage && HeadID > 1)
			{
				/* Overflow multi-page for part of m_PageState.m_pReclaimedPages */
				HeadRoom /= HeadID; // amortize page sizes
				HeadRoom += nMaxFreePerPage - HeadRoom % (nMaxFreePerPage + 1);
			}
			else if (HeadRoom < 0)
			{
				/* Rare case, not bothering to delete this record */
				HeadRoom = 0;
			}
			Key.m_Size = sizeof(HeadID);
			Key.m_pData = (uint8 *)&HeadID;
			Data.m_Size = (HeadRoom + 1) * sizeof(CPgno);
			Result = fg_CursorPut(&Cursor, &Key, &Data, EDatabaseWriteFlag_ReserveSpace);
			if (Result)
				return Result;
			/* IDL is initially empty, zero out at least the length */
			pPages = (CPgno *)Data.m_pData;
			iClean = HeadRoom > CleanLimit ? HeadRoom : 0;
			do
			{
				pPages[iClean] = 0;
			}
			while (--iClean >= 0);
			TotalRoom += HeadRoom;
		}

		/* Return loose page numbers to m_PageState.m_pReclaimedPages, though usually none are
		 * left at this point.  The pages themselves remain in m_pDirtyList.
		 */
		if (_pTransaction->m_pLoosePages)
		{
			CMdbPage *pPage = _pTransaction->m_pLoosePages;
			unsigned nLoose = _pTransaction->m_nLoosePages;
			CMdbIdl pLoose;
			/* Room for loose pages + temp IDL with same */
			fg_MidlNeed(&pEnvironment->m_PageState.m_pReclaimedPages, 2 * nLoose + 1);
			pReclaimedPages = pEnvironment->m_PageState.m_pReclaimedPages;
			pLoose = pReclaimedPages + DMdbIdlAlloclen(pReclaimedPages) - nLoose;
			for (nLoose = 0; pPage; pPage = DNextLoosePage(pPage))
				pLoose[++nLoose] = pPage->m_PageNumber;
			pLoose[0] = nLoose;
			fg_MidlSort(pLoose);
			fg_MidlXMerge(pReclaimedPages, pLoose);
			_pTransaction->m_pLoosePages = nullptr;
			_pTransaction->m_nLoosePages = 0;
			nReclaimed = pReclaimedPages[0];
		}

		/* Fill in the reserved m_PageState.m_pReclaimedPages records */
		Result = DMdbSuccess;
		if (nReclaimed)
		{
			CDatabaseValue Key, Data;

			pReclaimedPages += nReclaimed;
			Result = NPrivate::fg_CursorFirst(&Cursor, &Key, &Data);
			for (; !Result; Result = NPrivate::fg_CursorNext(&Cursor, &Key, &Data, EDatabaseCursorGet_Next))
			{
				CTransactionID RecordID = *(CTransactionID *)Key.m_pData;
				smint nLength = (smint)(Data.m_Size / sizeof(CMdbId)) - 1;
				CMdbId Saved;

				DMibCheck(nLength >= 0 && RecordID <= pEnvironment->m_PageState.m_LastRecordID);
				Key.m_pData = (uint8 *)&RecordID;
				if (nLength > nReclaimed)
				{
					nLength = nReclaimed;
					Data.m_Size = (nLength + 1) * sizeof(CMdbId);
				}
				pReclaimedPages -= nLength;
				Data.m_pData = (uint8 *)pReclaimedPages;
				Saved = pReclaimedPages[0];
				pReclaimedPages[0] = nLength;
				Result = fg_CursorPut(&Cursor, &Key, &Data, EDatabaseWriteFlag_OverwriteAtCurrentCursorPos);
				pReclaimedPages[0] = Saved;
				if (Result || !(nReclaimed -= nLength))
					break;
			}
		}
		return Result;
#else
		return 0;
#endif
	}
}
