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
		CMdbEnv *env = _pTransaction->mt_env;
		CMdbPage *ret = env->me_dpages;
		umint psize = env->me_psize, sz = psize, off;
		/* For ! #EDatabaseEnvironmentFlag_NoMemoryInit, psize counts how much to init.
		 * For a single page alloc, we init everything after the page header.
		 * For multi-page, we init the final page; if the caller needed that
		 * many pages they will be filling in at least up to the last page.
		 */
		if (_nPages == 1)
		{
			if (ret)
			{
				env->me_dpages = ret->mp_p.p_next;
				return ret;
			}
			psize -= off = DPageHeaderSize;
		}
		else
		{
			sz *= _nPages;
			off = sz - psize;
		}
		if ((ret = (CMdbPage *)NMemory::fg_Alloc(sz)) != nullptr)
		{
			if (!(env->me_flags & EDatabaseEnvironmentFlag_NoMemoryInit))
			{
				memset((char *)ret + off, 0, psize);
				ret->mp_pad = 0;
			}
		}
		else
			_pTransaction->mt_flags |= DMdbTxnError;
		return ret;
	}

	/** Free a single page.
	 * Saves single pages to a list, for future reuse.
	 * (This is not used for multi-page overflow pages.)
	 */
	void CDatabaseEnvironment_Internal::f_PageFree(CMdbPage *_pPage)
	{
		_pPage->mp_p.p_next = me_dpages;
		me_dpages = _pPage;
	}

	void CDatabaseEnvironment_Internal::f_DPageFreeMemory(CMdbPage *_pPage)
	{
		if (!DIsOverflow(_pPage) || _pPage->mp_pages == 1)
			NMemory::fg_Free(_pPage, me_psize);
		else
			NMemory::fg_Free(_pPage, _pPage->mp_pages * me_psize);
	}

	/** Free a dirty page */
	void CDatabaseEnvironment_Internal::f_DPageFree(CMdbPage *_pPage)
	{
		if (!DIsOverflow(_pPage) || _pPage->mp_pages == 1)
			f_PageFree(_pPage);
		else
		{
			/* large pages just get freed directly */
			NMemory::fg_Free(_pPage, _pPage->mp_pages * me_psize);
		}
	}

	/**	Return all dirty pages to dpage list */
	void NPrivate::fg_DListFree(CMdbTxn *_pTransaction)
	{
		CMdbEnv *env = _pTransaction->mt_env;
		CMdbId2L dl = _pTransaction->mt_u.dirty_list;
		unsigned i, n = dl[0].mid;

		for (i = 1; i <= n; i++)
			env->f_DPageFree((CMdbPage *)(dl[i].mptr));
		dl[0].mid = 0;
	}

#ifdef DMibDatabase_Always64BitSupport
	void NPrivate::fg_PageUnref(CMdbTxn *txn, CMdbPage *mp)
	{
		CPgno pgno;
		MDB_ID3L tl = txn->mt_rpages;
		unsigned x, rem;
		if (mp->mp_flags & (DPSubp | DPDirty))
			return;
		rem = mp->mp_p.p_pgno & (DMdbRpageChunk - 1);
		pgno = mp->mp_p.p_pgno ^ rem;
		x = fg_Mid3lSearch(tl, pgno);
		if (x != tl[0].mid && tl[x + 1].mid == mp->mp_p.p_pgno)
			x++;
		if (tl[x].mref)
			tl[x].mref--;
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
		int loose = 0;
		CPgno pgno = _pPage->mp_p.p_pgno;
		CMdbTxn *txn = _pCursor->mc_txn;

		if ((_pPage->mp_flags & DPDirty) && _pCursor->mc_dbi != DFreeDbi)
		{
			if (txn->mt_parent)
			{
				CMdbId2 *dl = txn->mt_u.dirty_list;
				/* If txn has a parent, make sure the page is in our
				 * dirty list.
				 */
				if (dl[0].mid)
				{
					unsigned x = fg_Mid2lSearch(dl, pgno);
					if (x <= dl[0].mid && dl[x].mid == pgno)
					{
						if (_pPage != dl[x].mptr)
						{ // bad cursor?
							_pCursor->mc_flags &= ~(DCInitialized | DCEof);
							txn->mt_flags |= DMdbTxnError;
							return DMdbProblem;
						}
						/* ok, it's ours */
						loose = 1;
					}
				}
			}
			else
			{
				/* no parent txn, so it's just ours */
				loose = 1;
			}
		}
		if (loose)
		{
			DMibDatabaseTrace("loosen db {} page {}", DDbi(_pCursor), _pPage->mp_p.p_pgno);
			DNextLoosePage(_pPage) = txn->mt_loose_pgs;
			txn->mt_loose_pgs = _pPage;
			txn->mt_loose_count++;
			_pPage->mp_flags |= DPLoose;
		}
		else
			fg_MidlAppend(&txn->mt_free_pgs, pgno);

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

		CMdbTxn *txn = _pCursor->mc_txn;
		CMdbCursor *m3, *m0 = _pCursor;
		CMdbXCursor *mx;
		CMdbPage *dp, *mp;
		CMdbNode *leaf;
		unsigned i, j;
		int rc = DMdbSuccess, level;

		/* Mark pages seen by cursors: First m0, then tracked cursors */
		for (i = txn->mt_numdbs;;)
		{
			if (_pCursor->mc_flags & DCInitialized)
			{
				for (m3 = _pCursor;; m3 = &mx->mx_cursor)
				{
					mp = NULL;
					for (j = 0; j < m3->mc_snum; j++)
					{
						mp = m3->mc_pg[j];
						if ((mp->mp_flags & c_Mask) == _PageFlags)
							mp->mp_flags ^= DPKeep;
					}
					mx = m3->mc_xcursor;
					/* Proceed to mx if it is at a sub-database */
					if (!(mx && (mx->mx_cursor.mc_flags & DCInitialized)))
						break;
					if (!(mp && (mp->mp_flags & DPLeaf)))
						break;
					leaf = DNodePtr(mp, m3->mc_ki[j - 1]);
					if (!(leaf->mn_flags & DFSubData))
						break;
				}
			}
			_pCursor = _pCursor->mc_next;
			for (; !_pCursor || _pCursor == m0; _pCursor = txn->mt_cursors[--i])
			{
				if (i == 0)
					goto mark_done;
			}
		}

	mark_done:
		if (_bAll)
		{
			/* Mark dirty root pages */
			for (i = 0; i < txn->mt_numdbs; i++)
			{
				if (txn->mt_dbflags[i] & DDbDirty)
				{
					CPgno pgno = txn->mt_dbs[i].md_root;
					if (pgno == DPInvalid)
						continue;
					if ((rc = NPrivate::fg_PageGet(m0, pgno, &dp, &level)) != DMdbSuccess)
						break;
					if ((dp->mp_flags & c_Mask) == _PageFlags && level <= 1)
						dp->mp_flags ^= DPKeep;
				}
			}
		}

		return rc;
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
		CMdbTxn *txn = _pCursor->mc_txn;
		CMdbPage *dp;
		CMdbId2L dl = txn->mt_u.dirty_list;
		uint32 i, j, need;
		int rc;

		if (_pCursor->mc_flags & DCSub)
			return DMdbSuccess;

		/* Estimate how much space this op will take */
		i = _pCursor->mc_db->md_depth;
		/* Named DBs also dirty the main DB */
		if (_pCursor->mc_dbi >= DCoreDbs)
			i += txn->mt_dbs[DMainDbi].md_depth;
		/* For puts, roughly factor in the key+data size */
		if (_pKey)
			i += int((DLeafSize(_pKey, _pData) + txn->mt_env->me_psize) / txn->mt_env->me_psize);
		i += i; // double it for good measure
		need = i;

		if (txn->mt_dirty_room > i)
			return DMdbSuccess;

		if (!txn->mt_spill_pgs)
		{
			txn->mt_spill_pgs = fg_MidlAlloc(DMdbIdlUmMax);
			if (!txn->mt_spill_pgs)
				return ENOMEM;
		}
		else
		{
			/* purge deleted slots */
			CMdbIdl sl = txn->mt_spill_pgs;
			uint32 num = sl[0];
			j = 0;
			for (i = 1; i <= num; i++)
			{
				if (!(sl[i] & 1))
					sl[++j] = sl[i];
			}
			sl[0] = j;
		}

		/* Preserve pages which may soon be dirtied again */
		if ((rc = fg_PagesXKeep(_pCursor, DPDirty, 1)) != DMdbSuccess)
			goto done;

		/* Less aggressive spill - we originally spilled the entire dirty list,
		 * with a few exceptions for cursor pages and DB root pages. But this
		 * turns out to be a lot of wasted effort because in a large txn many
		 * of those pages will need to be used again. So now we spill only 1/8th
		 * of the dirty pages. Testing revealed this to be a good tradeoff,
		 * better than 1/2, 1/4, or 1/10.
		 */
		if (need < DMdbIdlUmMax / 8)
			need = DMdbIdlUmMax / 8;

		/* Save the page IDs of all the pages we're flushing */
		/* flush from the tail forward, this saves a lot of shifting later on. */
		for (i = dl[0].mid; i && need; i--)
		{
			CMdbId pn = dl[i].mid << 1;
			dp = (CMdbPage *)(dl[i].mptr);
			if (dp->mp_flags & (DPLoose | DPKeep))
				continue;
			/* Can't spill twice, make sure it's not already in a parent's
			 * spill list.
			 */
			if (txn->mt_parent)
			{
				CMdbTxn *tx2;
				for (tx2 = txn->mt_parent; tx2; tx2 = tx2->mt_parent)
				{
					if (tx2->mt_spill_pgs)
					{
						j = fg_MidlSearch(tx2->mt_spill_pgs, pn);
						if (j <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[j] == pn)
						{
							dp->mp_flags |= DPKeep;
							break;
						}
					}
				}
				if (tx2)
					continue;
			}
			fg_MidlAppend(&txn->mt_spill_pgs, pn);
			need--;
		}
		fg_MidlSort(txn->mt_spill_pgs);

		/* Flush the spilled part of dirty list */
		if ((rc = fg_PageFlush(txn, i)) != DMdbSuccess)
			goto done;

		/* Reset any dirty pages we kept that page_flush didn't see */
		rc = fg_PagesXKeep(_pCursor, DPDirty | DPKeep, i);

	done:
		txn->mt_flags |= rc ? DMdbTxnError : DMdbTxnSpills;
		return rc;
	}

	/** Find oldest txnid still referenced. Expects _pTransaction->mt_txnid > 0. */
	NPrivate::CTransactionID NPrivate::fg_FindOldest(CMdbTxn *_pTransaction)
	{
		int i;
		CTransactionID mr, oldest = _pTransaction->mt_txnid - 1;
		if (_pTransaction->mt_env->m_pLocksMemoryMap)
		{
			CMdbReader *r = _pTransaction->mt_env->m_pLocksMemoryMap->m_Readers;
			for (i = _pTransaction->mt_env->m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed); --i >= 0;)
			{
				if (r[i].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Acquire))
				{
					mr = r[i].m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
					if (oldest > mr)
						oldest = mr;
				}
			}
		}
		return oldest;
	}

	/** Add a page to the txn's dirty list */
	void NPrivate::fg_PageDirty(CMdbTxn *_pTransaction, CMdbPage *_pPage)
	{
		CMdbId2 mid;
		[[maybe_unused]] int rc;
		int (*insert)(CMdbId2L _pIDs, CMdbId2 *_pID);

		if (_pTransaction->mt_flags & DMdbTxnWriteMap)
			insert = fg_Mid2lAppend;
		else
			insert = fg_Mid2lInsert;
		mid.mid = _pPage->mp_p.p_pgno;
		mid.mptr = _pPage;
		rc = insert(_pTransaction->mt_u.dirty_list, &mid);
		DMibCheck(rc == 0);
		_pTransaction->mt_dirty_room--;
	}

	/** Allocate page numbers and memory for writing.  Maintain me_pgstate.mf_pglast,
	 * me_pghead and mt_next_pgno.  Set #DMdbTxnError on failure.
	 *
	 * If there are free pages available from older transactions, they
	 * are re-used first. Otherwise allocate a new page at mt_next_pgno.
	 * Do not modify the freedB, just merge freeDB records into me_pgstate.mf_pghead[]
	 * and move me_pgstate.mf_pglast to say which records were consumed.  Only this
	 * function can create me_pgstate.mf_pghead and move me_pgstate.mf_pglast/mt_next_pgno.
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
		/* Get at most <Max_retries> more freeDB records once me_pgstate.mf_pghead
		 * has enough pages.  If not enough, use new pages from the map.
		 * If <Paranoid> and _pCursor is updating the freeDB, only get new
		 * records if me_pgstate.mf_pghead is empty. Then the freelist cannot play
		 * catch-up with itself by growing while trying to save it.
		 */
		[[maybe_unused]] constexpr aint c_Paranoid = 1;
		[[maybe_unused]] constexpr aint c_Max_retries = 500;
