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
	using namespace NPrivate;
	/** Allocate memory for a page.
	 * Re-use old malloc'd pages first for singletons, otherwise just NMemory::fg_Alloc.
	 * Set #DMdbTxnError on failure.
	 */
	NPrivate::CMdbPage *NPrivate::fg_PageMalloc(CMdbTxn *_pTransaction, unsigned _nPages)
	{
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		CMdbPage *pPage = pEnvironment->m_pReusablePages;
		umint PageSize = pEnvironment->m_PageSize, Size = PageSize, Offset;
		/* For ! #EDatabaseEnvironmentFlag_NoMemoryInit, PageSize counts how much to init.
		 * For a single page alloc, we init everything after the page header.
		 * For multi-page, we init the final page; if the caller needed that
		 * many pages they will be filling in at least up to the last page.
		 */
		if (_nPages == 1)
		{
			if (pPage)
			{
				pEnvironment->m_pReusablePages = pPage->m_pNext;
				return pPage;
			}
			PageSize -= Offset = DPageHeaderSize;
		}
		else
		{
			Size *= _nPages;
			Offset = Size - PageSize;
		}
		if ((pPage = (CMdbPage *)NMemory::fg_Alloc(Size)) != nullptr)
		{
			if (!(pEnvironment->m_Flags & EDatabaseEnvironmentFlag_NoMemoryInit))
			{
				memset((char *)pPage + Offset, 0, PageSize);
				pPage->m_Pad = 0;
			}
		}
		else
			_pTransaction->m_Flags |= DMdbTxnError;
		return pPage;
	}

	/** Free a single page.
	 * Saves single pages to a list, for future reuse.
	 * (This is not used for multi-page overflow pages.)
	 */
	void CDatabaseEnvironment_Internal::f_PageFree(CMdbPage *_pPage)
	{
		_pPage->m_pNext = m_pReusablePages;
		m_pReusablePages = _pPage;
	}

	void CDatabaseEnvironment_Internal::f_DPageFreeMemory(CMdbPage *_pPage)
	{
		if (!DIsOverflow(_pPage) || _pPage->m_nOverflowPages == 1)
			NMemory::fg_Free(_pPage, m_PageSize);
		else
			NMemory::fg_Free(_pPage, _pPage->m_nOverflowPages * m_PageSize);
	}

	/** Free a dirty page */
	void CDatabaseEnvironment_Internal::f_DPageFree(CMdbPage *_pPage)
	{
		if (!DIsOverflow(_pPage) || _pPage->m_nOverflowPages == 1)
			f_PageFree(_pPage);
		else
		{
			/* large pages just get freed directly */
			NMemory::fg_Free(_pPage, _pPage->m_nOverflowPages * m_PageSize);
		}
	}

	/**	Return all dirty pages to dpage list */
	void NPrivate::fg_DListFree(CMdbTxn *_pTransaction)
	{
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		CMdbId2L pDirtyList = _pTransaction->m_pDirtyList;
		unsigned iPage, nPages = pDirtyList[0].m_ID;

		for (iPage = 1; iPage <= nPages; iPage++)
			pEnvironment->f_DPageFree((CMdbPage *)(pDirtyList[iPage].m_pValue));
		pDirtyList[0].m_ID = 0;
	}

#ifdef DMibDatabase_Always64BitSupport
	void NPrivate::fg_PageUnref(CMdbTxn *_pTransaction, CMdbPage *_pPage)
	{
		CPgno PageNumber;
		CMdbId3L pReadOnlyPages = _pTransaction->m_pReadOnlyPages;
		unsigned iPage, Remainder;
		if (_pPage->m_Flags & (DPSubp | DPDirty))
			return;
		Remainder = _pPage->m_PageNumber & (DMdbRpageChunk - 1);
		PageNumber = _pPage->m_PageNumber ^ Remainder;
		iPage = fg_Mid3lSearch(pReadOnlyPages, PageNumber);
		if (iPage != pReadOnlyPages[0].m_ID && pReadOnlyPages[iPage + 1].m_ID == _pPage->m_PageNumber)
			iPage++;
		if (pReadOnlyPages[iPage].m_nReferences)
			pReadOnlyPages[iPage].m_nReferences--;
	}
#endif

	/** Loosen or free a single page.
	 * Saves single pages to a list for future reuse
	 * in this same txn. It has been pulled from the freeDB
	 * and already resides on the dirty list, but has been
	 * deleted. Use these pages first before pulling again
	 * from the freeDB.
	 *
	 * If the page wasn't dirtied in this txn, just add it
	 * to this txn's free list.
	 */
	int NPrivate::fg_PageLoose(CMdbCursor *_pCursor, CMdbPage *_pPage)
	{
		int bLoose = 0;
		CPgno PageNumber = _pPage->m_PageNumber;
		CMdbTxn *pTransaction = _pCursor->m_pTransaction;

		if ((_pPage->m_Flags & DPDirty) && _pCursor->m_DatabaseID != DFreeDbi)
		{
			if (pTransaction->m_pParent)
			{
				CMdbId2 *pDirtyList = pTransaction->m_pDirtyList;
				/* If txn has a parent, make sure the page is in our
				 * dirty list.
				 */
				if (pDirtyList[0].m_ID)
				{
					unsigned iDirty = fg_Mid2lSearch(pDirtyList, PageNumber);
					if (iDirty <= pDirtyList[0].m_ID && pDirtyList[iDirty].m_ID == PageNumber)
					{
						if (_pPage != pDirtyList[iDirty].m_pValue)
						{ // bad cursor?
							_pCursor->m_Flags &= ~(DCInitialized | DCEof);
							pTransaction->m_Flags |= DMdbTxnError;
							return DMdbProblem;
						}
						/* ok, it's ours */
						bLoose = 1;
					}
				}
			}
			else
			{
				/* no parent txn, so it's just ours */
				bLoose = 1;
			}
		}
		if (bLoose)
		{
			DMibDatabaseTrace("loosen db {} page {}", DDbi(_pCursor), _pPage->m_PageNumber);
			DNextLoosePage(_pPage) = pTransaction->m_pLoosePages;
			pTransaction->m_pLoosePages = _pPage;
			pTransaction->m_nLoosePages++;
			_pPage->m_Flags |= DPLoose;
		}
		else
			fg_MidlAppend(&pTransaction->m_pFreePages, PageNumber);

		return DMdbSuccess;
	}

	/** Set or clear DPKeep in dirty, non-overflow, non-sub pages watched by txn.
	 * @param[in] _pCursor A cursor handle for the current operation.
	 * @param[in] _PageFlags Flags of the pages to update:
	 * DPDirty to set DPKeep, DPDirty|DPKeep to clear it.
	 * @param[in] _bAll No shortcuts. Needed except after a full #fg_PageFlush().
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PagesXKeep(CMdbCursor *_pCursor, unsigned _PageFlags, int _bAll)
	{
		constexpr aint c_Mask = DPSubp | DPDirty | DPLoose | DPKeep;

		CMdbTxn *pTransaction = _pCursor->m_pTransaction;
		CMdbCursor *pWalkCursor, *pOriginalCursor = _pCursor;
		CMdbXCursor *pXCursor;
		CMdbPage *pDirtyPage, *pPage;
		CMdbNode *pLeaf;
		unsigned iDatabase, iLevel;
		int Result = DMdbSuccess, Level;

		/* Mark pages seen by cursors: First pOriginalCursor, then tracked cursors */
		for (iDatabase = pTransaction->m_nDatabases;;)
		{
			if (_pCursor->m_Flags & DCInitialized)
			{
				for (pWalkCursor = _pCursor;; pWalkCursor = &pXCursor->m_Cursor)
				{
					pPage = NULL;
					for (iLevel = 0; iLevel < pWalkCursor->m_nPages; iLevel++)
					{
						pPage = pWalkCursor->m_Pages[iLevel];
						if ((pPage->m_Flags & c_Mask) == _PageFlags)
							pPage->m_Flags ^= DPKeep;
					}
					pXCursor = pWalkCursor->m_pXCursor;
					/* Proceed to pXCursor if it is at a sub-database */
					if (!(pXCursor && (pXCursor->m_Cursor.m_Flags & DCInitialized)))
						break;
					if (!(pPage && (pPage->m_Flags & DPLeaf)))
						break;
					pLeaf = DNodePtr(pPage, pWalkCursor->m_NodeIndices[iLevel - 1]);
					if (!(pLeaf->m_Flags & DFSubData))
						break;
				}
			}
			_pCursor = _pCursor->m_pNext;
			for (; !_pCursor || _pCursor == pOriginalCursor; _pCursor = pTransaction->m_pCursors[--iDatabase])
			{
				if (iDatabase == 0)
					goto mark_done;
			}
		}

	mark_done:
		if (_bAll)
		{
			/* Mark dirty root pages */
			for (iDatabase = 0; iDatabase < pTransaction->m_nDatabases; iDatabase++)
			{
				if (pTransaction->m_pDatabaseFlags[iDatabase] & DDbDirty)
				{
					CPgno PageNumber = pTransaction->m_pDatabases[iDatabase].m_RootPage;
					if (PageNumber == DPInvalid)
						continue;
					if ((Result = NPrivate::fg_PageGet(pOriginalCursor, PageNumber, &pDirtyPage, &Level)) != DMdbSuccess)
						break;
					if ((pDirtyPage->m_Flags & c_Mask) == _PageFlags && Level <= 1)
						pDirtyPage->m_Flags ^= DPKeep;
				}
			}
		}

		return Result;
	}

	/**	Spill pages from the dirty list back to disk.
	 * This is intended to prevent running into #DMdbTxnFull situations,
	 * but note that they may still occur in a few cases:
	 *	1) our estimate of the txn size could be too small. Currently this
	 *	 seems unlikely, except with a large number of #EDatabaseWriteFlag_BatchDuplicateData items.
	 *	2) child txns may run out of space if their parents dirtied a
	 *	 lot of pages and never spilled them. TODO: we probably should do
	 *	 a preemptive spill during #f_TxnBegin() of a child txn, if
	 *	 the parent's dirty_room is below a given threshold.
	 *
	 * Otherwise, if not using nested txns, it is expected that apps will
	 * not run into #DMdbTxnFull any more. The pages are flushed to disk
	 * the same way as for a txn commit, e.g. their DPDirty flag is cleared.
	 * If the txn never references them again, they can be left alone.
	 * If the txn only reads them, they can be used without any fuss.
	 * If the txn writes them again, they can be dirtied immediately without
	 * going thru all of the work of #fg_PageTouch(). Such references are
	 * handled by #fg_PageUnspill().
	 *
	 * Also note, we never spill DB root pages, nor pages of active cursors,
	 * because we'll need these back again soon anyway. And in nested txns,
	 * we can't spill a page in a child txn if it was already spilled in a
	 * parent txn. That would alter the parent txns' data even though
	 * the child hasn't committed yet, and we'd have no way to undo it if
	 * the child aborted.
	 *
	 * @param[in] _pCursor cursor A cursor handle identifying the transaction and
	 *	database for which we are checking space.
	 * @param[in] _pKey For a put operation, the key being stored.
	 * @param[in] _pData For a put operation, the data being stored.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageSpill(CMdbCursor *_pCursor, CDatabaseValue *_pKey, CDatabaseValue *_pData)
	{
		CMdbTxn *pTransaction = _pCursor->m_pTransaction;
		CMdbPage *pDirtyPage;
		CMdbId2L pDirtyList = pTransaction->m_pDirtyList;
		uint32 iEntry, iTarget, nToSpill;
		int Result;

		if (_pCursor->m_Flags & DCSub)
			return DMdbSuccess;

		/* Estimate how much space this op will take */
		iEntry = _pCursor->m_pDatabase->m_Depth;
		/* Named DBs also dirty the main DB */
		if (_pCursor->m_DatabaseID >= DCoreDbs)
			iEntry += pTransaction->m_pDatabases[DMainDbi].m_Depth;
		/* For puts, roughly factor in the key+data size */
		if (_pKey)
			iEntry += int((DLeafSize(_pKey, _pData) + pTransaction->m_pEnvironment->m_PageSize) / pTransaction->m_pEnvironment->m_PageSize);
		iEntry += iEntry; // double it for good measure
		nToSpill = iEntry;

		if (pTransaction->m_DirtyRoom > iEntry)
			return DMdbSuccess;

		if (!pTransaction->m_pSpillPages)
		{
			pTransaction->m_pSpillPages = fg_MidlAlloc(DMdbIdlUmMax);
			if (!pTransaction->m_pSpillPages)
				return ENOMEM;
		}
		else
		{
			/* purge deleted slots */
			CMdbIdl pSpillList = pTransaction->m_pSpillPages;
			uint32 nSpilled = pSpillList[0];
			iTarget = 0;
			for (iEntry = 1; iEntry <= nSpilled; iEntry++)
			{
				if (!(pSpillList[iEntry] & 1))
					pSpillList[++iTarget] = pSpillList[iEntry];
			}
			pSpillList[0] = iTarget;
		}

		/* Preserve pages which may soon be dirtied again */
		if ((Result = fg_PagesXKeep(_pCursor, DPDirty, 1)) != DMdbSuccess)
			goto done;

		/* Less aggressive spill - we originally spilled the entire dirty list,
		 * with a few exceptions for cursor pages and DB root pages. But this
		 * turns out to be a lot of wasted effort because in a large txn many
		 * of those pages will need to be used again. So now we spill only 1/8th
		 * of the dirty pages. Testing revealed this to be a good tradeoff,
		 * better than 1/2, 1/4, or 1/10.
		 */
		if (nToSpill < DMdbIdlUmMax / 8)
			nToSpill = DMdbIdlUmMax / 8;

		/* Save the page IDs of all the pages we're flushing */
		/* flush from the tail forward, this saves a lot of shifting later on. */
		for (iEntry = pDirtyList[0].m_ID; iEntry && nToSpill; iEntry--)
		{
			CMdbId ShiftedPageNumber = pDirtyList[iEntry].m_ID << 1;
			pDirtyPage = (CMdbPage *)(pDirtyList[iEntry].m_pValue);
			if (pDirtyPage->m_Flags & (DPLoose | DPKeep))
				continue;
			/* Can't spill twice, make sure it's not already in a parent's
			 * spill list.
			 */
			if (pTransaction->m_pParent)
			{
				CMdbTxn *pAncestor;
				for (pAncestor = pTransaction->m_pParent; pAncestor; pAncestor = pAncestor->m_pParent)
				{
					if (pAncestor->m_pSpillPages)
					{
						iTarget = fg_MidlSearch(pAncestor->m_pSpillPages, ShiftedPageNumber);
						if (iTarget <= pAncestor->m_pSpillPages[0] && pAncestor->m_pSpillPages[iTarget] == ShiftedPageNumber)
						{
							pDirtyPage->m_Flags |= DPKeep;
							break;
						}
					}
				}
				if (pAncestor)
					continue;
			}
			fg_MidlAppend(&pTransaction->m_pSpillPages, ShiftedPageNumber);
			nToSpill--;
		}
		fg_MidlSort(pTransaction->m_pSpillPages);

		/* Flush the spilled part of dirty list */
		if ((Result = fg_PageFlush(pTransaction, iEntry)) != DMdbSuccess)
			goto done;

		/* Reset any dirty pages we kept that page_flush didn't see */
		Result = fg_PagesXKeep(_pCursor, DPDirty | DPKeep, iEntry);

	done:
		pTransaction->m_Flags |= Result ? DMdbTxnError : DMdbTxnSpills;
		return Result;
	}

	/** Find oldest txnid still referenced. Expects _pTransaction->m_TransactionID > 0. */
	NPrivate::CTransactionID NPrivate::fg_FindOldest(CMdbTxn *_pTransaction)
	{
		int iReader;
		CTransactionID ReaderTransactionID, OldestID = _pTransaction->m_TransactionID - 1;
		if (_pTransaction->m_pEnvironment->m_pLocksMemoryMap)
		{
			CMdbReader *pReaders = _pTransaction->m_pEnvironment->m_pLocksMemoryMap->m_Readers;
			for (iReader = _pTransaction->m_pEnvironment->m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed); --iReader >= 0;)
			{
				if (pReaders[iReader].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Acquire))
				{
					ReaderTransactionID = pReaders[iReader].m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
					if (OldestID > ReaderTransactionID)
						OldestID = ReaderTransactionID;
				}
			}
		}
		return OldestID;
	}

	/** Add a page to the txn's dirty list */
	void NPrivate::fg_PageDirty(CMdbTxn *_pTransaction, CMdbPage *_pPage)
	{
		CMdbId2 DirtyEntry;
		[[maybe_unused]] int Result;
		int (*fInsert)(CMdbId2L _pIDs, CMdbId2 *_pID);

		if (_pTransaction->m_Flags & DMdbTxnWriteMap)
			fInsert = fg_Mid2lAppend;
		else
			fInsert = fg_Mid2lInsert;
		DirtyEntry.m_ID = _pPage->m_PageNumber;
		DirtyEntry.m_pValue = _pPage;
		Result = fInsert(_pTransaction->m_pDirtyList, &DirtyEntry);
		DMibCheck(Result == 0);
		_pTransaction->m_DirtyRoom--;
	}

	/** Allocate page numbers and memory for writing.  Maintain m_PageState.m_LastRecordID,
	 * m_PageState.m_pReclaimedPages and m_NextPageNumber.  Set #DMdbTxnError on failure.
	 *
	 * If there are free pages available from older transactions, they
	 * are re-used first. Otherwise allocate a new page at m_NextPageNumber.
	 * Do not modify the freedB, just merge freeDB records into m_PageState.m_pReclaimedPages[]
	 * and move m_PageState.m_LastRecordID to say which records were consumed.  Only this
	 * function can create m_PageState.m_pReclaimedPages and move m_PageState.m_LastRecordID/m_NextPageNumber.
	 * When #DMdbDevel & 2, it is not affected by #fg_FreelistSave(): it
	 * then uses the transaction's original snapshot of the freeDB.
	 * @param[in] _pCursor cursor A cursor handle identifying the transaction and
	 *	database for which we are allocating.
	 * @param[in] _nPages the number of pages to allocate.
	 * @param[out] o_pPage Address of the allocated page(s). Requests for multiple pages
	 *  will always be satisfied by a single contiguous chunk of memory.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageAlloc(CMdbCursor *_pCursor, int _nPages, CMdbPage **o_pPage)
	{
#ifdef MDB_PARANOID	/* Seems like we can ignore this now */
		/* Get at most <Max_retries> more freeDB records once m_PageState.m_pReclaimedPages
		 * has enough pages.  If not enough, use new pages from the map.
		 * If <Paranoid> and _pCursor is updating the freeDB, only get new
		 * records if m_PageState.m_pReclaimedPages is empty. Then the freelist cannot play
		 * catch-up with itself by growing while trying to save it.
		 */
		[[maybe_unused]] constexpr aint c_Paranoid = 1;
		[[maybe_unused]] constexpr aint c_MaxRetries = 500;
