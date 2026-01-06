// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2019 Howard Chu, Symas Corp.
 All rights reserved.

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
#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NDatabase::NPrivate
{
	using namespace NConcurrency;
	static constexpr mint gc_CopyBufferSize = 1024*1024;

	struct CCompactingCopyState
	{
		CCompactingCopyState(MDB_env *_pEnvironment, HANDLE _DestinationFile)
			: m_pEnvironment{_pEnvironment}
			, m_OutputFileHandle{_DestinationFile}
			, m_BlockingActorCheckout(fg_BlockingActor())
		{
		}

		~CCompactingCopyState()
		{
			if (m_WriteBuffers[0])
				NMemory::fg_Free(m_WriteBuffers[0], gc_CopyBufferSize*2);
		}

		void f_Init(mint _PageSize)
		{
			m_WriteBuffers[0] = (uint8 *)NMemory::fg_AllocAligned(gc_CopyBufferSize*2, _PageSize);
			NMemory::fg_MemClear(m_WriteBuffers[0], gc_CopyBufferSize*2);
			m_WriteBuffers[1] = m_WriteBuffers[0] + gc_CopyBufferSize;
		}

		CBlockingActorCheckout m_BlockingActorCheckout;

		uint8 *m_WriteBuffers[2] = {nullptr, nullptr};
		uint8 *m_OverflowPages[2] = {nullptr, nullptr};
		mint m_WriteBuffersLength[2] = {0, 0};
		mint m_OverflowPagesLength[2] = {0, 0};
		MDB_env *m_pEnvironment = nullptr;
		MDB_txn *m_pInternalTransaction = nullptr;
		pgno_t m_NextPageNo = NUM_METAS;
		HANDLE m_OutputFileHandle = INVALID_HANDLE_VALUE;
		int m_Status = 0;
		int m_iBuffer = 0;
	};

	/** Dedicated writer thread for compacting copy. */
	void DMibDatabaseInfrequentSection fg_DoBufferWrite(CCompactingCopyState &_CopyState, mint _iBuffer)
	{
		CCompactingCopyState &CopyState = _CopyState;
		int wsize;
		int rc = 0;
	#ifdef DPlatformFamily_Windows
		DWORD len;
	#define DO_WRITE(rc, fd, ptr, w2, len)	rc = WriteFile(fd, ptr, w2, &len, nullptr)
	#else
		int len;
	#define DO_WRITE(rc, fd, ptr, w2, len)	len = write(fd, ptr, w2); rc = (len >= 0)
	#endif

		wsize = CopyState.m_WriteBuffersLength[_iBuffer];
		uint8 *pWriteBuffer = CopyState.m_WriteBuffers[_iBuffer];
	again:
		while (wsize > 0) {
			DO_WRITE(rc, CopyState.m_OutputFileHandle, pWriteBuffer, wsize, len);
			if (!rc) {
				rc = ErrCode();
				break;
			} else if (len > 0) {
				rc = MDB_SUCCESS;
				pWriteBuffer += len;
				wsize -= len;
				continue;
			} else {
				rc = EIO;
				break;
			}
		}
		if (rc) {
			CopyState.m_Status = rc;
			return;
		}
		/* If there's an overflow page tail, write it too */
		if (CopyState.m_OverflowPagesLength[_iBuffer]) {
			wsize = CopyState.m_OverflowPagesLength[_iBuffer];
			pWriteBuffer = CopyState.m_OverflowPages[_iBuffer];
			CopyState.m_OverflowPagesLength[_iBuffer] = 0;
			goto again;
		}
		CopyState.m_WriteBuffersLength[_iBuffer] = 0;
	}

	/** Tell the writer thread there's a buffer ready to write */
	int DMibDatabaseInfrequentSection fg_DispatchCopyBuffer(CCompactingCopyState &_CopyState)
	{
		auto &CopyState = _CopyState;
		mint iBuffer = CopyState.m_iBuffer;
		// Block on last write finishing
		fg_Dispatch(CopyState.m_BlockingActorCheckout.f_Actor(), []{}).f_CallSync();

		if (CopyState.m_Status)
			return CopyState.m_Status;

		fg_Dispatch
			(
				CopyState.m_BlockingActorCheckout.f_Actor()
				, [pCopyState = &_CopyState, iBuffer]()
				{
					fg_DoBufferWrite(*pCopyState, iBuffer);
				}
			)
			.f_DiscardResult()
		;

		CopyState.m_iBuffer = iBuffer ^ 1;
		return 0;
	}

	/** Depth-first tree traversal for compacting copy. */
	int DMibDatabaseInfrequentSection fg_TraverseTreeCopyRecursive(CCompactingCopyState &_CopyState, pgno_t *pg, int flags)
	{
		MDB_cursor mc = {0};
		MDB_node *ni;
		MDB_page *mo, *mp, *leaf;
		char *buf, *ptr;
		int rc;
		uint32 i;

		/* Empty DB, nothing to do */
		if (*pg == P_INVALID)
			return MDB_SUCCESS;

		auto &CopyState = _CopyState;

		mc.mc_snum = 1;
		mc.mc_txn = CopyState.m_pInternalTransaction;
		mc.mc_flags = CopyState.m_pInternalTransaction->mt_flags & (C_ORIG_RDONLY|C_WRITEMAP);

		rc = mdb_page_get(&mc, *pg, &mc.mc_pg[0], nullptr);
		if (rc)
			return rc;
		rc = mdb_page_search_root(&mc, nullptr, MDB_PS_FIRST);
		if (rc)
			return rc;

		/* Make cursor pages writable */
		mint BuffAllocSize = CopyState.m_pEnvironment->me_psize * mc.mc_snum;
		buf = ptr = (char *)NMemory::fg_Alloc(BuffAllocSize);

		for (i=0; i<mc.mc_top; i++) {
			mdb_page_copy((MDB_page *)ptr, mc.mc_pg[i], CopyState.m_pEnvironment->me_psize);
			mc.mc_pg[i] = (MDB_page *)ptr;
			ptr += CopyState.m_pEnvironment->me_psize;
		}

		/* This is writable space for a leaf page. Usually not needed. */
		leaf = (MDB_page *)ptr;

		auto iBuffer = CopyState.m_iBuffer;
		while (mc.mc_snum > 0) {
			unsigned n;
			mp = mc.mc_pg[mc.mc_top];
			n = NUMKEYS(mp);

			if (IS_LEAF(mp)) {
				if (!IS_LEAF2(mp) && !(flags & F_DUPDATA)) {
					for (i=0; i<n; i++) {
						ni = NODEPTR(mp, i);
						if (ni->mn_flags & F_BIGDATA) {
							MDB_page *omp;
							pgno_t pg;

							/* Need writable leaf */
							if (mp != leaf) {
								mc.mc_pg[mc.mc_top] = leaf;
								mdb_page_copy(leaf, mp, CopyState.m_pEnvironment->me_psize);
								mp = leaf;
								ni = NODEPTR(mp, i);
							}

							memcpy(&pg, NODEDATA(ni), sizeof(pg));
							memcpy(NODEDATA(ni), &CopyState.m_NextPageNo, sizeof(pgno_t));
							rc = mdb_page_get(&mc, pg, &omp, nullptr);
							if (rc)
								goto done;
							if (CopyState.m_WriteBuffersLength[iBuffer] >= gc_CopyBufferSize)
							{
								rc = fg_DispatchCopyBuffer(CopyState);
								if (rc)
									goto done;
								iBuffer = CopyState.m_iBuffer;
							}
							mo = (MDB_page *)(CopyState.m_WriteBuffers[iBuffer] + CopyState.m_WriteBuffersLength[iBuffer]);
							memcpy(mo, omp, CopyState.m_pEnvironment->me_psize);
							mo->mp_p.p_pgno = CopyState.m_NextPageNo;
							CopyState.m_NextPageNo += omp->mp_pages;
							CopyState.m_WriteBuffersLength[iBuffer] += CopyState.m_pEnvironment->me_psize;
							if (omp->mp_pages > 1) {
								CopyState.m_OverflowPagesLength[iBuffer] = CopyState.m_pEnvironment->me_psize * (omp->mp_pages - 1);
								CopyState.m_OverflowPages[iBuffer] = (uint8 *)omp + CopyState.m_pEnvironment->me_psize;
								rc = fg_DispatchCopyBuffer(CopyState);
								if (rc)
									goto done;
								iBuffer = CopyState.m_iBuffer;
							}
						} else if (ni->mn_flags & F_SUBDATA) {
							MDB_db db;

							/* Need writable leaf */
							if (mp != leaf) {
								mc.mc_pg[mc.mc_top] = leaf;
								mdb_page_copy(leaf, mp, CopyState.m_pEnvironment->me_psize);
								mp = leaf;
								ni = NODEPTR(mp, i);
							}

							memcpy(&db, NODEDATA(ni), sizeof(db));
							CopyState.m_iBuffer = iBuffer;
							rc = fg_TraverseTreeCopyRecursive(CopyState, &db.md_root, ni->mn_flags & F_DUPDATA);
							if (rc)
								goto done;
							iBuffer = CopyState.m_iBuffer;
							memcpy(NODEDATA(ni), &db, sizeof(db));
						}
					}
				}
			} else {
				mc.mc_ki[mc.mc_top]++;
				if (mc.mc_ki[mc.mc_top] < n) {
					pgno_t pg;
	again:
					ni = NODEPTR(mp, mc.mc_ki[mc.mc_top]);
					pg = NODEPGNO(ni);
					rc = mdb_page_get(&mc, pg, &mp, nullptr);
					if (rc)
						goto done;
					mc.mc_top++;
					mc.mc_snum++;
					mc.mc_ki[mc.mc_top] = 0;
					if (IS_BRANCH(mp))
					{
						/* Whenever we advance to a sibling branch page,
						 * we must proceed all the way down to its first leaf.
						 */
						mdb_page_copy(mc.mc_pg[mc.mc_top], mp, CopyState.m_pEnvironment->me_psize);
						goto again;
					} else
						mc.mc_pg[mc.mc_top] = mp;
					continue;
				}
			}
			if (CopyState.m_WriteBuffersLength[iBuffer] >= gc_CopyBufferSize)
			{
				rc = fg_DispatchCopyBuffer(CopyState);
				if (rc)
					goto done;
				iBuffer = CopyState.m_iBuffer;
			}
			mo = (MDB_page *)(CopyState.m_WriteBuffers[iBuffer] + CopyState.m_WriteBuffersLength[iBuffer]);
			mdb_page_copy(mo, mp, CopyState.m_pEnvironment->me_psize);
			mo->mp_p.p_pgno = CopyState.m_NextPageNo++;
			CopyState.m_WriteBuffersLength[iBuffer] += CopyState.m_pEnvironment->me_psize;
			if (mc.mc_top) {
				/* Update parent if there is one */
				ni = NODEPTR(mc.mc_pg[mc.mc_top-1], mc.mc_ki[mc.mc_top-1]);
				SETPGNO(ni, mo->mp_p.p_pgno);
				mdb_cursor_pop(&mc);
			} else {
				/* Otherwise we're done */
				*pg = mo->mp_p.p_pgno;
				break;
			}
		}
	done:
		NMemory::fg_Free(buf, BuffAllocSize);
		return rc;
	}
}

