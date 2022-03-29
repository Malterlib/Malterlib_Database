// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
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
	 * Set #MDB_TXN_ERROR on failure.
	 */
	NPrivate::MDB_page *NPrivate::mdb_page_malloc(MDB_txn *txn, unsigned num)
	{
		MDB_env *env = txn->mt_env;
		MDB_page *ret = env->me_dpages;
		mint psize = env->me_psize, sz = psize, off;
		/* For ! #EDatabaseEnvironmentFlag_NoMemoryInit, psize counts how much to init.
		 * For a single page alloc, we init everything after the page header.
		 * For multi-page, we init the final page; if the caller needed that
		 * many pages they will be filling in at least up to the last page.
		 */
		if (num == 1) {
			if (ret) {
				env->me_dpages = ret->mp_p.p_next;
				return ret;
			}
			psize -= off = PAGEHDRSZ;
		} else {
			sz *= num;
			off = sz - psize;
		}
		if ((ret = (MDB_page *)NMemory::fg_Alloc(sz)) != nullptr) {
			if (!(env->me_flags & EDatabaseEnvironmentFlag_NoMemoryInit)) {
				memset((char *)ret + off, 0, psize);
				ret->mp_pad = 0;
			}
		} else {
			txn->mt_flags |= MDB_TXN_ERROR;
		}
		return ret;
	}
	/** Free a single page.
	 * Saves single pages to a list, for future reuse.
	 * (This is not used for multi-page overflow pages.)
	 */
	void CDatabaseEnvironment_Internal::mdb_page_free(MDB_page *mp)
	{
		mp->mp_p.p_next = me_dpages;
		me_dpages = mp;
	}

	/** Free a dirty page */
	void CDatabaseEnvironment_Internal::mdb_dpage_free(MDB_page *dp)
	{
		if (!IS_OVERFLOW(dp) || dp->mp_pages == 1) {
			mdb_page_free(dp);
		} else {
			/* large pages just get freed directly */
			NMemory::fg_FreeNoSize(dp);
		}
	}

	/**	Return all dirty pages to dpage list */
	void NPrivate::mdb_dlist_free(MDB_txn *txn)
	{
		MDB_env *env = txn->mt_env;
		MDB_ID2L dl = txn->mt_u.dirty_list;
		unsigned i, n = dl[0].mid;

		for (i = 1; i <= n; i++) {
			env->mdb_dpage_free((MDB_page *)(dl[i].mptr));
		}
		dl[0].mid = 0;
	}