#else
		[[maybe_unused]] constexpr aint c_Paranoid = 0;
		[[maybe_unused]] constexpr aint c_Max_retries = INT_MAX;
#endif
		int rc, retry = _nPages * 60;
		CMdbTxn *txn = _pCursor->mc_txn;
		CMdbEnv *env = txn->mt_env;
		CPgno pgno, *mop = env->me_pgstate.mf_pghead;
		unsigned i, j, mop_len = mop ? mop[0] : 0, n2 = _nPages - 1;
		CMdbPage *np;
		CTransactionID oldest = 0, last;
		EDatabaseCursorGet op;
		CMdbCursor m2;
		int found_old = 0;

		/* If there are any loose pages, just use them */
		if (_nPages == 1 && txn->mt_loose_pgs)
		{
			np = txn->mt_loose_pgs;
			txn->mt_loose_pgs = DNextLoosePage(np);
			txn->mt_loose_count--;
			DMibDatabaseTrace("db {} use loose page {}", DDbi(_pCursor), np->mp_p.p_pgno);
			*o_pPage = np;
			return DMdbSuccess;
		}

		*o_pPage = nullptr;

		/* If our dirty list is already full, we can't do anything */
		if (txn->mt_dirty_room == 0)
		{
			rc = DMdbTxnFull;
			goto fail;
		}

		for (op = EDatabaseCursorGet_First;; op = EDatabaseCursorGet_Next)
		{
			CDatabaseValue key, data;
			CMdbNode *leaf;
			CPgno *idl;

			/* Seek a big enough contiguous page range. Prefer
			 * pages at the tail, just truncating the list.
			 */
			if (mop_len > n2)
			{
				i = mop_len;
				do
				{
					pgno = mop[i];
					if (mop[i - n2] == pgno + n2)
						goto search_done;
				}
				while (--i > n2);
				if (--retry < 0)
					break;
			}

			if (op == EDatabaseCursorGet_First)
			{ // 1st iteration
				/* Prepare to fetch more and coalesce */
				last = env->me_pgstate.mf_pglast;
				oldest = env->me_pgoldest;
				NPrivate::fg_CursorInit(&m2, txn, DFreeDbi, nullptr);
#if (DMdbDevel) & 2	/* "& 2" so DMdbDevel=1 won't hide bugs breaking freeDB */
				/* Use original snapshot. TODO: Should need less care in code
				 * which modifies the database. Maybe we can delete some code?
				 */
				m2.mc_flags |= DCOrigReadOnly;
				m2.mc_db = &env->me_metas[(txn->mt_txnid - 1) & 1]->mm_dbs[DFreeDbi];
				m2.mc_dbflag = (unsigned char *)""; // probably unnecessary
#endif
				if (last)
				{
					op = EDatabaseCursorGet_SetRange;
					key.m_pData = (uint8 *)&last; // will look up last+1
					key.m_Size = sizeof(last);
				}
				if (c_Paranoid && _pCursor->mc_dbi == DFreeDbi)
					retry = -1;
			}
			if (c_Paranoid && retry < 0 && mop_len)
				break;

			last++;
			/* Do not fetch more if the record will be too recent */
			if (oldest <= last)
			{
				if (!found_old)
				{
					oldest = fg_FindOldest(txn);
					env->me_pgoldest = oldest;
					found_old = 1;
				}
				if (oldest <= last)
					break;
			}
			rc = fg_CursorGet(&m2, &key, nullptr, op);
			if (rc)
			{
				if (rc == DMdbNotFound)
					break;
				goto fail;
			}
			last = *(CTransactionID *)key.m_pData;
			if (oldest <= last)
			{
				if (!found_old)
				{
					oldest = fg_FindOldest(txn);
					env->me_pgoldest = oldest;
					found_old = 1;
				}
				if (oldest <= last)
					break;
			}
			np = m2.mc_pg[m2.mc_top];
			leaf = DNodePtr(np, m2.mc_ki[m2.mc_top]);
			if ((rc = NPrivate::fg_NodeRead(&m2, leaf, &data)) != DMdbSuccess)
				goto fail;

			idl = (CMdbId *)data.m_pData;
			i = idl[0];
			if (!mop)
			{
				if (!(env->me_pgstate.mf_pghead = mop = fg_MidlAlloc(i)))
				{
					rc = ENOMEM;
					goto fail;
				}
			}
			else
			{
				fg_MidlNeed(&env->me_pgstate.mf_pghead, i);
				mop = env->me_pgstate.mf_pghead;
			}
			env->me_pgstate.mf_pglast = last;
#if (DMibDatabaseDebug) > 1
			DMibDatabaseTrace("IDL read txn {} root {} num {}", last, txn->mt_dbs[DFreeDbi].md_root, i);
			for (j = i; j; j--)
				DMibDatabaseTrace("IDL {}", idl[j]);
#endif
			/* Merge in descending sorted order */
			fg_MidlXMerge(mop, idl);
			mop_len = mop[0];
		}

		/* Use new pages from the map when nothing suitable in the freeDB */
		i = 0;
		pgno = txn->mt_next_pgno;
		if (pgno + _nPages >= env->me_maxpg)
		{
			DPuts("DB size maxed out");
			rc = DMdbMapFull;
			goto fail;
		}
#if defined(DPlatformFamily_Windows) && !defined(DMibDatabase_Always64BitSupport)
		if (!(env->me_flags & EDatabaseEnvironmentFlag_ReadOnly))
		{
			void *p;
			p = (CMdbPage *)(env->me_map + env->me_psize * pgno);
			p = VirtualAlloc(p, env->me_psize * _nPages, MEM_COMMIT, (env->me_flags & EDatabaseEnvironmentFlag_WriteMap) ? PAGE_READWRITE : PAGE_READONLY);
			if (!p)
			{
				DPuts("VirtualAlloc failed");
				rc = DErrCode();
				goto fail;
			}
		}