namespace NMib::NDatabase
{
	/** Copy environment with compaction. */
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_CopyCompact(HANDLE fd)
	{
		using namespace NPrivate;
		CCompactingCopyState CopyState{this, fd};
		int rc;
		pgno_t root, new_root;

		CopyState.f_Init(me_os_psize);

		auto Transaction = mdb_txn_begin(nullptr, EDatabaseEnvironmentFlag_ReadOnly);
		auto pInternalTransaction = Transaction.fp_GetInternal();

		MDB_page *mp = (MDB_page *)CopyState.m_WriteBuffers[0];
		memset(mp, 0, NUM_METAS * me_psize);
		mp->mp_p.p_pgno = 0;
		mp->mp_flags = P_META;
		MDB_meta *mm = (MDB_meta *)METADATA(mp);
		mdb_env_init_meta0(mm);
		mm->mm_address = me_metas[0]->mm_address;

		mp = (MDB_page *)(CopyState.m_WriteBuffers[0] + me_psize);
		mp->mp_p.p_pgno = 1;
		mp->mp_flags = P_META;
		*(MDB_meta *)METADATA(mp) = *mm;
		mm = (MDB_meta *)METADATA(mp);

		/* Set metapage 1 with current main DB */
		root = new_root = pInternalTransaction->mt_dbs[MAIN_DBI].md_root;
		if (root != P_INVALID)
		{
			/* Count the number of free pages, subtract from lastpg to find
			 * number of active pages
			 */
			MDB_ID freecount = 0;
			MDB_cursor mc;
			CDatabaseValue key;
			CDatabaseValue data;
			mdb_cursor_init(&mc, pInternalTransaction, FREE_DBI, nullptr);
			while ((rc = mdb_cursor_get(&mc, &key, &data, MDB_NEXT)) == 0)
				freecount += *(MDB_ID *)data.m_pData;
			if (rc != MDB_NOTFOUND)
				DMibError(NStr::fg_Format("Error compacting database (read cursor): {}", mdb_strerror(rc)));
			freecount += pInternalTransaction->mt_dbs[FREE_DBI].md_branch_pages +
				pInternalTransaction->mt_dbs[FREE_DBI].md_leaf_pages +
				pInternalTransaction->mt_dbs[FREE_DBI].md_overflow_pages;

			/* Set metapage 1 */
			new_root = pInternalTransaction->mt_next_pgno - 1 - freecount;
			mm->mm_last_pg = new_root;
			mm->mm_dbs[MAIN_DBI] = pInternalTransaction->mt_dbs[MAIN_DBI];
			mm->mm_dbs[MAIN_DBI].md_root = new_root;
		}
		else
		{
			/* When the DB is empty, handle it specially to
			 * fix any breakage like page leaks from ITS#8174.
			 */
			mm->mm_dbs[MAIN_DBI].md_flags = pInternalTransaction->mt_dbs[MAIN_DBI].md_flags;
		}

		if (root != P_INVALID || mm->mm_dbs[MAIN_DBI].md_flags)
			mm->mm_txnid.f_Store(1, NAtomic::EMemoryOrder_Relaxed);		/* use metapage 1 */

		CopyState.m_WriteBuffersLength[0] = me_psize * NUM_METAS;
		CopyState.m_pInternalTransaction = pInternalTransaction;
		fg_DispatchCopyBuffer(CopyState);
		fg_Dispatch(CopyState.m_BlockingActorCheckout.f_Actor(), []{}).f_CallSync();
		rc = fg_TraverseTreeCopyRecursive(CopyState, &root, 0);

		if (rc == MDB_SUCCESS && root != new_root)
			rc = MDB_INCOMPATIBLE;	/* page leak or corrupt DB */

		if (rc)
			DMibError(NStr::fg_Format("Error compacting database: {}", mdb_strerror(rc)));

		if (rc == MDB_SUCCESS && CopyState.m_WriteBuffersLength[CopyState.m_iBuffer])
			rc = fg_DispatchCopyBuffer(CopyState);

		// Block
		fg_Dispatch(CopyState.m_BlockingActorCheckout.f_Actor(), []{}).f_CallSync();
		if (rc)
			DMibError(NStr::fg_Format("Error compacting database: {}", mdb_strerror(rc)));

		rc = fg_DispatchCopyBuffer(CopyState); // Get status
		if (rc)
			DMibError(NStr::fg_Format("Error compacting database: {}", mdb_strerror(rc)));
		fg_Dispatch(CopyState.m_BlockingActorCheckout.f_Actor(), []{}).f_CallSync();
	}
}