#else
		[[maybe_unused]] constexpr aint c_Paranoid = 0;
		[[maybe_unused]] constexpr aint c_MaxRetries = INT_MAX;
#endif
		int Result, nRetries = _nPages * 60;
		CMdbTxn *pTransaction = _pCursor->m_pTransaction;
		CMdbEnv *pEnvironment = pTransaction->m_pEnvironment;
		CPgno PageNumber, *pReclaimedPages = pEnvironment->m_PageState.m_pReclaimedPages;
		unsigned iPage, iMove, nReclaimed = pReclaimedPages ? pReclaimedPages[0] : 0, nExtraPages = _nPages - 1;
		CMdbPage *pNewPage;
		CTransactionID OldestID = 0, LastID;
		EDatabaseCursorGet Operation;
		CMdbCursor FreeCursor;
		int bFoundOld = 0;

		/* If there are any loose pages, just use them */
		if (_nPages == 1 && pTransaction->m_pLoosePages)
		{
			pNewPage = pTransaction->m_pLoosePages;
			pTransaction->m_pLoosePages = DNextLoosePage(pNewPage);
			pTransaction->m_nLoosePages--;
			DMibDatabaseTrace("db {} use loose page {}", DDbi(_pCursor), pNewPage->m_PageNumber);
			*o_pPage = pNewPage;
			return DMdbSuccess;
		}

		*o_pPage = nullptr;

		/* If our dirty list is already full, we can't do anything */
		if (pTransaction->m_DirtyRoom == 0)
		{
			Result = DMdbTxnFull;
			goto fail;
		}

		for (Operation = EDatabaseCursorGet_First;; Operation = EDatabaseCursorGet_Next)
		{
			CDatabaseValue Key, Data;
			CMdbNode *pLeaf;
			CPgno *pIDs;

			/* Seek a big enough contiguous page range. Prefer
			 * pages at the tail, just truncating the list.
			 */
			if (nReclaimed > nExtraPages)
			{
				iPage = nReclaimed;
				do
				{
					PageNumber = pReclaimedPages[iPage];
					if (pReclaimedPages[iPage - nExtraPages] == PageNumber + nExtraPages)
						goto search_done;
				}
				while (--iPage > nExtraPages);
				if (--nRetries < 0)
					break;
			}

			if (Operation == EDatabaseCursorGet_First)
			{ // 1st iteration
				/* Prepare to fetch more and coalesce */
				LastID = pEnvironment->m_PageState.m_LastRecordID;
				OldestID = pEnvironment->m_OldestTransactionID;
				NPrivate::fg_CursorInit(&FreeCursor, pTransaction, DFreeDbi, nullptr);
#if (DMdbDevel) & 2	/* "& 2" so DMdbDevel=1 won't hide bugs breaking freeDB */
				/* Use original snapshot. TODO: Should need less care in code
				 * which modifies the database. Maybe we can delete some code?
				 */
				FreeCursor.m_Flags |= DCOrigReadOnly;
				FreeCursor.m_pDatabase = &pEnvironment->m_Metas[(pTransaction->m_TransactionID - 1) & 1]->m_Databases[DFreeDbi];
				FreeCursor.m_pDatabaseFlag = (unsigned char *)""; // probably unnecessary
#endif
				if (LastID)
				{
					Operation = EDatabaseCursorGet_SetRange;
					Key.m_pData = (uint8 *)&LastID; // will look up last+1
					Key.m_Size = sizeof(LastID);
				}
				if (c_Paranoid && _pCursor->m_DatabaseID == DFreeDbi)
					nRetries = -1;
			}
			if (c_Paranoid && nRetries < 0 && nReclaimed)
				break;

			LastID++;
			/* Do not fetch more if the record will be too recent */
			if (OldestID <= LastID)
			{
				if (!bFoundOld)
				{
					OldestID = fg_FindOldest(pTransaction);
					pEnvironment->m_OldestTransactionID = OldestID;
					bFoundOld = 1;
				}
				if (OldestID <= LastID)
					break;
			}
			Result = fg_CursorGet(&FreeCursor, &Key, nullptr, Operation);
			if (Result)
			{
				if (Result == DMdbNotFound)
					break;
				goto fail;
			}
			LastID = *(CTransactionID *)Key.m_pData;
			if (OldestID <= LastID)
			{
				if (!bFoundOld)
				{
					OldestID = fg_FindOldest(pTransaction);
					pEnvironment->m_OldestTransactionID = OldestID;
					bFoundOld = 1;
				}
				if (OldestID <= LastID)
					break;
			}
			pNewPage = FreeCursor.m_Pages[FreeCursor.m_iTop];
			pLeaf = DNodePtr(pNewPage, FreeCursor.m_NodeIndices[FreeCursor.m_iTop]);
			if ((Result = NPrivate::fg_NodeRead(&FreeCursor, pLeaf, &Data)) != DMdbSuccess)
				goto fail;

			pIDs = (CMdbId *)Data.m_pData;
			iPage = pIDs[0];
			if (!pReclaimedPages)
			{
				if (!(pEnvironment->m_PageState.m_pReclaimedPages = pReclaimedPages = fg_MidlAlloc(iPage)))
				{
					Result = ENOMEM;
					goto fail;
				}
			}
			else
			{
				fg_MidlNeed(&pEnvironment->m_PageState.m_pReclaimedPages, iPage);
				pReclaimedPages = pEnvironment->m_PageState.m_pReclaimedPages;
			}
			pEnvironment->m_PageState.m_LastRecordID = LastID;
#if (DMibDatabaseDebug) > 1
			DMibDatabaseTrace("IDL read txn {} root {} num {}", LastID, pTransaction->m_pDatabases[DFreeDbi].m_RootPage, iPage);
			for (iMove = iPage; iMove; iMove--)
				DMibDatabaseTrace("IDL {}", pIDs[iMove]);
#endif
			/* Merge in descending sorted order */
			fg_MidlXMerge(pReclaimedPages, pIDs);
			nReclaimed = pReclaimedPages[0];
		}

		/* Use new pages from the map when nothing suitable in the freeDB */
		iPage = 0;
		PageNumber = pTransaction->m_NextPageNumber;
		if (PageNumber + _nPages >= pEnvironment->m_nMaxPages)
		{
			DPuts("DB size maxed out");
			Result = DMdbMapFull;
			goto fail;
		}
#if defined(DPlatformFamily_Windows) && !defined(DMibDatabase_Always64BitSupport)
		if (!(pEnvironment->m_Flags & EDatabaseEnvironmentFlag_ReadOnly))
		{
			void *pPages;
			pPages = (CMdbPage *)(pEnvironment->m_pMap + pEnvironment->m_PageSize * PageNumber);
			pPages = VirtualAlloc(pPages, pEnvironment->m_PageSize * _nPages, MEM_COMMIT, (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMap) ? PAGE_READWRITE : PAGE_READONLY);
			if (!pPages)
			{
				DPuts("VirtualAlloc failed");
				Result = DErrCode();
				goto fail;
			}
		}
