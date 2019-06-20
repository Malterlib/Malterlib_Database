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
	using namespace NPrivate;
	/** Search for key within a page, using binary search.
	 * Returns the smallest entry larger or equal to the key.
	 * If exactp is non-null, stores whether the found entry was an exact match
	 * in *exactp (1 or 0).
	 * Updates the cursor index with the index of the found entry.
	 * If no entry larger or equal to the key is found, returns nullptr.
	 */
	NPrivate::MDB_node *NPrivate::mdb_node_search(MDB_cursor *mc, CDatabaseValue const *key, int *exactp)
	{
		uint32	 i = 0, nkeys;
		int		 low, high;
		int		 rc = 0;
		MDB_page *mp = mc->mc_pg[mc->mc_top];
		MDB_node	*node = nullptr;
		CDatabaseValue	 nodekey;
		FDatabaseCompare *pCompare;
		DKBUF;

		nkeys = NUMKEYS(mp);

		DMibDatabaseTrace("searching {} keys in {} {}page {}", nkeys, IS_LEAF(mp) ? "leaf" : "branch", IS_SUBP(mp) ? "sub-" : "", mdb_dbg_pgno(mp));

		low = IS_LEAF(mp) ? 0 : 1;
		high = nkeys - 1;
		pCompare = &(mc->mc_dbx->m_fKeyCompare);

		/* Branch pages have no data, so if using integer keys,
		 * alignment is guaranteed. Use faster mdb_cmp_int.
		 */
		if (IS_BRANCH(mp) && *pCompare == g_mdb_cmp_cint) {
			if (NODEPTR(mp, 1)->mn_ksize == sizeof(CDatabaseOffset))
				pCompare = &g_mdb_cmp_long;
			else
				pCompare = &g_mdb_cmp_int;
		}

		if (IS_LEAF2(mp)) {
			nodekey.m_Size = mc->mc_db->md_pad;
			node = NODEPTR(mp, 0);	/* fake */
			while (low <= high) {
				i = (low + high) >> 1;
				nodekey.m_pData = LEAF2KEY(mp, i, nodekey.m_Size);
				rc = (*pCompare)(*key, nodekey);
				DMibDatabaseTrace("found leaf index {} [{}], rc = {}", i, DKEY(&nodekey), rc);
				if (rc == 0)
					break;
				if (rc > 0)
					low = i + 1;
				else
					high = i - 1;
			}
		} else {
			while (low <= high) {
				i = (low + high) >> 1;

				node = NODEPTR(mp, i);
				nodekey.m_Size = NODEKSZ(node);
				nodekey.m_pData = NODEKEY(node);

				rc = (*pCompare)(*key, nodekey);
	#if DMibDatabaseDebug
				if (IS_LEAF(mp))
				{
					DMibDatabaseTrace("found leaf index {} [{}], rc = {}", i, DKEY(&nodekey), rc);
				}
				else
				{
					DMibDatabaseTrace("found branch index {} [{} -> {}], rc = {}", i, DKEY(&nodekey), NODEPGNO(node), rc);
				}
	#endif
				if (rc == 0)
					break;
				if (rc > 0)
					low = i + 1;
				else
					high = i - 1;
			}
		}

		if (rc > 0) {	/* Found entry is less than the key. */
			i++;	/* Skip to get the smallest entry larger than key. */
			if (!IS_LEAF2(mp))
				node = NODEPTR(mp, i);
		}
		if (exactp)
			*exactp = (rc == 0 && nkeys > 0);
		/* store the key index */
		mc->mc_ki[mc->mc_top] = i;
		if (i >= nkeys)
			/* There is no entry larger or equal to the key. */
			return nullptr;

		/* nodeptr is fake for LEAF2 */
		return node;
	}


	/** Return the data associated with a given node.
	 * @param[in] mc The cursor for this operation.
	 * @param[in] leaf The node being read.
	 * @param[out] data Updated to point to the node's data.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_node_read(MDB_cursor *mc, MDB_node *leaf, CDatabaseValue *data)
	{
		MDB_page	*omp;		/* overflow page */
		pgno_t		 pgno;
		int rc;

		if (MC_OVPG(mc)) {
			MDB_PAGE_UNREF(mc->mc_txn, MC_OVPG(mc));
			MC_SET_OVPG(mc, NULL);
		}
		if (!F_ISSET(leaf->mn_flags, F_BIGDATA)) {
			data->m_Size = NODEDSZ(leaf);
			data->m_pData = NODEDATA(leaf);
			return MDB_SUCCESS;
		}

		/* Read overflow data.
		 */
		data->m_Size = NODEDSZ(leaf);
		memcpy(&pgno, NODEDATA(leaf), sizeof(pgno));
		if ((rc = mdb_page_get(mc, pgno, &omp, nullptr)) != 0) {
			DMibDatabaseTrace("read overflow page {} failed", pgno);
			return rc;
		}
		data->m_pData = METADATA(omp);
		MC_SET_OVPG(mc, omp);

		return MDB_SUCCESS;
	}

	/** Add a node to the page pointed to by the cursor.
	 * Set #MDB_TXN_ERROR on failure.
	 * @param[in] mc The cursor for this operation.
	 * @param[in] indx The index on the page where the new node should be added.
	 * @param[in] key The key for the new node.
	 * @param[in] data The data for the new node, if any.
	 * @param[in] pgno The page number, if adding a branch node.
	 * @param[in] flags Flags for the node.
	 * @return 0 on success, non-zero on failure. Possible errors are:
	 * <ul>
	 *	<li>ENOMEM - failed to allocate overflow pages for the node.
	 *	<li>MDB_PAGE_FULL - there is insufficient room in the page. This error
	 *	should never happen since all callers already calculate the
	 *	page's free space before calling this function.
	 * </ul>
	 */
	int NPrivate::mdb_node_add(MDB_cursor *mc, indx_t indx, CDatabaseValue *key, CDatabaseValue *data, pgno_t pgno, uint32 flags)
	{
		uint32	 i;
		mint		 node_size = NODESIZE;
		smint		 room;
		indx_t		 ofs;
		MDB_node	*node;
		MDB_page	*mp = mc->mc_pg[mc->mc_top];
		MDB_page	*ofp = nullptr;		/* overflow page */
		void		*ndata;
		DKBUF;

		DMibCheck(mp->mp_upper >= mp->mp_lower);

		DMibDatabaseTrace
			(
				"add to {} {}page {} index {}, data size {} key size {} [{}]"
				, IS_LEAF(mp) ? "leaf" : "branch"
				, IS_SUBP(mp) ? "sub-" : ""
				, mdb_dbg_pgno(mp)
				, indx
				, data ? data->m_Size : 0
				, key ? key->m_Size : 0
				, key ? DKEY(key) : "null"
			)
		;

		if (IS_LEAF2(mp)) {
			/* Move higher keys up one slot. */
			int ksize = mc->mc_db->md_pad, dif;
			uint8 *ptr = LEAF2KEY(mp, indx, ksize);
			dif = NUMKEYS(mp) - indx;
			if (dif > 0)
				memmove(ptr+ksize, ptr, dif*ksize);
			/* insert new key */
			memcpy(ptr, key->m_pData, ksize);

			/* Just using these for counting */
			mp->mp_lower += sizeof(indx_t);
			mp->mp_upper -= ksize - sizeof(indx_t);
			return MDB_SUCCESS;
		}

		room = (smint)SIZELEFT(mp) - (smint)sizeof(indx_t);
		if (key != nullptr)
			node_size += key->m_Size;
		if (IS_LEAF(mp)) {
			DMibCheck(key && data);
			if (F_ISSET(flags, F_BIGDATA)) {
				/* Data already on overflow page. */
				node_size += sizeof(pgno_t);
			} else if (node_size + data->m_Size > mc->mc_txn->mt_env->me_nodemax) {
				int ovpages = OVPAGES(data->m_Size, mc->mc_txn->mt_env->me_psize);
				int rc;
				/* Put data on overflow page. */
				DMibDatabaseTrace("data size is {}, node would be {}, put data on overflow page", data->m_Size, node_size+data->m_Size);
				node_size = EVEN(node_size + sizeof(pgno_t));
				if ((smint)node_size > room)
					goto full;
				if ((rc = mdb_page_new(mc, P_OVERFLOW, ovpages, &ofp)))
					return rc;
				DMibDatabaseTrace("allocated overflow page {}", ofp->mp_p.p_pgno);
				flags |= F_BIGDATA;
				goto update;
			} else {
				node_size += data->m_Size;
			}
		}
		node_size = EVEN(node_size);
		if ((smint)node_size > room)
			goto full;

	update:
		/* Move higher pointers up one slot. */
		for (i = NUMKEYS(mp); i > indx; i--)
			mp->mp_ptrs[i] = mp->mp_ptrs[i - 1];

		/* Adjust free space offsets. */
		ofs = mp->mp_upper - node_size;
		DMibCheck(ofs >= mp->mp_lower + sizeof(indx_t));
		mp->mp_ptrs[indx] = ofs;
		mp->mp_upper = ofs;
		mp->mp_lower += sizeof(indx_t);

		/* Write the node data. */
		node = NODEPTR(mp, indx);
		node->mn_ksize = (key == nullptr) ? 0 : key->m_Size;
		node->mn_flags = flags;
		if (IS_LEAF(mp))
			SETDSZ(node,data->m_Size);
		else
			SETPGNO(node,pgno);

		if (key)
			memcpy(NODEKEY(node), key->m_pData, key->m_Size);

		if (IS_LEAF(mp)) {
			ndata = (MDB_page *)NODEDATA(node);
			if (ofp == nullptr) {
				if (F_ISSET(flags, F_BIGDATA))
					memcpy(ndata, data->m_pData, sizeof(pgno_t));
				else if (F_ISSET(flags, EDatabaseWriteFlag_ReserveSpace))
					data->m_pData = (uint8 *)ndata;
				else
					memcpy(ndata, data->m_pData, data->m_Size);
			} else {
				memcpy(ndata, &ofp->mp_p.p_pgno, sizeof(pgno_t));
				ndata = METADATA(ofp);
				if (F_ISSET(flags, EDatabaseWriteFlag_ReserveSpace))
					data->m_pData = (uint8 *)ndata;
				else
					memcpy(ndata, data->m_pData, data->m_Size);
			}
		}

		return MDB_SUCCESS;

	full:
		DMibDatabaseTrace("not enough room in page {}, got {} ptrs", mdb_dbg_pgno(mp), NUMKEYS(mp));
		DMibDatabaseTrace("upper-lower = {} - {} = {}", mp->mp_upper,mp->mp_lower,room);
		DMibDatabaseTrace("node size = {}", node_size);
		mc->mc_txn->mt_flags |= MDB_TXN_ERROR;
		return MDB_PAGE_FULL;
	}

	/** Delete the specified node from a page.
	 * @param[in] mc Cursor pointing to the node to delete.
	 * @param[in] ksize The size of a node. Only used if the page is
	 * part of a #EDatabaseSubFlag_SortedDuplicatesFixedSize database.
	 */
	void NPrivate::mdb_node_del(MDB_cursor *mc, int ksize)
	{
		MDB_page *mp = mc->mc_pg[mc->mc_top];
		indx_t	indx = mc->mc_ki[mc->mc_top];
		uint32	 sz;
		indx_t		 i, j, numkeys, ptr;
		MDB_node	*node;
		uint8		*base;

		DMibDatabaseTrace("delete node {} on {} page {}", indx, IS_LEAF(mp) ? "leaf" : "branch", mdb_dbg_pgno(mp));
		numkeys = NUMKEYS(mp);
		DMibCheck(indx < numkeys);

		if (IS_LEAF2(mp)) {
			int x = numkeys - 1 - indx;
			base = LEAF2KEY(mp, indx, ksize);
			if (x)
				memmove(base, base + ksize, x * ksize);
			mp->mp_lower -= sizeof(indx_t);
			mp->mp_upper += ksize - sizeof(indx_t);
			return;
		}

		node = NODEPTR(mp, indx);
		sz = NODESIZE + node->mn_ksize;
		if (IS_LEAF(mp)) {
			if (F_ISSET(node->mn_flags, F_BIGDATA))
				sz += sizeof(pgno_t);
			else
				sz += NODEDSZ(node);
		}
		sz = EVEN(sz);

		ptr = mp->mp_ptrs[indx];
		for (i = j = 0; i < numkeys; i++) {
			if (i != indx) {
				mp->mp_ptrs[j] = mp->mp_ptrs[i];
				if (mp->mp_ptrs[i] < ptr)
					mp->mp_ptrs[j] += sz;
				j++;
			}
		}

		base = (uint8 *)mp + mp->mp_upper + PAGEBASE;
		memmove(base + sz, base, ptr - mp->mp_upper);

		mp->mp_lower -= sizeof(indx_t);
		mp->mp_upper += sz;
	}

	/** Compact the main page after deleting a node on a subpage.
	 * @param[in] mp The main page to operate on.
	 * @param[in] indx The index of the subpage on the main page.
	 */
	void NPrivate::mdb_node_shrink(MDB_page *mp, indx_t indx)
	{
		MDB_node *node;
		MDB_page *sp, *xp;
		char *base;
		indx_t delta, nsize, len, ptr;
		int i;

		node = NODEPTR(mp, indx);
		sp = (MDB_page *)NODEDATA(node);
		delta = SIZELEFT(sp);
		nsize = NODEDSZ(node) - delta;

		/* Prepare to shift upward, set len = length(subpage part to shift) */
		if (IS_LEAF2(sp)) {
			len = nsize;
			if (nsize & 1)
				return;		/* do not make the node uneven-sized */
		} else {
			xp = (MDB_page *)((char *)sp + delta); /* destination subpage */
			for (i = NUMKEYS(sp); --i >= 0; )
				xp->mp_ptrs[i] = sp->mp_ptrs[i] - delta;
			len = PAGEHDRSZ;
		}
		sp->mp_upper = sp->mp_lower;
		COPY_PGNO(sp->mp_p.p_pgno, mp->mp_p.p_pgno);
		SETDSZ(node, nsize);

		/* Shift <lower nodes...initial part of subpage> upward */
		base = (char *)mp + mp->mp_upper + PAGEBASE;
		memmove(base + delta, base, (char *)sp + len - base);

		ptr = mp->mp_ptrs[indx];
		for (i = NUMKEYS(mp); --i >= 0; ) {
			if (mp->mp_ptrs[i] <= ptr)
				mp->mp_ptrs[i] += delta;
		}
		mp->mp_upper += delta;
	}

	/** Move a node from csrc to cdst.
	 */
	int NPrivate::mdb_node_move(MDB_cursor *csrc, MDB_cursor *cdst, int fromleft)
	{
		MDB_node		*srcnode;
		CDatabaseValue		 key, data;
		pgno_t	srcpg;
		MDB_cursor mn;
		int			 rc;
		unsigned short flags;

		DKBUF;

		/* Mark src and dst as dirty. */
		if ((rc = mdb_page_touch(csrc)) ||
			(rc = mdb_page_touch(cdst)))
			return rc;

		if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
			key.m_Size = csrc->mc_db->md_pad;
			key.m_pData = LEAF2KEY(csrc->mc_pg[csrc->mc_top], csrc->mc_ki[csrc->mc_top], key.m_Size);
			data.m_Size = 0;
			data.m_pData = nullptr;
			srcpg = 0;
			flags = 0;
		} else {
			srcnode = NODEPTR(csrc->mc_pg[csrc->mc_top], csrc->mc_ki[csrc->mc_top]);
			DMibCheck(!((mint)srcnode & 1));
			srcpg = NODEPGNO(srcnode);
			flags = srcnode->mn_flags;
			if (csrc->mc_ki[csrc->mc_top] == 0 && IS_BRANCH(csrc->mc_pg[csrc->mc_top])) {
				uint32 snum = csrc->mc_snum;
				MDB_node *s2;
				/* must find the lowest key below src */
				rc = mdb_page_search_lowest(csrc);
				if (rc)
					return rc;
				if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
					key.m_Size = csrc->mc_db->md_pad;
					key.m_pData = LEAF2KEY(csrc->mc_pg[csrc->mc_top], 0, key.m_Size);
				} else {
					s2 = NODEPTR(csrc->mc_pg[csrc->mc_top], 0);
					key.m_Size = NODEKSZ(s2);
					key.m_pData = NODEKEY(s2);
				}
				csrc->mc_snum = snum--;
				csrc->mc_top = snum;
			} else {
				key.m_Size = NODEKSZ(srcnode);
				key.m_pData = NODEKEY(srcnode);
			}
			data.m_Size = NODEDSZ(srcnode);
			data.m_pData = NODEDATA(srcnode);
		}
		mn.mc_xcursor = nullptr;
		if (IS_BRANCH(cdst->mc_pg[cdst->mc_top]) && cdst->mc_ki[cdst->mc_top] == 0) {
			uint32 snum = cdst->mc_snum;
			MDB_node *s2;
			CDatabaseValue bkey;
			/* must find the lowest key below dst */
			mdb_cursor_copy(cdst, &mn);
			rc = mdb_page_search_lowest(&mn);
			if (rc)
				return rc;
			if (IS_LEAF2(mn.mc_pg[mn.mc_top])) {
				bkey.m_Size = mn.mc_db->md_pad;
				bkey.m_pData = LEAF2KEY(mn.mc_pg[mn.mc_top], 0, bkey.m_Size);
			} else {
				s2 = NODEPTR(mn.mc_pg[mn.mc_top], 0);
				bkey.m_Size = NODEKSZ(s2);
				bkey.m_pData = NODEKEY(s2);
			}
			mn.mc_snum = snum--;
			mn.mc_top = snum;
			mn.mc_ki[snum] = 0;
			rc = mdb_update_key(&mn, &bkey);
			if (rc)
				return rc;
		}

		DMibDatabaseTrace
			(
				"moving {} node {} [{}] on page {} to node {} on page {}"
				, IS_LEAF(csrc->mc_pg[csrc->mc_top]) ? "leaf" : "branch"
				, csrc->mc_ki[csrc->mc_top]
				, DKEY(&key)
				, csrc->mc_pg[csrc->mc_top]->mp_p.p_pgno
				, cdst->mc_ki[cdst->mc_top]
				, cdst->mc_pg[cdst->mc_top]->mp_p.p_pgno
			)
		;

		/* Add the node to the destination page.
		 */
		rc = mdb_node_add(cdst, cdst->mc_ki[cdst->mc_top], &key, &data, srcpg, flags);
		if (rc != MDB_SUCCESS)
			return rc;

		/* Delete the node from the source page.
		 */
		mdb_node_del(csrc, key.m_Size);

		{
			/* Adjust other cursors pointing to mp */
			MDB_cursor *m2, *m3;
			CDatabaseSubID dbi = csrc->mc_dbi;
			MDB_page *mpd, *mps;

			mps = csrc->mc_pg[csrc->mc_top];
			/* If we're adding on the left, bump others up */
			if (fromleft) {
				mpd = cdst->mc_pg[csrc->mc_top];
				for (m2 = csrc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
					if (csrc->mc_flags & C_SUB)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (!(m3->mc_flags & C_INITIALIZED) || m3->mc_top < csrc->mc_top)
						continue;
					if (m3 != cdst &&
						m3->mc_pg[csrc->mc_top] == mpd &&
						m3->mc_ki[csrc->mc_top] >= cdst->mc_ki[csrc->mc_top]) {
						m3->mc_ki[csrc->mc_top]++;
					}
					if (m3 !=csrc &&
						m3->mc_pg[csrc->mc_top] == mps &&
						m3->mc_ki[csrc->mc_top] == csrc->mc_ki[csrc->mc_top]) {
						m3->mc_pg[csrc->mc_top] = cdst->mc_pg[cdst->mc_top];
						m3->mc_ki[csrc->mc_top] = cdst->mc_ki[cdst->mc_top];
						m3->mc_ki[csrc->mc_top-1]++;
					}
					if (IS_LEAF(mps))
						XCURSOR_REFRESH(m3, csrc->mc_top, m3->mc_pg[csrc->mc_top]);
				}
			} else
			/* Adding on the right, bump others down */
			{
				for (m2 = csrc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
					if (csrc->mc_flags & C_SUB)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (m3 == csrc) continue;
					if (!(m3->mc_flags & C_INITIALIZED) || m3->mc_top < csrc->mc_top)
						continue;
					if (m3->mc_pg[csrc->mc_top] == mps) {
						if (!m3->mc_ki[csrc->mc_top]) {
							m3->mc_pg[csrc->mc_top] = cdst->mc_pg[cdst->mc_top];
							m3->mc_ki[csrc->mc_top] = cdst->mc_ki[cdst->mc_top];
							m3->mc_ki[csrc->mc_top-1]--;
						} else {
							m3->mc_ki[csrc->mc_top]--;
						}
						if (IS_LEAF(mps))
							XCURSOR_REFRESH(m3, csrc->mc_top, m3->mc_pg[csrc->mc_top]);
					}
				}
			}
		}

		/* Update the parent separators.
		 */
		if (csrc->mc_ki[csrc->mc_top] == 0) {
			if (csrc->mc_ki[csrc->mc_top-1] != 0) {
				if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
					key.m_pData = LEAF2KEY(csrc->mc_pg[csrc->mc_top], 0, key.m_Size);
				} else {
					srcnode = NODEPTR(csrc->mc_pg[csrc->mc_top], 0);
					key.m_Size = NODEKSZ(srcnode);
					key.m_pData = NODEKEY(srcnode);
				}
				DMibDatabaseTrace("update separator for source page {} to [{}]", csrc->mc_pg[csrc->mc_top]->mp_p.p_pgno, DKEY(&key));
				mdb_cursor_copy(csrc, &mn);
				mn.mc_snum--;
				mn.mc_top--;
				/* We want mdb_rebalance to find mn when doing fixups */
				WITH_CURSOR_TRACKING(mn,
					rc = mdb_update_key(&mn, &key));
				if (rc)
					return rc;
			}
			if (IS_BRANCH(csrc->mc_pg[csrc->mc_top])) {
				CDatabaseValue	 nullkey;
				indx_t	ix = csrc->mc_ki[csrc->mc_top];
				nullkey.m_Size = 0;
				csrc->mc_ki[csrc->mc_top] = 0;
				rc = mdb_update_key(csrc, &nullkey);
				csrc->mc_ki[csrc->mc_top] = ix;
				DMibCheck(rc == MDB_SUCCESS);
			}
		}

		if (cdst->mc_ki[cdst->mc_top] == 0) {
			if (cdst->mc_ki[cdst->mc_top-1] != 0) {
				if (IS_LEAF2(csrc->mc_pg[csrc->mc_top])) {
					key.m_pData = LEAF2KEY(cdst->mc_pg[cdst->mc_top], 0, key.m_Size);
				} else {
					srcnode = NODEPTR(cdst->mc_pg[cdst->mc_top], 0);
					key.m_Size = NODEKSZ(srcnode);
					key.m_pData = NODEKEY(srcnode);
				}
				DMibDatabaseTrace("update separator for destination page {} to [{}]", cdst->mc_pg[cdst->mc_top]->mp_p.p_pgno, DKEY(&key));
				mdb_cursor_copy(cdst, &mn);
				mn.mc_snum--;
				mn.mc_top--;
				/* We want mdb_rebalance to find mn when doing fixups */
				WITH_CURSOR_TRACKING(mn,
					rc = mdb_update_key(&mn, &key));
				if (rc)
					return rc;
			}
			if (IS_BRANCH(cdst->mc_pg[cdst->mc_top])) {
				CDatabaseValue	 nullkey;
				indx_t	ix = cdst->mc_ki[cdst->mc_top];
				nullkey.m_Size = 0;
				cdst->mc_ki[cdst->mc_top] = 0;
				rc = mdb_update_key(cdst, &nullkey);
				cdst->mc_ki[cdst->mc_top] = ix;
				DMibCheck(rc == MDB_SUCCESS);
			}
		}

		return MDB_SUCCESS;
	}

	/** Calculate the size of a leaf node.
	 * The size depends on the environment's page size; if a data item
	 * is too large it will be put onto an overflow page and the node
	 * size will only include the key and not the data. Sizes are always
	 * rounded up to an even number of bytes, to guarantee 2-byte alignment
	 * of the #MDB_node headers.
	 * @param[in] env The environment handle.
	 * @param[in] key The key for the node.
	 * @param[in] data The data for the node.
	 * @return The number of bytes needed to store the node.
	 */
	mint CDatabaseEnvironment_Internal::mdb_leaf_size(CDatabaseValue *key, CDatabaseValue *data)
	{
		mint		 sz;

		sz = LEAFSIZE(key, data);
		if (sz > me_nodemax) {
			/* put on overflow page */
			sz -= data->m_Size - sizeof(pgno_t);
		}

		return EVEN(sz + sizeof(indx_t));
	}

	/** Calculate the size of a branch node.
	 * The size should depend on the environment's page size but since
	 * we currently don't support spilling large keys onto overflow
	 * pages, it's simply the size of the #MDB_node header plus the
	 * size of the key. Sizes are always rounded up to an even number
	 * of bytes, to guarantee 2-byte alignment of the #MDB_node headers.
	 * @param[in] env The environment handle.
	 * @param[in] key The key for the node.
	 * @return The number of bytes needed to store the node.
	 */
	mint CDatabaseEnvironment_Internal::mdb_branch_size(CDatabaseValue *key)
	{
		mint		 sz;

		sz = INDXSIZE(key);
		if (sz > me_nodemax) {
			/* put on overflow page */
			/* not implemented */
			/* sz -= key->size - sizeof(pgno_t); */
		}

		return sz + sizeof(indx_t);
	}

	/** Replace the key for a branch node with a new key.
	 * Set #MDB_TXN_ERROR on failure.
	 * @param[in] mc Cursor pointing to the node to operate on.
	 * @param[in] key The new key to use.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_update_key(MDB_cursor *mc, CDatabaseValue *key)
	{
		MDB_page		*mp;
		MDB_node		*node;
		char			*base;
		mint			 len;
		int				 delta, ksize, oksize;
		indx_t			 ptr, i, numkeys, indx;
		DKBUF;

		indx = mc->mc_ki[mc->mc_top];
		mp = mc->mc_pg[mc->mc_top];
		node = NODEPTR(mp, indx);
		ptr = mp->mp_ptrs[indx];
	#if DMibDatabaseDebug
		{
			CDatabaseValue	k2;
			char kbuf2[DKBUF_MAXKEYSIZE*2+1];
			k2.m_pData = NODEKEY(node);
			k2.m_Size = node->mn_ksize;
			DMibDatabaseTrace
				(
					"update key {} (ofs {}) [{}] to [{}] on page {}"
					, indx
					, ptr
					, mdb_dkey(&k2, kbuf2)
					, DKEY(key)
					, mp->mp_p.p_pgno
				)
			;
		}
	#endif

		/* Sizes must be 2-byte aligned. */
		ksize = EVEN(key->m_Size);
		oksize = EVEN(node->mn_ksize);
		delta = ksize - oksize;

		/* Shift node contents if EVEN(key length) changed. */
		if (delta) {
			if (delta > 0 && SIZELEFT(mp) < delta) {
				pgno_t pgno;
				/* not enough space left, do a delete and split */
				DMibDatabaseTrace("Not enough room, delta = {}, splitting...", delta);
				pgno = NODEPGNO(node);
				mdb_node_del(mc, 0);
				return mdb_page_split(mc, key, nullptr, pgno, MDB_SPLIT_REPLACE);
			}

			numkeys = NUMKEYS(mp);
			for (i = 0; i < numkeys; i++) {
				if (mp->mp_ptrs[i] <= ptr)
					mp->mp_ptrs[i] -= delta;
			}

			base = (char *)mp + mp->mp_upper + PAGEBASE;
			len = ptr - mp->mp_upper + NODESIZE;
			memmove(base - delta, base, len);
			mp->mp_upper -= delta;

			node = NODEPTR(mp, indx);
		}

		/* But even if no shift was needed, update ksize */
		if (node->mn_ksize != key->m_Size)
			node->mn_ksize = key->m_Size;

		if (key->m_Size)
			memcpy(NODEKEY(node), key->m_pData, key->m_Size);

		return MDB_SUCCESS;
	}

	/** Rebalance the tree after a delete operation.
	 * @param[in] mc Cursor pointing to the page where rebalancing
	 * should begin.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_rebalance(MDB_cursor *mc)
	{
		MDB_node	*node;
		int rc, fromleft;
		uint32 ptop, minkeys, thresh;
		MDB_cursor	mn;
		indx_t oldki;

		if (IS_BRANCH(mc->mc_pg[mc->mc_top])) {
			minkeys = 2;
			thresh = 1;
		} else {
			minkeys = 1;
			thresh = FILL_THRESHOLD;
		}
		DMibDatabaseTrace
			(
				"rebalancing {} page {} (has {} keys, {fe1} full)"
				, IS_LEAF(mc->mc_pg[mc->mc_top]) ? "leaf" : "branch"
				, mdb_dbg_pgno(mc->mc_pg[mc->mc_top])
				, NUMKEYS(mc->mc_pg[mc->mc_top])
				, (float)PAGEFILL(mc->mc_txn->mt_env, mc->mc_pg[mc->mc_top]) / 10
			)
		;

		if (PAGEFILL(mc->mc_txn->mt_env, mc->mc_pg[mc->mc_top]) >= thresh &&
			NUMKEYS(mc->mc_pg[mc->mc_top]) >= minkeys) {
			DMibDatabaseTrace("no need to rebalance page {}, above fill threshold", mdb_dbg_pgno(mc->mc_pg[mc->mc_top]));
			return MDB_SUCCESS;
		}

		if (mc->mc_snum < 2) {
			MDB_page *mp = mc->mc_pg[0];
			if (IS_SUBP(mp)) {
				DPUTS("Can't rebalance a subpage, ignoring");
				return MDB_SUCCESS;
			}
			if (NUMKEYS(mp) == 0) {
				DPUTS("tree is completely empty");
				mc->mc_db->md_root = P_INVALID;
				mc->mc_db->md_depth = 0;
				mc->mc_db->md_leaf_pages = 0;
				mdb_midl_append(&mc->mc_txn->mt_free_pgs, mp->mp_p.p_pgno);
				/* Adjust cursors pointing to mp */
				mc->mc_snum = 0;
				mc->mc_top = 0;
				mc->mc_flags &= ~C_INITIALIZED;
				{
					MDB_cursor *m2, *m3;
					CDatabaseSubID dbi = mc->mc_dbi;

					for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
						if (mc->mc_flags & C_SUB)
							m3 = &m2->mc_xcursor->mx_cursor;
						else
							m3 = m2;
						if (!(m3->mc_flags & C_INITIALIZED) || (m3->mc_snum < mc->mc_snum))
							continue;
						if (m3->mc_pg[0] == mp) {
							m3->mc_snum = 0;
							m3->mc_top = 0;
							m3->mc_flags &= ~C_INITIALIZED;
						}
					}
				}
			} else if (IS_BRANCH(mp) && NUMKEYS(mp) == 1) {
				int i;
				DPUTS("collapsing root page!");
				mdb_midl_append(&mc->mc_txn->mt_free_pgs, mp->mp_p.p_pgno);
				mc->mc_db->md_root = NODEPGNO(NODEPTR(mp, 0));
				rc = mdb_page_get(mc, mc->mc_db->md_root, &mc->mc_pg[0], nullptr);
				if (rc)
					return rc;
				mc->mc_db->md_depth--;
				mc->mc_db->md_branch_pages--;
				mc->mc_ki[0] = mc->mc_ki[1];
				for (i = 1; i<mc->mc_db->md_depth; i++) {
					mc->mc_pg[i] = mc->mc_pg[i+1];
					mc->mc_ki[i] = mc->mc_ki[i+1];
				}
				{
					/* Adjust other cursors pointing to mp */
					MDB_cursor *m2, *m3;
					CDatabaseSubID dbi = mc->mc_dbi;

					for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2=m2->mc_next) {
						if (mc->mc_flags & C_SUB)
							m3 = &m2->mc_xcursor->mx_cursor;
						else
							m3 = m2;
						if (m3 == mc) continue;
						if (!(m3->mc_flags & C_INITIALIZED))
							continue;
						if (m3->mc_pg[0] == mp) {
							for (i=0; i<mc->mc_db->md_depth; i++) {
								m3->mc_pg[i] = m3->mc_pg[i+1];
								m3->mc_ki[i] = m3->mc_ki[i+1];
							}
							m3->mc_snum--;
							m3->mc_top--;
						}
					}
				}
			} else
				DPUTS("root page doesn't need rebalancing");
			return MDB_SUCCESS;
		}

		/* The parent (branch page) must have at least 2 pointers,
		 * otherwise the tree is invalid.
		 */
		ptop = mc->mc_top-1;
		DMibCheck(NUMKEYS(mc->mc_pg[ptop]) > 1);

		/* Leaf page fill factor is below the threshold.
		 * Try to move keys from left or right neighbor, or
		 * merge with a neighbor page.
		 */

		/* Find neighbors.
		 */
		mdb_cursor_copy(mc, &mn);
		mn.mc_xcursor = nullptr;

		oldki = mc->mc_ki[mc->mc_top];
		if (mc->mc_ki[ptop] == 0) {
			/* We're the leftmost leaf in our parent.
			 */
			DPUTS("reading right neighbor");
			mn.mc_ki[ptop]++;
			node = NODEPTR(mc->mc_pg[ptop], mn.mc_ki[ptop]);
			rc = mdb_page_get(mc, NODEPGNO(node), &mn.mc_pg[mn.mc_top], nullptr);
			if (rc)
				return rc;
			mn.mc_ki[mn.mc_top] = 0;
			mc->mc_ki[mc->mc_top] = NUMKEYS(mc->mc_pg[mc->mc_top]);
			fromleft = 0;
		} else {
			/* There is at least one neighbor to the left.
			 */
			DPUTS("reading left neighbor");
			mn.mc_ki[ptop]--;
			node = NODEPTR(mc->mc_pg[ptop], mn.mc_ki[ptop]);
			rc = mdb_page_get(mc, NODEPGNO(node), &mn.mc_pg[mn.mc_top], nullptr);
			if (rc)
				return rc;
			mn.mc_ki[mn.mc_top] = NUMKEYS(mn.mc_pg[mn.mc_top]) - 1;
			mc->mc_ki[mc->mc_top] = 0;
			fromleft = 1;
		}

		DMibDatabaseTrace
			(
				"found neighbor page {} ({} keys, {fe1} full)"
				, mn.mc_pg[mn.mc_top]->mp_p.p_pgno
				, NUMKEYS(mn.mc_pg[mn.mc_top])
				, (float)PAGEFILL(mc->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) / 10
			)
		;

		/* If the neighbor page is above threshold and has enough keys,
		 * move one key from it. Otherwise we should try to merge them.
		 * (A branch page must never have less than 2 keys.)
		 */
		if (PAGEFILL(mc->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) >= thresh && NUMKEYS(mn.mc_pg[mn.mc_top]) > minkeys) {
			rc = mdb_node_move(&mn, mc, fromleft);
			if (fromleft) {
				/* if we inserted on left, bump position up */
				oldki++;
			}
		} else {
			if (!fromleft) {
				rc = mdb_page_merge(&mn, mc);
			} else {
				oldki += NUMKEYS(mn.mc_pg[mn.mc_top]);
				mn.mc_ki[mn.mc_top] += mc->mc_ki[mn.mc_top] + 1;
				/* We want mdb_rebalance to find mn when doing fixups */
				WITH_CURSOR_TRACKING(mn,
					rc = mdb_page_merge(mc, &mn));
				mdb_cursor_copy(&mn, mc);
			}
			mc->mc_flags &= ~C_EOF;
		}
		mc->mc_ki[mc->mc_top] = oldki;
		return rc;
	}

	/** Add all the DB's pages to the free list.
	 * @param[in] mc Cursor on the DB to free.
	 * @param[in] subs non-Zero to check for sub-DBs in this DB.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::mdb_drop0(MDB_cursor *mc, int subs)
	{
		int rc;

		rc = mdb_page_search(mc, nullptr, MDB_PS_FIRST);
		if (rc == MDB_SUCCESS) {
			MDB_txn *txn = mc->mc_txn;
			MDB_node *ni;
			MDB_cursor mx;
			uint32 i;

			/* DUPSORT sub-DBs have no ovpages/DBs. Omit scanning leaves.
			 * This also avoids any P_LEAF2 pages, which have no nodes.
			 * Also if the DB doesn't have sub-DBs and has no overflow
			 * pages, omit scanning leaves.
			 */
			if ((mc->mc_flags & C_SUB) ||
				(!subs && !mc->mc_db->md_overflow_pages))
				mdb_cursor_pop(mc);

			mdb_cursor_copy(mc, &mx);
	#ifdef DMibDatabase_Always64BitSupport
			/* bump refcount for mx's pages */
			for (i=0; i<mc->mc_snum; i++)
				mdb_page_get(&mx, mc->mc_pg[i]->mp_p.p_pgno, &mx.mc_pg[i], nullptr);
	#endif
			while (mc->mc_snum > 0) {
				MDB_page *mp = mc->mc_pg[mc->mc_top];
				unsigned n = NUMKEYS(mp);
				if (IS_LEAF(mp)) {
					for (i=0; i<n; i++) {
						ni = NODEPTR(mp, i);
						if (ni->mn_flags & F_BIGDATA) {
							MDB_page *omp;
							pgno_t pg;
							memcpy(&pg, NODEDATA(ni), sizeof(pg));
							rc = mdb_page_get(mc, pg, &omp, nullptr);
							if (rc != 0)
								goto done;
							DMibCheck(IS_OVERFLOW(omp));
							mdb_midl_append_range(&txn->mt_free_pgs, pg, omp->mp_pages);
							mc->mc_db->md_overflow_pages -= omp->mp_pages;
							if (!mc->mc_db->md_overflow_pages && !subs)
								break;
						} else if (subs && (ni->mn_flags & F_SUBDATA)) {
							mdb_xcursor_init1(mc, ni);
							rc = mdb_drop0(&mc->mc_xcursor->mx_cursor, 0);
							if (rc)
								goto done;
						}
					}
					if (!subs && !mc->mc_db->md_overflow_pages)
						goto pop;
				} else {
					mdb_midl_need(&txn->mt_free_pgs, n);
					for (i=0; i<n; i++)
					{
						pgno_t pg;
						ni = NODEPTR(mp, i);
						pg = NODEPGNO(ni);
						/* free it */
						mdb_midl_xappend(txn->mt_free_pgs, pg);
					}
				}
				if (!mc->mc_top)
					break;
				mc->mc_ki[mc->mc_top] = i;
				rc = mdb_cursor_sibling(mc, 1);
				if (rc) {
					if (rc != MDB_NOTFOUND)
						goto done;
					/* no more siblings, go back to beginning
					 * of previous level.
					 */
	pop:
					mdb_cursor_pop(mc);
					mc->mc_ki[0] = 0;
					for (i=1; i<mc->mc_snum; i++) {
						mc->mc_ki[i] = 0;
						mc->mc_pg[i] = mx.mc_pg[i];
					}
				}
			}
			/* free it */
			mdb_midl_append(&txn->mt_free_pgs, mc->mc_db->md_root);
	done:
			if (rc)
				txn->mt_flags |= MDB_TXN_ERROR;
	#ifdef DMibDatabase_Always64BitSupport
			/* drop refcount for mx's pages */
			mdb_cursor_unref(&mx);
	#endif
		} else if (rc == MDB_NOTFOUND) {
			rc = MDB_SUCCESS;
		}
		mc->mc_flags &= ~C_INITIALIZED;
		return rc;
	}
}