#endif

	search_done:
		if (env->me_flags & EDatabaseEnvironmentFlag_WriteMap)
			np = (CMdbPage *)(env->me_map + env->me_psize * pgno);
		else
		{
			if (!(np = fg_PageMalloc(txn, _nPages)))
			{
				rc = ENOMEM;
				goto fail;
			}
		}
		if (i)
		{
			mop[0] = mop_len -= _nPages;
			/* Move any stragglers down */
			for (j = i - _nPages; j < mop_len;)
				mop[++j] = mop[++i];
		}
		else
			txn->mt_next_pgno = pgno + _nPages;
		np->mp_p.p_pgno = pgno;
		fg_PageDirty(txn, np);
		*o_pPage = np;

		return DMdbSuccess;

	fail:
		txn->mt_flags |= DMdbTxnError;
		return rc;
	}

	/** Copy the used portions of a non-overflow page.
	 * @param[in] _pDestination page to copy into
	 * @param[in] _pSource page to copy from
	 * @param[in] _PageSize size of a page
	 */
	void NPrivate::fg_PageCopy(CMdbPage *_pDestination, CMdbPage *_pSource, uint32 _PageSize)
	{
		constexpr aint c_Align = sizeof(CPgno);

		CIndx upper = _pSource->mp_upper, lower = _pSource->mp_lower, unused = upper - lower;

		/* If page isn't full, just copy the used portion. Adjust
		 * alignment so memcpy may copy words instead of bytes.
		 */
		if ((unused &= -c_Align) && !DIsLeaf2(_pSource))
		{
			upper = (upper + DPageBase) & -c_Align;
			memcpy(_pDestination, _pSource, (lower + DPageBase + (c_Align - 1)) & -c_Align);
			memcpy((CPgno *)((char *)_pDestination + upper), (CPgno *)((char *)_pSource + upper), _PageSize - upper);
		}
		else
			memcpy(_pDestination, _pSource, _PageSize - unused);
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
		CMdbEnv *env = _pTransaction->mt_env;
		CMdbTxn const *tx2;
		unsigned x;
		CPgno pgno = _pPage->mp_p.p_pgno, pn = pgno << 1;

		for (tx2 = _pTransaction; tx2; tx2 = tx2->mt_parent)
		{
			if (!tx2->mt_spill_pgs)
				continue;
			x = fg_MidlSearch(tx2->mt_spill_pgs, pn);
			if (x <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[x] == pn)
			{
				CMdbPage *np;
				int num;
				if (_pTransaction->mt_dirty_room == 0)
					return DMdbTxnFull;
				if (DIsOverflow(_pPage))
					num = _pPage->mp_pages;
				else
					num = 1;
				if (env->me_flags & EDatabaseEnvironmentFlag_WriteMap)
					np = _pPage;
				else
				{
					np = fg_PageMalloc(_pTransaction, num);
					if (!np)
						return ENOMEM;
					if (num > 1)
						memcpy(np, _pPage, num * env->me_psize);
					else
						fg_PageCopy(np, _pPage, env->me_psize);
				}
				if (tx2 == _pTransaction)
				{
					/* If in current txn, this page is no longer spilled.
					 * If it happens to be the last page, truncate the spill list.
					 * Otherwise mark it as deleted by setting the LSB.
					 */
					if (x == tx2->mt_spill_pgs[0])
						tx2->mt_spill_pgs[0]--;
					else
						tx2->mt_spill_pgs[x] |= 1;
				} // otherwise, if belonging to a parent txn, the
				  // page remains spilled until child commits

				fg_PageDirty(_pTransaction, np);
				np->mp_flags |= DPDirty;
				*o_pDirtyPage = np;
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
		CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top], *np;
		CMdbTxn *txn = _pCursor->mc_txn;
		CMdbCursor *m2, *m3;
		CPgno pgno;
		int rc;

		if (!DFIsSet(mp->mp_flags, DPDirty))
		{
			if (txn->mt_flags & DMdbTxnSpills)
			{
				np = nullptr;
				rc = fg_PageUnspill(txn, mp, &np);
				if (rc)
					goto fail;
				if (np)
					goto done;
			}
			fg_MidlNeed(&txn->mt_free_pgs, 1);
			if ((rc = fg_PageAlloc(_pCursor, 1, &np)))
				goto fail;
			pgno = np->mp_p.p_pgno;
			DMibDatabaseTrace("touched db {} page {} -> {}", DDbi(_pCursor), mp->mp_p.p_pgno, pgno);
			DMibCheck(mp->mp_p.p_pgno != pgno);
			fg_MidlXAppend(txn->mt_free_pgs, mp->mp_p.p_pgno);
			/* Update the parent page, if any, to point to the new page */
			if (_pCursor->mc_top)
			{
				CMdbPage *parent = _pCursor->mc_pg[_pCursor->mc_top - 1];
				CMdbNode *node = DNodePtr(parent, _pCursor->mc_ki[_pCursor->mc_top - 1]);
				DSetPgno(node, pgno);
			}
			else
				_pCursor->mc_db->md_root = pgno;
		}
		else if (txn->mt_parent && !DIsSubp(mp))
		{
			CMdbId2 mid, *dl = txn->mt_u.dirty_list;
			pgno = mp->mp_p.p_pgno;
			/* If txn has a parent, make sure the page is in our
			 * dirty list.
			 */
			if (dl[0].mid)
			{
				unsigned x = fg_Mid2lSearch(dl, pgno);
				if (x <= dl[0].mid && dl[x].mid == pgno)
				{
					if (mp != dl[x].mptr)
					{ // bad cursor?
						_pCursor->mc_flags &= ~(DCInitialized | DCEof);
						txn->mt_flags |= DMdbTxnError;
						return DMdbProblem;
					}
					return 0;
				}
			}
			DMibCheck(dl[0].mid < DMdbIdlUmMax);
			/* No - copy it */
			np = fg_PageMalloc(txn, 1);
			if (!np)
				return ENOMEM;
			mid.mid = pgno;
			mid.mptr = np;
			rc = fg_Mid2lInsert(dl, &mid);
			DMibCheck(rc == 0);
		}
		else
			return 0;

		fg_PageCopy(np, mp, txn->mt_env->me_psize);
		np->mp_p.p_pgno = pgno;
		np->mp_flags |= DPDirty;

	done:
		/* Adjust cursors pointing to mp */
		_pCursor->mc_pg[_pCursor->mc_top] = np;
		m2 = txn->mt_cursors[_pCursor->mc_dbi];
		if (_pCursor->mc_flags & DCSub)
		{
			for (; m2; m2 = m2->mc_next)
			{
				m3 = &m2->mc_xcursor->mx_cursor;
				if (m3->mc_snum < _pCursor->mc_snum)
					continue;
				if (m3->mc_pg[_pCursor->mc_top] == mp)
					m3->mc_pg[_pCursor->mc_top] = np;
			}
		}
		else
		{
			for (; m2; m2 = m2->mc_next)
			{
				if (m2->mc_snum < _pCursor->mc_snum)
					continue;
				if (m2 == _pCursor)
					continue;
				if (m2->mc_pg[_pCursor->mc_top] == mp)
				{
					m2->mc_pg[_pCursor->mc_top] = np;
					if (DIsLeaf(np))
						DXCursorRefresh(m2, _pCursor->mc_top, np);
				}
			}
		}
		DMdbPageUnref(_pCursor->mc_txn, mp);
		return 0;

	fail:
		txn->mt_flags |= DMdbTxnError;
		return rc;
	}

	/** Flush (some) dirty pages to the map, after clearing their dirty flag.
	 * @param[in] _pTransaction the transaction that's being committed
	 * @param[in] _nKeep number of initial pages in dirty_list to keep dirty.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageFlush(CMdbTxn *_pTransaction, int _nKeep)
	{
		CMdbEnv *env = _pTransaction->mt_env;
		CMdbId2L dl = _pTransaction->mt_u.dirty_list;
		unsigned psize = env->me_psize, j;
		int i, pagecount = dl[0].mid, rc;
		umint size = 0;
		off_t pos = 0;
		CPgno pgno = 0;
		CMdbPage *dp = nullptr;
#ifdef DPlatformFamily_Windows
		OVERLAPPED ov;
#else
		struct iovec iov[DMdbCommitPages];
		smint wsize = 0, wres;
		off_t wpos = 0, next_pos = 1; // impossible pos, so pos != next_pos
		int n = 0;
#endif

		j = i = _nKeep;

		if (env->me_flags & EDatabaseEnvironmentFlag_WriteMap)
		{
			/* Clear dirty flags */
			while (++i <= pagecount)
			{
				dp = (CMdbPage *)(dl[i].mptr);
				/* Don't flush this page yet */
				if (dp->mp_flags & (DPLoose | DPKeep))
				{
					dp->mp_flags &= ~DPKeep;
					dl[++j] = dl[i];
					continue;
				}
				dp->mp_flags &= ~DPDirty;
			}
			goto done;
		}

		/* Write the pages */
		for (;;)
		{
			if (++i <= pagecount)
			{
				dp = (CMdbPage *)dl[i].mptr;
				/* Don't flush this page yet */
				if (dp->mp_flags & (DPLoose | DPKeep))
				{
					dp->mp_flags &= ~DPKeep;
					dl[i].mid = 0;
					continue;
				}
				pgno = dl[i].mid;
				/* clear dirty flag */
				dp->mp_flags &= ~DPDirty;
				pos = pgno * psize;
				size = psize;
				if (DIsOverflow(dp))
					size *= dp->mp_pages;
			}
#ifdef DPlatformFamily_Windows
			else
				break;

			/* Windows actually supports scatter/gather I/O, but only on
			 * unbuffered file handles. Since we're relying on the OS page
			 * cache for all our data, that's self-defeating. So we just
			 * write pages one at a time. We use the ov structure to set
			 * the write offset, to at least save the overhead of a Seek
			 * system call.
			 */
			DMibDatabaseTrace("committing page {}", pgno);
			memset(&ov, 0, sizeof(ov));
			ov.Offset = pos & 0xffffffff;
			ov.OffsetHigh = pos >> 16 >> 16;
			if (!WriteFile(env->me_fd, dp, size, nullptr, &ov))
			{
				rc = DErrCode();
				DMibDatabaseTrace("WriteFile: {}", rc);
				return rc;
			}
#else
			/* Write up to DMdbCommitPages dirty pages at a time. */
			if (pos != next_pos || n == DMdbCommitPages || wsize + size > DMaxWrite)
			{
				if (n)
				{
				retry_write:
					/* Write previous page(s) */
#	ifdef MDB_USE_PWRITEV
					wres = pwritev(env->me_fd, iov, n, wpos);
#	else
					if (n == 1)
						wres = pwrite(env->me_fd, iov[0].iov_base, wsize, wpos);
					else
					{
					retry_seek:
						if (lseek(env->me_fd, wpos, SEEK_SET) == -1)
						{
							rc = DErrCode();
							if (rc == EINTR)
								goto retry_seek;
							DMibDatabaseTrace("lseek: {}", fg_StrError(rc));
							return rc;
						}
						wres = writev(env->me_fd, iov, n);
					}
#	endif
					if (wres != wsize)
					{
						if (wres < 0)
						{
							rc = DErrCode();
							if (rc == EINTR)
								goto retry_write;
							DMibDatabaseTrace("Write error: {}", fg_StrError(rc));
						}
						else
						{
							rc = EIO; // TODO: Use which error code?
							DPuts("short write, filesystem full?");
						}
						return rc;
					}
					n = 0;
				}
				if (i > pagecount)
					break;
				wpos = pos;
				wsize = 0;
			}
			DMibDatabaseTrace("committing page {}", pgno);
			next_pos = pos + size;
			iov[n].iov_len = size;
			iov[n].iov_base = (char *)dp;
			wsize += size;
			n++;
#endif	/* DPlatformFamily_Windows */
		}