#endif

	search_done:
		if (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMap)
			pNewPage = (CMdbPage *)(pEnvironment->m_pMap + pEnvironment->m_PageSize * PageNumber);
		else
		{
			if (!(pNewPage = fg_PageMalloc(pTransaction, _nPages)))
			{
				Result = ENOMEM;
				goto fail;
			}
		}
		if (iPage)
		{
			pReclaimedPages[0] = nReclaimed -= _nPages;
			/* Move any stragglers down */
			for (iMove = iPage - _nPages; iMove < nReclaimed;)
				pReclaimedPages[++iMove] = pReclaimedPages[++iPage];
		}
		else
			pTransaction->m_NextPageNumber = PageNumber + _nPages;
		pNewPage->m_PageNumber = PageNumber;
		fg_PageDirty(pTransaction, pNewPage);
		*o_pPage = pNewPage;

		return DMdbSuccess;

	fail:
		pTransaction->m_Flags |= DMdbTxnError;
		return Result;
	}

	/** Copy the used portions of a non-overflow page.
	 * @param[in] _pDestination page to copy into
	 * @param[in] _pSource page to copy from
	 * @param[in] _PageSize size of a page
	 */
	void NPrivate::fg_PageCopy(CMdbPage *_pDestination, CMdbPage *_pSource, uint32 _PageSize)
	{
		constexpr aint c_Align = sizeof(CPgno);

		CIndx Upper = _pSource->m_FreeSpace.m_Upper, Lower = _pSource->m_FreeSpace.m_Lower, Unused = Upper - Lower;

		/* If page isn't full, just copy the used portion. Adjust
		 * alignment so memcpy may copy words instead of bytes.
		 */
		if ((Unused &= -c_Align) && !DIsLeaf2(_pSource))
		{
			Upper = (Upper + DPageBase) & -c_Align;
			memcpy(_pDestination, _pSource, (Lower + DPageBase + (c_Align - 1)) & -c_Align);
			memcpy((CPgno *)((char *)_pDestination + Upper), (CPgno *)((char *)_pSource + Upper), _PageSize - Upper);
		}
		else
			memcpy(_pDestination, _pSource, _PageSize - Unused);
	}

	/** Pull a page off the txn's spill list, if present.
	 * If a page being referenced was spilled to disk in this txn, bring
	 * it back and make it dirty/writable again.
	 * @param[in] _pTransaction the transaction handle.
	 * @param[in] _pPage the page being referenced. It must not be dirty.
	 * @param[out] o_pDirtyPage the writable page, if any. o_pDirtyPage is unchanged if
	 * _pPage wasn't spilled.
	 */
	int NPrivate::fg_PageUnspill(CMdbTxn *_pTransaction, CMdbPage *_pPage, CMdbPage **o_pDirtyPage)
	{
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		CMdbTxn const *pAncestor;
		unsigned iSpill;
		CPgno PageNumber = _pPage->m_PageNumber, ShiftedPageNumber = PageNumber << 1;

		for (pAncestor = _pTransaction; pAncestor; pAncestor = pAncestor->m_pParent)
		{
			if (!pAncestor->m_pSpillPages)
				continue;
			iSpill = fg_MidlSearch(pAncestor->m_pSpillPages, ShiftedPageNumber);
			if (iSpill <= pAncestor->m_pSpillPages[0] && pAncestor->m_pSpillPages[iSpill] == ShiftedPageNumber)
			{
				CMdbPage *pNewPage;
				int nPages;
				if (_pTransaction->m_DirtyRoom == 0)
					return DMdbTxnFull;
				if (DIsOverflow(_pPage))
					nPages = _pPage->m_nOverflowPages;
				else
					nPages = 1;
				if (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMap)
					pNewPage = _pPage;
				else
				{
					pNewPage = fg_PageMalloc(_pTransaction, nPages);
					if (!pNewPage)
						return ENOMEM;
					if (nPages > 1)
						memcpy(pNewPage, _pPage, nPages * pEnvironment->m_PageSize);
					else
						fg_PageCopy(pNewPage, _pPage, pEnvironment->m_PageSize);
				}
				if (pAncestor == _pTransaction)
				{
					/* If in current txn, this page is no longer spilled.
					 * If it happens to be the last page, truncate the spill list.
					 * Otherwise mark it as deleted by setting the LSB.
					 */
					if (iSpill == pAncestor->m_pSpillPages[0])
						pAncestor->m_pSpillPages[0]--;
					else
						pAncestor->m_pSpillPages[iSpill] |= 1;
				} // otherwise, if belonging to a parent txn, the
				  // page remains spilled until child commits

				fg_PageDirty(_pTransaction, pNewPage);
				pNewPage->m_Flags |= DPDirty;
				*o_pDirtyPage = pNewPage;
				break;
			}
		}
		return DMdbSuccess;
	}

	/** Touch a page: make it dirty and re-insert into tree with updated pgno.
	 * Set #DMdbTxnError on failure.
	 * @param[in] _pCursor cursor pointing to the page to be touched
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageTouch(CMdbCursor *_pCursor)
	{
		CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop], *pNewPage;
		CMdbTxn *pTransaction = _pCursor->m_pTransaction;
		CMdbCursor *pOther, *pOtherCursor;
		CPgno PageNumber;
		int Result;

		if (!DFIsSet(pPage->m_Flags, DPDirty))
		{
			if (pTransaction->m_Flags & DMdbTxnSpills)
			{
				pNewPage = nullptr;
				Result = fg_PageUnspill(pTransaction, pPage, &pNewPage);
				if (Result)
					goto fail;
				if (pNewPage)
					goto done;
			}
			fg_MidlNeed(&pTransaction->m_pFreePages, 1);
			if ((Result = fg_PageAlloc(_pCursor, 1, &pNewPage)))
				goto fail;
			PageNumber = pNewPage->m_PageNumber;
			DMibDatabaseTrace("touched db {} page {} -> {}", DDbi(_pCursor), pPage->m_PageNumber, PageNumber);
			DMibCheck(pPage->m_PageNumber != PageNumber);
			fg_MidlXAppend(pTransaction->m_pFreePages, pPage->m_PageNumber);
			/* Update the parent page, if any, to point to the new page */
			if (_pCursor->m_iTop)
			{
				CMdbPage *pParent = _pCursor->m_Pages[_pCursor->m_iTop - 1];
				CMdbNode *pNode = DNodePtr(pParent, _pCursor->m_NodeIndices[_pCursor->m_iTop - 1]);
				DSetPgno(pNode, PageNumber);
			}
			else
				_pCursor->m_pDatabase->m_RootPage = PageNumber;
		}
		else if (pTransaction->m_pParent && !DIsSubp(pPage))
		{
			CMdbId2 DirtyEntry, *pDirtyList = pTransaction->m_pDirtyList;
			PageNumber = pPage->m_PageNumber;
			/* If txn has a parent, make sure the page is in our
			 * dirty list.
			 */
			if (pDirtyList[0].m_ID)
			{
				unsigned iDirty = fg_Mid2lSearch(pDirtyList, PageNumber);
				if (iDirty <= pDirtyList[0].m_ID && pDirtyList[iDirty].m_ID == PageNumber)
				{
					if (pPage != pDirtyList[iDirty].m_pValue)
					{ // bad cursor?
						_pCursor->m_Flags &= ~(DCInitialized | DCEof);
						pTransaction->m_Flags |= DMdbTxnError;
						return DMdbProblem;
					}
					return 0;
				}
			}
			DMibCheck(pDirtyList[0].m_ID < DMdbIdlUmMax);
			/* No - copy it */
			pNewPage = fg_PageMalloc(pTransaction, 1);
			if (!pNewPage)
				return ENOMEM;
			DirtyEntry.m_ID = PageNumber;
			DirtyEntry.m_pValue = pNewPage;
			Result = fg_Mid2lInsert(pDirtyList, &DirtyEntry);
			DMibCheck(Result == 0);
		}
		else
			return 0;

		fg_PageCopy(pNewPage, pPage, pTransaction->m_pEnvironment->m_PageSize);
		pNewPage->m_PageNumber = PageNumber;
		pNewPage->m_Flags |= DPDirty;

	done:
		/* Adjust cursors pointing to pPage */
		_pCursor->m_Pages[_pCursor->m_iTop] = pNewPage;
		pOther = pTransaction->m_pCursors[_pCursor->m_DatabaseID];
		if (_pCursor->m_Flags & DCSub)
		{
			for (; pOther; pOther = pOther->m_pNext)
			{
				pOtherCursor = &pOther->m_pXCursor->m_Cursor;
				if (pOtherCursor->m_nPages < _pCursor->m_nPages)
					continue;
				if (pOtherCursor->m_Pages[_pCursor->m_iTop] == pPage)
					pOtherCursor->m_Pages[_pCursor->m_iTop] = pNewPage;
			}
		}
		else
		{
			for (; pOther; pOther = pOther->m_pNext)
			{
				if (pOther->m_nPages < _pCursor->m_nPages)
					continue;
				if (pOther == _pCursor)
					continue;
				if (pOther->m_Pages[_pCursor->m_iTop] == pPage)
				{
					pOther->m_Pages[_pCursor->m_iTop] = pNewPage;
					if (DIsLeaf(pNewPage))
						DXCursorRefresh(pOther, _pCursor->m_iTop, pNewPage);
				}
			}
		}
		DMdbPageUnref(_pCursor->m_pTransaction, pPage);
		return 0;

	fail:
		pTransaction->m_Flags |= DMdbTxnError;
		return Result;
	}

	/** Flush (some) dirty pages to the map, after clearing their dirty flag.
	 * @param[in] _pTransaction the transaction that's being committed
	 * @param[in] _nKeep number of initial pages in m_pDirtyList to keep dirty.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageFlush(CMdbTxn *_pTransaction, int _nKeep)
	{
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		CMdbId2L pDirtyList = _pTransaction->m_pDirtyList;
		unsigned PageSize = pEnvironment->m_PageSize, iKept;
		int iDirty, nPages = pDirtyList[0].m_ID, Result;
		umint Size = 0;
		off_t Position = 0;
		CPgno PageNumber = 0;
		CMdbPage *pDirtyPage = nullptr;
#ifdef DPlatformFamily_Windows
		OVERLAPPED Overlapped;
#else
		struct iovec IOVectors[DMdbCommitPages];
		smint WriteSize = 0, nWritten;
		off_t WritePosition = 0, NextPosition = 1; // impossible Position, so Position != NextPosition
		int nIOVectors = 0;
#endif

		iKept = iDirty = _nKeep;

		if (pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMap)
		{
			/* Clear dirty flags */
			while (++iDirty <= nPages)
			{
				pDirtyPage = (CMdbPage *)(pDirtyList[iDirty].m_pValue);
				/* Don't flush this page yet */
				if (pDirtyPage->m_Flags & (DPLoose | DPKeep))
				{
					pDirtyPage->m_Flags &= ~DPKeep;
					pDirtyList[++iKept] = pDirtyList[iDirty];
					continue;
				}
				pDirtyPage->m_Flags &= ~DPDirty;
			}
			goto done;
		}

		/* Write the pages */
		for (;;)
		{
			if (++iDirty <= nPages)
			{
				pDirtyPage = (CMdbPage *)pDirtyList[iDirty].m_pValue;
				/* Don't flush this page yet */
				if (pDirtyPage->m_Flags & (DPLoose | DPKeep))
				{
					pDirtyPage->m_Flags &= ~DPKeep;
					pDirtyList[iDirty].m_ID = 0;
					continue;
				}
				PageNumber = pDirtyList[iDirty].m_ID;
				/* clear dirty flag */
				pDirtyPage->m_Flags &= ~DPDirty;
				Position = PageNumber * PageSize;
				Size = PageSize;
				if (DIsOverflow(pDirtyPage))
					Size *= pDirtyPage->m_nOverflowPages;
			}
#ifdef DPlatformFamily_Windows
			else
				break;

			/* Windows actually supports scatter/gather I/O, but only on
			 * unbuffered file handles. Since we're relying on the OS page
			 * cache for all our data, that's self-defeating. So we just
			 * write pages one at a time. We use the Overlapped structure to set
			 * the write offset, to at least save the overhead of a Seek
			 * system call.
			 */
			DMibDatabaseTrace("committing page {}", PageNumber);
			memset(&Overlapped, 0, sizeof(Overlapped));
			Overlapped.Offset = Position & 0xffffffff;
			Overlapped.OffsetHigh = Position >> 16 >> 16;
			if (!WriteFile(pEnvironment->m_FileHandle, pDirtyPage, Size, nullptr, &Overlapped))
			{
				Result = DErrCode();
				DMibDatabaseTrace("WriteFile: {}", Result);
				return Result;
			}
#else
			/* Write up to DMdbCommitPages dirty pages at a time. */
			if (Position != NextPosition || nIOVectors == DMdbCommitPages || WriteSize + Size > DMaxWrite)
			{
				if (nIOVectors)
				{
				retry_write:
					/* Write previous page(s) */
#	ifdef MDB_USE_PWRITEV
					nWritten = pwritev(pEnvironment->m_FileHandle, IOVectors, nIOVectors, WritePosition);
#	else
					if (nIOVectors == 1)
						nWritten = pwrite(pEnvironment->m_FileHandle, IOVectors[0].iov_base, WriteSize, WritePosition);
					else
					{
					retry_seek:
						if (lseek(pEnvironment->m_FileHandle, WritePosition, SEEK_SET) == -1)
						{
							Result = DErrCode();
							if (Result == EINTR)
								goto retry_seek;
							DMibDatabaseTrace("lseek: {}", fg_StrError(Result));
							return Result;
						}
						nWritten = writev(pEnvironment->m_FileHandle, IOVectors, nIOVectors);
					}
#	endif
					if (nWritten != WriteSize)
					{
						if (nWritten < 0)
						{
							Result = DErrCode();
							if (Result == EINTR)
								goto retry_write;
							DMibDatabaseTrace("Write error: {}", fg_StrError(Result));
						}
						else
						{
							Result = EIO; // TODO: Use which error code?
							DPuts("short write, filesystem full?");
						}
						return Result;
					}
					nIOVectors = 0;
				}
				if (iDirty > nPages)
					break;
				WritePosition = Position;
				WriteSize = 0;
			}
			DMibDatabaseTrace("committing page {}", PageNumber);
			NextPosition = Position + Size;
			IOVectors[nIOVectors].iov_len = Size;
			IOVectors[nIOVectors].iov_base = (char *)pDirtyPage;
			WriteSize += Size;
			nIOVectors++;
#endif	/* DPlatformFamily_Windows */
		}
