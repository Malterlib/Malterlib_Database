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
	 * If o_pExact is non-null, stores whether the found entry was an exact match
	 * in *o_pExact (1 or 0).
	 * Updates the cursor index with the index of the found entry.
	 * If no entry larger or equal to the key is found, returns nullptr.
	 */
	NPrivate::CMdbNode *NPrivate::fg_NodeSearch(CMdbCursor *_pCursor, CDatabaseValue const *_pKey, int *o_pExact)
	{
		uint32 i = 0, nkeys;
		int low, high;
		int rc = 0;
		CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top];
		CMdbNode *node = nullptr;
		CDatabaseValue nodekey;
		FDatabaseCompare *pCompare;
		DKBuf;

		nkeys = DNumKeys(mp);

		DMibDatabaseTrace("searching {} keys in {} {}page {}", nkeys, DIsLeaf(mp) ? "leaf" : "branch", DIsSubp(mp) ? "sub-" : "", fg_DbgPgno(mp));

		low = DIsLeaf(mp) ? 0 : 1;
		high = nkeys - 1;
		pCompare = &(_pCursor->mc_dbx->m_fKeyCompare);

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
			nodekey.m_Size = _pCursor->mc_db->md_pad;
			node = DNodePtr(mp, 0); // fake
			while (low <= high)
			{
				i = (low + high) >> 1;
				nodekey.m_pData = DLeaf2Key(mp, i, nodekey.m_Size);
				rc = (*pCompare)(*_pKey, nodekey);
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

				rc = (*pCompare)(*_pKey, nodekey);
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
		if (o_pExact)
			*o_pExact = (rc == 0 && nkeys > 0);
		/* store the key index */
		_pCursor->mc_ki[_pCursor->mc_top] = i;
		if (i >= nkeys)
			/* There is no entry larger or equal to the key. */
			return nullptr;

		/* nodeptr is fake for LEAF2 */
		return node;
	}

	/** Return the data associated with a given node.
	 * @param[in] _pCursor The cursor for this operation.
	 * @param[in] _pLeaf The node being read.
	 * @param[out] _pData Updated to point to the node's data.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_NodeRead(CMdbCursor *_pCursor, CMdbNode *_pLeaf, CDatabaseValue *_pData)
	{
		CMdbPage *omp; // overflow page
		CPgno pgno;
		int rc;

		if (DMcOvpg(_pCursor))
		{
			DMdbPageUnref(_pCursor->mc_txn, DMcOvpg(_pCursor));
			DMcSetOvpg(_pCursor, NULL);
		}
		if (!DFIsSet(_pLeaf->mn_flags, DFBigData))
		{
			_pData->m_Size = DNodeDataSize(_pLeaf);
			_pData->m_pData = DNodeData(_pLeaf);
			return DMdbSuccess;
		}

		/* Read overflow data.
		*/
		_pData->m_Size = DNodeDataSize(_pLeaf);
		memcpy(&pgno, DNodeData(_pLeaf), sizeof(pgno));
		if ((rc = fg_PageGet(_pCursor, pgno, &omp, nullptr)) != 0)
		{
			DMibDatabaseTrace("read overflow page {} failed", pgno);
			return rc;
		}
		_pData->m_pData = DMetaData(omp);
		DMcSetOvpg(_pCursor, omp);

		return DMdbSuccess;
	}

	/** Add a node to the page pointed to by the cursor.
	 * Set #DMdbTxnError on failure.
	 * @param[in] _pCursor The cursor for this operation.
	 * @param[in] _iNode The index on the page where the new node should be added.
	 * @param[in] _pKey The key for the new node.
	 * @param[in] _pData The data for the new node, if any.
	 * @param[in] _PageNumber The page number, if adding a branch node.
	 * @param[in] _Flags Flags for the node.
	 * @return 0 on success, non-zero on failure. Possible errors are:
	 * <ul>
	 *	<li>ENOMEM - failed to allocate overflow pages for the node.
	 *	<li>DMdbPageFull - there is insufficient room in the page. This error
	 *	should never happen since all callers already calculate the
	 *	page's free space before calling this function.
	 * </ul>
	 */
	int NPrivate::fg_NodeAdd(CMdbCursor *_pCursor, CIndx _iNode, CDatabaseValue *_pKey, CDatabaseValue *_pData, CPgno _PageNumber, uint32 _Flags)
	{
		uint32 i;
		umint node_size = DNodeSize;
		smint room;
		CIndx ofs;
		CMdbNode *node;
		CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top];
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
				, _iNode
				, _pData ? _pData->m_Size : 0
				, _pKey ? _pKey->m_Size : 0
				, _pKey ? DKey(_pKey) : "null"
			)
		;

		if (DIsLeaf2(mp))
		{
			/* Move higher keys up one slot. */
			int ksize = _pCursor->mc_db->md_pad, dif;
			uint8 *ptr = DLeaf2Key(mp, _iNode, ksize);
			dif = DNumKeys(mp) - _iNode;
			if (dif > 0)
				memmove(ptr + ksize, ptr, dif*ksize);
			/* insert new key */
			memcpy(ptr, _pKey->m_pData, ksize);

			/* Just using these for counting */
			mp->mp_lower += sizeof(CIndx);
			mp->mp_upper -= CIndx(ksize - sizeof(CIndx));
			return DMdbSuccess;
		}

		room = (smint)DSizeLeft(mp) - (smint)sizeof(CIndx);
		if (_pKey != nullptr)
			node_size += _pKey->m_Size;
		if (DIsLeaf(mp))
		{
			DMibCheck(_pKey && _pData);
			if (DFIsSet(_Flags, DFBigData))
			{
				/* Data already on overflow page. */
				node_size += sizeof(CPgno);
			}
			else if (node_size + _pData->m_Size > _pCursor->mc_txn->mt_env->me_nodemax)
			{
				int ovpages = DOverflowPages(_pData->m_Size, _pCursor->mc_txn->mt_env->me_psize);
				int rc;
				/* Put data on overflow page. */
				DMibDatabaseTrace("data size is {}, node would be {}, put data on overflow page", _pData->m_Size, node_size + _pData->m_Size);
				node_size = DEven(node_size + sizeof(CPgno));
				if ((smint)node_size > room)
					goto full;
				if ((rc = fg_PageNew(_pCursor, DPOverflow, ovpages, &ofp)))
					return rc;
				DMibDatabaseTrace("allocated overflow page {}", ofp->mp_p.p_pgno);
				_Flags |= DFBigData;
				goto update;
			}
			else
				node_size += _pData->m_Size;
		}
		node_size = DEven(node_size);
		if ((smint)node_size > room)
			goto full;

	update:
		/* Move higher pointers up one slot. */
		for (i = DNumKeys(mp); i > _iNode; i--)
			mp->mp_ptrs[i] = mp->mp_ptrs[i - 1];

		/* Adjust free space offsets. */
		ofs = mp->mp_upper - node_size;
		DMibCheck(ofs >= mp->mp_lower + sizeof(CIndx));
		mp->mp_ptrs[_iNode] = ofs;
		mp->mp_upper = ofs;
		mp->mp_lower += sizeof(CIndx);

		/* Write the node data. */
		node = DNodePtr(mp, _iNode);
		node->mn_ksize = (_pKey == nullptr) ? 0 : _pKey->m_Size;
		node->mn_flags = _Flags;
		if (DIsLeaf(mp))
			DSetDataSize(node, _pData->m_Size);
		else
			DSetPgno(node, _PageNumber);

		if (_pKey)
			memcpy(DNodeKey(node), _pKey->m_pData, _pKey->m_Size);

		if (DIsLeaf(mp))
		{
			ndata = (CMdbPage *)DNodeData(node);
			if (ofp == nullptr)
			{
				if (DFIsSet(_Flags, DFBigData))
					memcpy(ndata, _pData->m_pData, sizeof(CPgno));
				else if (DFIsSet(_Flags, EDatabaseWriteFlag_ReserveSpace))
					_pData->m_pData = (uint8 *)ndata;
				else if (_pData->m_Size)
					memcpy(ndata, _pData->m_pData, _pData->m_Size);
			}
			else
			{
				memcpy(ndata, &ofp->mp_p.p_pgno, sizeof(CPgno));
				ndata = DMetaData(ofp);
				if (DFIsSet(_Flags, EDatabaseWriteFlag_ReserveSpace))
					_pData->m_pData = (uint8 *)ndata;
				else if (_pData->m_Size)
					memcpy(ndata, _pData->m_pData, _pData->m_Size);
			}
		}

		return DMdbSuccess;

	full:
		DMibDatabaseTrace("not enough room in page {}, got {} ptrs", fg_DbgPgno(mp), DNumKeys(mp));
		DMibDatabaseTrace("upper-lower = {} - {} = {}", mp->mp_upper, mp->mp_lower, room);
		DMibDatabaseTrace("node size = {}", node_size);
		_pCursor->mc_txn->mt_flags |= DMdbTxnError;
		return DMdbPageFull;
	}

	/** Delete the specified node from a page.
	 * @param[in] _pCursor Cursor pointing to the node to delete.
	 * @param[in] _KeySize The size of a node. Only used if the page is
	 * part of a #EDatabaseSubFlag_SortedDuplicatesFixedSize database.
	 */
	void NPrivate::fg_NodeDel(CMdbCursor *_pCursor, int _KeySize)
	{
		CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top];
		CIndx indx = _pCursor->mc_ki[_pCursor->mc_top];
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
			base = DLeaf2Key(mp, indx, _KeySize);
			if (x)
				memmove(base, base + _KeySize, x * _KeySize);
			mp->mp_lower -= sizeof(CIndx);
			mp->mp_upper += CIndx(_KeySize - sizeof(CIndx));
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
	 * @param[in] _pPage The main page to operate on.
	 * @param[in] _iNode The index of the subpage on the main page.
	 */
	void NPrivate::fg_NodeShrink(CMdbPage *_pPage, CIndx _iNode)
	{
		CMdbNode *node;
		CMdbPage *sp, *xp;
		char *base;
		CIndx delta, nsize, len, ptr;
		int i;

		node = DNodePtr(_pPage, _iNode);
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
		DCopyPgno(sp->mp_p.p_pgno, _pPage->mp_p.p_pgno);
		DSetDataSize(node, uint32(nsize));

		/* Shift <lower nodes...initial part of subpage> upward */
		base = (char *)_pPage + _pPage->mp_upper + DPageBase;
		memmove(base + delta, base, (char *)sp + len - base);

		ptr = _pPage->mp_ptrs[_iNode];
		for (i = DNumKeys(_pPage); --i >= 0;)
		{
			if (_pPage->mp_ptrs[i] <= ptr)
				_pPage->mp_ptrs[i] += delta;
		}
		_pPage->mp_upper += delta;
	}

	/** Move a node from _pSourceCursor to _pDestinationCursor.
	 */
	int NPrivate::fg_NodeMove(CMdbCursor *_pSourceCursor, CMdbCursor *_pDestinationCursor, int _bFromLeft)
	{
		CMdbNode *srcnode;
		CDatabaseValue key, data;
		CPgno srcpg;
		CMdbCursor mn;
		int rc;
		unsigned short flags;

		DKBuf;

		/* Mark src and dst as dirty. */
		if ((rc = fg_PageTouch(_pSourceCursor)) || (rc = fg_PageTouch(_pDestinationCursor)))
			return rc;

		if (DIsLeaf2(_pSourceCursor->mc_pg[_pSourceCursor->mc_top]))
		{
			key.m_Size = _pSourceCursor->mc_db->md_pad;
			key.m_pData = DLeaf2Key(_pSourceCursor->mc_pg[_pSourceCursor->mc_top], _pSourceCursor->mc_ki[_pSourceCursor->mc_top], key.m_Size);
			data.m_Size = 0;
			data.m_pData = nullptr;
			srcpg = 0;
			flags = 0;
		}
		else
		{
			srcnode = DNodePtr(_pSourceCursor->mc_pg[_pSourceCursor->mc_top], _pSourceCursor->mc_ki[_pSourceCursor->mc_top]);
			DMibCheck(!((umint)srcnode & 1));
			srcpg = DNodePgno(srcnode);
			flags = srcnode->mn_flags;
			if (_pSourceCursor->mc_ki[_pSourceCursor->mc_top] == 0 && DIsBranch(_pSourceCursor->mc_pg[_pSourceCursor->mc_top]))
			{
				uint32 snum = _pSourceCursor->mc_snum;
				CMdbNode *s2;
				/* must find the lowest key below src */
				rc = fg_PageSearchLowest(_pSourceCursor);
				if (rc)
					return rc;
				if (DIsLeaf2(_pSourceCursor->mc_pg[_pSourceCursor->mc_top]))
				{
					key.m_Size = _pSourceCursor->mc_db->md_pad;
					key.m_pData = DLeaf2Key(_pSourceCursor->mc_pg[_pSourceCursor->mc_top], 0, key.m_Size);
				}
				else
				{
					s2 = DNodePtr(_pSourceCursor->mc_pg[_pSourceCursor->mc_top], 0);
					key.m_Size = DNodeKeySize(s2);
					key.m_pData = DNodeKey(s2);
				}
				_pSourceCursor->mc_snum = snum--;
				_pSourceCursor->mc_top = snum;
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
		if (DIsBranch(_pDestinationCursor->mc_pg[_pDestinationCursor->mc_top]) && _pDestinationCursor->mc_ki[_pDestinationCursor->mc_top] == 0)
		{
			uint32 snum = _pDestinationCursor->mc_snum;
			CMdbNode *s2;
			CDatabaseValue bkey;
			/* must find the lowest key below dst */
			fg_CursorCopy(_pDestinationCursor, &mn);
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
				, DIsLeaf(_pSourceCursor->mc_pg[_pSourceCursor->mc_top]) ? "leaf" : "branch"
				, _pSourceCursor->mc_ki[_pSourceCursor->mc_top]
				, DKey(&key)
				, _pSourceCursor->mc_pg[_pSourceCursor->mc_top]->mp_p.p_pgno
				, _pDestinationCursor->mc_ki[_pDestinationCursor->mc_top]
				, _pDestinationCursor->mc_pg[_pDestinationCursor->mc_top]->mp_p.p_pgno
			)
		;

		/* Add the node to the destination page.
		*/
		rc = fg_NodeAdd(_pDestinationCursor, _pDestinationCursor->mc_ki[_pDestinationCursor->mc_top], &key, &data, srcpg, flags);
		if (rc != DMdbSuccess)
			return rc;

		/* Delete the node from the source page.
		*/
		fg_NodeDel(_pSourceCursor, key.m_Size);

		{
			/* Adjust other cursors pointing to mp */
			CMdbCursor *m2, *m3;
			CDatabaseSubID dbi = _pSourceCursor->mc_dbi;
			CMdbPage *mpd, *mps;

			mps = _pSourceCursor->mc_pg[_pSourceCursor->mc_top];
			/* If we're adding on the left, bump others up */
			if (_bFromLeft)
			{
				mpd = _pDestinationCursor->mc_pg[_pSourceCursor->mc_top];
				for (m2 = _pSourceCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
				{
					if (_pSourceCursor->mc_flags & DCSub)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (!(m3->mc_flags & DCInitialized) || m3->mc_top < _pSourceCursor->mc_top)
						continue;
					if (m3 != _pDestinationCursor && m3->mc_pg[_pSourceCursor->mc_top] == mpd && m3->mc_ki[_pSourceCursor->mc_top] >= _pDestinationCursor->mc_ki[_pSourceCursor->mc_top])
						m3->mc_ki[_pSourceCursor->mc_top]++;
					if (m3 != _pSourceCursor && m3->mc_pg[_pSourceCursor->mc_top] == mps && m3->mc_ki[_pSourceCursor->mc_top] == _pSourceCursor->mc_ki[_pSourceCursor->mc_top])
					{
						m3->mc_pg[_pSourceCursor->mc_top] = _pDestinationCursor->mc_pg[_pDestinationCursor->mc_top];
						m3->mc_ki[_pSourceCursor->mc_top] = _pDestinationCursor->mc_ki[_pDestinationCursor->mc_top];
						m3->mc_ki[_pSourceCursor->mc_top - 1]++;
					}
					if (DIsLeaf(mps))
						DXCursorRefresh(m3, _pSourceCursor->mc_top, m3->mc_pg[_pSourceCursor->mc_top]);
				}
			}
			else
			/* Adding on the right, bump others down */
			{
				for (m2 = _pSourceCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
				{
					if (_pSourceCursor->mc_flags & DCSub)
						m3 = &m2->mc_xcursor->mx_cursor;
					else
						m3 = m2;
					if (m3 == _pSourceCursor)
						continue;
					if (!(m3->mc_flags & DCInitialized) || m3->mc_top < _pSourceCursor->mc_top)
						continue;
					if (m3->mc_pg[_pSourceCursor->mc_top] == mps)
					{
						if (!m3->mc_ki[_pSourceCursor->mc_top])
						{
							m3->mc_pg[_pSourceCursor->mc_top] = _pDestinationCursor->mc_pg[_pDestinationCursor->mc_top];
							m3->mc_ki[_pSourceCursor->mc_top] = _pDestinationCursor->mc_ki[_pDestinationCursor->mc_top];
							m3->mc_ki[_pSourceCursor->mc_top - 1]--;
						}
						else
							m3->mc_ki[_pSourceCursor->mc_top]--;
						if (DIsLeaf(mps))
							DXCursorRefresh(m3, _pSourceCursor->mc_top, m3->mc_pg[_pSourceCursor->mc_top]);
					}
				}
			}
		}

		/* Update the parent separators.
		*/
		if (_pSourceCursor->mc_ki[_pSourceCursor->mc_top] == 0)
		{
			if (_pSourceCursor->mc_ki[_pSourceCursor->mc_top - 1] != 0)
			{
				if (DIsLeaf2(_pSourceCursor->mc_pg[_pSourceCursor->mc_top]))
					key.m_pData = DLeaf2Key(_pSourceCursor->mc_pg[_pSourceCursor->mc_top], 0, key.m_Size);
				else
				{
					srcnode = DNodePtr(_pSourceCursor->mc_pg[_pSourceCursor->mc_top], 0);
					key.m_Size = DNodeKeySize(srcnode);
					key.m_pData = DNodeKey(srcnode);
				}
				DMibDatabaseTrace("update separator for source page {} to [{}]", _pSourceCursor->mc_pg[_pSourceCursor->mc_top]->mp_p.p_pgno, DKey(&key));
				fg_CursorCopy(_pSourceCursor, &mn);
				mn.mc_snum--;
				mn.mc_top--;
				/* We want fg_Rebalance to find mn when doing fixups */
				DWithCursorTracking(mn, rc = fg_UpdateKey(&mn, &key));
				if (rc)
					return rc;
			}
			if (DIsBranch(_pSourceCursor->mc_pg[_pSourceCursor->mc_top]))
			{
				CDatabaseValue nullkey;
				CIndx ix = _pSourceCursor->mc_ki[_pSourceCursor->mc_top];
				nullkey.m_Size = 0;
				_pSourceCursor->mc_ki[_pSourceCursor->mc_top] = 0;
				rc = fg_UpdateKey(_pSourceCursor, &nullkey);
				_pSourceCursor->mc_ki[_pSourceCursor->mc_top] = ix;
				DMibCheck(rc == DMdbSuccess);
			}
		}

		if (_pDestinationCursor->mc_ki[_pDestinationCursor->mc_top] == 0)
		{
			if (_pDestinationCursor->mc_ki[_pDestinationCursor->mc_top - 1] != 0)
			{
				if (DIsLeaf2(_pSourceCursor->mc_pg[_pSourceCursor->mc_top]))
					key.m_pData = DLeaf2Key(_pDestinationCursor->mc_pg[_pDestinationCursor->mc_top], 0, key.m_Size);
				else
				{
					srcnode = DNodePtr(_pDestinationCursor->mc_pg[_pDestinationCursor->mc_top], 0);
					key.m_Size = DNodeKeySize(srcnode);
					key.m_pData = DNodeKey(srcnode);
				}
				DMibDatabaseTrace("update separator for destination page {} to [{}]", _pDestinationCursor->mc_pg[_pDestinationCursor->mc_top]->mp_p.p_pgno, DKey(&key));
				fg_CursorCopy(_pDestinationCursor, &mn);
				mn.mc_snum--;
				mn.mc_top--;
				/* We want fg_Rebalance to find mn when doing fixups */
				DWithCursorTracking(mn, rc = fg_UpdateKey(&mn, &key));
				if (rc)
					return rc;
			}
			if (DIsBranch(_pDestinationCursor->mc_pg[_pDestinationCursor->mc_top]))
			{
				CDatabaseValue nullkey;
				CIndx ix = _pDestinationCursor->mc_ki[_pDestinationCursor->mc_top];
				nullkey.m_Size = 0;
				_pDestinationCursor->mc_ki[_pDestinationCursor->mc_top] = 0;
				rc = fg_UpdateKey(_pDestinationCursor, &nullkey);
				_pDestinationCursor->mc_ki[_pDestinationCursor->mc_top] = ix;
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
	 * @param[in] _pKey The key for the node.
	 * @param[in] _pData The data for the node.
	 * @return The number of bytes needed to store the node.
	 */
	umint CDatabaseEnvironment_Internal::f_LeafSize(CDatabaseValue *_pKey, CDatabaseValue *_pData)
	{
		umint sz;

		sz = DLeafSize(_pKey, _pData);
		if (sz > me_nodemax)
		{
			/* put on overflow page */
			sz -= _pData->m_Size - sizeof(CPgno);
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
	 * @param[in] _pKey The key for the node.
	 * @return The number of bytes needed to store the node.
	 */
	umint CDatabaseEnvironment_Internal::f_BranchSize(CDatabaseValue *_pKey)
	{
		umint sz;

		sz = DIndexSize(_pKey);
		if (sz > me_nodemax)
		{
			/* put on overflow page */
			/* not implemented */
			/* sz -= _pKey->size - sizeof(CPgno); */
		}

		return sz + sizeof(CIndx);
	}

	/** Replace the key for a branch node with a new key.
	 * Set #DMdbTxnError on failure.
	 * @param[in] _pCursor Cursor pointing to the node to operate on.
	 * @param[in] _pKey The new key to use.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_UpdateKey(CMdbCursor *_pCursor, CDatabaseValue *_pKey)
	{
		CMdbPage *mp;
		CMdbNode *node;
		char *base;
		umint len;
		int delta, ksize, oksize;
		CIndx ptr, i, numkeys, indx;
		DKBuf;

		indx = _pCursor->mc_ki[_pCursor->mc_top];
		mp = _pCursor->mc_pg[_pCursor->mc_top];
		node = DNodePtr(mp, indx);
		ptr = mp->mp_ptrs[indx];
#if DMibDatabaseDebug
		{
			CDatabaseValue k2;
			char kbuf2[DKBufMaxKeySize * 2 + 1];
			k2.m_pData = DNodeKey(node);
			k2.m_Size = node->mn_ksize;
			DMibDatabaseTrace("update key {} (ofs {}) [{}] to [{}] on page {}", indx, ptr, fg_DKey(&k2, kbuf2), DKey(_pKey), mp->mp_p.p_pgno);
		}
#endif

		/* Sizes must be 2-byte aligned. */
		ksize = DEven(_pKey->m_Size);
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
				fg_NodeDel(_pCursor, 0);
				return fg_PageSplit(_pCursor, _pKey, nullptr, pgno, DMdbSplitReplace);
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
		if (node->mn_ksize != _pKey->m_Size)
			node->mn_ksize = _pKey->m_Size;

		if (_pKey->m_Size)
			memcpy(DNodeKey(node), _pKey->m_pData, _pKey->m_Size);

		return DMdbSuccess;
	}

	/** Rebalance the tree after a delete operation.
	 * @param[in] _pCursor Cursor pointing to the page where rebalancing
	 * should begin.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_Rebalance(CMdbCursor *_pCursor)
	{
		CMdbNode *node;
		int rc, fromleft;
		uint32 ptop, minkeys, thresh;
		CMdbCursor mn;
		CIndx oldki;

		if (DIsBranch(_pCursor->mc_pg[_pCursor->mc_top]))
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
				, DIsLeaf(_pCursor->mc_pg[_pCursor->mc_top]) ? "leaf" : "branch"
				, fg_DbgPgno(_pCursor->mc_pg[_pCursor->mc_top])
				, DNumKeys(_pCursor->mc_pg[_pCursor->mc_top])
				, (float)DPageFill(_pCursor->mc_txn->mt_env, _pCursor->mc_pg[_pCursor->mc_top]) / 10
			)
		;

		if (DPageFill(_pCursor->mc_txn->mt_env, _pCursor->mc_pg[_pCursor->mc_top]) >= thresh && DNumKeys(_pCursor->mc_pg[_pCursor->mc_top]) >= minkeys)
		{
			DMibDatabaseTrace("no need to rebalance page {}, above fill threshold", fg_DbgPgno(_pCursor->mc_pg[_pCursor->mc_top]));
			return DMdbSuccess;
		}

		if (_pCursor->mc_snum < 2)
		{
			CMdbPage *mp = _pCursor->mc_pg[0];
			if (DIsSubp(mp))
			{
				DPuts("Can't rebalance a subpage, ignoring");
				return DMdbSuccess;
			}
			if (DNumKeys(mp) == 0)
			{
				DPuts("tree is completely empty");
				_pCursor->mc_db->md_root = DPInvalid;
				_pCursor->mc_db->md_depth = 0;
				_pCursor->mc_db->md_leaf_pages = 0;
				fg_MidlAppend(&_pCursor->mc_txn->mt_free_pgs, mp->mp_p.p_pgno);
				/* Adjust cursors pointing to mp */
				_pCursor->mc_snum = 0;
				_pCursor->mc_top = 0;
				_pCursor->mc_flags &= ~DCInitialized;
				{
					CMdbCursor *m2, *m3;
					CDatabaseSubID dbi = _pCursor->mc_dbi;

					for (m2 = _pCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
					{
						if (_pCursor->mc_flags & DCSub)
							m3 = &m2->mc_xcursor->mx_cursor;
						else
							m3 = m2;
						if (!(m3->mc_flags & DCInitialized) || (m3->mc_snum < _pCursor->mc_snum))
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
				fg_MidlAppend(&_pCursor->mc_txn->mt_free_pgs, mp->mp_p.p_pgno);
				_pCursor->mc_db->md_root = DNodePgno(DNodePtr(mp, 0));
				rc = fg_PageGet(_pCursor, _pCursor->mc_db->md_root, &_pCursor->mc_pg[0], nullptr);
				if (rc)
					return rc;
				_pCursor->mc_db->md_depth--;
				_pCursor->mc_db->md_branch_pages--;
				_pCursor->mc_ki[0] = _pCursor->mc_ki[1];
				for (i = 1; i < _pCursor->mc_db->md_depth; i++)
				{
					_pCursor->mc_pg[i] = _pCursor->mc_pg[i + 1];
					_pCursor->mc_ki[i] = _pCursor->mc_ki[i + 1];
				}
				{
					/* Adjust other cursors pointing to mp */
					CMdbCursor *m2, *m3;
					CDatabaseSubID dbi = _pCursor->mc_dbi;

					for (m2 = _pCursor->mc_txn->mt_cursors[dbi]; m2; m2 = m2->mc_next)
					{
						if (_pCursor->mc_flags & DCSub)
							m3 = &m2->mc_xcursor->mx_cursor;
						else
							m3 = m2;
						if (m3 == _pCursor)
							continue;
						if (!(m3->mc_flags & DCInitialized))
							continue;
						if (m3->mc_pg[0] == mp)
						{
							for (i = 0; i < _pCursor->mc_db->md_depth; i++)
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
		ptop = _pCursor->mc_top - 1;
		DMibCheck(DNumKeys(_pCursor->mc_pg[ptop]) > 1);

		/* Leaf page fill factor is below the threshold.
		 * Try to move keys from left or right neighbor, or
		 * merge with a neighbor page.
		 */

		/* Find neighbors.
		*/
		fg_CursorCopy(_pCursor, &mn);
		mn.mc_xcursor = nullptr;

		oldki = _pCursor->mc_ki[_pCursor->mc_top];
		if (_pCursor->mc_ki[ptop] == 0)
		{
			/* We're the leftmost leaf in our parent.
			*/
			DPuts("reading right neighbor");
			mn.mc_ki[ptop]++;
			node = DNodePtr(_pCursor->mc_pg[ptop], mn.mc_ki[ptop]);
			rc = fg_PageGet(_pCursor, DNodePgno(node), &mn.mc_pg[mn.mc_top], nullptr);
			if (rc)
				return rc;
			mn.mc_ki[mn.mc_top] = 0;
			_pCursor->mc_ki[_pCursor->mc_top] = DNumKeys(_pCursor->mc_pg[_pCursor->mc_top]);
			fromleft = 0;
		}
		else
		{
			/* There is at least one neighbor to the left.
			*/
			DPuts("reading left neighbor");
			mn.mc_ki[ptop]--;
			node = DNodePtr(_pCursor->mc_pg[ptop], mn.mc_ki[ptop]);
			rc = fg_PageGet(_pCursor, DNodePgno(node), &mn.mc_pg[mn.mc_top], nullptr);
			if (rc)
				return rc;
			mn.mc_ki[mn.mc_top] = DNumKeys(mn.mc_pg[mn.mc_top]) - 1;
			_pCursor->mc_ki[_pCursor->mc_top] = 0;
			fromleft = 1;
		}

		DMibDatabaseTrace
			(
				"found neighbor page {} ({} keys, {fe1} full)"
				, mn.mc_pg[mn.mc_top]->mp_p.p_pgno
				, DNumKeys(mn.mc_pg[mn.mc_top])
				, (float)DPageFill(_pCursor->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) / 10
			)
		;

		/* If the neighbor page is above threshold and has enough keys,
		 * move one key from it. Otherwise we should try to merge them.
		 * (A branch page must never have less than 2 keys.)
		 */
		if (DPageFill(_pCursor->mc_txn->mt_env, mn.mc_pg[mn.mc_top]) >= thresh && DNumKeys(mn.mc_pg[mn.mc_top]) > minkeys)
		{
			rc = fg_NodeMove(&mn, _pCursor, fromleft);
			if (fromleft)
			{
				/* if we inserted on left, bump position up */
				oldki++;
			}
		}
		else
		{
			if (!fromleft)
				rc = fg_PageMerge(&mn, _pCursor);
			else
			{
				oldki += DNumKeys(mn.mc_pg[mn.mc_top]);
				mn.mc_ki[mn.mc_top] += _pCursor->mc_ki[mn.mc_top] + 1;
				/* We want fg_Rebalance to find mn when doing fixups */
				DWithCursorTracking(mn, rc = fg_PageMerge(_pCursor, &mn));
				fg_CursorCopy(&mn, _pCursor);
			}
			_pCursor->mc_flags &= ~DCEof;
		}
		_pCursor->mc_ki[_pCursor->mc_top] = oldki;
		return rc;
	}

	/** Add all the DB's pages to the free list.
	 * @param[in] _pCursor Cursor on the DB to free.
	 * @param[in] _bSubDatabases non-Zero to check for sub-DBs in this DB.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_Drop0(CMdbCursor *_pCursor, int _bSubDatabases)
	{
		int rc;

		rc = fg_PageSearch(_pCursor, nullptr, DMdbPsFirst);
		if (rc == DMdbSuccess)
		{
			CMdbTxn *txn = _pCursor->mc_txn;
			CMdbNode *ni;
			CMdbCursor mx;
			uint32 i;

			/* DUPSORT sub-DBs have no ovpages/DBs. Omit scanning leaves.
			 * This also avoids any DPLeaf2 pages, which have no nodes.
			 * Also if the DB doesn't have sub-DBs and has no overflow
			 * pages, omit scanning leaves.
			 */
			if ((_pCursor->mc_flags & DCSub) || (!_bSubDatabases && !_pCursor->mc_db->md_overflow_pages))
				fg_CursorPop(_pCursor);

			fg_CursorCopy(_pCursor, &mx);
#ifdef DMibDatabase_Always64BitSupport
			/* bump refcount for mx's pages */
			for (i = 0; i < _pCursor->mc_snum; i++)
				fg_PageGet(&mx, _pCursor->mc_pg[i]->mp_p.p_pgno, &mx.mc_pg[i], nullptr);
#endif
			while (_pCursor->mc_snum > 0)
			{
				CMdbPage *mp = _pCursor->mc_pg[_pCursor->mc_top];
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
							rc = fg_PageGet(_pCursor, pg, &omp, nullptr);
							if (rc != 0)
								goto done;
							DMibCheck(DIsOverflow(omp));
							fg_MidlAppendRange(&txn->mt_free_pgs, pg, omp->mp_pages);
							_pCursor->mc_db->md_overflow_pages -= omp->mp_pages;
							if (!_pCursor->mc_db->md_overflow_pages && !_bSubDatabases)
								break;
						}
						else if (_bSubDatabases && (ni->mn_flags & DFSubData))
						{
							fg_XCursorInit1(_pCursor, ni);
							rc = fg_Drop0(&_pCursor->mc_xcursor->mx_cursor, 0);
							if (rc)
								goto done;
						}
					}
					if (!_bSubDatabases && !_pCursor->mc_db->md_overflow_pages)
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
				if (!_pCursor->mc_top)
					break;
				_pCursor->mc_ki[_pCursor->mc_top] = i;
				rc = fg_CursorSibling(_pCursor, 1);
				if (rc)
				{
					if (rc != DMdbNotFound)
						goto done;
					/* no more siblings, go back to beginning
					 * of previous level.
					 */
				pop:
					fg_CursorPop(_pCursor);
					_pCursor->mc_ki[0] = 0;
					for (i = 1; i < _pCursor->mc_snum; i++)
					{
						_pCursor->mc_ki[i] = 0;
						_pCursor->mc_pg[i] = mx.mc_pg[i];
					}
				}
			}
			/* free it */
			fg_MidlAppend(&txn->mt_free_pgs, _pCursor->mc_db->md_root);
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
		_pCursor->mc_flags &= ~DCInitialized;
		return rc;
	}
}
