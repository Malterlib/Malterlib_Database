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

	/** Return the page number of \b mp which may be sub-page, for debug output */
	pgno_t mdb_dbg_pgno(MDB_page const *mp)
	{
		pgno_t ret;
		DCopyPgno(ret, mp->mp_p.p_pgno);
		return ret;
	}

	/** Display a key in hexadecimal and return the address of the result.
	 * @param[in] key the key to display
	 * @param[in] buf the buffer to write into. Should always be #DKBuf.
	 * @return The key in hexadecimal form.
	 */
	char const *mdb_dkey(CDatabaseValue const *key, char *buf)
	{
		char *ptr = buf;
		unsigned char *c = (unsigned char *)key->m_pData;
		uint32 i;
		size_t buf_len = DKBufMaxKeySize * 2;

		if (!key)
			return "";

		if (key->m_Size > DKBufMaxKeySize)
			return "MDB_MAXKEYSIZE";
		/* may want to make this a dynamic check: if the key is mostly
		 * printable characters, print it as-is instead of converting to hex.
		 */
#	if 1
		buf[0] = '\0';
		for (i = 0; i < key->m_Size; i++)
		{
			auto *pPrevPtr = ptr;
			ptr += snprintf(ptr, buf_len, "%02x", *c++);
			buf_len -= ptr - pPrevPtr;
		}
#	else
		sprintf(buf, "%.*s", key->m_Size, key->m_pData);
#	endif
		return buf;
	}

	char const *mdb_leafnode_type(MDB_node const *n)
	{
		static char const * const tp[2][2] = {{"", ": DB"}, {": sub-page", ": sub-DB"}};
		return DFIsSet(n->mn_flags, DFBigData) ? ": overflow page" : tp[DFIsSet(n->mn_flags, DFDupData)][DFIsSet(n->mn_flags, DFSubData)];
	}

	/** Display all the keys in the page. */
	void mdb_page_list(MDB_page const *mp)
	{
		pgno_t pgno = mdb_dbg_pgno(mp);
		char const *type, *state = (mp->mp_flags & DPDirty) ? ", dirty" : "";
		MDB_node *node;
		uint32 i, nkeys, nsize, total = 0;
		CDatabaseValue key;
		DKBuf;

		switch (mp->mp_flags & (DPBranch | DPLeaf | DPLeaf2 | DPMeta | DPOverflow | DPSubp))
		{
		case DPBranch: type = "Branch page"; break;
		case DPLeaf: type = "Leaf page"; break;
		case DPLeaf | DPSubp: type = "Sub-page"; break;
		case DPLeaf | DPLeaf2: type = "LEAF2 page"; break;
		case DPLeaf | DPLeaf2 | DPSubp: type = "LEAF2 sub-page"; break;
		case DPOverflow:
			DMibConErrOut("Overflow page {} pages {}{}\n", pgno, mp->mp_pages, state);
			return;
		case DPMeta:
			DMibConErrOut("Meta-page {} txnid {}\n", pgno, fg_RemoveQualifiers(((MDB_meta *)DMetaData(mp))->mm_txnid.f_Load(NAtomic::gc_MemoryOrder_Relaxed)));
			return;
		default:
			DMibConErrOut("Bad page {} flags 0x{nfh,sf0,sj4}\n", pgno, mp->mp_flags);
			return;
		}

		nkeys = DNumKeys(mp);
		DMibConErrOut("{} {} numkeys {}{}\n", type, pgno, nkeys, state);

		for (i = 0; i < nkeys; i++)
		{
			if (DIsLeaf2(mp))
			{ // LEAF2 pages have no mp_ptrs[] or node headers
				key.m_Size = nsize = mp->mp_pad;
				key.m_pData = DLeaf2Key(mp, i, nsize);
				total += nsize;
				DMibConErrOut("key {}: nsize {}, {}\n", i, nsize, DKey(&key));
				continue;
			}
			node = DNodePtr(mp, i);
			key.m_Size = node->mn_ksize;
			key.m_pData = node->mn_data;
			nsize = uint32(DNodeSize + key.m_Size);
			if (DIsBranch(mp))
			{
				DMibConErrOut("key {}: page {}, {}\n", i, DNodePgno(node), DKey(&key));
				total += nsize;
			}
			else
			{
				if (DFIsSet(node->mn_flags, DFBigData))
					nsize += sizeof(pgno_t);
				else
					nsize += DNodeDataSize(node);
				total += nsize;
				nsize += sizeof(indx_t);
				DMibConErrOut("key {}: nsize {}, {}{}\n", i, nsize, DKey(&key), mdb_leafnode_type(node));
			}
			total = DEven(total);
		}
		DMibConErrOut("Total: header {} + contents {} + unused {}\n", DIsLeaf2(mp) ? DPageHeaderSize : DPageBase + mp->mp_lower, total, DSizeLeft(mp));
	}

	void mdb_cursor_chk(MDB_cursor const *mc)
	{
		using namespace NPrivate;
		uint32 i;
		MDB_node *node;
		MDB_page *mp;

		if (!mc->mc_snum || !(mc->mc_flags & DCInitialized))
			return;
		for (i = 0; i < mc->mc_top; i++)
		{
			mp = mc->mc_pg[i];
			node = DNodePtr(mp, mc->mc_ki[i]);
			if (DNodePgno(node) != mc->mc_pg[i + 1]->mp_p.p_pgno)
				printf("oops!\n");
		}
		if (mc->mc_ki[i] >= DNumKeys(mc->mc_pg[i]))
			printf("ack!\n");
		if (DXCursorInited(mc))
		{
			node = DNodePtr(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top]);
			if (((node->mn_flags & (DFDupData | DFSubData)) == DFDupData) && mc->mc_xcursor->mx_cursor.mc_pg[0] != (MDB_page *)DNodeData(node))
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
	void mdb_audit(MDB_txn *txn)
	{
		MDB_cursor mc;
		CDatabaseValue key, data;
		MDB_ID freecount, count;
		CDatabaseSubID i;
		int rc;

		freecount = 0;
		mdb_cursor_init(&mc, txn, DFreeDbi, nullptr);
		while ((rc = mdb_cursor_get(&mc, &key, &data, MDB_NEXT)) == 0)
			freecount += *(MDB_ID *)data.m_pData;
		DMibCheck(rc == DMdbNotFound);

		count = 0;
		for (i = 0; i < txn->mt_numdbs; i++)
		{
			MDB_xcursor mx;
			if (!(txn->mt_dbflags[i] & DDbValid))
				continue;
			mdb_cursor_init(&mc, txn, i, &mx);
			if (txn->mt_dbs[i].md_root == DPInvalid)
				continue;
			count += txn->mt_dbs[i].md_branch_pages + txn->mt_dbs[i].md_leaf_pages + txn->mt_dbs[i].md_overflow_pages;
			if (txn->mt_dbs[i].md_flags & EDatabaseSubFlag_SortedDuplicates)
			{
				rc = mdb_page_search(&mc, nullptr, DMdbPsFirst);
				for (; rc == DMdbSuccess; rc = mdb_cursor_sibling(&mc, 1))
				{
					unsigned j;
					MDB_page *mp;
					mp = mc.mc_pg[mc.mc_top];
					for (j = 0; j < DNumKeys(mp); j++)
					{
						MDB_node *leaf = DNodePtr(mp, j);
						if (leaf->mn_flags & DFSubData)
						{
							MDB_db db;
							memcpy(&db, DNodeData(leaf), sizeof(db));
							count += db.md_branch_pages + db.md_leaf_pages + db.md_overflow_pages;
						}
					}
				}
				DMibCheck(rc == DMdbNotFound);
			}
		}
		if (freecount + count + DNumMetas != txn->mt_next_pgno)
			DMibConErrOut("audit: {} freecount: {} count: {} total: {} next_pgno: {}\n", txn->mt_txnid, freecount, count + DNumMetas, freecount + count + DNumMetas, txn->mt_next_pgno);
	}
#endif
}