#ifdef DMibDatabase_Always64BitSupport
		if (pgno > _pTransaction->mt_last_pgno)
			_pTransaction->mt_last_pgno = pgno;
#endif

		/* MIPS has cache coherency issues, this is a no-op everywhere else
		 * Note: for any size >= on-chip cache size, entire on-chip cache is
		 * flushed.
		 */
		DCacheFlush(env->me_map, _pTransaction->mt_next_pgno * env->me_psize, DCACHE);

		for (i = _nKeep; ++i <= pagecount;)
		{
			dp = (CMdbPage *)dl[i].mptr;
			/* This is a page we skipped above */
			if (!dl[i].mid)
			{
				dl[++j] = dl[i];
				dl[j].mid = dp->mp_p.p_pgno;
				continue;
			}
			env->f_DPageFree(dp);
		}

	done:
		i--;
		_pTransaction->mt_dirty_room += i - j;
		dl[0].mid = j;
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
	 * explicitly, in the added mc_ovpg field. A single cursor can only hold a
	 * reference to one overflow page at a time.
	 *
	 * @param[in] txn the transaction for this access.
	 * @param[in] pgno the page number for the page to retrieve.
	 * @param[out] ret address of a pointer where the page's address will be stored.
	 * @return 0 on success, non-zero on failure.
	 */
	int fg_RPageGet(CMdbTxn *txn, CPgno pg0, CMdbPage **ret)
	{
		CMdbEnv *env = txn->mt_env;
		CMdbPage *p;
		MDB_ID3L tl = txn->mt_rpages;
		MDB_ID3L el = env->me_rpages;
		MDB_ID3 id3;
		unsigned x, rem;
		CPgno pgno;
		int rc, retries = 1;
#	ifdef DPlatformFamily_Windows
		LARGE_INTEGER off;
		SIZE_T len;
#		define DSetOff(d_Off,d_Val)	d_Off.QuadPart = d_Val
#		define DMap(d_Rc,d_Env,d_Addr,d_Len,d_Off)	\
			d_Addr = nullptr; \
			d_Rc = g_fDatabase_NtMapViewOfSection(d_Env->me_fmh, GetCurrentProcess(), &d_Addr, 0, \
				d_Len, &d_Off, &d_Len, ViewUnmap, (d_Env->me_flags & EDatabaseEnvironmentFlag_ReadOnly) ? 0 : MEM_RESERVE, PAGE_READONLY); \
			if (d_Rc) d_Rc = fg_Nt2Win32(d_Rc)
#	else
		off_t off;
		umint len;
#		define DSetOff(d_Off,d_Val)	d_Off = d_Val
#		define DMap(d_Rc,d_Env,d_Addr,d_Len,d_Off)	\
			d_Addr = mmap(nullptr, d_Len, PROT_READ, MAP_SHARED, d_Env->me_fd, d_Off); \
			d_Rc = (d_Addr == MAP_FAILED) ? errno : 0
#	endif

		/* remember the offset of the actual page number, so we can
		 * return the correct pointer at the end.
		 */
		rem = pg0 & (DMdbRpageChunk - 1);
		pgno = pg0 ^ rem;

		id3.mid = 0;
		x = fg_Mid3lSearch(tl, pgno);
		if (x <= tl[0].mid && tl[x].mid == pgno)
		{
			if (x != tl[0].mid && tl[x + 1].mid == pg0)
				x++;
			/* check for overflow size */
			p = (CMdbPage *)((char *)tl[x].mptr + rem * env->me_psize);
			if (DIsOverflow(p) && p->mp_pages + rem > tl[x].mcnt)
			{
				id3.mcnt = p->mp_pages + rem;
				len = id3.mcnt * env->me_psize;
				DSetOff(off, pgno * env->me_psize);
				DMap(rc, env, id3.mptr, len, off);
				if (rc)
					return rc;
				/* check for local-only page */
				if (rem)
				{
					DMibCheck(tl[x].mid != pg0);
					/* hope there's room to insert this locally.
					 * setting mid here tells later code to just insert
					 * this id3 instead of searching for a match.
					 */
					id3.mid = pg0;
					goto notlocal;
				}
				else
				{
					/* ignore the mapping we got from env, use new one */
					tl[x].mptr = id3.mptr;
					tl[x].mcnt = id3.mcnt;
					/* if no active ref, see if we can replace in env */
					if (!tl[x].mref)
					{
						unsigned i;
						pthread_mutex_lock(&env->me_rpmutex);
						i = fg_Mid3lSearch(el, tl[x].mid);
						if (el[i].mref == 1)
						{
							/* just us, replace it */
							munmap(el[i].mptr, el[i].mcnt * env->me_psize);
							el[i].mptr = tl[x].mptr;
							el[i].mcnt = tl[x].mcnt;
						}
						else
						{
							/* there are others, remove ourself */
							el[i].mref--;
						}
						pthread_mutex_unlock(&env->me_rpmutex);
					}
				}
			}
			id3.mptr = tl[x].mptr;
			id3.mcnt = tl[x].mcnt;
			tl[x].mref++;
			goto ok;
		}

	notlocal:
		if (tl[0].mid >= DMdbTrpageMax - txn->mt_rpcheck)
		{
			unsigned i, y;
			/* purge unref'd pages from our list and unref in env */
			pthread_mutex_lock(&env->me_rpmutex);
		retry:
			y = 0;
			for (i = 1; i <= tl[0].mid; i++)
			{
				if (!tl[i].mref)
				{
					if (!y)
						y = i;
					/* tmp overflow pages don't go to env */
					if (tl[i].mid & (DMdbRpageChunk - 1))
					{
						munmap(tl[i].mptr, tl[i].mcnt * env->me_psize);
						continue;
					}
					x = fg_Mid3lSearch(el, tl[i].mid);
					el[x].mref--;
				}
			}
			pthread_mutex_unlock(&env->me_rpmutex);
			if (!y)
			{
				/* we didn't find any unref'd chunks.
				 * if we're out of room, fail.
				 */
				if (tl[0].mid >= DMdbTrpageMax)
					return DMdbTxnFull;
				/* otherwise, raise threshold for next time around
				 * and let this go.
				 */
				txn->mt_rpcheck /= 2;
			}
			else
			{
				/* we found some unused; consolidate the list */
				for (i = y + 1; i <= tl[0].mid; i++)
				{
					if (tl[i].mref)
						tl[y++] = tl[i];
				}
				tl[0].mid = y - 1;
				/* decrease the check threshold toward its original value */
				if (!txn->mt_rpcheck)
					txn->mt_rpcheck = 1;
				while (txn->mt_rpcheck < tl[0].mid && txn->mt_rpcheck < DMdbTrpageSize / 2)
					txn->mt_rpcheck *= 2;
			}
		}
		if (tl[0].mid < DMdbTrpageSize)
		{
			id3.mref = 1;
			if (id3.mid)
				goto found;
			/* don't map past last written page in read-only envs */
			if ((env->me_flags & EDatabaseEnvironmentFlag_ReadOnly) && pgno + DMdbRpageChunk - 1 > txn->mt_last_pgno)
				id3.mcnt = txn->mt_last_pgno + 1 - pgno;
			else
				id3.mcnt = DMdbRpageChunk;
			len = id3.mcnt * env->me_psize;
			id3.mid = pgno;

			/* search for page in env */
			pthread_mutex_lock(&env->me_rpmutex);
			x = fg_Mid3lSearch(el, pgno);
			if (x <= el[0].mid && el[x].mid == pgno)
			{
				id3.mptr = el[x].mptr;
				id3.mcnt = el[x].mcnt;
				/* check for overflow size */
				p = (CMdbPage *)((char *)id3.mptr + rem * env->me_psize);
				if (DIsOverflow(p) && p->mp_pages + rem > id3.mcnt)
				{
					id3.mcnt = p->mp_pages + rem;
					len = id3.mcnt * env->me_psize;
					DSetOff(off, pgno * env->me_psize);
					DMap(rc, env, id3.mptr, len, off);
					if (rc)
						goto fail;
					if (!el[x].mref)
					{
						munmap(el[x].mptr, env->me_psize * el[x].mcnt);
						el[x].mptr = id3.mptr;
						el[x].mcnt = id3.mcnt;
					}
					else
					{
						id3.mid = pg0;
						pthread_mutex_unlock(&env->me_rpmutex);
						goto found;
					}
				}
				el[x].mref++;
				pthread_mutex_unlock(&env->me_rpmutex);
				goto found;
			}
			if (el[0].mid >= DMdbErpageMax - env->me_rpcheck)
			{
				/* purge unref'd pages */
				unsigned i, y = 0;
				for (i = 1; i <= el[0].mid; i++)
				{
					if (!el[i].mref)
					{
						if (!y)
							y = i;
						munmap(el[i].mptr, env->me_psize * el[i].mcnt);
					}
				}
				if (!y)
				{
					if (retries)
					{
						/* see if we can unref some local pages */
						retries--;
						id3.mid = 0;
						goto retry;
					}
					if (el[0].mid >= DMdbErpageMax)
					{
						pthread_mutex_unlock(&env->me_rpmutex);
						return DMdbMapFull;
					}
					env->me_rpcheck /= 2;
				}
				else
				{
					for (i = y + 1; i <= el[0].mid; i++)
					{
						if (el[i].mref)
							el[y++] = el[i];
					}
					el[0].mid = y - 1;
					if (!env->me_rpcheck)
						env->me_rpcheck = 1;
					while (env->me_rpcheck < el[0].mid && env->me_rpcheck < DMdbErpageSize / 2)
						env->me_rpcheck *= 2;
				}
			}
			DSetOff(off, pgno * env->me_psize);
			DMap(rc, env, id3.mptr, len, off);
			if (rc)
			{
			fail:
				pthread_mutex_unlock(&env->me_rpmutex);
				return rc;
			}
			/* check for overflow size */
			p = (CMdbPage *)((char *)id3.mptr + rem * env->me_psize);
			if (DIsOverflow(p) && p->mp_pages + rem > id3.mcnt)
			{
				id3.mcnt = p->mp_pages + rem;
				munmap(id3.mptr, len);
				len = id3.mcnt * env->me_psize;
				DMap(rc, env, id3.mptr, len, off);
				if (rc)
					goto fail;
			}
			fg_Mid3lInsert(el, &id3);
			pthread_mutex_unlock(&env->me_rpmutex);
		found:
			fg_Mid3lInsert(tl, &id3);
		}
		else
			return DMdbTxnFull;
	ok:
		p = (CMdbPage *)((char *)id3.mptr + rem * env->me_psize);
#	if DMibDatabaseDebug	/* we don't need this check any more */
		if (DIsOverflow(p))
			DMibCheck(p->mp_pages + rem <= id3.mcnt);
#	endif
		*ret = p;
		return DMdbSuccess;
	}