#ifdef DMibDatabase_Always64BitSupport
	void NPrivate::mdb_page_unref(MDB_txn *txn, MDB_page *mp)
	{
		pgno_t pgno;
		MDB_ID3L tl = txn->mt_rpages;
		unsigned x, rem;
		if (mp->mp_flags & (P_SUBP|P_DIRTY))
			return;
		rem = mp->mp_p.p_pgno & (MDB_RPAGE_CHUNK-1);
		pgno = mp->mp_p.p_pgno ^ rem;
		x = mdb_mid3l_search(tl, pgno);
		if (x != tl[0].mid && tl[x+1].mid == mp->mp_p.p_pgno)
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
	int NPrivate::mdb_page_loose(MDB_cursor *mc, MDB_page *mp)
	{
		int loose = 0;
		pgno_t pgno = mp->mp_p.p_pgno;
		MDB_txn *txn = mc->mc_txn;

		if ((mp->mp_flags & P_DIRTY) && mc->mc_dbi != FREE_DBI) {
			if (txn->mt_parent) {
				MDB_ID2 *dl = txn->mt_u.dirty_list;
				/* If txn has a parent, make sure the page is in our
				 * dirty list.
				 */
				if (dl[0].mid) {
					unsigned x = mdb_mid2l_search(dl, pgno);
					if (x <= dl[0].mid && dl[x].mid == pgno) {
						if (mp != dl[x].mptr) { /* bad cursor? */
							mc->mc_flags &= ~(C_INITIALIZED|C_EOF);
							txn->mt_flags |= MDB_TXN_ERROR;
							return MDB_PROBLEM;
						}
						/* ok, it's ours */
						loose = 1;
					}
				}
			} else {
				/* no parent txn, so it's just ours */
				loose = 1;
			}
		}
		if (loose) {
			DMibDatabaseTrace("loosen db {} page {}", DDBI(mc), mp->mp_p.p_pgno);
			NEXT_LOOSE_PAGE(mp) = txn->mt_loose_pgs;
			txn->mt_loose_pgs = mp;
			txn->mt_loose_count++;
			mp->mp_flags |= P_LOOSE;
		} else
			mdb_midl_append(&txn->mt_free_pgs, pgno);

		return MDB_SUCCESS;
	}

	/** Set or clear P_KEEP in dirty, non-overflow, non-sub pages watched by txn.
	 * @param[in] mc A cursor handle for the current operation.
	 * @param[in] pflags Flags of the pages to update:
	 * P_DIRTY to set P_KEEP, P_DIRTY|P_KEEP to clear it.
	 * @param[in] all No shortcuts. Needed except after a full #mdb_page_flush().
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_pages_xkeep(MDB_cursor *mc, unsigned pflags, int all)
	{
		enum { Mask = P_SUBP|P_DIRTY|P_LOOSE|P_KEEP };
		MDB_txn *txn = mc->mc_txn;
		MDB_cursor *m3, *m0 = mc;
		MDB_xcursor *mx;
		MDB_page *dp, *mp;
		MDB_node *leaf;
		unsigned i, j;
		int rc = MDB_SUCCESS, level;

		/* Mark pages seen by cursors: First m0, then tracked cursors */
		for (i = txn->mt_numdbs;; ) {
			if (mc->mc_flags & C_INITIALIZED) {
				for (m3 = mc;; m3 = &mx->mx_cursor) {
					mp = NULL;
					for (j=0; j<m3->mc_snum; j++) {
						mp = m3->mc_pg[j];
						if ((mp->mp_flags & Mask) == pflags)
							mp->mp_flags ^= P_KEEP;
					}
					mx = m3->mc_xcursor;
					/* Proceed to mx if it is at a sub-database */
					if (! (mx && (mx->mx_cursor.mc_flags & C_INITIALIZED)))
						break;
					if (! (mp && (mp->mp_flags & P_LEAF)))
						break;
					leaf = NODEPTR(mp, m3->mc_ki[j-1]);
					if (!(leaf->mn_flags & F_SUBDATA))
						break;
				}
			}
			mc = mc->mc_next;
			for (; !mc || mc == m0; mc = txn->mt_cursors[--i])
				if (i == 0)
					goto mark_done;
		}

	mark_done:
		if (all) {
			/* Mark dirty root pages */
			for (i=0; i<txn->mt_numdbs; i++) {
				if (txn->mt_dbflags[i] & DB_DIRTY) {
					pgno_t pgno = txn->mt_dbs[i].md_root;
					if (pgno == P_INVALID)
						continue;
					if ((rc = NPrivate::mdb_page_get(m0, pgno, &dp, &level)) != MDB_SUCCESS)
						break;
					if ((dp->mp_flags & Mask) == pflags && level <= 1)
						dp->mp_flags ^= P_KEEP;
				}
			}
		}

		return rc;
	}

	/**	Spill pages from the dirty list back to disk.
	 * This is intended to prevent running into #MDB_TXN_FULL situations,
	 * but note that they may still occur in a few cases:
	 *	1) our estimate of the txn size could be too small. Currently this
	 *	 seems unlikely, except with a large number of #EDatabaseWriteFlag_BatchDuplicateData items.
	 *	2) child txns may run out of space if their parents dirtied a
	 *	 lot of pages and never spilled them. TODO: we probably should do
	 *	 a preemptive spill during #mdb_txn_begin() of a child txn, if
	 *	 the parent's dirty_room is below a given threshold.
	 *
	 * Otherwise, if not using nested txns, it is expected that apps will
	 * not run into #MDB_TXN_FULL any more. The pages are flushed to disk
	 * the same way as for a txn commit, e.g. their P_DIRTY flag is cleared.
	 * If the txn never references them again, they can be left alone.
	 * If the txn only reads them, they can be used without any fuss.
	 * If the txn writes them again, they can be dirtied immediately without
	 * going thru all of the work of #mdb_page_touch(). Such references are
	 * handled by #mdb_page_unspill().
	 *
	 * Also note, we never spill DB root pages, nor pages of active cursors,
	 * because we'll need these back again soon anyway. And in nested txns,
	 * we can't spill a page in a child txn if it was already spilled in a
	 * parent txn. That would alter the parent txns' data even though
	 * the child hasn't committed yet, and we'd have no way to undo it if
	 * the child aborted.
	 *
	 * @param[in] m0 cursor A cursor handle identifying the transaction and
	 *	database for which we are checking space.
	 * @param[in] key For a put operation, the key being stored.
	 * @param[in] data For a put operation, the data being stored.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_spill(MDB_cursor *m0, CDatabaseValue *key, CDatabaseValue *data)
	{
		MDB_txn *txn = m0->mc_txn;
		MDB_page *dp;
		MDB_ID2L dl = txn->mt_u.dirty_list;
		uint32 i, j, need;
		int rc;

		if (m0->mc_flags & C_SUB)
			return MDB_SUCCESS;

		/* Estimate how much space this op will take */
		i = m0->mc_db->md_depth;
		/* Named DBs also dirty the main DB */
		if (m0->mc_dbi >= CORE_DBS)
			i += txn->mt_dbs[MAIN_DBI].md_depth;
		/* For puts, roughly factor in the key+data size */
		if (key)
			i += int((LEAFSIZE(key, data) + txn->mt_env->me_psize) / txn->mt_env->me_psize);
		i += i;	/* double it for good measure */
		need = i;

		if (txn->mt_dirty_room > i)
			return MDB_SUCCESS;

		if (!txn->mt_spill_pgs) {
			txn->mt_spill_pgs = mdb_midl_alloc(MDB_IDL_UM_MAX);
			if (!txn->mt_spill_pgs)
				return ENOMEM;
		} else {
			/* purge deleted slots */
			MDB_IDL sl = txn->mt_spill_pgs;
			uint32 num = sl[0];
			j=0;
			for (i=1; i<=num; i++) {
				if (!(sl[i] & 1))
					sl[++j] = sl[i];
			}
			sl[0] = j;
		}

		/* Preserve pages which may soon be dirtied again */
		if ((rc = mdb_pages_xkeep(m0, P_DIRTY, 1)) != MDB_SUCCESS)
			goto done;

		/* Less aggressive spill - we originally spilled the entire dirty list,
		 * with a few exceptions for cursor pages and DB root pages. But this
		 * turns out to be a lot of wasted effort because in a large txn many
		 * of those pages will need to be used again. So now we spill only 1/8th
		 * of the dirty pages. Testing revealed this to be a good tradeoff,
		 * better than 1/2, 1/4, or 1/10.
		 */
		if (need < MDB_IDL_UM_MAX / 8)
			need = MDB_IDL_UM_MAX / 8;

		/* Save the page IDs of all the pages we're flushing */
		/* flush from the tail forward, this saves a lot of shifting later on. */
		for (i=dl[0].mid; i && need; i--) {
			MDB_ID pn = dl[i].mid << 1;
			dp = (MDB_page *)(dl[i].mptr);
			if (dp->mp_flags & (P_LOOSE|P_KEEP))
				continue;
			/* Can't spill twice, make sure it's not already in a parent's
			 * spill list.
			 */
			if (txn->mt_parent) {
				MDB_txn *tx2;
				for (tx2 = txn->mt_parent; tx2; tx2 = tx2->mt_parent) {
					if (tx2->mt_spill_pgs) {
						j = mdb_midl_search(tx2->mt_spill_pgs, pn);
						if (j <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[j] == pn) {
							dp->mp_flags |= P_KEEP;
							break;
						}
					}
				}
				if (tx2)
					continue;
			}
			mdb_midl_append(&txn->mt_spill_pgs, pn);
			need--;
		}
		mdb_midl_sort(txn->mt_spill_pgs);

		/* Flush the spilled part of dirty list */
		if ((rc = mdb_page_flush(txn, i)) != MDB_SUCCESS)
			goto done;

		/* Reset any dirty pages we kept that page_flush didn't see */
		rc = mdb_pages_xkeep(m0, P_DIRTY|P_KEEP, i);

	done:
		txn->mt_flags |= rc ? MDB_TXN_ERROR : MDB_TXN_SPILLS;
		return rc;
	}

	/** Find oldest txnid still referenced. Expects txn->mt_txnid > 0. */
	NPrivate::CTransactionID NPrivate::mdb_find_oldest(MDB_txn *txn)
	{
		int i;
		CTransactionID mr, oldest = txn->mt_txnid - 1;
		if (txn->mt_env->m_pLocksMemoryMap) {
			MDB_reader *r = txn->mt_env->m_pLocksMemoryMap->m_Readers;
			for (i = txn->mt_env->m_pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::EMemoryOrder_Relaxed); --i >= 0; ) {
				if (r[i].m_OwningProcessID.f_Load(NAtomic::EMemoryOrder_Acquire)) {
					mr = r[i].m_BeginReadTransactionID.f_Load(NAtomic::EMemoryOrder_Relaxed);
					if (oldest > mr)
						oldest = mr;
				}
			}
		}
		return oldest;
	}

	/** Add a page to the txn's dirty list */
	void NPrivate::mdb_page_dirty(MDB_txn *txn, MDB_page *mp)
	{
		MDB_ID2 mid;
		[[maybe_unused]] int rc;
		int (*insert)(MDB_ID2L, MDB_ID2 *);

		if (txn->mt_flags & MDB_TXN_WRITEMAP) {
			insert = mdb_mid2l_append;
		} else {
			insert = mdb_mid2l_insert;
		}
		mid.mid = mp->mp_p.p_pgno;
		mid.mptr = mp;
		rc = insert(txn->mt_u.dirty_list, &mid);
		DMibCheck(rc == 0);
		txn->mt_dirty_room--;
	}

	/** Allocate page numbers and memory for writing.  Maintain me_pgstate.mf_pglast,
	 * me_pghead and mt_next_pgno.  Set #MDB_TXN_ERROR on failure.
	 *
	 * If there are free pages available from older transactions, they
	 * are re-used first. Otherwise allocate a new page at mt_next_pgno.
	 * Do not modify the freedB, just merge freeDB records into me_pgstate.mf_pghead[]
	 * and move me_pgstate.mf_pglast to say which records were consumed.  Only this
	 * function can create me_pgstate.mf_pghead and move me_pgstate.mf_pglast/mt_next_pgno.
	 * When #MDB_DEVEL & 2, it is not affected by #mdb_freelist_save(): it
	 * then uses the transaction's original snapshot of the freeDB.
	 * @param[in] mc cursor A cursor handle identifying the transaction and
	 *	database for which we are allocating.
	 * @param[in] num the number of pages to allocate.
	 * @param[out] mp Address of the allocated page(s). Requests for multiple pages
	 *  will always be satisfied by a single contiguous chunk of memory.
	 * @return 0 on success, non-zero on failure.
	 */
	int
	NPrivate::mdb_page_alloc(MDB_cursor *mc, int num, MDB_page **mp)
	{
	#ifdef MDB_PARANOID	/* Seems like we can ignore this now */
		/* Get at most <Max_retries> more freeDB records once me_pgstate.mf_pghead
		 * has enough pages.  If not enough, use new pages from the map.
		 * If <Paranoid> and mc is updating the freeDB, only get new
		 * records if me_pgstate.mf_pghead is empty. Then the freelist cannot play
		 * catch-up with itself by growing while trying to save it.
		 */
		enum { Paranoid = 1, Max_retries = 500 };
	#else
		enum { Paranoid = 0, Max_retries = INT_MAX /*infinite*/ };
	#endif
		int rc, retry = num * 60;
		MDB_txn *txn = mc->mc_txn;
		MDB_env *env = txn->mt_env;
		pgno_t pgno, *mop = env->me_pgstate.mf_pghead;
		unsigned i, j, mop_len = mop ? mop[0] : 0, n2 = num-1;
		MDB_page *np;
		CTransactionID oldest = 0, last;
		EDatabaseCursorGet op;
		MDB_cursor m2;
		int found_old = 0;

		/* If there are any loose pages, just use them */
		if (num == 1 && txn->mt_loose_pgs) {
			np = txn->mt_loose_pgs;
			txn->mt_loose_pgs = NEXT_LOOSE_PAGE(np);
			txn->mt_loose_count--;
			DMibDatabaseTrace("db {} use loose page {}", DDBI(mc), np->mp_p.p_pgno);
			*mp = np;
			return MDB_SUCCESS;
		}

		*mp = nullptr;

		/* If our dirty list is already full, we can't do anything */
		if (txn->mt_dirty_room == 0) {
			rc = MDB_TXN_FULL;
			goto fail;
		}

		for (op = MDB_FIRST;; op = MDB_NEXT) {
			CDatabaseValue key, data;
			MDB_node *leaf;
			pgno_t *idl;

			/* Seek a big enough contiguous page range. Prefer
			 * pages at the tail, just truncating the list.
			 */
			if (mop_len > n2) {
				i = mop_len;
				do {
					pgno = mop[i];
					if (mop[i-n2] == pgno+n2)
						goto search_done;
				} while (--i > n2);
				if (--retry < 0)
					break;
			}

			if (op == MDB_FIRST) {	/* 1st iteration */
				/* Prepare to fetch more and coalesce */
				last = env->me_pgstate.mf_pglast;
				oldest = env->me_pgoldest;
				NPrivate::mdb_cursor_init(&m2, txn, FREE_DBI, nullptr);
	#if (MDB_DEVEL) & 2	/* "& 2" so MDB_DEVEL=1 won't hide bugs breaking freeDB */
				/* Use original snapshot. TODO: Should need less care in code
				 * which modifies the database. Maybe we can delete some code?
				 */
				m2.mc_flags |= C_ORIG_RDONLY;
				m2.mc_db = &env->me_metas[(txn->mt_txnid-1) & 1]->mm_dbs[FREE_DBI];
				m2.mc_dbflag = (unsigned char *)""; /* probably unnecessary */
	#endif
				if (last) {
					op = MDB_SET_RANGE;
					key.m_pData = (uint8 *)&last; /* will look up last+1 */
					key.m_Size = sizeof(last);
				}
				if (Paranoid && mc->mc_dbi == FREE_DBI)
					retry = -1;
			}
			if (Paranoid && retry < 0 && mop_len)
				break;

			last++;
			/* Do not fetch more if the record will be too recent */
			if (oldest <= last) {
				if (!found_old) {
					oldest = mdb_find_oldest(txn);
					env->me_pgoldest = oldest;
					found_old = 1;
				}
				if (oldest <= last)
					break;
			}
			rc = mdb_cursor_get(&m2, &key, nullptr, op);
			if (rc) {
				if (rc == MDB_NOTFOUND)
					break;
				goto fail;
			}
			last = *(CTransactionID*)key.m_pData;
			if (oldest <= last) {
				if (!found_old) {
					oldest = mdb_find_oldest(txn);
					env->me_pgoldest = oldest;
					found_old = 1;
				}
				if (oldest <= last)
					break;
			}
			np = m2.mc_pg[m2.mc_top];
			leaf = NODEPTR(np, m2.mc_ki[m2.mc_top]);
			if ((rc = NPrivate::mdb_node_read(&m2, leaf, &data)) != MDB_SUCCESS)
				goto fail;

			idl = (MDB_ID *) data.m_pData;
			i = idl[0];
			if (!mop) {
				if (!(env->me_pgstate.mf_pghead = mop = mdb_midl_alloc(i))) {
					rc = ENOMEM;
					goto fail;
				}
			} else {
				mdb_midl_need(&env->me_pgstate.mf_pghead, i);
				mop = env->me_pgstate.mf_pghead;
			}
			env->me_pgstate.mf_pglast = last;
	#if (DMibDatabaseDebug) > 1
			DMibDatabaseTrace("IDL read txn {} root {} num {}", last, txn->mt_dbs[FREE_DBI].md_root, i);
			for (j = i; j; j--)
				DMibDatabaseTrace("IDL {}", idl[j]);
	#endif
			/* Merge in descending sorted order */
			mdb_midl_xmerge(mop, idl);
			mop_len = mop[0];
		}

		/* Use new pages from the map when nothing suitable in the freeDB */
		i = 0;
		pgno = txn->mt_next_pgno;
		if (pgno + num >= env->me_maxpg) {
				DPUTS("DB size maxed out");
				rc = MDB_MAP_FULL;
				goto fail;
		}
	#if defined(DPlatformFamily_Windows) && !defined(DMibDatabase_Always64BitSupport)
		if (!(env->me_flags & EDatabaseEnvironmentFlag_ReadOnly)) {
			void *p;
			p = (MDB_page *)(env->me_map + env->me_psize * pgno);
			p = VirtualAlloc(p, env->me_psize * num, MEM_COMMIT,
				(env->me_flags & EDatabaseEnvironmentFlag_WriteMap) ? PAGE_READWRITE:
				PAGE_READONLY);
			if (!p) {
				DPUTS("VirtualAlloc failed");
				rc = ErrCode();
				goto fail;
			}
		}
	#endif

	search_done:
		if (env->me_flags & EDatabaseEnvironmentFlag_WriteMap) {
			np = (MDB_page *)(env->me_map + env->me_psize * pgno);
		} else {
			if (!(np = mdb_page_malloc(txn, num))) {
				rc = ENOMEM;
				goto fail;
			}
		}
		if (i) {
			mop[0] = mop_len -= num;
			/* Move any stragglers down */
			for (j = i-num; j < mop_len; )
				mop[++j] = mop[++i];
		} else {
			txn->mt_next_pgno = pgno + num;
		}
		np->mp_p.p_pgno = pgno;
		mdb_page_dirty(txn, np);
		*mp = np;

		return MDB_SUCCESS;

	fail:
		txn->mt_flags |= MDB_TXN_ERROR;
		return rc;
	}

	/** Copy the used portions of a non-overflow page.
	 * @param[in] dst page to copy into
	 * @param[in] src page to copy from
	 * @param[in] psize size of a page
	 */
	void NPrivate::mdb_page_copy(MDB_page *dst, MDB_page *src, uint32 psize)
	{
		enum { Align = sizeof(pgno_t) };
		indx_t upper = src->mp_upper, lower = src->mp_lower, unused = upper-lower;

		/* If page isn't full, just copy the used portion. Adjust
		 * alignment so memcpy may copy words instead of bytes.
		 */
		if ((unused &= -Align) && !IS_LEAF2(src)) {
			upper = (upper + PAGEBASE) & -Align;
			memcpy(dst, src, (lower + PAGEBASE + (Align-1)) & -Align);
			memcpy((pgno_t *)((char *)dst+upper), (pgno_t *)((char *)src+upper),
				psize - upper);
		} else {
			memcpy(dst, src, psize - unused);
		}
	}

	/** Pull a page off the txn's spill list, if present.
	 * If a page being referenced was spilled to disk in this txn, bring
	 * it back and make it dirty/writable again.
	 * @param[in] txn the transaction handle.
	 * @param[in] mp the page being referenced. It must not be dirty.
	 * @param[out] ret the writable page, if any. ret is unchanged if
	 * mp wasn't spilled.
	 */
	int NPrivate::mdb_page_unspill(MDB_txn *txn, MDB_page *mp, MDB_page **ret)
	{
		MDB_env *env = txn->mt_env;
		const MDB_txn *tx2;
		unsigned x;
		pgno_t pgno = mp->mp_p.p_pgno, pn = pgno << 1;

		for (tx2 = txn; tx2; tx2=tx2->mt_parent) {
			if (!tx2->mt_spill_pgs)
				continue;
			x = mdb_midl_search(tx2->mt_spill_pgs, pn);
			if (x <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[x] == pn) {
				MDB_page *np;
				int num;
				if (txn->mt_dirty_room == 0)
					return MDB_TXN_FULL;
				if (IS_OVERFLOW(mp))
					num = mp->mp_pages;
				else
					num = 1;
				if (env->me_flags & EDatabaseEnvironmentFlag_WriteMap) {
					np = mp;
				} else {
					np = mdb_page_malloc(txn, num);
					if (!np)
						return ENOMEM;
					if (num > 1)
						memcpy(np, mp, num * env->me_psize);
					else
						mdb_page_copy(np, mp, env->me_psize);
				}
				if (tx2 == txn) {
					/* If in current txn, this page is no longer spilled.
					 * If it happens to be the last page, truncate the spill list.
					 * Otherwise mark it as deleted by setting the LSB.
					 */
					if (x == txn->mt_spill_pgs[0])
						txn->mt_spill_pgs[0]--;
					else
						txn->mt_spill_pgs[x] |= 1;
				}	/* otherwise, if belonging to a parent txn, the
					 * page remains spilled until child commits
					 */

				mdb_page_dirty(txn, np);
				np->mp_flags |= P_DIRTY;
				*ret = np;
				break;
			}
		}
		return MDB_SUCCESS;
	}

	/** Touch a page: make it dirty and re-insert into tree with updated pgno.
	 * Set #MDB_TXN_ERROR on failure.
	 * @param[in] mc cursor pointing to the page to be touched
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_touch(MDB_cursor *mc)
	{
		MDB_page *mp = mc->mc_pg[mc->mc_top], *np;
		MDB_txn *txn = mc->mc_txn;
		MDB_cursor *m2, *m3;
		pgno_t	pgno;
		int rc;

		if (!F_ISSET(mp->mp_flags, P_DIRTY)) {
			if (txn->mt_flags & MDB_TXN_SPILLS) {
				np = nullptr;
				rc = mdb_page_unspill(txn, mp, &np);
				if (rc)
					goto fail;
				if (np)
					goto done;
			}
			mdb_midl_need(&txn->mt_free_pgs, 1);
			if ((rc = mdb_page_alloc(mc, 1, &np)))
				goto fail;
			pgno = np->mp_p.p_pgno;
			DMibDatabaseTrace("touched db {} page {} -> {}", DDBI(mc), mp->mp_p.p_pgno, pgno);
			DMibCheck(mp->mp_p.p_pgno != pgno);
			mdb_midl_xappend(txn->mt_free_pgs, mp->mp_p.p_pgno);
			/* Update the parent page, if any, to point to the new page */
			if (mc->mc_top) {
				MDB_page *parent = mc->mc_pg[mc->mc_top-1];
				MDB_node *node = NODEPTR(parent, mc->mc_ki[mc->mc_top-1]);
				SETPGNO(node, pgno);
			} else {
				mc->mc_db->md_root = pgno;
			}
		} else if (txn->mt_parent && !IS_SUBP(mp)) {
			MDB_ID2 mid, *dl = txn->mt_u.dirty_list;
			pgno = mp->mp_p.p_pgno;
			/* If txn has a parent, make sure the page is in our
			 * dirty list.
			 */
			if (dl[0].mid) {
				unsigned x = mdb_mid2l_search(dl, pgno);
				if (x <= dl[0].mid && dl[x].mid == pgno) {
					if (mp != dl[x].mptr) { /* bad cursor? */
						mc->mc_flags &= ~(C_INITIALIZED|C_EOF);
						txn->mt_flags |= MDB_TXN_ERROR;
						return MDB_PROBLEM;
					}
					return 0;
				}
			}
			DMibCheck(dl[0].mid < MDB_IDL_UM_MAX);
			/* No - copy it */
			np = mdb_page_malloc(txn, 1);
			if (!np)
				return ENOMEM;
			mid.mid = pgno;
			mid.mptr = np;
			rc = mdb_mid2l_insert(dl, &mid);
			DMibCheck(rc == 0);
		} else {
			return 0;
		}

		mdb_page_copy(np, mp, txn->mt_env->me_psize);
		np->mp_p.p_pgno = pgno;
		np->mp_flags |= P_DIRTY;

	done:
		/* Adjust cursors pointing to mp */
		mc->mc_pg[mc->mc_top] = np;
		m2 = txn->mt_cursors[mc->mc_dbi];
		if (mc->mc_flags & C_SUB) {
			for (; m2; m2=m2->mc_next) {
				m3 = &m2->mc_xcursor->mx_cursor;
				if (m3->mc_snum < mc->mc_snum) continue;
				if (m3->mc_pg[mc->mc_top] == mp)
					m3->mc_pg[mc->mc_top] = np;
			}
		} else {
			for (; m2; m2=m2->mc_next) {
				if (m2->mc_snum < mc->mc_snum) continue;
				if (m2 == mc) continue;
				if (m2->mc_pg[mc->mc_top] == mp) {
					m2->mc_pg[mc->mc_top] = np;
					if (IS_LEAF(np))
						XCURSOR_REFRESH(m2, mc->mc_top, np);
				}
			}
		}
		MDB_PAGE_UNREF(mc->mc_txn, mp);
		return 0;

	fail:
		txn->mt_flags |= MDB_TXN_ERROR;
		return rc;
	}

	/** Flush (some) dirty pages to the map, after clearing their dirty flag.
	 * @param[in] txn the transaction that's being committed
	 * @param[in] keep number of initial pages in dirty_list to keep dirty.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_flush(MDB_txn *txn, int keep)
	{
		MDB_env		*env = txn->mt_env;
		MDB_ID2L	dl = txn->mt_u.dirty_list;
		unsigned	psize = env->me_psize, j;
		int			i, pagecount = dl[0].mid, rc;
		mint		size = 0;
		off_t		pos = 0;
		pgno_t		pgno = 0;
		MDB_page	*dp = nullptr;
	#ifdef DPlatformFamily_Windows
		OVERLAPPED	ov;
	#else
		struct iovec iov[MDB_COMMIT_PAGES];
		smint		wsize = 0, wres;
		off_t		wpos = 0, next_pos = 1; /* impossible pos, so pos != next_pos */
		int			n = 0;
	#endif

		j = i = keep;

		if (env->me_flags & EDatabaseEnvironmentFlag_WriteMap) {
			/* Clear dirty flags */
			while (++i <= pagecount) {
				dp = (MDB_page	*)(dl[i].mptr);
				/* Don't flush this page yet */
				if (dp->mp_flags & (P_LOOSE|P_KEEP)) {
					dp->mp_flags &= ~P_KEEP;
					dl[++j] = dl[i];
					continue;
				}
				dp->mp_flags &= ~P_DIRTY;
			}
			goto done;
		}

		/* Write the pages */
		for (;;) {
			if (++i <= pagecount) {
				dp = (MDB_page *)dl[i].mptr;
				/* Don't flush this page yet */
				if (dp->mp_flags & (P_LOOSE|P_KEEP)) {
					dp->mp_flags &= ~P_KEEP;
					dl[i].mid = 0;
					continue;
				}
				pgno = dl[i].mid;
				/* clear dirty flag */
				dp->mp_flags &= ~P_DIRTY;
				pos = pgno * psize;
				size = psize;
				if (IS_OVERFLOW(dp)) size *= dp->mp_pages;
			}
	#ifdef DPlatformFamily_Windows
			else break;

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
			if (!WriteFile(env->me_fd, dp, size, nullptr, &ov)) {
				rc = ErrCode();
				DMibDatabaseTrace("WriteFile: {}", rc);
				return rc;
			}
	#else
			/* Write up to MDB_COMMIT_PAGES dirty pages at a time. */
			if (pos!=next_pos || n==MDB_COMMIT_PAGES || wsize+size>MAX_WRITE) {
				if (n) {
	retry_write:
					/* Write previous page(s) */
	#ifdef MDB_USE_PWRITEV
					wres = pwritev(env->me_fd, iov, n, wpos);
	#else
					if (n == 1) {
						wres = pwrite(env->me_fd, iov[0].iov_base, wsize, wpos);
					} else {
	retry_seek:
						if (lseek(env->me_fd, wpos, SEEK_SET) == -1) {
							rc = ErrCode();
							if (rc == EINTR)
								goto retry_seek;
							DMibDatabaseTrace("lseek: {}", mdb_strerror(rc));
							return rc;
						}
						wres = writev(env->me_fd, iov, n);
					}
	#endif
					if (wres != wsize) {
						if (wres < 0) {
							rc = ErrCode();
							if (rc == EINTR)
								goto retry_write;
							DMibDatabaseTrace("Write error: {}", mdb_strerror(rc));
						} else {
							rc = EIO; /* TODO: Use which error code? */
							DPUTS("short write, filesystem full?");
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
		if (pgno > txn->mt_last_pgno)
			txn->mt_last_pgno = pgno;
	#endif

		/* MIPS has cache coherency issues, this is a no-op everywhere else
		 * Note: for any size >= on-chip cache size, entire on-chip cache is
		 * flushed.
		 */
		CACHEFLUSH(env->me_map, txn->mt_next_pgno * env->me_psize, DCACHE);

		for (i = keep; ++i <= pagecount; ) {
			dp = (MDB_page *)dl[i].mptr;
			/* This is a page we skipped above */
			if (!dl[i].mid) {
				dl[++j] = dl[i];
				dl[j].mid = dp->mp_p.p_pgno;
				continue;
			}
			env->mdb_dpage_free(dp);
		}

	done:
		i--;
		txn->mt_dirty_room += i - j;
		dl[0].mid = j;
		return MDB_SUCCESS;
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
	 * MDB_TXN_FULL, and filling the per-env list returns MDB_MAP_FULL.
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
	int mdb_rpage_get(MDB_txn *txn, pgno_t pg0, MDB_page **ret)
	{
		MDB_env *env = txn->mt_env;
		MDB_page *p;
		MDB_ID3L tl = txn->mt_rpages;
		MDB_ID3L el = env->me_rpages;
		MDB_ID3 id3;
		unsigned x, rem;
		pgno_t pgno;
		int rc, retries = 1;
	#ifdef DPlatformFamily_Windows
		LARGE_INTEGER off;
		SIZE_T len;
	#define SET_OFF(off,val)	off.QuadPart = val
	#define MAP(rc,env,addr,len,off)	\
		addr = nullptr; \
		rc = g_fDatabase_NtMapViewOfSection(env->me_fmh, GetCurrentProcess(), &addr, 0, \
			len, &off, &len, ViewUnmap, (env->me_flags & EDatabaseEnvironmentFlag_ReadOnly) ? 0 : MEM_RESERVE, PAGE_READONLY); \
		if (rc) rc = mdb_nt2win32(rc)
	#else
		off_t off;
		mint len;
	#define SET_OFF(off,val)	off = val
	#define MAP(rc,env,addr,len,off)	\
		addr = mmap(nullptr, len, PROT_READ, MAP_SHARED, env->me_fd, off); \
		rc = (addr == MAP_FAILED) ? errno : 0
	#endif

		/* remember the offset of the actual page number, so we can
		 * return the correct pointer at the end.
		 */
		rem = pg0 & (MDB_RPAGE_CHUNK-1);
		pgno = pg0 ^ rem;

		id3.mid = 0;
		x = mdb_mid3l_search(tl, pgno);
		if (x <= tl[0].mid && tl[x].mid == pgno) {
			if (x != tl[0].mid && tl[x+1].mid == pg0)
				x++;
			/* check for overflow size */
			p = (MDB_page *)((char *)tl[x].mptr + rem * env->me_psize);
			if (IS_OVERFLOW(p) && p->mp_pages + rem > tl[x].mcnt) {
				id3.mcnt = p->mp_pages + rem;
				len = id3.mcnt * env->me_psize;
				SET_OFF(off, pgno * env->me_psize);
				MAP(rc, env, id3.mptr, len, off);
				if (rc)
					return rc;
				/* check for local-only page */
				if (rem) {
					DMibCheck(tl[x].mid != pg0);
					/* hope there's room to insert this locally.
					 * setting mid here tells later code to just insert
					 * this id3 instead of searching for a match.
					 */
					id3.mid = pg0;
					goto notlocal;
				} else {
					/* ignore the mapping we got from env, use new one */
					tl[x].mptr = id3.mptr;
					tl[x].mcnt = id3.mcnt;
					/* if no active ref, see if we can replace in env */
					if (!tl[x].mref) {
						unsigned i;
						pthread_mutex_lock(&env->me_rpmutex);
						i = mdb_mid3l_search(el, tl[x].mid);
						if (el[i].mref == 1) {
							/* just us, replace it */
							munmap(el[i].mptr, el[i].mcnt * env->me_psize);
							el[i].mptr = tl[x].mptr;
							el[i].mcnt = tl[x].mcnt;
						} else {
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
		if (tl[0].mid >= MDB_TRPAGE_MAX - txn->mt_rpcheck) {
			unsigned i, y;
			/* purge unref'd pages from our list and unref in env */
			pthread_mutex_lock(&env->me_rpmutex);
	retry:
			y = 0;
			for (i=1; i<=tl[0].mid; i++) {
				if (!tl[i].mref) {
					if (!y) y = i;
					/* tmp overflow pages don't go to env */
					if (tl[i].mid & (MDB_RPAGE_CHUNK-1)) {
						munmap(tl[i].mptr, tl[i].mcnt * env->me_psize);
						continue;
					}
					x = mdb_mid3l_search(el, tl[i].mid);
					el[x].mref--;
				}
			}
			pthread_mutex_unlock(&env->me_rpmutex);
			if (!y) {
				/* we didn't find any unref'd chunks.
				 * if we're out of room, fail.
				 */
				if (tl[0].mid >= MDB_TRPAGE_MAX)
					return MDB_TXN_FULL;
				/* otherwise, raise threshold for next time around
				 * and let this go.
				 */
				txn->mt_rpcheck /= 2;
			} else {
				/* we found some unused; consolidate the list */
				for (i=y+1; i<= tl[0].mid; i++)
					if (tl[i].mref)
						tl[y++] = tl[i];
				tl[0].mid = y-1;
				/* decrease the check threshold toward its original value */
				if (!txn->mt_rpcheck)
					txn->mt_rpcheck = 1;
				while (txn->mt_rpcheck < tl[0].mid && txn->mt_rpcheck < MDB_TRPAGE_SIZE/2)
					txn->mt_rpcheck *= 2;
			}
		}
		if (tl[0].mid < MDB_TRPAGE_SIZE) {
			id3.mref = 1;
			if (id3.mid)
				goto found;
			/* don't map past last written page in read-only envs */
			if ((env->me_flags & EDatabaseEnvironmentFlag_ReadOnly) && pgno + MDB_RPAGE_CHUNK-1 > txn->mt_last_pgno)
				id3.mcnt = txn->mt_last_pgno + 1 - pgno;
			else
				id3.mcnt = MDB_RPAGE_CHUNK;
			len = id3.mcnt * env->me_psize;
			id3.mid = pgno;

			/* search for page in env */
			pthread_mutex_lock(&env->me_rpmutex);
			x = mdb_mid3l_search(el, pgno);
			if (x <= el[0].mid && el[x].mid == pgno) {
				id3.mptr = el[x].mptr;
				id3.mcnt = el[x].mcnt;
				/* check for overflow size */
				p = (MDB_page *)((char *)id3.mptr + rem * env->me_psize);
				if (IS_OVERFLOW(p) && p->mp_pages + rem > id3.mcnt) {
					id3.mcnt = p->mp_pages + rem;
					len = id3.mcnt * env->me_psize;
					SET_OFF(off, pgno * env->me_psize);
					MAP(rc, env, id3.mptr, len, off);
					if (rc)
						goto fail;
					if (!el[x].mref) {
						munmap(el[x].mptr, env->me_psize * el[x].mcnt);
						el[x].mptr = id3.mptr;
						el[x].mcnt = id3.mcnt;
					} else {
						id3.mid = pg0;
						pthread_mutex_unlock(&env->me_rpmutex);
						goto found;
					}
				}
				el[x].mref++;
				pthread_mutex_unlock(&env->me_rpmutex);
				goto found;
			}
			if (el[0].mid >= MDB_ERPAGE_MAX - env->me_rpcheck) {
				/* purge unref'd pages */
				unsigned i, y = 0;
				for (i=1; i<=el[0].mid; i++) {
					if (!el[i].mref) {
						if (!y) y = i;
						munmap(el[i].mptr, env->me_psize * el[i].mcnt);
					}
				}
				if (!y) {
					if (retries) {
						/* see if we can unref some local pages */
						retries--;
						id3.mid = 0;
						goto retry;
					}
					if (el[0].mid >= MDB_ERPAGE_MAX) {
						pthread_mutex_unlock(&env->me_rpmutex);
						return MDB_MAP_FULL;
					}
					env->me_rpcheck /= 2;
				} else {
					for (i=y+1; i<= el[0].mid; i++)
						if (el[i].mref)
							el[y++] = el[i];
					el[0].mid = y-1;
					if (!env->me_rpcheck)
						env->me_rpcheck = 1;
					while (env->me_rpcheck < el[0].mid && env->me_rpcheck < MDB_ERPAGE_SIZE/2)
						env->me_rpcheck *= 2;
				}
			}
			SET_OFF(off, pgno * env->me_psize);
			MAP(rc, env, id3.mptr, len, off);
			if (rc) {
	fail:
				pthread_mutex_unlock(&env->me_rpmutex);
				return rc;
			}
			/* check for overflow size */
			p = (MDB_page *)((char *)id3.mptr + rem * env->me_psize);
			if (IS_OVERFLOW(p) && p->mp_pages + rem > id3.mcnt) {
				id3.mcnt = p->mp_pages + rem;
				munmap(id3.mptr, len);
				len = id3.mcnt * env->me_psize;
				MAP(rc, env, id3.mptr, len, off);
				if (rc)
					goto fail;
			}
			mdb_mid3l_insert(el, &id3);
			pthread_mutex_unlock(&env->me_rpmutex);
	found:
			mdb_mid3l_insert(tl, &id3);
		} else {
			return MDB_TXN_FULL;
		}
	ok:
		p = (MDB_page *)((char *)id3.mptr + rem * env->me_psize);
	#if DMibDatabaseDebug	/* we don't need this check any more */
		if (IS_OVERFLOW(p)) {
			DMibCheck(p->mp_pages + rem <= id3.mcnt);
		}
	#endif
		*ret = p;
		return MDB_SUCCESS;
	}
	#endif

	/** Find the address of the page corresponding to a given page number.
	 * Set #MDB_TXN_ERROR on failure.
	 * @param[in] mc the cursor accessing the page.
	 * @param[in] pgno the page number for the page to retrieve.
	 * @param[out] ret address of a pointer where the page's address will be stored.
	 * @param[out] lvl dirty_list inheritance level of found page. 1=current txn, 0=mapped page.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_get(MDB_cursor *mc, pgno_t pgno, MDB_page **ret, int *lvl)
	{
		MDB_txn *txn = mc->mc_txn;
		MDB_page *p = nullptr;
		int level;

		if (! (mc->mc_flags & (C_ORIG_RDONLY|C_WRITEMAP))) {
			MDB_txn *tx2 = txn;
			level = 1;
			do {
				MDB_ID2L dl = tx2->mt_u.dirty_list;
				unsigned x;
				/* Spilled pages were dirtied in this txn and flushed
				 * because the dirty list got full. Bring this page
				 * back in from the map (but don't unspill it here,
				 * leave that unless page_touch happens again).
				 */
				if (tx2->mt_spill_pgs) {
					MDB_ID pn = pgno << 1;
					x = mdb_midl_search(tx2->mt_spill_pgs, pn);
					if (x <= tx2->mt_spill_pgs[0] && tx2->mt_spill_pgs[x] == pn)
						goto mapped;
				}
				if (dl[0].mid) {
					unsigned x = mdb_mid2l_search(dl, pgno);
					if (x <= dl[0].mid && dl[x].mid == pgno) {
						p = (MDB_page *)dl[x].mptr;
						goto done;
					}
				}
				level++;
			} while ((tx2 = tx2->mt_parent) != nullptr);
		}

		if (pgno >= txn->mt_next_pgno) {
			DMibDatabaseTrace("page {} not found", pgno);
			txn->mt_flags |= MDB_TXN_ERROR;
			return MDB_PAGE_NOTFOUND;
		}

		level = 0;

	mapped:
		{
	#ifdef DMibDatabase_Always64BitSupport
			int rc = mdb_rpage_get(txn, pgno, &p);
			if (rc) {
				txn->mt_flags |= MDB_TXN_ERROR;
				return rc;
			}
	#else
			MDB_env *env = txn->mt_env;
			p = (MDB_page *)(env->me_map + env->me_psize * pgno);
	#endif
		}

	done:
		*ret = p;
		if (lvl)
			*lvl = level;
		return MDB_SUCCESS;
	}

	/** Finish #mdb_page_search() / #mdb_page_search_lowest().
	 *	The cursor is at the root page, set up the rest of it.
	 */
	int NPrivate::mdb_page_search_root(MDB_cursor *mc, CDatabaseValue const *key, int flags)
	{
		MDB_page	*mp = mc->mc_pg[mc->mc_top];
		int rc;
		DKBUF;

		while (IS_BRANCH(mp)) {
			MDB_node	*node;
			indx_t		i;

			DMibDatabaseTrace("branch page {} has {} keys", mp->mp_p.p_pgno, NUMKEYS(mp));
			/* Don't assert on branch pages in the FreeDB. We can get here
			 * while in the process of rebalancing a FreeDB branch page; we must
			 * let that proceed. ITS#8336
			 */
			DMibCheck(!mc->mc_dbi || NUMKEYS(mp) > 1);
			DMibDatabaseTrace("found index 0 to page {}", NODEPGNO(NODEPTR(mp, 0)));

			if (flags & (MDB_PS_FIRST|MDB_PS_LAST)) {
				i = 0;
				if (flags & MDB_PS_LAST) {
					i = NUMKEYS(mp) - 1;
					/* if already init'd, see if we're already in right place */
					if (mc->mc_flags & C_INITIALIZED) {
						if (mc->mc_ki[mc->mc_top] == i) {
							mc->mc_top = mc->mc_snum++;
							mp = mc->mc_pg[mc->mc_top];
							goto ready;
						}
					}
				}
			} else {
				int	 exact;
				node = mdb_node_search(mc, key, &exact);
				if (node == nullptr)
					i = NUMKEYS(mp) - 1;
				else {
					i = mc->mc_ki[mc->mc_top];
					if (!exact) {
						DMibCheck(i > 0);
						i--;
					}
				}
				DMibDatabaseTrace("following index {} for key [{}]", i, DKEY(key));
			}

			DMibCheck(i < NUMKEYS(mp));
			node = NODEPTR(mp, i);

			if ((rc = mdb_page_get(mc, NODEPGNO(node), &mp, nullptr)) != 0)
				return rc;

			mc->mc_ki[mc->mc_top] = i;
			if ((rc = mdb_cursor_push(mc, mp)))
				return rc;

		ready:
			if (flags & MDB_PS_MODIFY) {
				if ((rc = mdb_page_touch(mc)) != 0)
					return rc;
				mp = mc->mc_pg[mc->mc_top];
			}
		}

		if (!IS_LEAF(mp)) {
			DMibDatabaseTrace("internal error, index points to a {nfh,sf0,sj2} page!?", mp->mp_flags);
			mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
			return MDB_CORRUPTED;
		}

		DMibDatabaseTrace("found leaf page {} for key [{}]", mp->mp_p.p_pgno, key ? DKEY(key) : "null");
		mc->mc_flags |= C_INITIALIZED;
		mc->mc_flags &= ~C_EOF;

		return MDB_SUCCESS;
	}

	/** Search for the lowest key under the current branch page.
	 * This just bypasses a NUMKEYS check in the current page
	 * before calling mdb_page_search_root(), because the callers
	 * are all in situations where the current page is known to
	 * be underfilled.
	 */
	int NPrivate::mdb_page_search_lowest(MDB_cursor *mc)
	{
		MDB_page	*mp = mc->mc_pg[mc->mc_top];
		MDB_node	*node = NODEPTR(mp, 0);
		int rc;

		if ((rc = mdb_page_get(mc, NODEPGNO(node), &mp, nullptr)) != 0)
			return rc;

		mc->mc_ki[mc->mc_top] = 0;
		if ((rc = mdb_cursor_push(mc, mp)))
			return rc;
		return mdb_page_search_root(mc, nullptr, MDB_PS_FIRST);
	}

	/** Search for the page a given key should be in.
	 * Push it and its parent pages on the cursor stack.
	 * @param[in,out] mc the cursor for this operation.
	 * @param[in] key the key to search for, or nullptr for first/last page.
	 * @param[in] flags If MDB_PS_MODIFY is set, visited pages in the DB
	 *   are touched (updated with new page numbers).
	 *   If MDB_PS_FIRST or MDB_PS_LAST is set, find first or last leaf.
	 *   This is used by #mdb_cursor_first() and #mdb_cursor_last().
	 *   If MDB_PS_ROOTONLY set, just fetch root node, no further lookups.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_search(MDB_cursor *mc, CDatabaseValue const *key, int flags)
	{
		int		 rc;
		pgno_t		 root;

		/* Make sure the txn is still viable, then find the root from
		 * the txn's db table and set it as the root of the cursor's stack.
		 */
		mc->mc_txn->f_CheckBlocked();

		{
			/* Make sure we're using an up-to-date root */
			if (*mc->mc_dbflag & DB_STALE) {
					MDB_cursor mc2;
					if (TXN_DBI_CHANGED(mc->mc_txn, mc->mc_dbi))
						DMibError("Stale write transaction");
					mdb_cursor_init(&mc2, mc->mc_txn, MAIN_DBI, nullptr);
					rc = mdb_page_search(&mc2, &mc->mc_dbx->m_DatabaseName, 0);
					if (rc)
						return rc;
					{
						CDatabaseValue data;
						int exact = 0;
						uint16 flags;
						MDB_node *leaf = mdb_node_search(&mc2,
							&mc->mc_dbx->m_DatabaseName, &exact);
						if (!exact)
							return MDB_NOTFOUND;
						if ((leaf->mn_flags & (F_DUPDATA|F_SUBDATA)) != F_SUBDATA)
							return MDB_INCOMPATIBLE; /* not a named DB */
						rc = mdb_node_read(&mc2, leaf, &data);
						if (rc)
							return rc;
						memcpy(&flags, ((char *) data.m_pData + offsetof(MDB_db, md_flags)),
							sizeof(uint16));
						/* The txn may not know this DBI, or another process may
						 * have dropped and recreated the DB with other flags.
						 */
						if ((mc->mc_db->md_flags & PERSISTENT_FLAGS) != flags)
							return MDB_INCOMPATIBLE;
						memcpy(mc->mc_db, data.m_pData, sizeof(MDB_db));
					}
					*mc->mc_dbflag &= ~DB_STALE;
			}
			root = mc->mc_db->md_root;

			if (root == P_INVALID) {		/* Tree is empty. */
				DPUTS("tree is empty");
				return MDB_NOTFOUND;
			}
		}

		DMibCheck(root > 1);
		if (!mc->mc_pg[0] || mc->mc_pg[0]->mp_p.p_pgno != root) {
	#ifdef DMibDatabase_Always64BitSupport
			if (mc->mc_pg[0])
				MDB_PAGE_UNREF(mc->mc_txn, mc->mc_pg[0]);
	#endif
			if ((rc = mdb_page_get(mc, root, &mc->mc_pg[0], nullptr)) != 0)
				return rc;
		}

	#ifdef DMibDatabase_Always64BitSupport
		{
			int i;
			for (i=1; i<mc->mc_snum; i++)
				MDB_PAGE_UNREF(mc->mc_txn, mc->mc_pg[i]);
		}
	#endif
		mc->mc_snum = 1;
		mc->mc_top = 0;

		DMibDatabaseTrace("db {} root page {} has flags 0x{nfh,sf0,sj4}", DDBI(mc), root, mc->mc_pg[0]->mp_flags);

		if (flags & MDB_PS_MODIFY) {
			if ((rc = mdb_page_touch(mc)))
				return rc;
		}

		if (flags & MDB_PS_ROOTONLY)
			return MDB_SUCCESS;

		return mdb_page_search_root(mc, key, flags);
	}

	int NPrivate::mdb_ovpage_free(MDB_cursor *mc, MDB_page *mp)
	{
		MDB_txn *txn = mc->mc_txn;
		pgno_t pg = mp->mp_p.p_pgno;
		unsigned x = 0, ovpages = mp->mp_pages;
		MDB_env *env = txn->mt_env;
		MDB_IDL sl = txn->mt_spill_pgs;
		MDB_ID pn = pg << 1;

		DMibDatabaseTrace("free ov page {} ({})", pg, ovpages);
		/* If the page is dirty or on the spill list we just acquired it,
		 * so we should give it back to our current free list, if any.
		 * Otherwise put it onto the list of pages we freed in this txn.
		 *
		 * Won't create me_pgstate.mf_pghead: me_pgstate.mf_pglast must be inited along with it.
		 * Unsupported in nested txns: They would need to hide the page
		 * range in ancestor txns' dirty and spilled lists.
		 */
		if (env->me_pgstate.mf_pghead &&
			!txn->mt_parent &&
			((mp->mp_flags & P_DIRTY) ||
			 (sl && (x = mdb_midl_search(sl, pn)) <= sl[0] && sl[x] == pn)))
		{
			unsigned i, j;
			pgno_t *mop;
			MDB_ID2 *dl, ix, iy;
			mdb_midl_need(&env->me_pgstate.mf_pghead, ovpages);
			if (!(mp->mp_flags & P_DIRTY)) {
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
			for (ix = dl[x]; ix.mptr != mp; ix = iy) {
				if (x > 1) {
					x--;
					iy = dl[x];
					dl[x] = ix;
				} else {
					DMibCheck(x > 1);
					j = ++(dl[0].mid);
					dl[j] = ix;		/* Unsorted. OK when MDB_TXN_ERROR. */
					txn->mt_flags |= MDB_TXN_ERROR;
					return MDB_PROBLEM;
				}
			}
			txn->mt_dirty_room++;
			if (!(env->me_flags & EDatabaseEnvironmentFlag_WriteMap))
				env->mdb_dpage_free(mp);
	release:
			/* Insert in me_pgstate.mf_pghead */
			mop = env->me_pgstate.mf_pghead;
			j = mop[0] + ovpages;
			for (i = mop[0]; i && mop[i] < pg; i--)
				mop[j--] = mop[i];
			while (j>i)
				mop[j--] = pg++;
			mop[0] += ovpages;
		}
		else
			mdb_midl_append_range(&txn->mt_free_pgs, pg, ovpages);
	#ifdef DMibDatabase_Always64BitSupport
		if (mc->mc_ovpg == mp)
			mc->mc_ovpg = NULL;
	#endif
		mc->mc_db->md_overflow_pages -= ovpages;
		return 0;
	}

	/** Allocate and initialize new pages for a database.
	 * Set #MDB_TXN_ERROR on failure.
	 * @param[in] mc a cursor on the database being added to.
	 * @param[in] flags flags defining what type of page is being allocated.
	 * @param[in] num the number of pages to allocate. This is usually 1,
	 * unless allocating overflow pages for a large record.
	 * @param[out] mp Address of a page, or nullptr on failure.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_new(MDB_cursor *mc, uint32 flags, int num, MDB_page **mp)
	{
		MDB_page	*np;
		int rc;

		if ((rc = mdb_page_alloc(mc, num, &np)))
			return rc;
		DMibDatabaseTrace("allocated new mpage {}, page size {}", np->mp_p.p_pgno, mc->mc_txn->mt_env->me_psize);
		np->mp_flags = flags | P_DIRTY;
		np->mp_lower = (PAGEHDRSZ-PAGEBASE);
		np->mp_upper = mc->mc_txn->mt_env->me_psize - PAGEBASE;

		if (IS_BRANCH(np))
			mc->mc_db->md_branch_pages++;
		else if (IS_LEAF(np))
			mc->mc_db->md_leaf_pages++;
		else if (IS_OVERFLOW(np)) {
			mc->mc_db->md_overflow_pages += num;
			np->mp_pages = num;
		}
		*mp = np;

		return 0;
	}

	/** Merge one page into another.
	 *  The nodes from the page pointed to by \b csrc will
	 *	be copied to the page pointed to by \b cdst and then
	 *	the \b csrc page will be freed.
	 * @param[in] csrc Cursor pointing to the source page.
	 * @param[in] cdst Cursor pointing to the destination page.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_merge(MDB_cursor *csrc, MDB_cursor *cdst)
	{
		MDB_page	*psrc, *pdst;
		MDB_node	*srcnode;
		CDatabaseValue		 key, data;
		unsigned	 nkeys;
		int			 rc;
		indx_t		 i, j;

		psrc = csrc->mc_pg[csrc->mc_top];
		pdst = cdst->mc_pg[cdst->mc_top];

		DMibDatabaseTrace("merging page {} into {}", psrc->mp_p.p_pgno, pdst->mp_p.p_pgno);

		DMibCheck(csrc->mc_snum > 1);	/* can't merge root page */
		DMibCheck(cdst->mc_snum > 1);

		/* Mark dst as dirty. */
		if ((rc = mdb_page_touch(cdst)))
			return rc;

		/* get dst page again now that we've touched it. */
		pdst = cdst->mc_pg[cdst->mc_top];

		/* Move all nodes from src to dst.
		 */
		j = nkeys = NUMKEYS(pdst);
		if (IS_LEAF2(psrc)) {
			key.m_Size = csrc->mc_db->md_pad;
			key.m_pData = METADATA(psrc);
			for (i = 0; i < NUMKEYS(psrc); i++, j++) {
				rc = mdb_node_add(cdst, j, &key, nullptr, 0, 0);
				if (rc != MDB_SUCCESS)
					return rc;
				key.m_pData = (uint8 *)key.m_pData + key.m_Size;
			}
		} else {
			for (i = 0; i < NUMKEYS(psrc); i++, j++) {
				srcnode = NODEPTR(psrc, i);
				if (i == 0 && IS_BRANCH(psrc)) {
					MDB_cursor mn;
					MDB_node *s2;
					mdb_cursor_copy(csrc, &mn);
					mn.mc_xcursor = nullptr;
					/* must find the lowest key below src */
					rc = mdb_page_search_lowest(&mn);
					if (rc)
						return rc;
					if (IS_LEAF2(mn.mc_pg[mn.mc_top])) {
						key.m_Size = mn.mc_db->md_pad;
						key.m_pData = LEAF2KEY(mn.mc_pg[mn.mc_top], 0, key.m_Size);
					} else {
						s2 = NODEPTR(mn.mc_pg[mn.mc_top], 0);
						key.m_Size = NODEKSZ(s2);
						key.m_pData = NODEKEY(s2);
					}
				} else {
					key.m_Size = srcnode->mn_ksize;
					key.m_pData = NODEKEY(srcnode);
				}

				data.m_Size = NODEDSZ(srcnode);
				data.m_pData = (uint8 *)(MDB_page *)NODEDATA(srcnode);
				rc = mdb_node_add(cdst, j, &key, &data, NODEPGNO(srcnode), srcnode->mn_flags);
				if (rc != MDB_SUCCESS)
					return rc;
			}
		}

		DMibDatabaseTrace("dst page {} now has {} keys ({fe1} filled)", pdst->mp_p.p_pgno, NUMKEYS(pdst), (float)PAGEFILL(cdst->mc_txn->mt_env, pdst) / 10);

		/* Unlink the src page from parent and add to free list.
		 */
		csrc->mc_top--;
		mdb_node_del(csrc, 0);
		if (csrc->mc_ki[csrc->mc_top] == 0) {
			key.m_Size = 0;
			rc = mdb_update_key(csrc, &key);
			if (rc) {
				csrc->mc_top++;
				return rc;
			}
		}
		csrc->mc_top++;

		psrc = csrc->mc_pg[csrc->mc_top];
		/* If not operating on FreeDB, allow this page to be reused
		 * in this txn. Otherwise just add to free list.
		 */
		rc = mdb_page_loose(csrc, psrc);
		if (rc)
			return rc;
		if (IS_LEAF(psrc))
			csrc->mc_db->md_leaf_pages--;
		else
			csrc->mc_db->md_branch_pages--;
		{
			/* Adjust other cursors pointing to mp */
			MDB_cursor *m2, *m3;
			CDatabaseSubID dbi = csrc->mc_dbi;
			uint32 top = csrc->mc_top;

			for (m2 = csrc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
				if (csrc->mc_flags & C_SUB)
					m3 = &m2->mc_xcursor->mx_cursor;
				else
					m3 = m2;
				if (m3 == csrc) continue;
				if (m3->mc_snum < csrc->mc_snum) continue;
				if (m3->mc_pg[top] == psrc) {
					m3->mc_pg[top] = pdst;
					m3->mc_ki[top] += nkeys;
					m3->mc_ki[top-1] = cdst->mc_ki[top-1];
				} else if (m3->mc_pg[top-1] == csrc->mc_pg[top-1] &&
					m3->mc_ki[top-1] > csrc->mc_ki[top-1]) {
					m3->mc_ki[top-1]--;
				}
				if (IS_LEAF(psrc))
					XCURSOR_REFRESH(m3, top, m3->mc_pg[top]);
			}
		}
		{
			uint32 snum = cdst->mc_snum;
			uint16 depth = cdst->mc_db->md_depth;
			mdb_cursor_pop(cdst);
			rc = mdb_rebalance(cdst);
			/* Did the tree height change? */
			if (depth != cdst->mc_db->md_depth)
				snum += cdst->mc_db->md_depth - depth;
			cdst->mc_snum = snum;
			cdst->mc_top = snum-1;
		}
		return rc;
	}

	/** Split a page and insert a new node.
	 * Set #MDB_TXN_ERROR on failure.
	 * @param[in,out] mc Cursor pointing to the page and desired insertion index.
	 * The cursor will be updated to point to the actual page and index where
	 * the node got inserted after the split.
	 * @param[in] newkey The key for the newly inserted node.
	 * @param[in] newdata The data for the newly inserted node.
	 * @param[in] newpgno The page number, if the new node is a branch node.
	 * @param[in] nflags The #NODE_ADD_FLAGS for the new node.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_page_split(MDB_cursor *mc, CDatabaseValue *newkey, CDatabaseValue *newdata, pgno_t newpgno, uint32 nflags)
	{
		uint32 flags;
		int		 rc = MDB_SUCCESS, new_root = 0, did_split = 0;
		indx_t		 newindx;
		pgno_t		 pgno = 0;
		int	 i, j, split_indx, nkeys, pmax;
		MDB_env 	*env = mc->mc_txn->mt_env;
		MDB_node	*node;
		CDatabaseValue	 sepkey, rkey, xdata, *rdata = &xdata;
		MDB_page	*copy = nullptr;
		MDB_page	*mp, *rp, *pp;
		int ptop;
		MDB_cursor	mn;
		DKBUF;

		mp = mc->mc_pg[mc->mc_top];
		newindx = mc->mc_ki[mc->mc_top];
		nkeys = NUMKEYS(mp);

		DMibDatabaseTrace("-----> splitting {} page {} and adding [{}] at index {}/{}", IS_LEAF(mp) ? "leaf" : "branch", mp->mp_p.p_pgno, DKEY(newkey), mc->mc_ki[mc->mc_top], nkeys);

		/* Create a right sibling. */
		if ((rc = mdb_page_new(mc, mp->mp_flags, 1, &rp)))
			return rc;
		rp->mp_pad = mp->mp_pad;
		DMibDatabaseTrace("new right sibling: page {}", rp->mp_p.p_pgno);

		/* Usually when splitting the root page, the cursor
		 * height is 1. But when called from mdb_update_key,
		 * the cursor height may be greater because it walks
		 * up the stack while finding the branch slot to update.
		 */
		if (mc->mc_top < 1) {
			if ((rc = mdb_page_new(mc, P_BRANCH, 1, &pp)))
				goto done;
			/* shift current top to make room for new parent */
			for (i=mc->mc_snum; i>0; i--) {
				mc->mc_pg[i] = mc->mc_pg[i-1];
				mc->mc_ki[i] = mc->mc_ki[i-1];
			}
			mc->mc_pg[0] = pp;
			mc->mc_ki[0] = 0;
			mc->mc_db->md_root = pp->mp_p.p_pgno;
			DMibDatabaseTrace("root split! new root = {}", pp->mp_p.p_pgno);
			new_root = mc->mc_db->md_depth++;

			/* Add left (implicit) pointer. */
			if ((rc = mdb_node_add(mc, 0, nullptr, nullptr, mp->mp_p.p_pgno, 0)) != MDB_SUCCESS) {
				/* undo the pre-push */
				mc->mc_pg[0] = mc->mc_pg[1];
				mc->mc_ki[0] = mc->mc_ki[1];
				mc->mc_db->md_root = mp->mp_p.p_pgno;
				mc->mc_db->md_depth--;
				goto done;
			}
			mc->mc_snum++;
			mc->mc_top++;
			ptop = 0;
		} else {
			ptop = mc->mc_top-1;
			DMibDatabaseTrace("parent branch page is {}", mc->mc_pg[ptop]->mp_p.p_pgno);
		}

		mdb_cursor_copy(mc, &mn);
		mn.mc_xcursor = nullptr;
		mn.mc_pg[mn.mc_top] = rp;
		mn.mc_ki[ptop] = mc->mc_ki[ptop]+1;

		if (nflags & EDatabaseWriteFlag_Append) {
			mn.mc_ki[mn.mc_top] = 0;
			sepkey = *newkey;
			split_indx = newindx;
			nkeys = 0;
		} else {

			split_indx = (nkeys+1) / 2;

			if (IS_LEAF2(rp)) {
				uint8 *split, *ins;
				int x;
				uint32 lsize, rsize, ksize;
				/* Move half of the keys to the right sibling */
				x = mc->mc_ki[mc->mc_top] - split_indx;
				ksize = mc->mc_db->md_pad;
				split = LEAF2KEY(mp, split_indx, ksize);
				rsize = (nkeys - split_indx) * ksize;
				lsize = (nkeys - split_indx) * sizeof(indx_t);
				mp->mp_lower -= lsize;
				rp->mp_lower += lsize;
				mp->mp_upper += rsize - lsize;
				rp->mp_upper -= rsize - lsize;
				sepkey.m_Size = ksize;
				if (newindx == split_indx) {
					sepkey.m_pData = newkey->m_pData;
				} else {
					sepkey.m_pData = split;
				}
				if (x<0) {
					ins = LEAF2KEY(mp, mc->mc_ki[mc->mc_top], ksize);
					memcpy(rp->mp_ptrs, split, rsize);
					sepkey.m_pData = (uint8 *)rp->mp_ptrs;
					memmove(ins+ksize, ins, (split_indx - mc->mc_ki[mc->mc_top]) * ksize);
					memcpy(ins, newkey->m_pData, ksize);
					mp->mp_lower += sizeof(indx_t);
					mp->mp_upper -= indx_t(ksize - sizeof(indx_t));
				} else {
					if (x)
						memcpy(rp->mp_ptrs, split, x * ksize);
					ins = LEAF2KEY(rp, x, ksize);
					memcpy(ins, newkey->m_pData, ksize);
					memcpy(ins+ksize, split + x * ksize, rsize - x * ksize);
					rp->mp_lower += sizeof(indx_t);
					rp->mp_upper -= indx_t(ksize - sizeof(indx_t));
					mc->mc_ki[mc->mc_top] = x;
				}
			} else {
				int psize, nsize, k;
				/* Maximum free space in an empty page */
				pmax = env->me_psize - PAGEHDRSZ;
				if (IS_LEAF(mp))
					nsize = env->mdb_leaf_size(newkey, newdata);
				else
					nsize = env->mdb_branch_size(newkey);
				nsize = EVEN(nsize);

				/* grab a page to hold a temporary copy */
				copy = mdb_page_malloc(mc->mc_txn, 1);
				if (copy == nullptr) {
					rc = ENOMEM;
					goto done;
				}
				copy->mp_p.p_pgno  = mp->mp_p.p_pgno;
				copy->mp_flags = mp->mp_flags;
				copy->mp_lower = (PAGEHDRSZ-PAGEBASE);
				copy->mp_upper = env->me_psize - PAGEBASE;

				/* prepare to insert */
				for (i=0, j=0; i<nkeys; i++) {
					if (i == newindx) {
						copy->mp_ptrs[j++] = 0;
					}
					copy->mp_ptrs[j++] = mp->mp_ptrs[i];
				}

				/* When items are relatively large the split point needs
				 * to be checked, because being off-by-one will make the
				 * difference between success or failure in mdb_node_add.
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
				if (nkeys < 32 || nsize > pmax/16 || newindx >= nkeys) {
					/* Find split point */
					psize = 0;
					if (newindx <= split_indx || newindx >= nkeys) {
						i = 0; j = 1;
						k = newindx >= nkeys ? nkeys : split_indx+1+IS_LEAF(mp);
					} else {
						i = nkeys; j = -1;
						k = split_indx-1;
					}
					for (; i!=k; i+=j) {
						if (i == newindx) {
							psize += nsize;
							node = nullptr;
						} else {
							node = (MDB_node *)((char *)mp + copy->mp_ptrs[i] + PAGEBASE);
							psize += NODESIZE + NODEKSZ(node) + sizeof(indx_t);
							if (IS_LEAF(mp)) {
								if (F_ISSET(node->mn_flags, F_BIGDATA))
									psize += sizeof(pgno_t);
								else
									psize += NODEDSZ(node);
							}
							psize = EVEN(psize);
						}
						if (psize > pmax || i == k-j) {
							split_indx = i + (j<0);
							break;
						}
					}
				}
				if (split_indx == newindx) {
					sepkey.m_Size = newkey->m_Size;
					sepkey.m_pData = newkey->m_pData;
				} else {
					node = (MDB_node *)((char *)mp + copy->mp_ptrs[split_indx] + PAGEBASE);
					sepkey.m_Size = node->mn_ksize;
					sepkey.m_pData = NODEKEY(node);
				}
			}
		}

		DMibDatabaseTrace("separator is {} [{}]", split_indx, DKEY(&sepkey));

		/* Copy separator key to the parent.
		 */
		if (SIZELEFT(mn.mc_pg[ptop]) < env->mdb_branch_size(&sepkey)) {
			int snum = mc->mc_snum;
			mn.mc_snum--;
			mn.mc_top--;
			did_split = 1;
			/* We want other splits to find mn when doing fixups */
			WITH_CURSOR_TRACKING(mn,
				rc = mdb_page_split(&mn, &sepkey, nullptr, rp->mp_p.p_pgno, 0));
			if (rc)
				goto done;

			/* root split? */
			if (mc->mc_snum > snum) {
				ptop++;
			}
			/* Right page might now have changed parent.
			 * Check if left page also changed parent.
			 */
			if (mn.mc_pg[ptop] != mc->mc_pg[ptop] &&
				mc->mc_ki[ptop] >= NUMKEYS(mc->mc_pg[ptop])) {
				for (i=0; i<ptop; i++) {
					mc->mc_pg[i] = mn.mc_pg[i];
					mc->mc_ki[i] = mn.mc_ki[i];
				}
				mc->mc_pg[ptop] = mn.mc_pg[ptop];
				if (mn.mc_ki[ptop]) {
					mc->mc_ki[ptop] = mn.mc_ki[ptop] - 1;
				} else {
					/* find right page's left sibling */
					mc->mc_ki[ptop] = mn.mc_ki[ptop];
					rc = mdb_cursor_sibling(mc, 0);
				}
			}
		} else {
			mn.mc_top--;
			rc = mdb_node_add(&mn, mn.mc_ki[ptop], &sepkey, nullptr, rp->mp_p.p_pgno, 0);
			mn.mc_top++;
		}
		if (rc != MDB_SUCCESS) {
			if (rc == MDB_NOTFOUND) /* improper mdb_cursor_sibling() result */
				rc = MDB_PROBLEM;
			goto done;
		}
		if (nflags & EDatabaseWriteFlag_Append) {
			mc->mc_pg[mc->mc_top] = rp;
			mc->mc_ki[mc->mc_top] = 0;
			rc = mdb_node_add(mc, 0, newkey, newdata, newpgno, nflags);
			if (rc)
				goto done;
			for (i=0; i<mc->mc_top; i++)
				mc->mc_ki[i] = mn.mc_ki[i];
		} else if (!IS_LEAF2(mp)) {
			/* Move nodes */
			mc->mc_pg[mc->mc_top] = rp;
			i = split_indx;
			j = 0;
			do {
				if (i == newindx) {
					rkey.m_pData = newkey->m_pData;
					rkey.m_Size = newkey->m_Size;
					if (IS_LEAF(mp)) {
						rdata = newdata;
					} else
						pgno = newpgno;
					flags = nflags;
					/* Update index for the new key. */
					mc->mc_ki[mc->mc_top] = j;
				} else {
					node = (MDB_node *)((char *)mp + copy->mp_ptrs[i] + PAGEBASE);
					rkey.m_pData = NODEKEY(node);
					rkey.m_Size = node->mn_ksize;
					if (IS_LEAF(mp)) {
						xdata.m_pData = (uint8 *)(MDB_page *)NODEDATA(node);
						xdata.m_Size = NODEDSZ(node);
						rdata = &xdata;
					} else
						pgno = NODEPGNO(node);
					flags = node->mn_flags;
				}

				if (!IS_LEAF(mp) && j == 0) {
					/* First branch index doesn't need key data. */
					rkey.m_Size = 0;
				}

				rc = mdb_node_add(mc, j, &rkey, rdata, pgno, flags);
				if (rc)
					goto done;
				if (i == nkeys) {
					i = 0;
					j = 0;
					mc->mc_pg[mc->mc_top] = copy;
				} else {
					i++;
					j++;
				}
			} while (i != split_indx);

			nkeys = NUMKEYS(copy);
			for (i=0; i<nkeys; i++)
				mp->mp_ptrs[i] = copy->mp_ptrs[i];
			mp->mp_lower = copy->mp_lower;
			mp->mp_upper = copy->mp_upper;
			memcpy(NODEPTR(mp, nkeys-1), NODEPTR(copy, nkeys-1),
				env->me_psize - copy->mp_upper - PAGEBASE);

			/* reset back to original page */
			if (newindx < split_indx) {
				mc->mc_pg[mc->mc_top] = mp;
			} else {
				mc->mc_pg[mc->mc_top] = rp;
				mc->mc_ki[ptop]++;
				/* Make sure mc_ki is still valid.
				 */
				if (mn.mc_pg[ptop] != mc->mc_pg[ptop] &&
					mc->mc_ki[ptop] >= NUMKEYS(mc->mc_pg[ptop])) {
					for (i=0; i<=ptop; i++) {
						mc->mc_pg[i] = mn.mc_pg[i];
						mc->mc_ki[i] = mn.mc_ki[i];
					}
				}
			}
			if (nflags & EDatabaseWriteFlag_ReserveSpace) {
				node = NODEPTR(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
				if (!(node->mn_flags & F_BIGDATA))
					newdata->m_pData = (uint8 *)(MDB_page *)NODEDATA(node);
			}
		} else {
			if (newindx >= split_indx) {
				mc->mc_pg[mc->mc_top] = rp;
				mc->mc_ki[ptop]++;
				/* Make sure mc_ki is still valid.
				 */
				if (mn.mc_pg[ptop] != mc->mc_pg[ptop] &&
					mc->mc_ki[ptop] >= NUMKEYS(mc->mc_pg[ptop])) {
					for (i=0; i<=ptop; i++) {
						mc->mc_pg[i] = mn.mc_pg[i];
						mc->mc_ki[i] = mn.mc_ki[i];
					}
				}
			}
		}

		{
			/* Adjust other cursors pointing to mp */
			MDB_cursor *m2, *m3;
			CDatabaseSubID dbi = mc->mc_dbi;
			nkeys = NUMKEYS(mp);

			for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
				if (mc->mc_flags & C_SUB)
					m3 = &m2->mc_xcursor->mx_cursor;
				else
					m3 = m2;
				if (m3 == mc)
					continue;
				if (!(m2->mc_flags & m3->mc_flags & C_INITIALIZED))
					continue;
				if (new_root) {
					int k;
					/* sub cursors may be on different DB */
					if (m3->mc_pg[0] != mp)
						continue;
					/* root split */
					for (k=new_root; k>=0; k--) {
						m3->mc_ki[k+1] = m3->mc_ki[k];
						m3->mc_pg[k+1] = m3->mc_pg[k];
					}
					if (m3->mc_ki[0] >= nkeys) {
						m3->mc_ki[0] = 1;
					} else {
						m3->mc_ki[0] = 0;
					}
					m3->mc_pg[0] = mc->mc_pg[0];
					m3->mc_snum++;
					m3->mc_top++;
				}
				if (m3->mc_top >= mc->mc_top && m3->mc_pg[mc->mc_top] == mp) {
					if (m3->mc_ki[mc->mc_top] >= newindx && !(nflags & MDB_SPLIT_REPLACE))
						m3->mc_ki[mc->mc_top]++;
					if (m3->mc_ki[mc->mc_top] >= nkeys) {
						m3->mc_pg[mc->mc_top] = rp;
						m3->mc_ki[mc->mc_top] -= nkeys;
						for (i=0; i<mc->mc_top; i++) {
							m3->mc_ki[i] = mn.mc_ki[i];
							m3->mc_pg[i] = mn.mc_pg[i];
						}
					}
				} else if (!did_split && m3->mc_top >= ptop && m3->mc_pg[ptop] == mc->mc_pg[ptop] &&
					m3->mc_ki[ptop] >= mc->mc_ki[ptop]) {
					m3->mc_ki[ptop]++;
				}
				if (IS_LEAF(mp))
					XCURSOR_REFRESH(m3, mc->mc_top, m3->mc_pg[mc->mc_top]);
			}
		}
		DMibDatabaseTrace("mp left: {}, rp left: {}", SIZELEFT(mp), SIZELEFT(rp));

	done:
		if (copy)					/* tmp page */
			env->mdb_page_free(copy);
		if (rc)
			mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
		return rc;
	}
}
