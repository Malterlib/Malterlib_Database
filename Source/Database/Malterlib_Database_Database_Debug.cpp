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

namespace NMib::NDatabase::NPrivate
{
#if DMibDatabaseDebug
	int mdb_debug = 0;
	CTransactionID mdb_debug_start = -1;

	/** Return the page number of \b _pPage which may be sub-page, for debug output */
	CPgno fg_DbgPgno(CMdbPage const *_pPage)
	{
		CPgno ret;
		DCopyPgno(ret, _pPage->mp_p.p_pgno);
		return ret;
	}

	/** Display a key in hexadecimal and return the address of the result.
	 * @param[in] _pKey the key to display
	 * @param[in] _pBuffer the buffer to write into. Should always be #DKBuf.
	 * @return The key in hexadecimal form.
	 */
	char const *fg_DKey(CDatabaseValue const *_pKey, char *_pBuffer)
	{
		char *ptr = _pBuffer;
		unsigned char *c = (unsigned char *)_pKey->m_pData;
		uint32 i;
		size_t buf_len = DKBufMaxKeySize * 2;

		if (!_pKey)
			return "";

		if (_pKey->m_Size > DKBufMaxKeySize)
			return "MDB_MAXKEYSIZE";
		/* may want to make this a dynamic check: if the key is mostly
		 * printable characters, print it as-is instead of converting to hex.
		 */
#	if 1
		_pBuffer[0] = '\0';
		for (i = 0; i < _pKey->m_Size; i++)
		{
			auto *pPrevPtr = ptr;
			ptr += snprintf(ptr, buf_len, "%02x", *c++);
			buf_len -= ptr - pPrevPtr;
		}
#	else
		sprintf(_pBuffer, "%.*s", _pKey->m_Size, _pKey->m_pData);
#	endif
		return _pBuffer;
	}

	char const *fg_LeafNodeType(CMdbNode const *_pNode)
	{
		static char const * const tp[2][2] = {{"", ": DB"}, {": sub-page", ": sub-DB"}};
		return DFIsSet(_pNode->mn_flags, DFBigData) ? ": overflow page" : tp[DFIsSet(_pNode->mn_flags, DFDupData)][DFIsSet(_pNode->mn_flags, DFSubData)];
	}

	/** Display all the keys in the page. */
	void fg_PageList(CMdbPage const *_pPage)
	{
		CPgno pgno = fg_DbgPgno(_pPage);
		char const *type, *state = (_pPage->mp_flags & DPDirty) ? ", dirty" : "";
		CMdbNode *node;
		uint32 i, nkeys, nsize, total = 0;
		CDatabaseValue key;
		DKBuf;

		switch (_pPage->mp_flags & (DPBranch | DPLeaf | DPLeaf2 | DPMeta | DPOverflow | DPSubp))
		{
		case DPBranch: type = "Branch page"; break;
		case DPLeaf: type = "Leaf page"; break;
		case DPLeaf | DPSubp: type = "Sub-page"; break;
		case DPLeaf | DPLeaf2: type = "LEAF2 page"; break;
		case DPLeaf | DPLeaf2 | DPSubp: type = "LEAF2 sub-page"; break;
		case DPOverflow:
			DMibConErrOut("Overflow page {} pages {}{}\n", pgno, _pPage->mp_pages, state);
			return;
		case DPMeta:
			DMibConErrOut("Meta-page {} txnid {}\n", pgno, fg_RemoveQualifiers(((CMdbMeta *)DMetaData(_pPage))->mm_txnid.f_Load(NAtomic::gc_MemoryOrder_Relaxed)));
			return;
		default:
			DMibConErrOut("Bad page {} flags 0x{nfh,sf0,sj4}\n", pgno, _pPage->mp_flags);
			return;
		}

		nkeys = DNumKeys(_pPage);
		DMibConErrOut("{} {} numkeys {}{}\n", type, pgno, nkeys, state);

		for (i = 0; i < nkeys; i++)
		{
			if (DIsLeaf2(_pPage))
			{ // LEAF2 pages have no mp_ptrs[] or node headers
				key.m_Size = nsize = _pPage->mp_pad;
				key.m_pData = DLeaf2Key(_pPage, i, nsize);
				total += nsize;
				DMibConErrOut("key {}: nsize {}, {}\n", i, nsize, DKey(&key));
				continue;
			}
			node = DNodePtr(_pPage, i);
			key.m_Size = node->mn_ksize;
			key.m_pData = node->mn_data;
			nsize = uint32(DNodeSize + key.m_Size);
			if (DIsBranch(_pPage))
			{
				DMibConErrOut("key {}: page {}, {}\n", i, DNodePgno(node), DKey(&key));
				total += nsize;
			}
			else
			{
				if (DFIsSet(node->mn_flags, DFBigData))
					nsize += sizeof(CPgno);
				else
					nsize += DNodeDataSize(node);
				total += nsize;
				nsize += sizeof(CIndx);
				DMibConErrOut("key {}: nsize {}, {}{}\n", i, nsize, DKey(&key), fg_LeafNodeType(node));
			}
			total = DEven(total);
		}
		DMibConErrOut("Total: header {} + contents {} + unused {}\n", DIsLeaf2(_pPage) ? DPageHeaderSize : DPageBase + _pPage->mp_lower, total, DSizeLeft(_pPage));
	}