#ifdef DMibDatabase_Always64BitSupport
		if (PageNumber > _pTransaction->m_LastPageNumber)
			_pTransaction->m_LastPageNumber = PageNumber;
#endif

		/* MIPS has cache coherency issues, this is a no-op everywhere else
		 * Note: for any size >= on-chip cache size, entire on-chip cache is
		 * flushed.
		 */
		DCacheFlush(pEnvironment->m_pMap, _pTransaction->m_NextPageNumber * pEnvironment->m_PageSize, DCACHE);

		for (iDirty = _nKeep; ++iDirty <= nPages;)
		{
			pDirtyPage = (CMdbPage *)pDirtyList[iDirty].m_pValue;
			/* This is a page we skipped above */
			if (!pDirtyList[iDirty].m_ID)
			{
				pDirtyList[++iKept] = pDirtyList[iDirty];
				pDirtyList[iKept].m_ID = pDirtyPage->m_PageNumber;
				continue;
			}
			pEnvironment->f_DPageFree(pDirtyPage);
		}

	done:
		iDirty--;
		_pTransaction->m_DirtyRoom += iDirty - iKept;
		pDirtyList[0].m_ID = iKept;
		return DMdbSuccess;
	}

#ifdef DMibDatabase_Always64BitSupport
	/** Map a read-only page.
	 * There are two levels of tracking in use, a per-txn list and a per-env list.
	 * ref'ing and unref'ing the per-txn list is faster since it requires no
	 * locking. Pages are cached in the per-env list for global reuse, and a lock
	 * is required. Pages are not immediately unmapped when their refcnt goes to
	 * zero; they hang around in case they will be reused again soon.
	 *
	 * When the per-txn list gets full, all pages with refcnt=0 are purged from the
	 * list and their refcnts in the per-env list are decremented.
	 *
	 * When the per-env list gets full, all pages with refcnt=0 are purged from the
	 * list and their pages are unmapped.
	 *
	 * @note "full" means the list has reached its respective rpcheck threshold.
	 * This threshold slowly raises if no pages could be purged on a given check,
	 * and returns to its original value when enough pages were purged.
	 *
	 * If purging doesn't free any slots, filling the per-txn list will return
	 * DMdbTxnFull, and filling the per-env list returns DMdbMapFull.
	 *
	 * Reference tracking in a txn is imperfect, pages can linger with non-zero
	 * refcnt even without active references. It was deemed to be too invasive
	 * to add unrefs in every required location. However, all pages are unref'd
	 * at the end of the transaction. This guarantees that no stale references
	 * linger in the per-env list.
	 *
	 * Usually we map chunks of 16 pages at a time, but if an overflow page begins
	 * at the tail of the chunk we extend the chunk to include the entire overflow
	 * page. Unfortunately, pages can be turned into overflow pages after their
	 * chunk was already mapped. In that case we must remap the chunk if the
	 * overflow page is referenced. If the chunk's refcnt is 0 we can just remap
	 * it, otherwise we temporarily map a new chunk just for the overflow page.
	 *
	 * @note this chunk handling means we cannot guarantee that a data item
	 * returned from the DB will stay alive for the duration of the transaction:
	 *   We unref pages as soon as a cursor moves away from the page
	 *   A subsequent op may cause a purge, which may unmap any unref'd chunks
	 * The caller must copy the data if it must be used later in the same txn.
	 *
	 * Also - our reference counting revolves around cursors, but overflow pages
	 * aren't pointed to by a cursor's page stack. We have to remember them
	 * explicitly, in the added m_pOverflowPage field. A single cursor can only hold a
	 * reference to one overflow page at a time.
	 *
	 * @param[in] _pTransaction the transaction for this access.
	 * @param[in] _PageNumber the page number for the page to retrieve.
	 * @param[out] o_pPage address of a pointer where the page's address will be stored.
	 * @return 0 on success, non-zero on failure.
	 */
	int fg_RPageGet(CMdbTxn *_pTransaction, CPgno _PageNumber, CMdbPage **o_pPage)
	{
		CMdbEnv *pEnvironment = _pTransaction->m_pEnvironment;
		CMdbPage *pPage;
		CMdbId3L pTransactionPages = _pTransaction->m_pReadOnlyPages;
		CMdbId3L pEnvironmentPages = pEnvironment->m_pReadOnlyPages;
		CMdbId3 Entry;
		unsigned iPage, Remainder;
		CPgno ChunkPageNumber;
		int Result, nRetries = 1;
#	ifdef DPlatformFamily_Windows
		LARGE_INTEGER Offset;
		SIZE_T Length;
#		define DSetOff(d_Off,d_Val)	d_Off.QuadPart = d_Val
#		define DMap(d_Rc,d_Env,d_Addr,d_Len,d_Off)	\
			d_Addr = nullptr; \
			d_Rc = g_fDatabase_NtMapViewOfSection(d_Env->m_pFileMappingHandle, GetCurrentProcess(), &d_Addr, 0, \
				d_Len, &d_Off, &d_Len, ViewUnmap, (d_Env->m_Flags & EDatabaseEnvironmentFlag_ReadOnly) ? 0 : MEM_RESERVE, PAGE_READONLY); \
			if (d_Rc) d_Rc = fg_Nt2Win32(d_Rc)
#	else
		off_t Offset;
		umint Length;
#		define DSetOff(d_Off,d_Val)	d_Off = d_Val
#		define DMap(d_Rc,d_Env,d_Addr,d_Len,d_Off)	\
			d_Addr = mmap(nullptr, d_Len, PROT_READ, MAP_SHARED, d_Env->m_FileHandle, d_Off); \
			d_Rc = (d_Addr == MAP_FAILED) ? errno : 0
#	endif

		/* remember the offset of the actual page number, so we can
		 * return the correct pointer at the end.
		 */
		Remainder = _PageNumber & (DMdbRpageChunk - 1);
		ChunkPageNumber = _PageNumber ^ Remainder;

		Entry.m_ID = 0;
		iPage = fg_Mid3lSearch(pTransactionPages, ChunkPageNumber);
		if (iPage <= pTransactionPages[0].m_ID && pTransactionPages[iPage].m_ID == ChunkPageNumber)
		{
			if (iPage != pTransactionPages[0].m_ID && pTransactionPages[iPage + 1].m_ID == _PageNumber)
				iPage++;
			/* check for overflow size */
			pPage = (CMdbPage *)((char *)pTransactionPages[iPage].m_pValue + Remainder * pEnvironment->m_PageSize);
			if (DIsOverflow(pPage) && pPage->m_nOverflowPages + Remainder > pTransactionPages[iPage].m_nPages)
			{
				Entry.m_nPages = pPage->m_nOverflowPages + Remainder;
				Length = Entry.m_nPages * pEnvironment->m_PageSize;
				DSetOff(Offset, ChunkPageNumber * pEnvironment->m_PageSize);
				DMap(Result, pEnvironment, Entry.m_pValue, Length, Offset);
				if (Result)
					return Result;
				/* check for local-only page */
				if (Remainder)
				{
					DMibCheck(pTransactionPages[iPage].m_ID != _PageNumber);
					/* hope there's room to insert this locally.
					 * setting m_ID here tells later code to just insert
					 * this id3 instead of searching for a match.
					 */
					Entry.m_ID = _PageNumber;
					goto notlocal;
				}
				else
				{
					/* ignore the mapping we got from env, use new one */
					pTransactionPages[iPage].m_pValue = Entry.m_pValue;
					pTransactionPages[iPage].m_nPages = Entry.m_nPages;
					/* if no active ref, see if we can replace in env */
					if (!pTransactionPages[iPage].m_nReferences)
					{
						unsigned iEntry;
						pthread_mutex_lock(&pEnvironment->m_ReadOnlyPagesMutex);
						iEntry = fg_Mid3lSearch(pEnvironmentPages, pTransactionPages[iPage].m_ID);
						if (pEnvironmentPages[iEntry].m_nReferences == 1)
						{
							/* just us, replace it */
							munmap(pEnvironmentPages[iEntry].m_pValue, pEnvironmentPages[iEntry].m_nPages * pEnvironment->m_PageSize);
							pEnvironmentPages[iEntry].m_pValue = pTransactionPages[iPage].m_pValue;
							pEnvironmentPages[iEntry].m_nPages = pTransactionPages[iPage].m_nPages;
						}
						else
						{
							/* there are others, remove ourself */
							pEnvironmentPages[iEntry].m_nReferences--;
						}
						pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
					}
				}
			}
			Entry.m_pValue = pTransactionPages[iPage].m_pValue;
			Entry.m_nPages = pTransactionPages[iPage].m_nPages;
			pTransactionPages[iPage].m_nReferences++;
			goto ok;
		}

	notlocal:
		if (pTransactionPages[0].m_ID >= DMdbTrpageMax - _pTransaction->m_ReadOnlyPageCheck)
		{
			unsigned iEntry, iFree;
			/* purge unref'd pages from our list and unref in env */
			pthread_mutex_lock(&pEnvironment->m_ReadOnlyPagesMutex);
		retry:
			iFree = 0;
			for (iEntry = 1; iEntry <= pTransactionPages[0].m_ID; iEntry++)
			{
				if (!pTransactionPages[iEntry].m_nReferences)
				{
					if (!iFree)
						iFree = iEntry;
					/* tmp overflow pages don't go to env */
					if (pTransactionPages[iEntry].m_ID & (DMdbRpageChunk - 1))
					{
						munmap(pTransactionPages[iEntry].m_pValue, pTransactionPages[iEntry].m_nPages * pEnvironment->m_PageSize);
						continue;
					}
					iPage = fg_Mid3lSearch(pEnvironmentPages, pTransactionPages[iEntry].m_ID);
					pEnvironmentPages[iPage].m_nReferences--;
				}
			}
			pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
			if (!iFree)
			{
				/* we didn't find any unref'd chunks.
				 * if we're out of room, fail.
				 */
				if (pTransactionPages[0].m_ID >= DMdbTrpageMax)
					return DMdbTxnFull;
				/* otherwise, raise threshold for next time around
				 * and let this go.
				 */
				_pTransaction->m_ReadOnlyPageCheck /= 2;
			}
			else
			{
				/* we found some unused; consolidate the list */
				for (iEntry = iFree + 1; iEntry <= pTransactionPages[0].m_ID; iEntry++)
				{
					if (pTransactionPages[iEntry].m_nReferences)
						pTransactionPages[iFree++] = pTransactionPages[iEntry];
				}
				pTransactionPages[0].m_ID = iFree - 1;
				/* decrease the check threshold toward its original value */
				if (!_pTransaction->m_ReadOnlyPageCheck)
					_pTransaction->m_ReadOnlyPageCheck = 1;
				while (_pTransaction->m_ReadOnlyPageCheck < pTransactionPages[0].m_ID && _pTransaction->m_ReadOnlyPageCheck < DMdbTrpageSize / 2)
					_pTransaction->m_ReadOnlyPageCheck *= 2;
			}
		}
		if (pTransactionPages[0].m_ID < DMdbTrpageSize)
		{
			Entry.m_nReferences = 1;
			if (Entry.m_ID)
				goto found;
			/* don't map past last written page in read-only envs */
			if ((pEnvironment->m_Flags & EDatabaseEnvironmentFlag_ReadOnly) && ChunkPageNumber + DMdbRpageChunk - 1 > _pTransaction->m_LastPageNumber)
				Entry.m_nPages = _pTransaction->m_LastPageNumber + 1 - ChunkPageNumber;
			else
				Entry.m_nPages = DMdbRpageChunk;
			Length = Entry.m_nPages * pEnvironment->m_PageSize;
			Entry.m_ID = ChunkPageNumber;

			/* search for page in env */
			pthread_mutex_lock(&pEnvironment->m_ReadOnlyPagesMutex);
			iPage = fg_Mid3lSearch(pEnvironmentPages, ChunkPageNumber);
			if (iPage <= pEnvironmentPages[0].m_ID && pEnvironmentPages[iPage].m_ID == ChunkPageNumber)
			{
				Entry.m_pValue = pEnvironmentPages[iPage].m_pValue;
				Entry.m_nPages = pEnvironmentPages[iPage].m_nPages;
				/* check for overflow size */
				pPage = (CMdbPage *)((char *)Entry.m_pValue + Remainder * pEnvironment->m_PageSize);
				if (DIsOverflow(pPage) && pPage->m_nOverflowPages + Remainder > Entry.m_nPages)
				{
					Entry.m_nPages = pPage->m_nOverflowPages + Remainder;
					Length = Entry.m_nPages * pEnvironment->m_PageSize;
					DSetOff(Offset, ChunkPageNumber * pEnvironment->m_PageSize);
					DMap(Result, pEnvironment, Entry.m_pValue, Length, Offset);
					if (Result)
						goto fail;
					if (!pEnvironmentPages[iPage].m_nReferences)
					{
						munmap(pEnvironmentPages[iPage].m_pValue, pEnvironment->m_PageSize * pEnvironmentPages[iPage].m_nPages);
						pEnvironmentPages[iPage].m_pValue = Entry.m_pValue;
						pEnvironmentPages[iPage].m_nPages = Entry.m_nPages;
					}
					else
					{
						Entry.m_ID = _PageNumber;
						pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
						goto found;
					}
				}
				pEnvironmentPages[iPage].m_nReferences++;
				pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
				goto found;
			}
			if (pEnvironmentPages[0].m_ID >= DMdbErpageMax - pEnvironment->m_ReadOnlyPageCheck)
			{
				/* purge unref'd pages */
				unsigned iEntry, iFree = 0;
				for (iEntry = 1; iEntry <= pEnvironmentPages[0].m_ID; iEntry++)
				{
					if (!pEnvironmentPages[iEntry].m_nReferences)
					{
						if (!iFree)
							iFree = iEntry;
						munmap(pEnvironmentPages[iEntry].m_pValue, pEnvironment->m_PageSize * pEnvironmentPages[iEntry].m_nPages);
					}
				}
				if (!iFree)
				{
					if (nRetries)
					{
						/* see if we can unref some local pages */
						nRetries--;
						Entry.m_ID = 0;
						goto retry;
					}
					if (pEnvironmentPages[0].m_ID >= DMdbErpageMax)
					{
						pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
						return DMdbMapFull;
					}
					pEnvironment->m_ReadOnlyPageCheck /= 2;
				}
				else
				{
					for (iEntry = iFree + 1; iEntry <= pEnvironmentPages[0].m_ID; iEntry++)
					{
						if (pEnvironmentPages[iEntry].m_nReferences)
							pEnvironmentPages[iFree++] = pEnvironmentPages[iEntry];
					}
					pEnvironmentPages[0].m_ID = iFree - 1;
					if (!pEnvironment->m_ReadOnlyPageCheck)
						pEnvironment->m_ReadOnlyPageCheck = 1;
					while (pEnvironment->m_ReadOnlyPageCheck < pEnvironmentPages[0].m_ID && pEnvironment->m_ReadOnlyPageCheck < DMdbErpageSize / 2)
						pEnvironment->m_ReadOnlyPageCheck *= 2;
				}
			}
			DSetOff(Offset, ChunkPageNumber * pEnvironment->m_PageSize);
			DMap(Result, pEnvironment, Entry.m_pValue, Length, Offset);
			if (Result)
			{
			fail:
				pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
				return Result;
			}
			/* check for overflow size */
			pPage = (CMdbPage *)((char *)Entry.m_pValue + Remainder * pEnvironment->m_PageSize);
			if (DIsOverflow(pPage) && pPage->m_nOverflowPages + Remainder > Entry.m_nPages)
			{
				Entry.m_nPages = pPage->m_nOverflowPages + Remainder;
				munmap(Entry.m_pValue, Length);
				Length = Entry.m_nPages * pEnvironment->m_PageSize;
				DMap(Result, pEnvironment, Entry.m_pValue, Length, Offset);
				if (Result)
					goto fail;
			}
			fg_Mid3lInsert(pEnvironmentPages, &Entry);
			pthread_mutex_unlock(&pEnvironment->m_ReadOnlyPagesMutex);
		found:
			fg_Mid3lInsert(pTransactionPages, &Entry);
		}
		else
			return DMdbTxnFull;
	ok:
		pPage = (CMdbPage *)((char *)Entry.m_pValue + Remainder * pEnvironment->m_PageSize);
#	if DMibDatabaseDebug	/* we don't need this check any more */
		if (DIsOverflow(pPage))
			DMibCheck(pPage->m_nOverflowPages + Remainder <= Entry.m_nPages);
#	endif
		*o_pPage = pPage;
		return DMdbSuccess;
	}