#endif

	/** Find the address of the page corresponding to a given page number.
	 * Set #DMdbTxnError on failure.
	 * @param[in] mc the cursor accessing the page.
	 * @param[in] pgno the page number for the page to retrieve.
	 * @param[out] ret address of a pointer where the page's address will be stored.
	 * @param[out] lvl dirty_list inheritance level of found page. 1=current txn, 0=mapped page.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_PageGet(CMdbCursor *_pCursor, CPgno _PageNumber, CMdbPage **o_pPage, int *o_pLevel)
	{
		CMdbTxn *txn = _pCursor->mc_txn;
		CMdbPage *p = nullptr;
		int level;

		if (!(_pCursor->mc_flags & (DCOrigReadOnly | DCWriteMap)))
		{
			CMdbTxn *tx2 = txn;
			level = 1;
			do
			{
				CMdbId2L dl = tx2->mt_u.dirty_list;
				unsigned x;
				/* Spilled pages were dirtied in this txn and flushed
				 * because the dirty list got full. Bring this page
				 * back in from the map (but don't unspill it here,
				 * leave that unless page_touch happens again).
				 */
				if (tx2->mt_spill_pgs)
				{
					CMdbId pn = _PageNumber << 1;
					x = fg_MidlSearch(tx2->mt_spill_pgs, pn);
					if (x <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[x] == pn)
						goto mapped;
				}
				if (dl[0].mid)
				{
					unsigned x = fg_Mid2lSearch(dl, _PageNumber);
					if (x <= dl[0].mid && dl[x].mid == _PageNumber)
					{
						p = (CMdbPage *)dl[x].mptr;
						goto done;
					}
				}
				level++;
			}
			while ((tx2 = tx2->mt_parent) != nullptr);
		}

		if (_PageNumber >= txn->mt_next_pgno)
		{
			DMibDatabaseTrace("page {} not found", _PageNumber);
			txn->mt_flags |= DMdbTxnError;
			return DMdbPageNotfound;
		}

		level = 0;

	mapped:
		{
#ifdef DMibDatabase_Always64BitSupport
			int rc = fg_RPageGet(txn, _PageNumber, &p);
			if (rc)
			{
				txn->mt_flags |= DMdbTxnError;
				return rc;
			}
#else
			CMdbEnv *env = txn->mt_env;
			p = (CMdbPage *)(env->me_map + env->me_psize * _PageNumber);
#endif
		}

	done:
		*o_pPage = p;
		if (o_pLevel)
			*o_pLevel = level;
		return DMdbSuccess;
	}

	/** Finish #fg_PageSearch() / #fg_PageSearchLowest().
	 *	The cursor is at the root page, set up the rest of it.
	 */
	int NPrivate::fg_PageSearchRoot(CMdbCursor *_pCursor, CDatabaseValue const *_pKey, int _Flags)
	{
		CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top];
		int rc;
		DKBuf;

		while (DIsBranch(mp))
		{
			CMdbNode *node;
			CIndx i;

			DMibDatabaseTrace("branch page {} has {} keys", mp->mp_p.p_pgno, DNumKeys(mp));
			/* Don't assert on branch pages in the FreeDB. We can get here
			 * while in the process of rebalancing a FreeDB branch page; we must
			 * let that proceed. ITS#8336
			 */
			DMibCheck(!_pCursor->mc_dbi || DNumKeys(mp) > 1);
			DMibDatabaseTrace("found index 0 to page {}", DNodePgno(DNodePtr(mp, 0)));

			if (_Flags & (DMdbPsFirst | DMdbPsLast))
			{
				i = 0;
				if (_Flags & DMdbPsLast)
				{
					i = DNumKeys(mp) - 1;
					/* if already init'd, see if we're already in right place */
					if (_pCursor->mc_flags & DCInitialized)
					{
						if (_pCursor->mc_ki[_pCursor->mc_top] == i)
						{
							_pCursor->mc_top = _pCursor->mc_snum++;
							mp = _pCursor->mc_pg[_pCursor->mc_top];
							goto ready;
						}
					}
				}
			}
			else
			{
				int exact;
				node = fg_NodeSearch(_pCursor, _pKey, &exact);
				if (node == nullptr)
					i = DNumKeys(mp) - 1;
				else
				{
					i = _pCursor->mc_ki[_pCursor->mc_top];
					if (!exact)
					{
						DMibCheck(i > 0);
						i--;
					}
				}
				DMibDatabaseTrace("following index {} for key [{}]", i, DKey(_pKey));
			}

			DMibCheck(i < DNumKeys(mp));
			node = DNodePtr(mp, i);

			if ((rc = fg_PageGet(_pCursor, DNodePgno(node), &mp, nullptr)) != 0)
				return rc;

			_pCursor->mc_ki[_pCursor->mc_top] = i;
			if ((rc = fg_CursorPush(_pCursor, mp)))
				return rc;

		ready:
			if (_Flags & DMdbPsModify)
			{
				if ((rc = fg_PageTouch(_pCursor)) != 0)
					return rc;
				mp = _pCursor->mc_pg[_pCursor->mc_top];
			}
		}

		if (!DIsLeaf(mp))
		{
			DMibDatabaseTrace("internal error, index points to a {nfh,sf0,sj2} page!?", mp->mp_flags);
			_pCursor->mc_txn->mt_flags |= DMdbTxnError;
			return DMdbCorrupted;
		}

		DMibDatabaseTrace("found leaf page {} for key [{}]", mp->mp_p.p_pgno, _pKey ? DKey(_pKey) : "null");
		_pCursor->mc_flags |= DCInitialized;
		_pCursor->mc_flags &= ~DCEof;

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
		CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top];
		CMdbNode *node = DNodePtr(mp, 0);
		int rc;

		if ((rc = fg_PageGet(_pCursor, DNodePgno(node), &mp, nullptr)) != 0)
			return rc;

		_pCursor->mc_ki[_pCursor->mc_top] = 0;
		if ((rc = fg_CursorPush(_pCursor, mp)))
			return rc;
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
		int rc;
		CPgno root;

		/* Make sure the txn is still viable, then find the root from
		 * the txn's db table and set it as the root of the cursor's stack.
		 */
		_pCursor->mc_txn->f_CheckBlocked();

		{
			/* Make sure we're using an up-to-date root */
			if (*_pCursor->mc_dbflag & DDbStale)
			{
				CMdbCursor mc2;
				if (DTxnDbiChanged(_pCursor->mc_txn, _pCursor->mc_dbi))
					DMibError("Stale write transaction");
				fg_CursorInit(&mc2, _pCursor->mc_txn, DMainDbi, nullptr);
				rc = fg_PageSearch(&mc2, &_pCursor->mc_dbx->m_DatabaseName, 0);
				if (rc)
					return rc;
				{
					CDatabaseValue data;
					int exact = 0;
					uint16 flags;
					CMdbNode *leaf = fg_NodeSearch(&mc2, &_pCursor->mc_dbx->m_DatabaseName, &exact);
					if (!exact)
						return DMdbNotFound;
					if ((leaf->mn_flags & (DFDupData | DFSubData)) != DFSubData)
						return DMdbIncompatible; // not a named DB
					rc = fg_NodeRead(&mc2, leaf, &data);
					if (rc)
						return rc;
					memcpy(&flags, ((char *)data.m_pData + offsetof(CMdbDb, md_flags)), sizeof(uint16));
					/* The txn may not know this DBI, or another process may
					 * have dropped and recreated the DB with other flags.
					 */
					if ((_pCursor->mc_db->md_flags & DPersistentFlags) != flags)
						return DMdbIncompatible;
					memcpy(_pCursor->mc_db, data.m_pData, sizeof(CMdbDb));
				}
				*_pCursor->mc_dbflag &= ~DDbStale;
			}
			root = _pCursor->mc_db->md_root;

			if (root == DPInvalid)
			{ // Tree is empty.
				DPuts("tree is empty");
				return DMdbNotFound;
			}
		}

		DMibCheck(root > 1);
		if (!_pCursor->mc_pg[0] || _pCursor->mc_pg[0]->mp_p.p_pgno != root)
		{
#ifdef DMibDatabase_Always64BitSupport
			if (_pCursor->mc_pg[0])
				DMdbPageUnref(_pCursor->mc_txn, _pCursor->mc_pg[0]);
#endif
			if ((rc = fg_PageGet(_pCursor, root, &_pCursor->mc_pg[0], nullptr)) != 0)
				return rc;
		}