	void fg_CursorChk(CMdbCursor const *_pCursor)
	{
		using namespace NPrivate;
		uint32 i;
		CMdbNode *node;
		CMdbPage *mp;

		if (!_pCursor->mc_snum || !(_pCursor->mc_flags & DCInitialized))
			return;
		for (i = 0; i < _pCursor->mc_top; i++)
		{
			mp = _pCursor->mc_pg[i];
			node = DNodePtr(mp, _pCursor->mc_ki[i]);
			if (DNodePgno(node) != _pCursor->mc_pg[i + 1]->mp_p.p_pgno)
				printf("oops!\n");
		}
		if (_pCursor->mc_ki[i] >= DNumKeys(_pCursor->mc_pg[i]))
			printf("ack!\n");
		if (DXCursorInited(_pCursor))
		{
			node = DNodePtr(_pCursor->mc_pg[_pCursor->mc_top], _pCursor->mc_ki[_pCursor->mc_top]);
			if (((node->mn_flags & (DFDupData | DFSubData)) == DFDupData) && _pCursor->mc_xcursor->mx_cursor.mc_pg[0] != (CMdbPage *)DNodeData(node))
				printf("blah!\n");
		}
	}
#endif

#if (DMibDatabaseDebug) > 2
	/** Count all the pages in each DB and in the freelist
	 *  and make sure it matches the actual number of pages
	 *  being used.
	 *  All named DBs must be open for a correct count.
	 */
	void fg_Audit(CMdbTxn *_pTransaction)
	{
		CMdbCursor mc;
		CDatabaseValue key, data;
		CMdbId freecount, count;
		CDatabaseSubID i;
		int rc;

		freecount = 0;
		fg_CursorInit(&mc, _pTransaction, DFreeDbi, nullptr);
		while ((rc = fg_CursorGet(&mc, &key, &data, EDatabaseCursorGet_Next)) == 0)
			freecount += *(CMdbId *)data.m_pData;
		DMibCheck(rc == DMdbNotFound);

		count = 0;
		for (i = 0; i < _pTransaction->mt_numdbs; i++)
		{
			CMdbXCursor mx;
			if (!(_pTransaction->mt_dbflags[i] & DDbValid))
				continue;
			fg_CursorInit(&mc, _pTransaction, i, &mx);
			if (_pTransaction->mt_dbs[i].md_root == DPInvalid)
				continue;
			count += _pTransaction->mt_dbs[i].md_branch_pages + _pTransaction->mt_dbs[i].md_leaf_pages + _pTransaction->mt_dbs[i].md_overflow_pages;
			if (_pTransaction->mt_dbs[i].md_flags & EDatabaseSubFlag_SortedDuplicates)
			{
				rc = fg_PageSearch(&mc, nullptr, DMdbPsFirst);
				for (; rc == DMdbSuccess; rc = fg_CursorSibling(&mc, 1))
				{
					unsigned j;
					CMdbPage *mp;
					mp = mc.mc_pg[mc.mc_top];
					for (j = 0; j < DNumKeys(mp); j++)
					{
						CMdbNode *leaf = DNodePtr(mp, j);
						if (leaf->mn_flags & DFSubData)
						{
							CMdbDb db;
							memcpy(&db, DNodeData(leaf), sizeof(db));
							count += db.md_branch_pages + db.md_leaf_pages + db.md_overflow_pages;
						}
					}
				}
				DMibCheck(rc == DMdbNotFound);
			}
		}
		if (freecount + count + DNumMetas != _pTransaction->mt_next_pgno)
		{
			DMibConErrOut
				(
					"audit: {} freecount: {} count: {} total: {} next_pgno: {}\n"
					, _pTransaction->mt_txnid
					, freecount
					, count + DNumMetas
					, freecount + count + DNumMetas
					, _pTransaction->mt_next_pgno
				)
			;
		}
	}
#endif
}