#endif

	/** Find the address of the page corresponding to a given page number.
	 * Set #DMdbTxnError on failure.
	 * @param[in] mc the cursor accessing the page.
	 * @param[in] pgno the page number for the page to retrieve.
	 * @param[out] ret address of a pointer where the page's address will be stored.
	 * @param[out] lvl m_pDirtyList inheritance level of found page. 1=current txn, 0=mapped page.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageGet(CMdbCursor *_pCursor, CPgno _PageNumber, CMdbPage **o_pPage, int *o_pLevel)
	{
		CMdbTxn *pTransaction = _pCursor->m_pTransaction;
		CMdbPage *pPage = nullptr;
		int Level;

		if (!(_pCursor->m_Flags & (DCOrigReadOnly | DCWriteMap)))
		{
			CMdbTxn *pAncestor = pTransaction;
			Level = 1;
			do
			{
				CMdbId2L pDirtyList = pAncestor->m_pDirtyList;
				unsigned iIndex;
				/* Spilled pages were dirtied in this txn and flushed
				 * because the dirty list got full. Bring this page
				 * back in from the map (but don't unspill it here,
				 * leave that unless page_touch happens again).
				 */
				if (pAncestor->m_pSpillPages)
				{
					CMdbId ShiftedPageNumber = _PageNumber << 1;
					iIndex = fg_MidlSearch(pAncestor->m_pSpillPages, ShiftedPageNumber);
					if (iIndex <= pAncestor->m_pSpillPages[0] && pAncestor->m_pSpillPages[iIndex] == ShiftedPageNumber)
						goto mapped;
				}
				if (pDirtyList[0].m_ID)
				{
					unsigned iIndex = fg_Mid2lSearch(pDirtyList, _PageNumber);
					if (iIndex <= pDirtyList[0].m_ID && pDirtyList[iIndex].m_ID == _PageNumber)
					{
						pPage = (CMdbPage *)pDirtyList[iIndex].m_pValue;
						goto done;
					}
				}
				Level++;
			}
			while ((pAncestor = pAncestor->m_pParent) != nullptr);
		}

		if (_PageNumber >= pTransaction->m_NextPageNumber)
		{
			DMibDatabaseTrace("page {} not found", _PageNumber);
			pTransaction->m_Flags |= DMdbTxnError;
			return DMdbPageNotfound;
		}

		Level = 0;

	mapped:
		{
#ifdef DMibDatabase_Always64BitSupport
			int Result = fg_RPageGet(pTransaction, _PageNumber, &pPage);
			if (Result)
			{
				pTransaction->m_Flags |= DMdbTxnError;
				return Result;
			}
#else
			CMdbEnv *pEnvironment = pTransaction->m_pEnvironment;
			pPage = (CMdbPage *)(pEnvironment->m_pMap + pEnvironment->m_PageSize * _PageNumber);
#endif
		}

	done:
		*o_pPage = pPage;
		if (o_pLevel)
			*o_pLevel = Level;
		return DMdbSuccess;
	}

	/** Finish #fg_PageSearch() / #fg_PageSearchLowest().
	 *	The cursor is at the root page, set up the rest of it.
	 */
	int NPrivate::fg_PageSearchRoot(CMdbCursor *_pCursor, CDatabaseValue const *_pKey, int _Flags)
	{
		CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		int Result;
		DKBuf;

		while (DIsBranch(pPage))
		{
			CMdbNode *pNode;
			CIndx iNode;

			DMibDatabaseTrace("branch page {} has {} keys", pPage->m_PageNumber, DNumKeys(pPage));
			/* Don't assert on branch pages in the FreeDB. We can get here
			 * while in the process of rebalancing a FreeDB branch page; we must
			 * let that proceed. ITS#8336
			 */
			DMibCheck(!_pCursor->m_DatabaseID || DNumKeys(pPage) > 1);
			DMibDatabaseTrace("found index 0 to page {}", DNodePgno(DNodePtr(pPage, 0)));

			if (_Flags & (DMdbPsFirst | DMdbPsLast))
			{
				iNode = 0;
				if (_Flags & DMdbPsLast)
				{
					iNode = DNumKeys(pPage) - 1;
					/* if already init'd, see if we're already in right place */
					if (_pCursor->m_Flags & DCInitialized)
					{
						if (_pCursor->m_NodeIndices[_pCursor->m_iTop] == iNode)
						{
							_pCursor->m_iTop = _pCursor->m_nPages++;
							pPage = _pCursor->m_Pages[_pCursor->m_iTop];
							goto ready;
						}
					}
				}
			}
			else
			{
				int bExact;
				pNode = fg_NodeSearch(_pCursor, _pKey, &bExact);
				if (pNode == nullptr)
					iNode = DNumKeys(pPage) - 1;
				else
				{
					iNode = _pCursor->m_NodeIndices[_pCursor->m_iTop];
					if (!bExact)
					{
						DMibCheck(iNode > 0);
						iNode--;
					}
				}
				DMibDatabaseTrace("following index {} for key [{}]", iNode, DKey(_pKey));
			}

			DMibCheck(iNode < DNumKeys(pPage));
			pNode = DNodePtr(pPage, iNode);

			if ((Result = fg_PageGet(_pCursor, DNodePgno(pNode), &pPage, nullptr)) != 0)
				return Result;

			_pCursor->m_NodeIndices[_pCursor->m_iTop] = iNode;
			if ((Result = fg_CursorPush(_pCursor, pPage)))
				return Result;

		ready:
			if (_Flags & DMdbPsModify)
			{
				if ((Result = fg_PageTouch(_pCursor)) != 0)
					return Result;
				pPage = _pCursor->m_Pages[_pCursor->m_iTop];
			}
		}

		if (!DIsLeaf(pPage))
		{
			DMibDatabaseTrace("internal error, index points to a {nfh,sf0,sj2} page!?", pPage->m_Flags);
			_pCursor->m_pTransaction->m_Flags |= DMdbTxnError;
			return DMdbCorrupted;
		}

		DMibDatabaseTrace("found leaf page {} for key [{}]", pPage->m_PageNumber, _pKey ? DKey(_pKey) : "null");
		_pCursor->m_Flags |= DCInitialized;
		_pCursor->m_Flags &= ~DCEof;

		return DMdbSuccess;
	}

	/** Search for the lowest key under the current branch page.
	 * This just bypasses a DNumKeys check in the current page
	 * before calling fg_PageSearchRoot(), because the callers
	 * are all in situations where the current page is known to
	 * be underfilled.
	 */
	int NPrivate::fg_PageSearchLowest(CMdbCursor *_pCursor)
	{
		CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		CMdbNode *pNode = DNodePtr(pPage, 0);
		int Result;

		if ((Result = fg_PageGet(_pCursor, DNodePgno(pNode), &pPage, nullptr)) != 0)
			return Result;

		_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;
		if ((Result = fg_CursorPush(_pCursor, pPage)))
			return Result;
		return fg_PageSearchRoot(_pCursor, nullptr, DMdbPsFirst);
	}

	/** Search for the page a given key should be in.
	 * Push it and its parent pages on the cursor stack.
	 * @param[in,out] _pCursor the cursor for this operation.
	 * @param[in] _pKey the key to search for, or nullptr for first/last page.
	 * @param[in] _Flags If DMdbPsModify is set, visited pages in the DB
	 *   are touched (updated with new page numbers).
	 *   If DMdbPsFirst or DMdbPsLast is set, find first or last leaf.
	 *   This is used by #fg_CursorFirst() and #fg_CursorLast().
	 *   If DMdbPsRootonly set, just fetch root node, no further lookups.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageSearch(CMdbCursor *_pCursor, CDatabaseValue const *_pKey, int _Flags)
	{
		int Result;
		CPgno RootPage;

		/* Make sure the txn is still viable, then find the root from
		 * the txn's db table and set it as the root of the cursor's stack.
		 */
		_pCursor->m_pTransaction->f_CheckBlocked();

		{
			/* Make sure we're using an up-to-date root */
			if (*_pCursor->m_pDatabaseFlag & DDbStale)
			{
				CMdbCursor MainCursor;
				if (DTxnDbiChanged(_pCursor->m_pTransaction, _pCursor->m_DatabaseID))
					DMibError("Stale write transaction");
				fg_CursorInit(&MainCursor, _pCursor->m_pTransaction, DMainDbi, nullptr);
				Result = fg_PageSearch(&MainCursor, &_pCursor->m_pDatabaseInfo->m_DatabaseName, 0);
				if (Result)
					return Result;
				{
					CDatabaseValue Data;
					int bExact = 0;
					uint16 Flags;
					CMdbNode *pLeaf = fg_NodeSearch(&MainCursor, &_pCursor->m_pDatabaseInfo->m_DatabaseName, &bExact);
					if (!bExact)
						return DMdbNotFound;
					if ((pLeaf->m_Flags & (DFDupData | DFSubData)) != DFSubData)
						return DMdbIncompatible; // not a named DB
					Result = fg_NodeRead(&MainCursor, pLeaf, &Data);
					if (Result)
						return Result;
					memcpy(&Flags, ((char *)Data.m_pData + offsetof(CMdbDb, m_Flags)), sizeof(uint16));
					/* The txn may not know this DBI, or another process may
					 * have dropped and recreated the DB with other flags.
					 */
					if ((_pCursor->m_pDatabase->m_Flags & DPersistentFlags) != Flags)
						return DMdbIncompatible;
					memcpy(_pCursor->m_pDatabase, Data.m_pData, sizeof(CMdbDb));
				}
				*_pCursor->m_pDatabaseFlag &= ~DDbStale;
			}
			RootPage = _pCursor->m_pDatabase->m_RootPage;

			if (RootPage == DPInvalid)
			{ // Tree is empty.
				DPuts("tree is empty");
				return DMdbNotFound;
			}
		}

		DMibCheck(RootPage > 1);
		if (!_pCursor->m_Pages[0] || _pCursor->m_Pages[0]->m_PageNumber != RootPage)
		{
#ifdef DMibDatabase_Always64BitSupport
			if (_pCursor->m_Pages[0])
				DMdbPageUnref(_pCursor->m_pTransaction, _pCursor->m_Pages[0]);
#endif
			if ((Result = fg_PageGet(_pCursor, RootPage, &_pCursor->m_Pages[0], nullptr)) != 0)
				return Result;
		}

