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
	/** Search for key within a page, using binary search.
	 * Returns the smallest entry larger or equal to the key.
	 * If exactp is non-null, stores whether the found entry was an exact match
	 * in *exactp (1 or 0).
	 * Updates the cursor index with the index of the found entry.
	 * If no entry larger or equal to the key is found, returns nullptr.
	 */
	NPrivate::CMdbNode *NPrivate::fg_NodeSearch(CMdbCursor *mc, CDatabaseValue const *key, int *exactp)
	{
		uint32 i = 0, nkeys;
		int low, high;
		int rc = 0;
		CMdbPage *mp = mc->mc_pg[mc->mc_top];
		CMdbNode *node = nullptr;
		CDatabaseValue nodekey;
		FDatabaseCompare *pCompare;
		DKBuf;

		nkeys = DNumKeys(mp);

		DMibDatabaseTrace("searching {} keys in {} {}page {}", nkeys, DIsLeaf(mp) ? "leaf" : "branch", DIsSubp(mp) ? "sub-" : "", fg_DbgPgno(mp));

		low = DIsLeaf(mp) ? 0 : 1;
		high = nkeys - 1;
		pCompare = &(mc->mc_dbx->m_fKeyCompare);

		/* Branch pages have no data, so if using integer keys,
		 * alignment is guaranteed. Use faster fg_CmpInt.
		 */
		if (DIsBranch(mp) && *pCompare == g_mdb_cmp_cint)
		{
			if (DNodePtr(mp, 1)->mn_ksize == sizeof(CDatabaseOffset))
				pCompare = &g_mdb_cmp_long;
			else
				pCompare = &g_mdb_cmp_int;
		}

		if (DIsLeaf2(mp))
		{
			nodekey.m_Size = mc->mc_db->md_pad;
			node = DNodePtr(mp, 0); // fake
			while (low <= high)
			{
				i = (low + high) >> 1;
				nodekey.m_pData = DLeaf2Key(mp, i, nodekey.m_Size);
				rc = (*pCompare)(*key, nodekey);
				DMibDatabaseTrace("found leaf index {} [{}], rc = {}", i, DKey(&nodekey), rc);
				if (rc == 0)
					break;
				if (rc > 0)
					low = i + 1;
				else
					high = i - 1;
			}
		}
		else
		{
			while (low <= high)
			{
				i = (low + high) >> 1;

				node = DNodePtr(mp, i);
				nodekey.m_Size = DNodeKeySize(node);
				nodekey.m_pData = DNodeKey(node);

				rc = (*pCompare)(*key, nodekey);
#if DMibDatabaseDebug
				if (DIsLeaf(mp))
					DMibDatabaseTrace("found leaf index {} [{}], rc = {}", i, DKey(&nodekey), rc);
				else
					DMibDatabaseTrace("found branch index {} [{} -> {}], rc = {}", i, DKey(&nodekey), DNodePgno(node), rc);
#endif
				if (rc == 0)
					break;
				if (rc > 0)
					low = i + 1;
				else
					high = i - 1;
			}
		}

		if (rc > 0)
		{ // Found entry is less than the key.
			i++; // Skip to get the smallest entry larger than key.
			if (!DIsLeaf2(mp))
				node = DNodePtr(mp, i);
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
	int NPrivate::fg_NodeRead(CMdbCursor *mc, CMdbNode *leaf, CDatabaseValue *data)
	{
		CMdbPage *omp; // overflow page
		CPgno pgno;
		int rc;

		if (DMcOvpg(mc))
		{
			DMdbPageUnref(mc->mc_txn, DMcOvpg(mc));
			DMcSetOvpg(mc, NULL);
		}
		if (!DFIsSet(leaf->mn_flags, DFBigData))
		{
			data->m_Size = DNodeDataSize(leaf);
			data->m_pData = DNodeData(leaf);
			return DMdbSuccess;
		}

		/* Read overflow data.
		*/
		data->m_Size = DNodeDataSize(leaf);
		memcpy(&pgno, DNodeData(leaf), sizeof(pgno));
		if ((rc = fg_PageGet(mc, pgno, &omp, nullptr)) != 0)
		{
			DMibDatabaseTrace("read overflow page {} failed", pgno);
			return rc;
		}
		data->m_pData = DMetaData(omp);
		DMcSetOvpg(mc, omp);

		return DMdbSuccess;
	}

	/** Add a node to the page pointed to by the cursor.
	 * Set #DMdbTxnError on failure.
	 * @param[in] mc The cursor for this operation.
	 * @param[in] indx The index on the page where the new node should be added.
	 * @param[in] key The key for the new node.
	 * @param[in] data The data for the new node, if any.
	 * @param[in] pgno The page number, if adding a branch node.
	 * @param[in] flags Flags for the node.
	 * @return 0 on success, non-zero on failure. Possible errors are:
	 * <ul>
	 *	<li>ENOMEM - failed to allocate overflow pages for the node.
	 *	<li>DMdbPageFull - there is insufficient room in the page. This error
	 *	should never happen since all callers already calculate the
	 *	page's free space before calling this function.
	 * </ul>
	 */
	int NPrivate::fg_NodeAdd(CMdbCursor *mc, CIndx indx, CDatabaseValue *key, CDatabaseValue *data, CPgno pgno, uint32 flags)
	{
		uint32 i;
		umint node_size = DNodeSize;
		smint room;
		CIndx ofs;
		CMdbNode *node;
		CMdbPage *mp = mc->mc_pg[mc->mc_top];
		CMdbPage *ofp = nullptr; // overflow page
		void *ndata;
		DKBuf;

		DMibCheck(mp->mp_upper >= mp->mp_lower);

		DMibDatabaseTrace
			(
				"add to {} {}page {} index {}, data size {} key size {} [{}]"
				, DIsLeaf(mp) ? "leaf" : "branch"
				, DIsSubp(mp) ? "sub-" : ""
				, fg_DbgPgno(mp)
				, indx
				, data ? data->m_Size : 0
				, key ? key->m_Size : 0
				, key ? DKey(key) : "null"
			)
		;

		if (DIsLeaf2(mp))
		{
			/* Move higher keys up one slot. */
			int ksize = mc->mc_db->md_pad, dif;
			uint8 *ptr = DLeaf2Key(mp, indx, ksize);
			dif = DNumKeys(mp) - indx;
			if (dif > 0)
				memmove(ptr + ksize, ptr, dif*ksize);
			/* insert new key */
			memcpy(ptr, key->m_pData, ksize);

			/* Just using these for counting */
			mp->mp_lower += sizeof(CIndx);
			mp->mp_upper -= CIndx(ksize - sizeof(CIndx));
			return DMdbSuccess;
		}

		room = (smint)DSizeLeft(mp) - (smint)sizeof(CIndx);
		if (key != nullptr)
			node_size += key->m_Size;
		if (DIsLeaf(mp))
		{
			DMibCheck(key && data);
			if (DFIsSet(flags, DFBigData))
			{
				/* Data already on overflow page. */
				node_size += sizeof(CPgno);
			}
			else if (node_size + data->m_Size > mc->mc_txn->mt_env->me_nodemax)
			{
				int ovpages = DOverflowPages(data->m_Size, mc->mc_txn->mt_env->me_psize);
				int rc;
				/* Put data on overflow page. */
				DMibDatabaseTrace("data size is {}, node would be {}, put data on overflow page", data->m_Size, node_size + data->m_Size);
				node_size = DEven(node_size + sizeof(CPgno));
				if ((smint)node_size > room)
					goto full;
				if ((rc = fg_PageNew(mc, DPOverflow, ovpages, &ofp)))
					return rc;
				DMibDatabaseTrace("allocated overflow page {}", ofp->mp_p.p_pgno);
				flags |= DFBigData;
				goto update;
			}
			else
				node_size += data->m_Size;
		}
		node_size = DEven(node_size);
		if ((smint)node_size > room)
			goto full;

	update:
		/* Move higher pointers up one slot. */
		for (i = DNumKeys(mp); i > indx; i--)
			mp->mp_ptrs[i] = mp->mp_ptrs[i - 1];

		/* Adjust free space offsets. */
		ofs = mp->mp_upper - node_size;
		DMibCheck(ofs >= mp->mp_lower + sizeof(CIndx));
		mp->mp_ptrs[indx] = ofs;
		mp->mp_upper = ofs;
		mp->mp_lower += sizeof(CIndx);

		/* Write the node data. */
		node = DNodePtr(mp, indx);
		node->mn_ksize = (key == nullptr) ? 0 : key->m_Size;
		node->mn_flags = flags;
		if (DIsLeaf(mp))
			DSetDataSize(node, data->m_Size);
		else
			DSetPgno(node, pgno);

		if (key)
			memcpy(DNodeKey(node), key->m_pData, key->m_Size);

		if (DIsLeaf(mp))
		{
			ndata = (CMdbPage *)DNodeData(node);
			if (ofp == nullptr)
			{
				if (DFIsSet(flags, DFBigData))
					memcpy(ndata, data->m_pData, sizeof(CPgno));
				else if (DFIsSet(flags, EDatabaseWriteFlag_ReserveSpace))
					data->m_pData = (uint8 *)ndata;
				else if (data->m_Size)
					memcpy(ndata, data->m_pData, data->m_Size);
			}
			else
			{
				memcpy(ndata, &ofp->mp_p.p_pgno, sizeof(CPgno));
				ndata = DMetaData(ofp);
				if (DFIsSet(flags, EDatabaseWriteFlag_ReserveSpace))
					data->m_pData = (uint8 *)ndata;
				else if (data->m_Size)
					memcpy(ndata, data->m_pData, data->m_Size);
			}
		}

		return DMdbSuccess;

	full:
		DMibDatabaseTrace("not enough room in page {}, got {} ptrs", fg_DbgPgno(mp), DNumKeys(mp));
		DMibDatabaseTrace("upper-lower = {} - {} = {}", mp->mp_upper, mp->mp_lower, room);
		DMibDatabaseTrace("node size = {}", node_size);
		mc->mc_txn->mt_flags |= DMdbTxnError;
		return DMdbPageFull;
	}

	/** Delete the specified node from a page.
	 * @param[in] mc Cursor pointing to the node to delete.
	 * @param[in] ksize The size of a node. Only used if the page is
	 * part of a #EDatabaseSubFlag_SortedDuplicatesFixedSize database.
	 */
	void NPrivate::fg_NodeDel(CMdbCursor *mc, int ksize)
	{
		CMdbPage *mp = mc->mc_pg[mc->mc_top];
		CIndx indx = mc->mc_ki[mc->mc_top];
		uint32 sz;
		CIndx i, j, numkeys, ptr;
		CMdbNode *node;
		uint8 *base;

		DMibDatabaseTrace("delete node {} on {} page {}", indx, DIsLeaf(mp) ? "leaf" : "branch", fg_DbgPgno(mp));
		numkeys = DNumKeys(mp);
		DMibCheck(indx < numkeys);

		if (DIsLeaf2(mp))
		{
			int x = numkeys - 1 - indx;
			base = DLeaf2Key(mp, indx, ksize);
			if (x)
				memmove(base, base + ksize, x * ksize);
			mp->mp_lower -= sizeof(CIndx);
			mp->mp_upper += CIndx(ksize - sizeof(CIndx));
			return;
		}

		node = DNodePtr(mp, indx);
		sz = DNodeSize + node->mn_ksize;
		if (DIsLeaf(mp))
		{
			if (DFIsSet(node->mn_flags, DFBigData))
				sz += sizeof(CPgno);
			else
				sz += DNodeDataSize(node);
		}
		sz = DEven(sz);

		ptr = mp->mp_ptrs[indx];
		for (i = j = 0; i < numkeys; i++)
		{
			if (i != indx)
			{
				mp->mp_ptrs[j] = mp->mp_ptrs[i];
				if (mp->mp_ptrs[i] < ptr)
					mp->mp_ptrs[j] += sz;
				j++;
			}
		}

		base = (uint8 *)mp + mp->mp_upper + DPageBase;
		memmove(base + sz, base, ptr - mp->mp_upper);

		mp->mp_lower -= sizeof(CIndx);
		mp->mp_upper += sz;
	}

	/** Compact the main page after deleting a node on a subpage.
	 * @param[in] mp The main page to operate on.
	 * @param[in] indx The index of the subpage on the main page.
	 */
	void NPrivate::fg_NodeShrink(CMdbPage *mp, CIndx indx)
	{
		CMdbNode *node;
		CMdbPage *sp, *xp;
		char *base;
		CIndx delta, nsize, len, ptr;
		int i;

		node = DNodePtr(mp, indx);
		sp = (CMdbPage *)DNodeData(node);
		delta = DSizeLeft(sp);
		nsize = DNodeDataSize(node) - delta;

		/* Prepare to shift upward, set len = length(subpage part to shift) */
		if (DIsLeaf2(sp))
		{
			len = nsize;
			if (nsize & 1)
				return; // do not make the node uneven-sized
		}
		else
		{
			xp = (CMdbPage *)((char *)sp + delta); // destination subpage
			for (i = DNumKeys(sp); --i >= 0;)
				xp->mp_ptrs[i] = sp->mp_ptrs[i] - delta;
			len = DPageHeaderSize;
		}
		sp->mp_upper = sp->mp_lower;
		DCopyPgno(sp->mp_p.p_pgno, mp->mp_p.p_pgno);
		DSetDataSize(node, uint32(nsize));

		/* Shift <lower nodes...initial part of subpage> upward */
		base = (char *)mp + mp->mp_upper + DPageBase;
		memmove(base + delta, base, (char *)sp + len - base);

		ptr = mp->mp_ptrs[indx];
		for (i = DNumKeys(mp); --i >= 0;)
		{
			if (mp->mp_ptrs[i] <= ptr)
				mp->mp_ptrs[i] += delta;
		}
		mp->mp_upper += delta;
	}

	/** Move a node from csrc to cdst.
	 */
	int NPrivate::fg_NodeMove(CMdbCursor *csrc, CMdbCursor *cdst, int fromleft)
	{
		CMdbNode *srcnode;
		CDatabaseValue key, data;
		CPgno srcpg;
		CMdbCursor mn;
		int rc;
		unsigned short flags;

		DKBuf;

		/* Mark src and dst as dirty. */
		if ((rc = fg_PageTouch(csrc)) || (rc = fg_PageTouch(cdst)))
			return rc;

		if (DIsLeaf2(csrc->mc_pg[csrc->mc_top]))
		{
			key.m_Size = csrc->mc_db->md_pad;
			key.m_pData = DLeaf2Key(csrc->mc_pg[csrc->mc_top], csrc->mc_ki[csrc->mc_top], key.m_Size);
			data.m_Size = 0;
			data.m_pData = nullptr;
			srcpg = 0;
			flags = 0;
		}
		else
		{
			srcnode = DNodePtr(csrc->mc_pg[csrc->mc_top], csrc->mc_ki[csrc->mc_top]);
			DMibCheck(!((umint)srcnode & 1));
			srcpg = DNodePgno(srcnode);
			flags = srcnode->mn_flags;
			if (csrc->mc_ki[csrc->mc_top] == 0 && DIsBranch(csrc->mc_pg[csrc->mc_top]))
			{
				uint32 snum = csrc->mc_snum;
				CMdbNode *s2;
				/* must find the lowest key below src */
				rc = fg_PageSearchLowest(csrc);
				if (rc)
					return rc;
				if (DIsLeaf2(csrc->mc_pg[csrc->mc_top]))
				{
					key.m_Size = csrc->mc_db->md_pad;
					key.m_pData = DLeaf2Key(csrc->mc_pg[csrc->mc_top], 0, key.m_Size);
				}
				else
				{
					s2 = DNodePtr(csrc->mc_pg[csrc->mc_top], 0);
					key.m_Size = DNodeKeySize(s2);
					key.m_pData = DNodeKey(s2);
				}
				csrc->mc_snum = snum--;
				csrc->mc_top = snum;
			}
			else
			{
				key.m_Size = DNodeKeySize(srcnode);
				key.m_pData = DNodeKey(srcnode);
			}
			data.m_Size = DNodeDataSize(srcnode);
			data.m_pData = DNodeData(srcnode);
		}
		mn.mc_xcursor = nullptr;
		if (DIsBranch(cdst->mc_pg[cdst->mc_top]) && cdst->mc_ki[cdst->mc_top] == 0)
		{
			uint32 snum = cdst->mc_snum;
			CMdbNode *s2;
			CDatabaseValue bkey;
			/* must find the lowest key below dst */
			fg_CursorCopy(cdst, &mn);
			rc = fg_PageSearchLowest(&mn);
			if (rc)
				return rc;
			if (DIsLeaf2(mn.mc_pg[mn.mc_top]))
			{
				bkey.m_Size = mn.mc_db->md_pad;
				bkey.m_pData = DLeaf2Key(mn.mc_pg[mn.mc_top], 0, bkey.m_Size);
			}
			else
			{
				s2 = DNodePtr(mn.mc_pg[mn.mc_top], 0);
				bkey.m_Size = DNodeKeySize(s2);
				bkey.m_pData = DNodeKey(s2);
			}
			mn.mc_snum = snum--;
			mn.mc_top = snum;
			mn.mc_ki[snum] = 0;
			rc = fg_UpdateKey(&mn, &bkey);
			if (rc)
				return rc;
		}

		DMibDatabaseTrace
			(
				"moving {} node {} [{}] on page {} to node {} on page {}"
				, DIsLeaf(csrc->mc_pg[csrc->mc_top]) ? "leaf" : "branch"
				, csrc->mc_ki[csrc->mc_top]
				, DKey(&key)
				, csrc->mc_pg[csrc->mc_top]->mp_p.p_pgno
				, cdst->mc_ki[cdst->mc_top]
				, cdst->mc_pg[cdst->mc_top]->mp_p.p_pgno
			)
		;

		/* Add the node to the destination page.
		*/
		rc = fg_NodeAdd(cdst, cdst->mc_ki[cdst->mc_top], &key, &data, srcpg, flags);
		if (rc != DMdbSuccess)
			return rc;

		/* Delete the node from the source page.
		*/
		fg_NodeDel(csrc, key.m_Size);

		{
			/* Adjust other cursors pointing to mp */
			CMdbCursor *m2, *m3;
			CDatabaseSubID dbi = csrc->mc_dbi;
			CMdbPage *mpd, *mps;

			mps = csrc->mc_pg[csrc->mc_top];
			/* If we're adding on the left, bump others up */
			if (fromleft)
			{
				mpd = cdst->mc_pg[csrc->mc_top];
				for (m2 = csrc->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
				{
					if (csrc->mc_flags & DCSub)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (!(m3->mc_flags & DCInitialized) || m3->mc_top < csrc->mc_top)
						continue;
					if (m3 != cdst && m3->mc_pg[csrc->mc_top] == mpd && m3->mc_ki[csrc->mc_top] >= cdst->mc_ki[csrc->mc_top])
						m3->mc_ki[csrc->mc_top]++;
					if (m3 != csrc && m3->mc_pg[csrc->mc_top] == mps && m3->mc_ki[csrc->mc_top] == csrc->mc_ki[csrc->mc_top])
					{
						m3->mc_pg[csrc->mc_top] = cdst->mc_pg[cdst->mc_top];
						m3->mc_ki[csrc->mc_top] = cdst->mc_ki[cdst->mc_top];
						m3->mc_ki[csrc->mc_top - 1]++;
					}
					if (DIsLeaf(mps))
						DXCursorRefresh(m3, csrc->mc_top, m3->mc_pg[csrc->mc_top]);
				}
			}
			else
			/* Adding on the right, bump others down */
			{
				for (m2 = csrc->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
				{
					if (csrc->mc_flags & DCSub)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (m3 == csrc)
						continue;
					if (!(m3->mc_flags & DCInitialized) || m3->mc_top < csrc->mc_top)
						continue;
					if (m3->mc_pg[csrc->mc_top] == mps)
					{
						if (!m3->mc_ki[csrc->mc_top])
						{
							m3->mc_pg[csrc->mc_top] = cdst->mc_pg[cdst->mc_top];
							m3->mc_ki[csrc->mc_top] = cdst->mc_ki[cdst->mc_top];
							m3->mc_ki[csrc->mc_top - 1]--;
						}
						else
							m3->mc_ki[csrc->mc_top]--;
						if (DIsLeaf(mps))
							DXCursorRefresh(m3, csrc->mc_top, m3->mc_pg[csrc->mc_top]);
					}
				}
			}
		}

		/* Update the parent separators.
		*/
		if (csrc->mc_ki[csrc->mc_top] == 0)
		{
			if (csrc->mc_ki[csrc->mc_top - 1] != 0)
			{
				if (DIsLeaf2(csrc->mc_pg[csrc->mc_top]))
					key.m_pData = DLeaf2Key(csrc->mc_pg[csrc->mc_top], 0, key.m_Size);
				else
				{
					srcnode = DNodePtr(csrc->mc_pg[csrc->mc_top], 0);
					key.m_Size = DNodeKeySize(srcnode);
					key.m_pData = DNodeKey(srcnode);
				}
				DMibDatabaseTrace("update separator for source page {} to [{}]", csrc->mc_pg[csrc->mc_top]->mp_p.p_pgno, DKey(&key));
				fg_CursorCopy(csrc, &mn);
				mn.mc_snum--;
				mn.mc_top--;
				/* We want fg_Rebalance to find mn when doing fixups */
				DWithCursorTracking(mn, rc = fg_UpdateKey(&mn, &key));
				if (rc)
					return rc;
			}
			if (DIsBranch(csrc->mc_pg[csrc->mc_top]))
			{
				CDatabaseValue nullkey;
				CIndx ix = csrc->mc_ki[csrc->mc_top];
				nullkey.m_Size = 0;
				csrc->mc_ki[csrc->mc_top] = 0;
				rc = fg_UpdateKey(csrc, &nullkey);
				csrc->mc_ki[csrc->mc_top] = ix;
				DMibCheck(rc == DMdbSuccess);
			}
		}

		if (cdst->mc_ki[cdst->mc_top] == 0)
		{
			if (cdst->mc_ki[cdst->mc_top - 1] != 0)
			{
				if (DIsLeaf2(csrc->mc_pg[csrc->mc_top]))
					key.m_pData = DLeaf2Key(cdst->mc_pg[cdst->mc_top], 0, key.m_Size);
				else
				{
					srcnode = DNodePtr(cdst->mc_pg[cdst->mc_top], 0);
					key.m_Size = DNodeKeySize(srcnode);
					key.m_pData = DNodeKey(srcnode);
				}
				DMibDatabaseTrace("update separator for destination page {} to [{}]", cdst->mc_pg[cdst->mc_top]->mp_p.p_pgno, DKey(&key));
				fg_CursorCopy(cdst, &mn);
				mn.mc_snum--;
				mn.mc_top--;
				/* We want fg_Rebalance to find mn when doing fixups */
				DWithCursorTracking(mn, rc = fg_UpdateKey(&mn, &key));
				if (rc)
					return rc;
			}
			if (DIsBranch(cdst->mc_pg[cdst->mc_top]))
			{
				CDatabaseValue nullkey;
				CIndx ix = cdst->mc_ki[cdst->mc_top];
				nullkey.m_Size = 0;
				cdst->mc_ki[cdst->mc_top] = 0;
				rc = fg_UpdateKey(cdst, &nullkey);
				cdst->mc_ki[cdst->mc_top] = ix;
				DMibCheck(rc == DMdbSuccess);
			}
		}

		return DMdbSuccess;
	}

	/** Calculate the size of a leaf node.
	 * The size depends on the environment's page size; if a data item
	 * is too large it will be put onto an overflow page and the node
	 * size will only include the key and not the data. Sizes are always
	 * rounded up to an even number of bytes, to guarantee 2-byte alignment
	 * of the #CMdbNode headers.
	 * @param[in] env The environment handle.
	 * @param[in] key The key for the node.
	 * @param[in] data The data for the node.
	 * @return The number of bytes needed to store the node.
	 */
	umint CDatabaseEnvironment_Internal::f_LeafSize(CDatabaseValue *key, CDatabaseValue *data)
	{
		umint sz;

		sz = DLeafSize(key, data);
		if (sz > me_nodemax)
		{
			/* put on overflow page */
			sz -= data->m_Size - sizeof(CPgno);
		}

		return DEven(sz + sizeof(CIndx));
	}

	/** Calculate the size of a branch node.
	 * The size should depend on the environment's page size but since
	 * we currently don't support spilling large keys onto overflow
	 * pages, it's simply the size of the #CMdbNode header plus the
	 * size of the key. Sizes are always rounded up to an even number
	 * of bytes, to guarantee 2-byte alignment of the #CMdbNode headers.
	 * @param[in] env The environment handle.
	 * @param[in] key The key for the node.
	 * @return The number of bytes needed to store the node.
	 */
	umint CDatabaseEnvironment_Internal::f_BranchSize(CDatabaseValue *key)
	{
		umint sz;

		sz = DIndexSize(key);
		if (sz > me_nodemax)
		{
			/* put on overflow page */
			/* not implemented */
			/* sz -= key->size - sizeof(CPgno); */
		}

		return sz + sizeof(CIndx);
	}

	/** Replace the key for a branch node with a new key.
	 * Set #DMdbTxnError on failure.
	 * @param[in] mc Cursor pointing to the node to operate on.
	 * @param[in] key The new key to use.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_UpdateKey(CMdbCursor *mc, CDatabaseValue *key)
	{
		CMdbPage *mp;
		CMdbNode *node;
		char *base;
		umint len;
		int delta, ksize, oksize;
		CIndx ptr, i, numkeys, indx;
		DKBuf;

		indx = mc->mc_ki[mc->mc_top];
		mp = mc->mc_pg[mc->mc_top];
		node = DNodePtr(mp, indx);
		ptr = mp->mp_ptrs[indx];
#if DMibDatabaseDebug
		{
			CDatabaseValue k2;
			char kbuf2[DKBufMaxKeySize * 2 + 1];
			k2.m_pData = DNodeKey(node);
			k2.m_Size = node->mn_ksize;
			DMibDatabaseTrace("update key {} (ofs {}) [{}] to [{}] on page {}", indx, ptr, fg_DKey(&k2, kbuf2), DKey(key), mp->mp_p.p_pgno);
		}
#endif

		/* Sizes must be 2-byte aligned. */
		ksize = DEven(key->m_Size);
		oksize = DEven(node->mn_ksize);
		delta = ksize - oksize;

		/* Shift node contents if DEven(key length) changed. */
		if (delta)
		{
			if (delta > 0 && DSizeLeft(mp) < delta)
			{
				CPgno pgno;
				/* not enough space left, do a delete and split */
				DMibDatabaseTrace("Not enough room, delta = {}, splitting...", delta);
				pgno = DNodePgno(node);
				fg_NodeDel(mc, 0);
				return fg_PageSplit(mc, key, nullptr, pgno, DMdbSplitReplace);
			}

			numkeys = DNumKeys(mp);
			for (i = 0; i < numkeys; i++)
			{
				if (mp->mp_ptrs[i] <= ptr)
					mp->mp_ptrs[i] -= delta;
			}

			base = (char *)mp + mp->mp_upper + DPageBase;
			len = ptr - mp->mp_upper + DNodeSize;
			memmove(base - delta, base, len);
			mp->mp_upper -= delta;

			node = DNodePtr(mp, indx);
		}

		/* But even if no shift was needed, update ksize */
		if (node->mn_ksize != key->m_Size)
			node->mn_ksize = key->m_Size;

		if (key->m_Size)
			memcpy(DNodeKey(node), key->m_pData, key->m_Size);

		return DMdbSuccess;
	}

	/** Rebalance the tree after a delete operation.
	 * @param[in] mc Cursor pointing to the page where rebalancing
	 * should begin.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_Rebalance(CMdbCursor *mc)
	{
		CMdbNode *node;
		int rc, fromleft;
		uint32 ptop, minkeys, thresh;
		CMdbCursor mn;
		CIndx oldki;

		if (DIsBranch(mc->mc_pg[mc->mc_top]))
		{
			minkeys = 2;
			thresh = 1;
		}
		else
		{
			minkeys = 1;
			thresh = DFillThreshold;
		}
		DMibDatabaseTrace
			(
				"rebalancing {} page {} (has {} keys, {fe1} full)"
				, DIsLeaf(mc->mc_pg[mc->mc_top]) ? "leaf" : "branch"
				, fg_DbgPgno(mc->mc_pg[mc->mc_top])
				, DNumKeys(mc->mc_pg[mc->mc_top])
				, (float)DPageFill(mc->mc_txn->mt_env, mc->mc_pg[mc->mc_top]) / 10
			)
		;

		if (DPageFill(mc->mc_txn->mt_env, mc->mc_pg[mc->mc_top]) >= thresh && DNumKeys(mc->mc_pg[mc->mc_top]) >= minkeys)
		{
			DMibDatabaseTrace("no need to rebalance page {}, above fill threshold", fg_DbgPgno(mc->mc_pg[mc->mc_top]));
			return DMdbSuccess;
		}

		if (mc->mc_snum < 2)
		{
			CMdbPage *mp = mc->mc_pg[0];
			if (DIsSubp(mp))
			{
				DPuts("Can't rebalance a subpage, ignoring");
				return DMdbSuccess;
			}
			if (DNumKeys(mp) == 0)
			{
				DPuts("tree is completely empty");
				mc->mc_db->md_root = DPInvalid;
				mc->mc_db->md_depth = 0;
				mc->mc_db->md_leaf_pages = 0;
				fg_MidlAppend(&mc->mc_txn->mt_free_pgs, mp->mp_p.p_pgno);
				/* Adjust cursors pointing to mp */
				mc->mc_snum = 0;
				mc->mc_top = 0;
				mc->mc_flags &= ~DCInitialized;
				{
					CMdbCursor *m2, *m3;
					CDatabaseSubID dbi = mc->mc_dbi;

					for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
					{
						if (mc->mc_flags & DCSub)
							m3 = &m2->mc_xcursor->mx_cursor;
						else
							m3 = m2;
						if (!(m3->mc_flags & DCInitialized) || (m3->mc_snum < mc->mc_snum))
							continue;
						if (m3->mc_pg[0] == mp)
						{
							m3->mc_snum = 0;
							m3->mc_top = 0;
							m3->mc_flags &= ~DCInitialized;
						}
					}
				}
			}
			else if (DIsBranch(mp) && DNumKeys(mp) == 1)
			{
				int i;
				DPuts("collapsing root page!");
				fg_MidlAppend(&mc->mc_txn->mt_free_pgs, mp->mp_p.p_pgno);
				mc->mc_db->md_root = DNodePgno(DNodePtr(mp, 0));
				rc = fg_PageGet(mc, mc->mc_db->md_root, &mc->mc_pg[0], nullptr);
				if (rc)
					return rc;
				mc->mc_db->md_depth--;
				mc->mc_db->md_branch_pages--;
				mc->mc_ki[0] = mc->mc_ki[1];
				for (i = 1; i < mc->mc_db->md_depth; i++)
				{
					mc->mc_pg[i] = mc->mc_pg[i + 1];
					mc->mc_ki[i] = mc->mc_ki[i + 1];
				}
				{
					/* Adjust other cursors pointing to mp */
					CMdbCursor *m2, *m3;
					CDatabaseSubID dbi = mc->mc_dbi;

					for (m2 = mc->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
					{
						if (mc->mc_flags & DCSub)
							m3 = &m2->mc_xcursor->mx_cursor;
						else
							m3 = m2;
						if (m3 == mc)
							continue;
						if (!(m3->mc_flags & DCInitialized))
							continue;
						if (m3->mc_pg[0] == mp)
						{
							for (i = 0; i < mc->mc_db->md_depth; i++)
							{
								m3->mc_pg[i] = m3->mc_pg[i + 1];
								m3->mc_ki[i] = m3->mc_ki[i + 1];
							}
							m3->mc_snum--;
							m3->mc_top--;
						}
					}
				}
			}
			else
				DPuts("root page doesn't need rebalancing");
			return DMdbSuccess;
		}

		/* The parent (branch page) must have at least 2 pointers,
		 * otherwise the tree is invalid.
		 */
		ptop = mc->mc_top - 1;
		DMibCheck(DNumKeys(mc->mc_pg[ptop]) > 1);

		/* Leaf page fill factor is below the threshold.
		 * Try to move keys from left or right neighbor, or
		 * merge with a neighbor page.
		 */

		/* Find neighbors.
		*/
		fg_CursorCopy(mc, &mn);
		mn.mc_xcursor = nullptr;

		oldki = mc->mc_ki[mc->mc_top];
		if (mc->mc_ki[ptop] == 0)
		{
			/* We're the leftmost leaf in our parent.
			*/
			DPuts("reading right neighbor");
			mn.mc_ki[ptop]++;
			node = DNodePtr(mc->mc_pg[ptop], mn.mc_ki[ptop]);
			rc = fg_PageGet(mc, DNodePgno(node), &mn.mc_pg[mn.mc_top], nullptr);
			if (rc)
				return rc;
			mn.mc_ki[mn.mc_top] = 0;
			mc->mc_ki[mc->mc_top] = DNumKeys(mc->mc_pg[mc->mc_top]);
			fromleft = 0;
		}
		else
		{
			/* There is at least one neighbor to the left.
			*/
			DPuts("reading left neighbor");
			mn.mc_ki[ptop]--;
			node = DNodePtr(mc->mc_pg[ptop], mn.mc_ki[ptop]);
			rc = fg_PageGet(mc, DNodePgno(node), &mn.mc_pg[mn.mc_top], nullptr);
			if (rc)
				return rc;
			mn.mc_ki[mn.mc_top] = DNumKeys(mn.mc_pg[mn.mc_top]) - 1;
			mc->mc_ki[mc->mc_top] = 0;
			fromleft = 1;
		}

		DMibDatabaseTrace
			(
				"found neighbor page {} ({} keys, {fe1} full)"
				, mn.mc_pg[mn.mc_top]->mp_p.p_pgno
				, DNumKeys(mn.mc_pg[mn.mc_top])
				, (float)DPageFill(mc->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) / 10
			)
		;

		/* If the neighbor page is above threshold and has enough keys,
		 * move one key from it. Otherwise we should try to merge them.
		 * (A branch page must never have less than 2 keys.)
		 */
		if (DPageFill(mc->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) >= thresh && DNumKeys(mn.mc_pg[mn.mc_top]) > minkeys)
		{
			rc = fg_NodeMove(&mn, mc, fromleft);
			if (fromleft)
			{
				/* if we inserted on left, bump position up */
				oldki++;
			}
		}
		else
		{
			if (!fromleft)
				rc = fg_PageMerge(&mn, mc);
			else
			{
				oldki += DNumKeys(mn.mc_pg[mn.mc_top]);
				mn.mc_ki[mn.mc_top] += mc->mc_ki[mn.mc_top] + 1;
				/* We want fg_Rebalance to find mn when doing fixups */
				DWithCursorTracking(mn, rc = fg_PageMerge(mc, &mn));
				fg_CursorCopy(&mn, mc);
			}
			mc->mc_flags &= ~DCEof;
		}
		mc->mc_ki[mc->mc_top] = oldki;
		return rc;
	}

	/** Add all the DB's pages to the free list.
	 * @param[in] mc Cursor on the DB to free.
	 * @param[in] subs non-Zero to check for sub-DBs in this DB.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_Drop0(CMdbCursor *mc, int subs)
	{
		int rc;

		rc = fg_PageSearch(mc, nullptr, DMdbPsFirst);
		if (rc == DMdbSuccess)
		{
			CMdbTxn *txn = mc->mc_txn;
			CMdbNode *ni;
			CMdbCursor mx;
			uint32 i;

			/* DUPSORT sub-DBs have no ovpages/DBs. Omit scanning leaves.
			 * This also avoids any DPLeaf2 pages, which have no nodes.
			 * Also if the DB doesn't have sub-DBs and has no overflow
			 * pages, omit scanning leaves.
			 */
			if ((mc->mc_flags & DCSub) || (!subs && !mc->mc_db->md_overflow_pages))
				fg_CursorPop(mc);

			fg_CursorCopy(mc, &mx);
#ifdef DMibDatabase_Always64BitSupport
			/* bump refcount for mx's pages */
			for (i = 0; i < mc->mc_snum; i++)
				fg_PageGet(&mx, mc->mc_pg[i]->mp_p.p_pgno, &mx.mc_pg[i], nullptr);
#endif
			while (mc->mc_snum > 0)
			{
				CMdbPage *mp = mc->mc_pg[mc->mc_top];
				unsigned n = DNumKeys(mp);
				if (DIsLeaf(mp))
				{
					for (i = 0; i < n; i++)
					{
						ni = DNodePtr(mp, i);
						if (ni->mn_flags & DFBigData)
						{
							CMdbPage *omp;
							CPgno pg;
							memcpy(&pg, DNodeData(ni), sizeof(pg));
							rc = fg_PageGet(mc, pg, &omp, nullptr);
							if (rc != 0)
								goto done;
							DMibCheck(DIsOverflow(omp));
							fg_MidlAppendRange(&txn->mt_free_pgs, pg, omp->mp_pages);
							mc->mc_db->md_overflow_pages -= omp->mp_pages;
							if (!mc->mc_db->md_overflow_pages && !subs)
								break;
						}
						else if (subs && (ni->mn_flags & DFSubData))
						{
							fg_XCursorInit1(mc, ni);
							rc = fg_Drop0(&mc->mc_xcursor->mx_cursor, 0);
							if (rc)
								goto done;
						}
					}
					if (!subs && !mc->mc_db->md_overflow_pages)
						goto pop;
				}
				else
				{
					fg_MidlNeed(&txn->mt_free_pgs, n);
					for (i = 0; i < n; i++)
					{
						CPgno pg;
						ni = DNodePtr(mp, i);
						pg = DNodePgno(ni);
						/* free it */
						fg_MidlXAppend(txn->mt_free_pgs, pg);
					}
				}
				if (!mc->mc_top)
					break;
				mc->mc_ki[mc->mc_top] = i;
				rc = fg_CursorSibling(mc, 1);
				if (rc)
				{
					if (rc != DMdbNotFound)
						goto done;
					/* no more siblings, go back to beginning
					 * of previous level.
					 */
				pop:
					fg_CursorPop(mc);
					mc->mc_ki[0] = 0;
					for (i = 1; i < mc->mc_snum; i++)
					{
						mc->mc_ki[i] = 0;
						mc->mc_pg[i] = mx.mc_pg[i];
					}
				}
			}
			/* free it */
			fg_MidlAppend(&txn->mt_free_pgs, mc->mc_db->md_root);
		done:
			if (rc)
				txn->mt_flags |= DMdbTxnError;
#ifdef DMibDatabase_Always64BitSupport
			/* drop refcount for mx's pages */
			fg_CursorUnref(&mx);
#endif
		}
		else if (rc == DMdbNotFound)
			rc = DMdbSuccess;
		mc->mc_flags &= ~DCInitialized;
		return rc;
	}
}