#ifdef DMibDatabase_Always64BitSupport
		{
			int i;
			for (i = 1; i < _pCursor->mc_snum; i++)
				DMdbPageUnref(_pCursor->mc_txn, _pCursor->mc_pg[i]);
		}
#endif
		_pCursor->mc_snum = 1;
		_pCursor->mc_top = 0;

		DMibDatabaseTrace("db {} root page {} has flags 0x{nfh,sf0,sj4}", DDbi(_pCursor), root, _pCursor->mc_pg[0]->mp_flags);

		if (_Flags & DMdbPsModify)
		{
			if ((rc = fg_PageTouch(_pCursor)))
				return rc;
		}

		if (_Flags & DMdbPsRootonly)
			return DMdbSuccess;

		return fg_PageSearchRoot(_pCursor, _pKey, _Flags);
	}

	int NPrivate::fg_OvPageFree(CMdbCursor *_pCursor, CMdbPage *_pPage)
	{
		CMdbTxn *txn = _pCursor->mc_txn;
		CPgno pg = _pPage->mp_p.p_pgno;
		unsigned x = 0, ovpages = _pPage->mp_pages;
		CMdbEnv *env = txn->mt_env;
		CMdbIdl sl = txn->mt_spill_pgs;
		CMdbId pn = pg << 1;

		DMibDatabaseTrace("free ov page {} ({})", pg, ovpages);
		/* If the page is dirty or on the spill list we just acquired it,
		 * so we should give it back to our current free list, if any.
		 * Otherwise put it onto the list of pages we freed in this txn.
		 *
		 * Won't create me_pgstate.mf_pghead: me_pgstate.mf_pglast must be inited along with it.
		 * Unsupported in nested txns: They would need to hide the page
		 * range in ancestor txns' dirty and spilled lists.
		 */
		if (env->me_pgstate.mf_pghead && !txn->mt_parent && ((_pPage->mp_flags & DPDirty) || (sl && (x = fg_MidlSearch(sl, pn)) <= sl[0] && sl[x] == pn)))
		{
			unsigned i, j;
			CPgno *mop;
			CMdbId2 *dl, ix, iy;
			fg_MidlNeed(&env->me_pgstate.mf_pghead, ovpages);
			if (!(_pPage->mp_flags & DPDirty))
			{
				/* This page is no longer spilled */
				if (x == sl[0])
					sl[0]--;
				else
					sl[x] |= 1;
				goto release;
			}
			/* Remove from dirty list */
			dl = txn->mt_u.dirty_list;
			x = dl[0].mid--;
			for (ix = dl[x]; ix.mptr != _pPage; ix = iy)
			{
				if (x > 1)
				{
					x--;
					iy = dl[x];
					dl[x] = ix;
				}
				else
				{
					DMibCheck(x > 1);
					j = ++(dl[0].mid);
					dl[j] = ix; // Unsorted. OK when DMdbTxnError.
					txn->mt_flags |= DMdbTxnError;
					return DMdbProblem;
				}
			}
			txn->mt_dirty_room++;
			if (!(env->me_flags & EDatabaseEnvironmentFlag_WriteMap))
				env->f_DPageFree(_pPage);
		release:
			/* Insert in me_pgstate.mf_pghead */
			mop = env->me_pgstate.mf_pghead;
			j = mop[0] + ovpages;
			for (i = mop[0]; i && mop[i] < pg; i--)
				mop[j--] = mop[i];
			while (j > i)
				mop[j--] = pg++;
			mop[0] += ovpages;
		}
		else
			fg_MidlAppendRange(&txn->mt_free_pgs, pg, ovpages);
#ifdef DMibDatabase_Always64BitSupport
		if (_pCursor->mc_ovpg == _pPage)
			_pCursor->mc_ovpg = NULL;
#endif
		_pCursor->mc_db->md_overflow_pages -= ovpages;
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
		CMdbPage *np;
		int rc;

		if ((rc = fg_PageAlloc(_pCursor, _nPages, &np)))
			return rc;
		DMibDatabaseTrace("allocated new mpage {}, page size {}", np->mp_p.p_pgno, _pCursor->mc_txn->mt_env->me_psize);
		np->mp_flags = _Flags | DPDirty;
		np->mp_lower = (DPageHeaderSize - DPageBase);
		np->mp_upper = _pCursor->mc_txn->mt_env->me_psize - DPageBase;

		if (DIsBranch(np))
			_pCursor->mc_db->md_branch_pages++;
		else if (DIsLeaf(np))
			_pCursor->mc_db->md_leaf_pages++;
		else if (DIsOverflow(np))
		{
			_pCursor->mc_db->md_overflow_pages += _nPages;
			np->mp_pages = _nPages;
		}
		*o_pPage = np;

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
		CMdbPage *psrc, *pdst;
		CMdbNode *srcnode;
		CDatabaseValue key, data;
		unsigned nkeys;
		int rc;
		CIndx i, j;

		psrc = _pSourceCursor->mc_pg[_pSourceCursor->mc_top];
		pdst = _pDestinationCursor->mc_pg[_pDestinationCursor->mc_top];

		DMibDatabaseTrace("merging page {} into {}", psrc->mp_p.p_pgno, pdst->mp_p.p_pgno);

		DMibCheck(_pSourceCursor->mc_snum > 1); // can't merge root page
		DMibCheck(_pDestinationCursor->mc_snum > 1);

		/* Mark dst as dirty. */
		if ((rc = fg_PageTouch(_pDestinationCursor)))
			return rc;

		/* get dst page again now that we've touched it. */
		pdst = _pDestinationCursor->mc_pg[_pDestinationCursor->mc_top];

		/* Move all nodes from src to dst.
		*/
		j = nkeys = DNumKeys(pdst);
		if (DIsLeaf2(psrc))
		{
			key.m_Size = _pSourceCursor->mc_db->md_pad;
			key.m_pData = DMetaData(psrc);
			for (i = 0; i < DNumKeys(psrc); i++, j++)
			{
				rc = fg_NodeAdd(_pDestinationCursor, j, &key, nullptr, 0, 0);
				if (rc != DMdbSuccess)
					return rc;
				key.m_pData = (uint8 *)key.m_pData + key.m_Size;
			}
		}
		else
		{
			for (i = 0; i < DNumKeys(psrc); i++, j++)
			{
				srcnode = DNodePtr(psrc, i);
				if (i == 0 && DIsBranch(psrc))
				{
					CMdbCursor mn;
					CMdbNode *s2;
					fg_CursorCopy(_pSourceCursor, &mn);
					mn.mc_xcursor = nullptr;
					/* must find the lowest key below src */
					rc = fg_PageSearchLowest(&mn);
					if (rc)
						return rc;
					if (DIsLeaf2(mn.mc_pg[mn.mc_top]))
					{
						key.m_Size = mn.mc_db->md_pad;
						key.m_pData = DLeaf2Key(mn.mc_pg[mn.mc_top], 0, key.m_Size);
					}
					else
					{
						s2 = DNodePtr(mn.mc_pg[mn.mc_top], 0);
						key.m_Size = DNodeKeySize(s2);
						key.m_pData = DNodeKey(s2);
					}
				}
				else
				{
					key.m_Size = srcnode->mn_ksize;
					key.m_pData = DNodeKey(srcnode);
				}

				data.m_Size = DNodeDataSize(srcnode);
				data.m_pData = (uint8 *)(CMdbPage *)DNodeData(srcnode);
				rc = fg_NodeAdd(_pDestinationCursor, j, &key, &data, DNodePgno(srcnode), srcnode->mn_flags);
				if (rc != DMdbSuccess)
					return rc;
			}
		}

		DMibDatabaseTrace("dst page {} now has {} keys ({fe1} filled)", pdst->mp_p.p_pgno, DNumKeys(pdst), (float)DPageFill(_pDestinationCursor->mc_txn->mt_env, pdst) / 10);

		/* Unlink the src page from parent and add to free list.
		*/
		_pSourceCursor->mc_top--;
		fg_NodeDel(_pSourceCursor, 0);
		if (_pSourceCursor->mc_ki[_pSourceCursor->mc_top] == 0)
		{
			key.m_Size = 0;
			rc = fg_UpdateKey(_pSourceCursor, &key);
			if (rc)
			{
				_pSourceCursor->mc_top++;
				return rc;
			}
		}
		_pSourceCursor->mc_top++;

		psrc = _pSourceCursor->mc_pg[_pSourceCursor->mc_top];
		/* If not operating on FreeDB, allow this page to be reused
		 * in this txn. Otherwise just add to free list.
		 */
		rc = fg_PageLoose(_pSourceCursor, psrc);
		if (rc)
			return rc;
		if (DIsLeaf(psrc))
			_pSourceCursor->mc_db->md_leaf_pages--;
		else
			_pSourceCursor->mc_db->md_branch_pages--;
		{
			/* Adjust other cursors pointing to mp */
			CMdbCursor *m2, *m3;
			CDatabaseSubID dbi = _pSourceCursor->mc_dbi;
			uint32 top = _pSourceCursor->mc_top;

			for (m2 = _pSourceCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
			{
				if (_pSourceCursor->mc_flags & DCSub)
					m3 = &m2->mc_xcursor->mx_cursor;
				else
					m3 = m2;
				if (m3 == _pSourceCursor)
					continue;
				if (m3->mc_snum < _pSourceCursor->mc_snum)
					continue;
				if (m3->mc_pg[top] == psrc)
				{
					m3->mc_pg[top] = pdst;
					m3->mc_ki[top] += nkeys;
					m3->mc_ki[top - 1] = _pDestinationCursor->mc_ki[top - 1];
				}
				else if (m3->mc_pg[top - 1] == _pSourceCursor->mc_pg[top - 1] && m3->mc_ki[top - 1] > _pSourceCursor->mc_ki[top - 1])
					m3->mc_ki[top - 1]--;
				if (DIsLeaf(psrc))
					DXCursorRefresh(m3, top, m3->mc_pg[top]);
			}
		}
		{
			uint32 snum = _pDestinationCursor->mc_snum;
			uint16 depth = _pDestinationCursor->mc_db->md_depth;
			fg_CursorPop(_pDestinationCursor);
			rc = fg_Rebalance(_pDestinationCursor);
			/* Did the tree height change? */
			if (depth != _pDestinationCursor->mc_db->md_depth)
				snum += _pDestinationCursor->mc_db->md_depth - depth;
			_pDestinationCursor->mc_snum = snum;
			_pDestinationCursor->mc_top = snum - 1;
		}
		return rc;
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
		uint32 flags;
		int rc = DMdbSuccess, new_root = 0, did_split = 0;
		CIndx newindx;
		CPgno pgno = 0;
		int i, j, split_indx, nkeys, pmax;
		CMdbEnv *env = _pCursor->mc_txn->mt_env;
		CMdbNode *node;
		CDatabaseValue sepkey, rkey, xdata, *rdata = &xdata;
		CMdbPage *copy = nullptr;
		CMdbPage *mp, *rp, *pp;
		int ptop;
		CMdbCursor mn;
		DKBuf;

		mp = _pCursor->mc_pg[_pCursor->mc_top];
		newindx = _pCursor->mc_ki[_pCursor->mc_top];
		nkeys = DNumKeys(mp);

		DMibDatabaseTrace
			(
				"-----> splitting {} page {} and adding [{}] at index {}/{}"
				, DIsLeaf(mp) ? "leaf" : "branch"
				, mp->mp_p.p_pgno
				, DKey(_pNewKey)
				, _pCursor->mc_ki[_pCursor->mc_top]
				, nkeys
			)
		;

		/* Create a right sibling. */
		if ((rc = fg_PageNew(_pCursor, mp->mp_flags, 1, &rp)))
			return rc;
		rp->mp_pad = mp->mp_pad;
		DMibDatabaseTrace("new right sibling: page {}", rp->mp_p.p_pgno);

		/* Usually when splitting the root page, the cursor
		 * height is 1. But when called from fg_UpdateKey,
		 * the cursor height may be greater because it walks
		 * up the stack while finding the branch slot to update.
		 */
		if (_pCursor->mc_top < 1)
		{
			if ((rc = fg_PageNew(_pCursor, DPBranch, 1, &pp)))
				goto done;
			/* shift current top to make room for new parent */
			for (i = _pCursor->mc_snum; i > 0; i--)
			{
				_pCursor->mc_pg[i] = _pCursor->mc_pg[i - 1];
				_pCursor->mc_ki[i] = _pCursor->mc_ki[i - 1];
			}
			_pCursor->mc_pg[0] = pp;
			_pCursor->mc_ki[0] = 0;
			_pCursor->mc_db->md_root = pp->mp_p.p_pgno;
			DMibDatabaseTrace("root split! new root = {}", pp->mp_p.p_pgno);
			new_root = _pCursor->mc_db->md_depth++;

			/* Add left (implicit) pointer. */
			if ((rc = fg_NodeAdd(_pCursor, 0, nullptr, nullptr, mp->mp_p.p_pgno, 0)) != DMdbSuccess)
			{
				/* undo the pre-push */
				_pCursor->mc_pg[0] = _pCursor->mc_pg[1];
				_pCursor->mc_ki[0] = _pCursor->mc_ki[1];
				_pCursor->mc_db->md_root = mp->mp_p.p_pgno;
				_pCursor->mc_db->md_depth--;
				goto done;
			}
			_pCursor->mc_snum++;
			_pCursor->mc_top++;
			ptop = 0;
		}
		else
		{
			ptop = _pCursor->mc_top - 1;
			DMibDatabaseTrace("parent branch page is {}", _pCursor->mc_pg[ptop]->mp_p.p_pgno);
		}

		fg_CursorCopy(_pCursor, &mn);
		mn.mc_xcursor = nullptr;
		mn.mc_pg[mn.mc_top] = rp;
		mn.mc_ki[ptop] = _pCursor->mc_ki[ptop] + 1;

		if (_NodeFlags & EDatabaseWriteFlag_Append)
		{
			mn.mc_ki[mn.mc_top] = 0;
			sepkey = *_pNewKey;
			split_indx = newindx;
			nkeys = 0;
		}
		else
		{
			split_indx = (nkeys + 1) / 2;

			if (DIsLeaf2(rp))
			{
				uint8 *split, *ins;
				int x;
				uint32 lsize, rsize, ksize;
				/* Move half of the keys to the right sibling */
				x = _pCursor->mc_ki[_pCursor->mc_top] - split_indx;
				ksize = _pCursor->mc_db->md_pad;
				split = DLeaf2Key(mp, split_indx, ksize);
				rsize = (nkeys - split_indx) * ksize;
				lsize = (nkeys - split_indx) * sizeof(CIndx);
				mp->mp_lower -= lsize;
				rp->mp_lower += lsize;
				mp->mp_upper += rsize - lsize;
				rp->mp_upper -= rsize - lsize;
				sepkey.m_Size = ksize;
				if (newindx == split_indx)
					sepkey.m_pData = _pNewKey->m_pData;
				else
					sepkey.m_pData = split;
				if (x < 0)
				{
					ins = DLeaf2Key(mp, _pCursor->mc_ki[_pCursor->mc_top], ksize);
					memcpy(rp->mp_ptrs, split, rsize);
					sepkey.m_pData = (uint8 *)rp->mp_ptrs;
					memmove(ins + ksize, ins, (split_indx - _pCursor->mc_ki[_pCursor->mc_top]) * ksize);
					memcpy(ins, _pNewKey->m_pData, ksize);
					mp->mp_lower += sizeof(CIndx);
					mp->mp_upper -= CIndx(ksize - sizeof(CIndx));
				}
				else
				{
					if (x)
						memcpy(rp->mp_ptrs, split, x * ksize);
					ins = DLeaf2Key(rp, x, ksize);
					memcpy(ins, _pNewKey->m_pData, ksize);
					memcpy(ins + ksize, split + x * ksize, rsize - x * ksize);
					rp->mp_lower += sizeof(CIndx);
					rp->mp_upper -= CIndx(ksize - sizeof(CIndx));
					_pCursor->mc_ki[_pCursor->mc_top] = x;
				}
			}
			else
			{
				int psize, nsize, k;
				/* Maximum free space in an empty page */
				pmax = env->me_psize - DPageHeaderSize;
				if (DIsLeaf(mp))
					nsize = env->f_LeafSize(_pNewKey, _pNewData);
				else
					nsize = env->f_BranchSize(_pNewKey);
				nsize = DEven(nsize);

				/* grab a page to hold a temporary copy */
				copy = fg_PageMalloc(_pCursor->mc_txn, 1);
				if (copy == nullptr)
				{
					rc = ENOMEM;
					goto done;
				}
				copy->mp_p.p_pgno = mp->mp_p.p_pgno;
				copy->mp_flags = mp->mp_flags;
				copy->mp_lower = (DPageHeaderSize - DPageBase);
				copy->mp_upper = env->me_psize - DPageBase;

				/* prepare to insert */
				for (i = 0, j = 0; i < nkeys; i++)
				{
					if (i == newindx)
						copy->mp_ptrs[j++] = 0;
					copy->mp_ptrs[j++] = mp->mp_ptrs[i];
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
				if (nkeys < 32 || nsize > pmax / 16 || newindx >= nkeys)
				{
					/* Find split point */
					psize = 0;
					if (newindx <= split_indx || newindx >= nkeys)
					{
						i = 0;
						j = 1;
						k = newindx >= nkeys ? nkeys : split_indx + 1 + DIsLeaf(mp);
					}
					else
					{
						i = nkeys;
						j = -1;
						k = split_indx - 1;
					}
					for (; i != k; i += j)
					{
						if (i == newindx)
						{
							psize += nsize;
							node = nullptr;
						}
						else
						{
							node = (CMdbNode *)((char *)mp + copy->mp_ptrs[i] + DPageBase);
							psize += DNodeSize + DNodeKeySize(node) + sizeof(CIndx);
							if (DIsLeaf(mp))
							{
								if (DFIsSet(node->mn_flags, DFBigData))
									psize += sizeof(CPgno);
								else
									psize += DNodeDataSize(node);
							}
							psize = DEven(psize);
						}
						if (psize > pmax || i == k - j)
						{
							split_indx = i + (j < 0);
							break;
						}
					}
				}
				if (split_indx == newindx)
				{
					sepkey.m_Size = _pNewKey->m_Size;
					sepkey.m_pData = _pNewKey->m_pData;
				}
				else
				{
					node = (CMdbNode *)((char *)mp + copy->mp_ptrs[split_indx] + DPageBase);
					sepkey.m_Size = node->mn_ksize;
					sepkey.m_pData = DNodeKey(node);
				}
			}
		}

		DMibDatabaseTrace("separator is {} [{}]", split_indx, DKey(&sepkey));

		/* Copy separator key to the parent.
		*/
		if (DSizeLeft(mn.mc_pg[ptop]) < env->f_BranchSize(&sepkey))
		{
			int snum = _pCursor->mc_snum;
			mn.mc_snum--;
			mn.mc_top--;
			did_split = 1;
			/* We want other splits to find mn when doing fixups */
			DWithCursorTracking(mn, rc = fg_PageSplit(&mn, &sepkey, nullptr, rp->mp_p.p_pgno, 0));
			if (rc)
				goto done;

			/* root split? */
			if (_pCursor->mc_snum > snum)
				ptop++;
			/* Right page might now have changed parent.
			 * Check if left page also changed parent.
			 */
			if (mn.mc_pg[ptop] != _pCursor->mc_pg[ptop] && _pCursor->mc_ki[ptop] >= DNumKeys(_pCursor->mc_pg[ptop]))
			{
				for (i = 0; i < ptop; i++)
				{
					_pCursor->mc_pg[i] = mn.mc_pg[i];
					_pCursor->mc_ki[i] = mn.mc_ki[i];
				}
				_pCursor->mc_pg[ptop] = mn.mc_pg[ptop];
				if (mn.mc_ki[ptop])
					_pCursor->mc_ki[ptop] = mn.mc_ki[ptop] - 1;
				else
				{
					/* find right page's left sibling */
					_pCursor->mc_ki[ptop] = mn.mc_ki[ptop];
					rc = fg_CursorSibling(_pCursor, 0);
				}
			}
		}
		else
		{
			mn.mc_top--;
			rc = fg_NodeAdd(&mn, mn.mc_ki[ptop], &sepkey, nullptr, rp->mp_p.p_pgno, 0);
			mn.mc_top++;
		}
		if (rc != DMdbSuccess)
		{
			if (rc == DMdbNotFound) // improper fg_CursorSibling() result
				rc = DMdbProblem;
			goto done;
		}
		if (_NodeFlags & EDatabaseWriteFlag_Append)
		{
			_pCursor->mc_pg[_pCursor->mc_top] = rp;
			_pCursor->mc_ki[_pCursor->mc_top] = 0;
			rc = fg_NodeAdd(_pCursor, 0, _pNewKey, _pNewData, _NewPageNumber, _NodeFlags);
			if (rc)
				goto done;
			for (i = 0; i < _pCursor->mc_top; i++)
				_pCursor->mc_ki[i] = mn.mc_ki[i];
		}
		else if (!DIsLeaf2(mp))
		{
			DMibFastCheck(copy);
			/* Move nodes */
			_pCursor->mc_pg[_pCursor->mc_top] = rp;
			i = split_indx;
			j = 0;
			do
			{
				if (i == newindx)
				{
					rkey.m_pData = _pNewKey->m_pData;
					rkey.m_Size = _pNewKey->m_Size;
					if (DIsLeaf(mp))
						rdata = _pNewData;
					else
						pgno = _NewPageNumber;
					flags = _NodeFlags;
					/* Update index for the new key. */
					_pCursor->mc_ki[_pCursor->mc_top] = j;
				}
				else
				{
					node = (CMdbNode *)((char *)mp + copy->mp_ptrs[i] + DPageBase);
					rkey.m_pData = DNodeKey(node);
					rkey.m_Size = node->mn_ksize;
					if (DIsLeaf(mp))
					{
						xdata.m_pData = (uint8 *)(CMdbPage *)DNodeData(node);
						xdata.m_Size = DNodeDataSize(node);
						rdata = &xdata;
					}
					else
						pgno = DNodePgno(node);
					flags = node->mn_flags;
				}

				if (!DIsLeaf(mp) && j == 0)
				{
					/* First branch index doesn't need key data. */
					rkey.m_Size = 0;
				}

				rc = fg_NodeAdd(_pCursor, j, &rkey, rdata, pgno, flags);
				if (rc)
					goto done;
				if (i == nkeys)
				{
					i = 0;
					j = 0;
					_pCursor->mc_pg[_pCursor->mc_top] = copy;
				}
				else
				{
					i++;
					j++;
				}
			}
			while (i != split_indx);

			nkeys = DNumKeys(copy);
			for (i = 0; i < nkeys; i++)
				mp->mp_ptrs[i] = copy->mp_ptrs[i];
			mp->mp_lower = copy->mp_lower;
			mp->mp_upper = copy->mp_upper;
			memcpy(DNodePtr(mp, nkeys - 1), DNodePtr(copy, nkeys - 1), env->me_psize - copy->mp_upper - DPageBase);

			/* reset back to original page */
			if (newindx < split_indx)
				_pCursor->mc_pg[_pCursor->mc_top] = mp;
			else
			{
				_pCursor->mc_pg[_pCursor->mc_top] = rp;
				_pCursor->mc_ki[ptop]++;
				/* Make sure mc_ki is still valid.
				*/
				if (mn.mc_pg[ptop] != _pCursor->mc_pg[ptop] && _pCursor->mc_ki[ptop] >= DNumKeys(_pCursor->mc_pg[ptop]))
				{
					for (i = 0; i <= ptop; i++)
					{
						_pCursor->mc_pg[i] = mn.mc_pg[i];
						_pCursor->mc_ki[i] = mn.mc_ki[i];
					}
				}
			}
			if (_NodeFlags & EDatabaseWriteFlag_ReserveSpace)
			{
				node = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);
				if (!(node->mn_flags & DFBigData))
					_pNewData->m_pData = (uint8 *)(CMdbPage *)DNodeData(node);
			}
		}
		else
		{
			if (newindx >= split_indx)
			{
				_pCursor->mc_pg[_pCursor->mc_top] = rp;
				_pCursor->mc_ki[ptop]++;
				/* Make sure mc_ki is still valid.
				*/
				if (mn.mc_pg[ptop] != _pCursor->mc_pg[ptop] && _pCursor->mc_ki[ptop] >= DNumKeys(_pCursor->mc_pg[ptop]))
				{
					for (i = 0; i <= ptop; i++)
					{
						_pCursor->mc_pg[i] = mn.mc_pg[i];
						_pCursor->mc_ki[i] = mn.mc_ki[i];
					}
				}
			}
		}

		{
			/* Adjust other cursors pointing to mp */
			CMdbCursor *m2, *m3;
			CDatabaseSubID dbi = _pCursor->mc_dbi;
			nkeys = DNumKeys(mp);

			for (m2 = _pCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
			{
				if (_pCursor->mc_flags & DCSub)
					m3 = &m2->mc_xcursor->mx_cursor;
				else
					m3 = m2;
				if (m3 == _pCursor)
					continue;
				if (!(m2->mc_flags & m3->mc_flags & DCInitialized))
					continue;
				if (new_root)
				{
					int k;
					/* sub cursors may be on different DB */
					if (m3->mc_pg[0] != mp)
						continue;
					/* root split */
					for (k = new_root; k >= 0; k--)
					{
						m3->mc_ki[k + 1] = m3->mc_ki[k];
						m3->mc_pg[k + 1] = m3->mc_pg[k];
					}
					if (m3->mc_ki[0] >= nkeys)
						m3->mc_ki[0] = 1;
					else
						m3->mc_ki[0] = 0;
					m3->mc_pg[0] = _pCursor->mc_pg[0];
					m3->mc_snum++;
					m3->mc_top++;
				}
				if (m3->mc_top >= _pCursor->mc_top && m3->mc_pg[_pCursor->mc_top] == mp)
				{
					if (m3->mc_ki[_pCursor->mc_top] >= newindx && !(_NodeFlags & DMdbSplitReplace))
						m3->mc_ki[_pCursor->mc_top]++;
					if (m3->mc_ki[_pCursor->mc_top] >= nkeys)
					{
						m3->mc_pg[_pCursor->mc_top] = rp;
						m3->mc_ki[_pCursor->mc_top] -= nkeys;
						for (i = 0; i < _pCursor->mc_top; i++)
						{
							m3->mc_ki[i] = mn.mc_ki[i];
							m3->mc_pg[i] = mn.mc_pg[i];
						}
					}
				}
				else if (!did_split && m3->mc_top >= ptop && m3->mc_pg[ptop] == _pCursor->mc_pg[ptop] && m3->mc_ki[ptop] >= _pCursor->mc_ki[ptop])
					m3->mc_ki[ptop]++;
				if (DIsLeaf(mp))
					DXCursorRefresh(m3, _pCursor->mc_top, m3->mc_pg[_pCursor->mc_top]);
			}
		}
		DMibDatabaseTrace("mp left: {}, rp left: {}", DSizeLeft(mp), DSizeLeft(rp));

	done:
		if (copy) // tmp page
			env->f_PageFree(copy);
		if (rc)
			_pCursor->mc_txn->mt_flags |= DMdbTxnError;
		return rc;
	}
}