#ifdef DMibDatabase_Always64BitSupport
		{
			int iPage;
			for (iPage = 1; iPage < _pCursor->m_nPages; iPage++)
				DMdbPageUnref(_pCursor->m_pTransaction, _pCursor->m_Pages[iPage]);
		}
#endif
		_pCursor->m_nPages = 1;
		_pCursor->m_iTop = 0;

		DMibDatabaseTrace("db {} root page {} has flags 0x{nfh,sf0,sj4}", DDbi(_pCursor), RootPage, _pCursor->m_Pages[0]->m_Flags);

		if (_Flags & DMdbPsModify)
		{
			if ((Result = fg_PageTouch(_pCursor)))
				return Result;
		}

		if (_Flags & DMdbPsRootonly)
			return DMdbSuccess;

		return fg_PageSearchRoot(_pCursor, _pKey, _Flags);
	}

	int NPrivate::fg_OvPageFree(CMdbCursor *_pCursor, CMdbPage *_pPage)
	{
		CMdbTxn *pTransaction = _pCursor->m_pTransaction;
		CPgno PageNumber = _pPage->m_PageNumber;
		unsigned iIndex = 0, nOverflowPages = _pPage->m_nOverflowPages;
		CMdbEnv *pEnvironment = pTransaction->m_pEnvironment;
		CMdbIdl pSpillList = pTransaction->m_pSpillPages;
		CMdbId ShiftedPageNumber = PageNumber << 1;

		DMibDatabaseTrace("free ov page {} ({})", PageNumber, nOverflowPages);
		/* If the page is dirty or on the spill list we just acquired it,
		 * so we should give it back to our current free list, if any.
		 * Otherwise put it onto the list of pages we freed in this txn.
		 *
		 * Won't create m_PageState.m_pReclaimedPages: m_PageState.m_LastRecordID must be inited along with it.
		 * Unsupported in nested txns: They would need to hide the page
		 * range in ancestor txns' dirty and spilled lists.
		 */
		if
		(
			pEnvironment->m_PageState.m_pReclaimedPages
			&& !pTransaction->m_pParent
			&& ((_pPage->m_Flags & DPDirty) || (pSpillList && (iIndex = fg_MidlSearch(pSpillList, ShiftedPageNumber)) <= pSpillList[0] && pSpillList[iIndex] == ShiftedPageNumber))
		)
		{
			unsigned iSource, iDestination;
			CPgno *pReclaimedPages;
			CMdbId2 *pDirtyList, Entry, NextEntry;
			fg_MidlNeed(&pEnvironment->m_PageState.m_pReclaimedPages, nOverflowPages);
			if (!(_pPage->m_Flags & DPDirty))
			{
				/* This page is no longer spilled */
				if (iIndex == pSpillList[0])
					pSpillList[0]--;
				else
					pSpillList[iIndex] |= 1;
				goto release;
			}
			/* Remove from dirty list */
			pDirtyList = pTransaction->m_pDirtyList;
			iIndex = pDirtyList[0].m_ID--;
			for (Entry = pDirtyList[iIndex]; Entry.m_pValue != _pPage; Entry = NextEntry)
			{
				if (iIndex > 1)
				{
					iIndex--;
					NextEntry = pDirtyList[iIndex];
					pDirtyList[iIndex] = Entry;
				}
				else
				{
					DMibCheck(iIndex > 1);
					iDestination = ++(pDirtyList[0].m_ID);
					pDirtyList[iDestination] = Entry; // Unsorted. OK when DMdbTxnError.
					pTransaction->m_Flags |= DMdbTxnError;
					return DMdbProblem;
				}
			}
			pTransaction->m_DirtyRoom++;
			if (!(pEnvironment->m_Flags & EDatabaseEnvironmentFlag_WriteMap))
				pEnvironment->f_DPageFree(_pPage);
		release:
			/* Insert in m_PageState.m_pReclaimedPages */
			pReclaimedPages = pEnvironment->m_PageState.m_pReclaimedPages;
			iDestination = pReclaimedPages[0] + nOverflowPages;
			for (iSource = pReclaimedPages[0]; iSource && pReclaimedPages[iSource] < PageNumber; iSource--)
				pReclaimedPages[iDestination--] = pReclaimedPages[iSource];
			while (iDestination > iSource)
				pReclaimedPages[iDestination--] = PageNumber++;
			pReclaimedPages[0] += nOverflowPages;
		}
		else
			fg_MidlAppendRange(&pTransaction->m_pFreePages, PageNumber, nOverflowPages);
#ifdef DMibDatabase_Always64BitSupport
		if (_pCursor->m_pOverflowPage == _pPage)
			_pCursor->m_pOverflowPage = NULL;
#endif
		_pCursor->m_pDatabase->m_nOverflowPages -= nOverflowPages;
		return 0;
	}

	/** Allocate and initialize new pages for a database.
	 * Set #DMdbTxnError on failure.
	 * @param[in] _pCursor a cursor on the database being added to.
	 * @param[in] _Flags flags defining what type of page is being allocated.
	 * @param[in] _nPages the number of pages to allocate. This is usually 1,
	 * unless allocating overflow pages for a large record.
	 * @param[out] o_pPage Address of a page, or nullptr on failure.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageNew(CMdbCursor *_pCursor, uint32 _Flags, int _nPages, CMdbPage **o_pPage)
	{
		CMdbPage *pNewPage;
		int Result;

		if ((Result = fg_PageAlloc(_pCursor, _nPages, &pNewPage)))
			return Result;
		DMibDatabaseTrace("allocated new mpage {}, page size {}", pNewPage->m_PageNumber, _pCursor->m_pTransaction->m_pEnvironment->m_PageSize);
		pNewPage->m_Flags = _Flags | DPDirty;
		pNewPage->m_FreeSpace.m_Lower = (DPageHeaderSize - DPageBase);
		pNewPage->m_FreeSpace.m_Upper = _pCursor->m_pTransaction->m_pEnvironment->m_PageSize - DPageBase;

		if (DIsBranch(pNewPage))
			_pCursor->m_pDatabase->m_nBranchPages++;
		else if (DIsLeaf(pNewPage))
			_pCursor->m_pDatabase->m_nLeafPages++;
		else if (DIsOverflow(pNewPage))
		{
			_pCursor->m_pDatabase->m_nOverflowPages += _nPages;
			pNewPage->m_nOverflowPages = _nPages;
		}
		*o_pPage = pNewPage;

		return 0;
	}

	/** Merge one page into another.
	 *  The nodes from the page pointed to by \b _pSourceCursor will
	 *	be copied to the page pointed to by \b _pDestinationCursor and then
	 *	the \b _pSourceCursor page will be freed.
	 * @param[in] _pSourceCursor Cursor pointing to the source page.
	 * @param[in] _pDestinationCursor Cursor pointing to the destination page.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageMerge(CMdbCursor *_pSourceCursor, CMdbCursor *_pDestinationCursor)
	{
		CMdbPage *pSourcePage, *pDestinationPage;
		CMdbNode *pSourceNode;
		CDatabaseValue Key, Data;
		unsigned nKeys;
		int Result;
		CIndx iSource, iDestination;

		pSourcePage = _pSourceCursor->m_Pages[_pSourceCursor->m_iTop];
		pDestinationPage = _pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop];

		DMibDatabaseTrace("merging page {} into {}", pSourcePage->m_PageNumber, pDestinationPage->m_PageNumber);

		DMibCheck(_pSourceCursor->m_nPages > 1); // can't merge root page
		DMibCheck(_pDestinationCursor->m_nPages > 1);

		/* Mark dst as dirty. */
		if ((Result = fg_PageTouch(_pDestinationCursor)))
			return Result;

		/* get dst page again now that we've touched it. */
		pDestinationPage = _pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop];

		/* Move all nodes from src to dst.
		*/
		iDestination = nKeys = DNumKeys(pDestinationPage);
		if (DIsLeaf2(pSourcePage))
		{
			Key.m_Size = _pSourceCursor->m_pDatabase->m_Pad;
			Key.m_pData = DMetaData(pSourcePage);
			for (iSource = 0; iSource < DNumKeys(pSourcePage); iSource++, iDestination++)
			{
				Result = fg_NodeAdd(_pDestinationCursor, iDestination, &Key, nullptr, 0, 0);
				if (Result != DMdbSuccess)
					return Result;
				Key.m_pData = (uint8 *)Key.m_pData + Key.m_Size;
			}
		}
		else
		{
			for (iSource = 0; iSource < DNumKeys(pSourcePage); iSource++, iDestination++)
			{
				pSourceNode = DNodePtr(pSourcePage, iSource);
				if (iSource == 0 && DIsBranch(pSourcePage))
				{
					CMdbCursor CopyCursor;
					CMdbNode *pLowestNode;
					fg_CursorCopy(_pSourceCursor, &CopyCursor);
					CopyCursor.m_pXCursor = nullptr;
					/* must find the lowest key below src */
					Result = fg_PageSearchLowest(&CopyCursor);
					if (Result)
						return Result;
					if (DIsLeaf2(CopyCursor.m_Pages[CopyCursor.m_iTop]))
					{
						Key.m_Size = CopyCursor.m_pDatabase->m_Pad;
						Key.m_pData = DLeaf2Key(CopyCursor.m_Pages[CopyCursor.m_iTop], 0, Key.m_Size);
					}
					else
					{
						pLowestNode = DNodePtr(CopyCursor.m_Pages[CopyCursor.m_iTop], 0);
						Key.m_Size = DNodeKeySize(pLowestNode);
						Key.m_pData = DNodeKey(pLowestNode);
					}
				}
				else
				{
					Key.m_Size = pSourceNode->m_KeySize;
					Key.m_pData = DNodeKey(pSourceNode);
				}

				Data.m_Size = DNodeDataSize(pSourceNode);
				Data.m_pData = (uint8 *)(CMdbPage *)DNodeData(pSourceNode);
				Result = fg_NodeAdd(_pDestinationCursor, iDestination, &Key, &Data, DNodePgno(pSourceNode), pSourceNode->m_Flags);
				if (Result != DMdbSuccess)
					return Result;
			}
		}

		DMibDatabaseTrace
			(
				"dst page {} now has {} keys ({fe1} filled)"
				, pDestinationPage->m_PageNumber
				, DNumKeys(pDestinationPage)
				, (float)DPageFill(_pDestinationCursor->m_pTransaction->m_pEnvironment, pDestinationPage) / 10
			)
		;

		/* Unlink the src page from parent and add to free list.
		*/
		_pSourceCursor->m_iTop--;
		fg_NodeDel(_pSourceCursor, 0);
		if (_pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop] == 0)
		{
			Key.m_Size = 0;
			Result = fg_UpdateKey(_pSourceCursor, &Key);
			if (Result)
			{
				_pSourceCursor->m_iTop++;
				return Result;
			}
		}
		_pSourceCursor->m_iTop++;

		pSourcePage = _pSourceCursor->m_Pages[_pSourceCursor->m_iTop];
		/* If not operating on FreeDB, allow this page to be reused
		 * in this txn. Otherwise just add to free list.
		 */
		Result = fg_PageLoose(_pSourceCursor, pSourcePage);
		if (Result)
			return Result;
		if (DIsLeaf(pSourcePage))
			_pSourceCursor->m_pDatabase->m_nLeafPages--;
		else
			_pSourceCursor->m_pDatabase->m_nBranchPages--;
		{
			/* Adjust other cursors pointing to mp */
			CMdbCursor *pOther, *pOtherCursor;
			CDatabaseSubID DatabaseID = _pSourceCursor->m_DatabaseID;
			uint32 iTop = _pSourceCursor->m_iTop;

			for (pOther = _pSourceCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
			{
				if (_pSourceCursor->m_Flags & DCSub)
					pOtherCursor = &pOther->m_pXCursor->m_Cursor;
				else
					pOtherCursor = pOther;
				if (pOtherCursor == _pSourceCursor)
					continue;
				if (pOtherCursor->m_nPages < _pSourceCursor->m_nPages)
					continue;
				if (pOtherCursor->m_Pages[iTop] == pSourcePage)
				{
					pOtherCursor->m_Pages[iTop] = pDestinationPage;
					pOtherCursor->m_NodeIndices[iTop] += nKeys;
					pOtherCursor->m_NodeIndices[iTop - 1] = _pDestinationCursor->m_NodeIndices[iTop - 1];
				}
				else if (pOtherCursor->m_Pages[iTop - 1] == _pSourceCursor->m_Pages[iTop - 1] && pOtherCursor->m_NodeIndices[iTop - 1] > _pSourceCursor->m_NodeIndices[iTop - 1])
					pOtherCursor->m_NodeIndices[iTop - 1]--;
				if (DIsLeaf(pSourcePage))
					DXCursorRefresh(pOtherCursor, iTop, pOtherCursor->m_Pages[iTop]);
			}
		}
		{
			uint32 nPages = _pDestinationCursor->m_nPages;
			uint16 Depth = _pDestinationCursor->m_pDatabase->m_Depth;
			fg_CursorPop(_pDestinationCursor);
			Result = fg_Rebalance(_pDestinationCursor);
			/* Did the tree height change? */
			if (Depth != _pDestinationCursor->m_pDatabase->m_Depth)
				nPages += _pDestinationCursor->m_pDatabase->m_Depth - Depth;
			_pDestinationCursor->m_nPages = nPages;
			_pDestinationCursor->m_iTop = nPages - 1;
		}
		return Result;
	}

	/** Split a page and insert a new node.
	 * Set #DMdbTxnError on failure.
	 * @param[in,out] _pCursor Cursor pointing to the page and desired insertion index.
	 * The cursor will be updated to point to the actual page and index where
	 * the node got inserted after the split.
	 * @param[in] _pNewKey The key for the newly inserted node.
	 * @param[in] _pNewData The data for the newly inserted node.
	 * @param[in] _NewPageNumber The page number, if the new node is a branch node.
	 * @param[in] _NodeFlags The #DNodeAddFlags for the new node.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageSplit(CMdbCursor *_pCursor, CDatabaseValue *_pNewKey, CDatabaseValue *_pNewData, CPgno _NewPageNumber, uint32 _NodeFlags)
	{
		uint32 Flags;
		int Result = DMdbSuccess, NewRootLevel = 0, bDidSplit = 0;
		CIndx iNew;
		CPgno PageNumber = 0;
		int iIndex, iDestination, iSplit, nKeys, MaxSize;
		CMdbEnv *pEnvironment = _pCursor->m_pTransaction->m_pEnvironment;
		CMdbNode *pNode;
		CDatabaseValue SeparatorKey, NodeKey, NodeData, *pNodeData = &NodeData;
		CMdbPage *pCopy = nullptr;
		CMdbPage *pPage, *pRightPage, *pParentPage;
		int iParent;
		CMdbCursor RightCursor;
		DKBuf;

		pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		iNew = _pCursor->m_NodeIndices[_pCursor->m_iTop];
		nKeys = DNumKeys(pPage);

		DMibDatabaseTrace
			(
				"-----> splitting {} page {} and adding [{}] at index {}/{}"
				, DIsLeaf(pPage) ? "leaf" : "branch"
				, pPage->m_PageNumber
				, DKey(_pNewKey)
				, _pCursor->m_NodeIndices[_pCursor->m_iTop]
				, nKeys
			)
		;

		/* Create a right sibling. */
		if ((Result = fg_PageNew(_pCursor, pPage->m_Flags, 1, &pRightPage)))
			return Result;
		pRightPage->m_Pad = pPage->m_Pad;
		DMibDatabaseTrace("new right sibling: page {}", pRightPage->m_PageNumber);

		/* Usually when splitting the root page, the cursor
		 * height is 1. But when called from fg_UpdateKey,
		 * the cursor height may be greater because it walks
		 * up the stack while finding the branch slot to update.
		 */
		if (_pCursor->m_iTop < 1)
		{
			if ((Result = fg_PageNew(_pCursor, DPBranch, 1, &pParentPage)))
				goto done;
			/* shift current top to make room for new parent */
			for (iIndex = _pCursor->m_nPages; iIndex > 0; iIndex--)
			{
				_pCursor->m_Pages[iIndex] = _pCursor->m_Pages[iIndex - 1];
				_pCursor->m_NodeIndices[iIndex] = _pCursor->m_NodeIndices[iIndex - 1];
			}
			_pCursor->m_Pages[0] = pParentPage;
			_pCursor->m_NodeIndices[0] = 0;
			_pCursor->m_pDatabase->m_RootPage = pParentPage->m_PageNumber;
			DMibDatabaseTrace("root split! new root = {}", pParentPage->m_PageNumber);
			NewRootLevel = _pCursor->m_pDatabase->m_Depth++;

			/* Add left (implicit) pointer. */
			if ((Result = fg_NodeAdd(_pCursor, 0, nullptr, nullptr, pPage->m_PageNumber, 0)) != DMdbSuccess)
			{
				/* undo the pre-push */
				_pCursor->m_Pages[0] = _pCursor->m_Pages[1];
				_pCursor->m_NodeIndices[0] = _pCursor->m_NodeIndices[1];
				_pCursor->m_pDatabase->m_RootPage = pPage->m_PageNumber;
				_pCursor->m_pDatabase->m_Depth--;
				goto done;
			}
			_pCursor->m_nPages++;
			_pCursor->m_iTop++;
			iParent = 0;
		}
		else
		{
			iParent = _pCursor->m_iTop - 1;
			DMibDatabaseTrace("parent branch page is {}", _pCursor->m_Pages[iParent]->m_PageNumber);
		}

		fg_CursorCopy(_pCursor, &RightCursor);
		RightCursor.m_pXCursor = nullptr;
		RightCursor.m_Pages[RightCursor.m_iTop] = pRightPage;
		RightCursor.m_NodeIndices[iParent] = _pCursor->m_NodeIndices[iParent] + 1;

		if (_NodeFlags & EDatabaseWriteFlag_Append)
		{
			RightCursor.m_NodeIndices[RightCursor.m_iTop] = 0;
			SeparatorKey = *_pNewKey;
			iSplit = iNew;
			nKeys = 0;
		}
		else
		{
			iSplit = (nKeys + 1) / 2;

			if (DIsLeaf2(pRightPage))
			{
				uint8 *pSplit, *pInsert;
				int iRight;
				uint32 LeftSize, RightSize, KeySize;
				/* Move half of the keys to the right sibling */
				iRight = _pCursor->m_NodeIndices[_pCursor->m_iTop] - iSplit;
				KeySize = _pCursor->m_pDatabase->m_Pad;
				pSplit = DLeaf2Key(pPage, iSplit, KeySize);
				RightSize = (nKeys - iSplit) * KeySize;
				LeftSize = (nKeys - iSplit) * sizeof(CIndx);
				pPage->m_FreeSpace.m_Lower -= LeftSize;
				pRightPage->m_FreeSpace.m_Lower += LeftSize;
				pPage->m_FreeSpace.m_Upper += RightSize - LeftSize;
				pRightPage->m_FreeSpace.m_Upper -= RightSize - LeftSize;
				SeparatorKey.m_Size = KeySize;
				if (iNew == iSplit)
					SeparatorKey.m_pData = _pNewKey->m_pData;
				else
					SeparatorKey.m_pData = pSplit;
				if (iRight < 0)
				{
					pInsert = DLeaf2Key(pPage, _pCursor->m_NodeIndices[_pCursor->m_iTop], KeySize);
					memcpy(pRightPage->m_NodeOffsets, pSplit, RightSize);
					SeparatorKey.m_pData = (uint8 *)pRightPage->m_NodeOffsets;
					memmove(pInsert + KeySize, pInsert, (iSplit - _pCursor->m_NodeIndices[_pCursor->m_iTop]) * KeySize);
					memcpy(pInsert, _pNewKey->m_pData, KeySize);
					pPage->m_FreeSpace.m_Lower += sizeof(CIndx);
					pPage->m_FreeSpace.m_Upper -= CIndx(KeySize - sizeof(CIndx));
				}
				else
				{
					if (iRight)
						memcpy(pRightPage->m_NodeOffsets, pSplit, iRight * KeySize);
					pInsert = DLeaf2Key(pRightPage, iRight, KeySize);
					memcpy(pInsert, _pNewKey->m_pData, KeySize);
					memcpy(pInsert + KeySize, pSplit + iRight * KeySize, RightSize - iRight * KeySize);
					pRightPage->m_FreeSpace.m_Lower += sizeof(CIndx);
					pRightPage->m_FreeSpace.m_Upper -= CIndx(KeySize - sizeof(CIndx));
					_pCursor->m_NodeIndices[_pCursor->m_iTop] = iRight;
				}
			}
			else
			{
				int AccumulatedSize, NodeSize, iEnd;
				/* Maximum free space in an empty page */
				MaxSize = pEnvironment->m_PageSize - DPageHeaderSize;
				if (DIsLeaf(pPage))
					NodeSize = pEnvironment->f_LeafSize(_pNewKey, _pNewData);
				else
					NodeSize = pEnvironment->f_BranchSize(_pNewKey);
				NodeSize = DEven(NodeSize);

				/* grab a page to hold a temporary copy */
				pCopy = fg_PageMalloc(_pCursor->m_pTransaction, 1);
				if (pCopy == nullptr)
				{
					Result = ENOMEM;
					goto done;
				}
				pCopy->m_PageNumber = pPage->m_PageNumber;
				pCopy->m_Flags = pPage->m_Flags;
				pCopy->m_FreeSpace.m_Lower = (DPageHeaderSize - DPageBase);
				pCopy->m_FreeSpace.m_Upper = pEnvironment->m_PageSize - DPageBase;

				/* prepare to insert */
				for (iIndex = 0, iDestination = 0; iIndex < nKeys; iIndex++)
				{
					if (iIndex == iNew)
						pCopy->m_NodeOffsets[iDestination++] = 0;
					pCopy->m_NodeOffsets[iDestination++] = pPage->m_NodeOffsets[iIndex];
				}

				/* When items are relatively large the split point needs
				 * to be checked, because being off-by-one will make the
				 * difference between success or failure in fg_NodeAdd.
				 *
				 * It's also relevant if a page happens to be laid out
				 * such that one half of its nodes are all "small" and
				 * the other half of its nodes are "large." If the new
				 * item is also "large" and falls on the half with
				 * "large" nodes, it also may not fit.
				 *
				 * As a final tweak, if the new item goes on the last
				 * spot on the page (and thus, onto the new page), bias
				 * the split so the new page is emptier than the old page.
				 * This yields better packing during sequential inserts.
				 */
				if (nKeys < 32 || NodeSize > MaxSize / 16 || iNew >= nKeys)
				{
					/* Find split point */
					AccumulatedSize = 0;
					if (iNew <= iSplit || iNew >= nKeys)
					{
						iIndex = 0;
						iDestination = 1;
						iEnd = iNew >= nKeys ? nKeys : iSplit + 1 + DIsLeaf(pPage);
					}
					else
					{
						iIndex = nKeys;
						iDestination = -1;
						iEnd = iSplit - 1;
					}
					for (; iIndex != iEnd; iIndex += iDestination)
					{
						if (iIndex == iNew)
						{
							AccumulatedSize += NodeSize;
							pNode = nullptr;
						}
						else
						{
							pNode = (CMdbNode *)((char *)pPage + pCopy->m_NodeOffsets[iIndex] + DPageBase);
							AccumulatedSize += DNodeSize + DNodeKeySize(pNode) + sizeof(CIndx);
							if (DIsLeaf(pPage))
							{
								if (DFIsSet(pNode->m_Flags, DFBigData))
									AccumulatedSize += sizeof(CPgno);
								else
									AccumulatedSize += DNodeDataSize(pNode);
							}
							AccumulatedSize = DEven(AccumulatedSize);
						}
						if (AccumulatedSize > MaxSize || iIndex == iEnd - iDestination)
						{
							iSplit = iIndex + (iDestination < 0);
							break;
						}
					}
				}
				if (iSplit == iNew)
				{
					SeparatorKey.m_Size = _pNewKey->m_Size;
					SeparatorKey.m_pData = _pNewKey->m_pData;
				}
				else
				{
					pNode = (CMdbNode *)((char *)pPage + pCopy->m_NodeOffsets[iSplit] + DPageBase);
					SeparatorKey.m_Size = pNode->m_KeySize;
					SeparatorKey.m_pData = DNodeKey(pNode);
				}
			}
		}

		DMibDatabaseTrace("separator is {} [{}]", iSplit, DKey(&SeparatorKey));

		/* Copy separator key to the parent.
		*/
		if (DSizeLeft(RightCursor.m_Pages[iParent]) < pEnvironment->f_BranchSize(&SeparatorKey))
		{
			int nPages = _pCursor->m_nPages;
			RightCursor.m_nPages--;
			RightCursor.m_iTop--;
			bDidSplit = 1;
			/* We want other splits to find RightCursor when doing fixups */
			DWithCursorTracking(RightCursor, Result = fg_PageSplit(&RightCursor, &SeparatorKey, nullptr, pRightPage->m_PageNumber, 0));
			if (Result)
				goto done;

			/* root split? */
			if (_pCursor->m_nPages > nPages)
				iParent++;
			/* Right page might now have changed parent.
			 * Check if left page also changed parent.
			 */
			if (RightCursor.m_Pages[iParent] != _pCursor->m_Pages[iParent] && _pCursor->m_NodeIndices[iParent] >= DNumKeys(_pCursor->m_Pages[iParent]))
			{
				for (iIndex = 0; iIndex < iParent; iIndex++)
				{
					_pCursor->m_Pages[iIndex] = RightCursor.m_Pages[iIndex];
					_pCursor->m_NodeIndices[iIndex] = RightCursor.m_NodeIndices[iIndex];
				}
				_pCursor->m_Pages[iParent] = RightCursor.m_Pages[iParent];
				if (RightCursor.m_NodeIndices[iParent])
					_pCursor->m_NodeIndices[iParent] = RightCursor.m_NodeIndices[iParent] - 1;
				else
				{
					/* find right page's left sibling */
					_pCursor->m_NodeIndices[iParent] = RightCursor.m_NodeIndices[iParent];
					Result = fg_CursorSibling(_pCursor, 0);
				}
			}
		}
		else
		{
			RightCursor.m_iTop--;
			Result = fg_NodeAdd(&RightCursor, RightCursor.m_NodeIndices[iParent], &SeparatorKey, nullptr, pRightPage->m_PageNumber, 0);
			RightCursor.m_iTop++;
		}
		if (Result != DMdbSuccess)
		{
			if (Result == DMdbNotFound) // improper fg_CursorSibling() result
				Result = DMdbProblem;
			goto done;
		}
		if (_NodeFlags & EDatabaseWriteFlag_Append)
		{
			_pCursor->m_Pages[_pCursor->m_iTop] = pRightPage;
			_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;
			Result = fg_NodeAdd(_pCursor, 0, _pNewKey, _pNewData, _NewPageNumber, _NodeFlags);
			if (Result)
				goto done;
			for (iIndex = 0; iIndex < _pCursor->m_iTop; iIndex++)
				_pCursor->m_NodeIndices[iIndex] = RightCursor.m_NodeIndices[iIndex];
		}
		else if (!DIsLeaf2(pPage))
		{
			DMibFastCheck(pCopy);
			/* Move nodes */
			_pCursor->m_Pages[_pCursor->m_iTop] = pRightPage;
			iIndex = iSplit;
			iDestination = 0;
			do
			{
				if (iIndex == iNew)
				{
					NodeKey.m_pData = _pNewKey->m_pData;
					NodeKey.m_Size = _pNewKey->m_Size;
					if (DIsLeaf(pPage))
						pNodeData = _pNewData;
					else
						PageNumber = _NewPageNumber;
					Flags = _NodeFlags;
					/* Update index for the new key. */
					_pCursor->m_NodeIndices[_pCursor->m_iTop] = iDestination;
				}
				else
				{
					pNode = (CMdbNode *)((char *)pPage + pCopy->m_NodeOffsets[iIndex] + DPageBase);
					NodeKey.m_pData = DNodeKey(pNode);
					NodeKey.m_Size = pNode->m_KeySize;
					if (DIsLeaf(pPage))
					{
						NodeData.m_pData = (uint8 *)(CMdbPage *)DNodeData(pNode);
						NodeData.m_Size = DNodeDataSize(pNode);
						pNodeData = &NodeData;
					}
					else
						PageNumber = DNodePgno(pNode);
					Flags = pNode->m_Flags;
				}

				if (!DIsLeaf(pPage) && iDestination == 0)
				{
					/* First branch index doesn't need key data. */
					NodeKey.m_Size = 0;
				}

				Result = fg_NodeAdd(_pCursor, iDestination, &NodeKey, pNodeData, PageNumber, Flags);
				if (Result)
					goto done;
				if (iIndex == nKeys)
				{
					iIndex = 0;
					iDestination = 0;
					_pCursor->m_Pages[_pCursor->m_iTop] = pCopy;
				}
				else
				{
					iIndex++;
					iDestination++;
				}
			}
			while (iIndex != iSplit);

			nKeys = DNumKeys(pCopy);
			for (iIndex = 0; iIndex < nKeys; iIndex++)
				pPage->m_NodeOffsets[iIndex] = pCopy->m_NodeOffsets[iIndex];
			pPage->m_FreeSpace.m_Lower = pCopy->m_FreeSpace.m_Lower;
			pPage->m_FreeSpace.m_Upper = pCopy->m_FreeSpace.m_Upper;
			memcpy(DNodePtr(pPage, nKeys - 1), DNodePtr(pCopy, nKeys - 1), pEnvironment->m_PageSize - pCopy->m_FreeSpace.m_Upper - DPageBase);

			/* reset back to original page */
			if (iNew < iSplit)
				_pCursor->m_Pages[_pCursor->m_iTop] = pPage;
			else
			{
				_pCursor->m_Pages[_pCursor->m_iTop] = pRightPage;
				_pCursor->m_NodeIndices[iParent]++;
				/* Make sure m_NodeIndices is still valid.
				*/
				if (RightCursor.m_Pages[iParent] != _pCursor->m_Pages[iParent] && _pCursor->m_NodeIndices[iParent] >= DNumKeys(_pCursor->m_Pages[iParent]))
				{
					for (iIndex = 0; iIndex <= iParent; iIndex++)
					{
						_pCursor->m_Pages[iIndex] = RightCursor.m_Pages[iIndex];
						_pCursor->m_NodeIndices[iIndex] = RightCursor.m_NodeIndices[iIndex];
					}
				}
			}
			if (_NodeFlags & EDatabaseWriteFlag_ReserveSpace)
			{
				pNode = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);
				if (!(pNode->m_Flags & DFBigData))
					_pNewData->m_pData = (uint8 *)(CMdbPage *)DNodeData(pNode);
			}
		}
		else
		{
			if (iNew >= iSplit)
			{
				_pCursor->m_Pages[_pCursor->m_iTop] = pRightPage;
				_pCursor->m_NodeIndices[iParent]++;
				/* Make sure m_NodeIndices is still valid.
				*/
				if (RightCursor.m_Pages[iParent] != _pCursor->m_Pages[iParent] && _pCursor->m_NodeIndices[iParent] >= DNumKeys(_pCursor->m_Pages[iParent]))
				{
					for (iIndex = 0; iIndex <= iParent; iIndex++)
					{
						_pCursor->m_Pages[iIndex] = RightCursor.m_Pages[iIndex];
						_pCursor->m_NodeIndices[iIndex] = RightCursor.m_NodeIndices[iIndex];
					}
				}
			}
		}

		{
			/* Adjust other cursors pointing to pPage */
			CMdbCursor *pOther, *pOtherCursor;
			CDatabaseSubID DatabaseID = _pCursor->m_DatabaseID;
			nKeys = DNumKeys(pPage);

			for (pOther = _pCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
			{
				if (_pCursor->m_Flags & DCSub)
					pOtherCursor = &pOther->m_pXCursor->m_Cursor;
				else
					pOtherCursor = pOther;
				if (pOtherCursor == _pCursor)
					continue;
				if (!(pOther->m_Flags & pOtherCursor->m_Flags & DCInitialized))
					continue;
				if (NewRootLevel)
				{
					int iLevel;
					/* sub cursors may be on different DB */
					if (pOtherCursor->m_Pages[0] != pPage)
						continue;
					/* root split */
					for (iLevel = NewRootLevel; iLevel >= 0; iLevel--)
					{
						pOtherCursor->m_NodeIndices[iLevel + 1] = pOtherCursor->m_NodeIndices[iLevel];
						pOtherCursor->m_Pages[iLevel + 1] = pOtherCursor->m_Pages[iLevel];
					}
					if (pOtherCursor->m_NodeIndices[0] >= nKeys)
						pOtherCursor->m_NodeIndices[0] = 1;
					else
						pOtherCursor->m_NodeIndices[0] = 0;
					pOtherCursor->m_Pages[0] = _pCursor->m_Pages[0];
					pOtherCursor->m_nPages++;
					pOtherCursor->m_iTop++;
				}
				if (pOtherCursor->m_iTop >= _pCursor->m_iTop && pOtherCursor->m_Pages[_pCursor->m_iTop] == pPage)
				{
					if (pOtherCursor->m_NodeIndices[_pCursor->m_iTop] >= iNew && !(_NodeFlags & DMdbSplitReplace))
						pOtherCursor->m_NodeIndices[_pCursor->m_iTop]++;
					if (pOtherCursor->m_NodeIndices[_pCursor->m_iTop] >= nKeys)
					{
						pOtherCursor->m_Pages[_pCursor->m_iTop] = pRightPage;
						pOtherCursor->m_NodeIndices[_pCursor->m_iTop] -= nKeys;
						for (iIndex = 0; iIndex < _pCursor->m_iTop; iIndex++)
						{
							pOtherCursor->m_NodeIndices[iIndex] = RightCursor.m_NodeIndices[iIndex];
							pOtherCursor->m_Pages[iIndex] = RightCursor.m_Pages[iIndex];
						}
					}
				}
				else if
				(
					!bDidSplit
					&& pOtherCursor->m_iTop >= iParent
					&& pOtherCursor->m_Pages[iParent] == _pCursor->m_Pages[iParent]
					&& pOtherCursor->m_NodeIndices[iParent] >= _pCursor->m_NodeIndices[iParent]
				)
				{
					pOtherCursor->m_NodeIndices[iParent]++;
				}
				if (DIsLeaf(pPage))
					DXCursorRefresh(pOtherCursor, _pCursor->m_iTop, pOtherCursor->m_Pages[_pCursor->m_iTop]);
			}
		}
		DMibDatabaseTrace("mp left: {}, rp left: {}", DSizeLeft(pPage), DSizeLeft(pRightPage));

	done:
		if (pCopy) // tmp page
			pEnvironment->f_PageFree(pCopy);
		if (Result)
			_pCursor->m_pTransaction->m_Flags |= DMdbTxnError;
		return Result;
	}
}
