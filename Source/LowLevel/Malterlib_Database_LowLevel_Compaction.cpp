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

#include "Malterlib_Database_LowLevel.h"
#include "Malterlib_Database_LowLevel_Internal.h"

namespace NMib
{
	struct NDatabase::NPrivate::CCompactingCopyState
	{
		pthread_mutex_t mc_mutex;
		pthread_cond_t mc_cond;
		char *mc_wbuf[2];
		char *mc_over[2];
		MDB_env *mc_env;
		MDB_txn *mc_txn;
		int mc_wlen[2];
		int mc_olen[2];
		pgno_t mc_next_pgno;
		HANDLE mc_fd;
		int mc_status;
		volatile int mc_new;
		int mc_toggle;
	};

	/** Dedicated writer thread for compacting copy. */
	THREAD_RET DMibDatabaseInfrequentSection CALL_CONV NDatabase::NPrivate::mdb_env_copythr(void *arg)
	{
		CCompactingCopyState *my = (CCompactingCopyState *)arg;
		char *ptr;
		int toggle = 0, wsize, rc;
	#ifdef DPlatformFamily_Windows
		DWORD len;
	#define DO_WRITE(rc, fd, ptr, w2, len)	rc = WriteFile(fd, ptr, w2, &len, nullptr)
	#else
		int len;
	#define DO_WRITE(rc, fd, ptr, w2, len)	len = write(fd, ptr, w2); rc = (len >= 0)
	#endif

		pthread_mutex_lock(&my->mc_mutex);
		my->mc_new = 0;
		pthread_cond_signal(&my->mc_cond);
		for(;;) {
			while (!my->mc_new)
				pthread_cond_wait(&my->mc_cond, &my->mc_mutex);
			if (my->mc_new < 0) {
				my->mc_new = 0;
				break;
			}
			my->mc_new = 0;
			wsize = my->mc_wlen[toggle];
			ptr = my->mc_wbuf[toggle];
	again:
			while (wsize > 0) {
				DO_WRITE(rc, my->mc_fd, ptr, wsize, len);
				if (!rc) {
					rc = ErrCode();
					break;
				} else if (len > 0) {
					rc = MDB_SUCCESS;
					ptr += len;
					wsize -= len;
					continue;
				} else {
					rc = EIO;
					break;
				}
			}
			if (rc) {
				my->mc_status = rc;
				break;
			}
			/* If there's an overflow page tail, write it too */
			if (my->mc_olen[toggle]) {
				wsize = my->mc_olen[toggle];
				ptr = my->mc_over[toggle];
				my->mc_olen[toggle] = 0;
				goto again;
			}
			my->mc_wlen[toggle] = 0;
			toggle ^= 1;
			pthread_cond_signal(&my->mc_cond);
		}
		pthread_cond_signal(&my->mc_cond);
		pthread_mutex_unlock(&my->mc_mutex);
		return (THREAD_RET)0;
	#undef DO_WRITE
	}

	/** Tell the writer thread there's a buffer ready to write */
	int DMibDatabaseInfrequentSection NDatabase::NPrivate::mdb_env_cthr_toggle(CCompactingCopyState *my, int st)
	{
		int toggle = my->mc_toggle ^ 1;
		pthread_mutex_lock(&my->mc_mutex);
		if (my->mc_status) {
			pthread_mutex_unlock(&my->mc_mutex);
			return my->mc_status;
		}
		while (my->mc_new == 1)
			pthread_cond_wait(&my->mc_cond, &my->mc_mutex);
		my->mc_new = st;
		my->mc_toggle = toggle;
		pthread_cond_signal(&my->mc_cond);
		pthread_mutex_unlock(&my->mc_mutex);
		return 0;
	}

	static constexpr mint gc_CopyBufferSize = 1024*1024;
	
	/** Depth-first tree traversal for compacting copy. */
	int DMibDatabaseInfrequentSection NDatabase::NPrivate::mdb_env_cwalk(CCompactingCopyState *my, pgno_t *pg, int flags)
	{
		
		MDB_cursor mc = {0};
		MDB_node *ni;
		MDB_page *mo, *mp, *leaf;
		char *buf, *ptr;
		int rc, toggle;
		uint32 i;

		/* Empty DB, nothing to do */
		if (*pg == P_INVALID)
			return MDB_SUCCESS;

		mc.mc_snum = 1;
		mc.mc_txn = my->mc_txn;
		mc.mc_flags = my->mc_txn->mt_flags & (C_ORIG_RDONLY|C_WRITEMAP);

		rc = mdb_page_get(&mc, *pg, &mc.mc_pg[0], nullptr);
		if (rc)
			return rc;
		rc = mdb_page_search_root(&mc, nullptr, MDB_PS_FIRST);
		if (rc)
			return rc;

		/* Make cursor pages writable */
		buf = ptr = (char *)NMem::fg_AllocNoSize(my->mc_env->me_psize * mc.mc_snum);

		for (i=0; i<mc.mc_top; i++) {
			mdb_page_copy((MDB_page *)ptr, mc.mc_pg[i], my->mc_env->me_psize);
			mc.mc_pg[i] = (MDB_page *)ptr;
			ptr += my->mc_env->me_psize;
		}

		/* This is writable space for a leaf page. Usually not needed. */
		leaf = (MDB_page *)ptr;

		toggle = my->mc_toggle;
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
								mdb_page_copy(leaf, mp, my->mc_env->me_psize);
								mp = leaf;
								ni = NODEPTR(mp, i);
							}

							memcpy(&pg, NODEDATA(ni), sizeof(pg));
							rc = mdb_page_get(&mc, pg, &omp, nullptr);
							if (rc)
								goto done;
							if (my->mc_wlen[toggle] >= gc_CopyBufferSize) 
							{
								rc = mdb_env_cthr_toggle(my, 1);
								if (rc)
									goto done;
								toggle = my->mc_toggle;
							}
							mo = (MDB_page *)(my->mc_wbuf[toggle] + my->mc_wlen[toggle]);
							memcpy(mo, omp, my->mc_env->me_psize);
							mo->mp_p.p_pgno = my->mc_next_pgno;
							my->mc_next_pgno += omp->mp_pages;
							my->mc_wlen[toggle] += my->mc_env->me_psize;
							if (omp->mp_pages > 1) {
								my->mc_olen[toggle] = my->mc_env->me_psize * (omp->mp_pages - 1);
								my->mc_over[toggle] = (char *)omp + my->mc_env->me_psize;
								rc = mdb_env_cthr_toggle(my, 1);
								if (rc)
									goto done;
								toggle = my->mc_toggle;
							}
							memcpy(NODEDATA(ni), &mo->mp_p.p_pgno, sizeof(pgno_t));
						} else if (ni->mn_flags & F_SUBDATA) {
							MDB_db db;

							/* Need writable leaf */
							if (mp != leaf) {
								mc.mc_pg[mc.mc_top] = leaf;
								mdb_page_copy(leaf, mp, my->mc_env->me_psize);
								mp = leaf;
								ni = NODEPTR(mp, i);
							}

							memcpy(&db, NODEDATA(ni), sizeof(db));
							my->mc_toggle = toggle;
							rc = mdb_env_cwalk(my, &db.md_root, ni->mn_flags & F_DUPDATA);
							if (rc)
								goto done;
							toggle = my->mc_toggle;
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
						mdb_page_copy(mc.mc_pg[mc.mc_top], mp, my->mc_env->me_psize);
						goto again;
					} else
						mc.mc_pg[mc.mc_top] = mp;
					continue;
				}
			}
			if (my->mc_wlen[toggle] >= gc_CopyBufferSize) 
			{
				rc = mdb_env_cthr_toggle(my, 1);
				if (rc)
					goto done;
				toggle = my->mc_toggle;
			}
			mo = (MDB_page *)(my->mc_wbuf[toggle] + my->mc_wlen[toggle]);
			mdb_page_copy(mo, mp, my->mc_env->me_psize);
			mo->mp_p.p_pgno = my->mc_next_pgno++;
			my->mc_wlen[toggle] += my->mc_env->me_psize;
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
		NMem::fg_Free(buf);
		return rc;
	}

	/** Copy environment with compaction. */
	void DMibDatabaseInfrequentSection NDatabase::CLowLevelEnvironment_Internal::f_CopyCompact(HANDLE fd)
	{
		using namespace NPrivate;
		MDB_meta *mm;
		MDB_page *mp;
		CCompactingCopyState my;
		pthread_t thr;
		int rc;

	#ifdef DPlatformFamily_Windows
		my.mc_mutex = CreateMutex(nullptr, FALSE, nullptr);
		my.mc_cond = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		
	#else
		pthread_mutex_init(&my.mc_mutex, nullptr);
		pthread_cond_init(&my.mc_cond, nullptr);
	#endif

		{
			mint Size = gc_CopyBufferSize*2;
			my.mc_wbuf[0] = (char *)NMem::fg_AllocAligned(Size, me_os_psize);
		}
		
		auto Cleanup = g_OnScopeExit > [&]
			{
				NMem::fg_Free(my.mc_wbuf[0]);
			}
		;
		
		memset(my.mc_wbuf[0], 0, gc_CopyBufferSize*2);
		my.mc_wbuf[1] = my.mc_wbuf[0] + gc_CopyBufferSize;
		my.mc_wlen[0] = 0;
		my.mc_wlen[1] = 0;
		my.mc_olen[0] = 0;
		my.mc_olen[1] = 0;
		my.mc_next_pgno = NUM_METAS;
		my.mc_status = 0;
		my.mc_new = 1;
		my.mc_toggle = 0;
		my.mc_env = this;
		my.mc_fd = fd;
		THREAD_CREATE(thr, mdb_env_copythr, &my);

		auto Transaction = mdb_txn_begin(nullptr, ELowLevelEnvironmentFlag_ReadOnly);
		auto pInternalTransaction = Transaction.fp_GetInternal();

		mp = (MDB_page *)my.mc_wbuf[0];
		memset(mp, 0, NUM_METAS * me_psize);
		mp->mp_p.p_pgno = 0;
		mp->mp_flags = P_META;
		mm = (MDB_meta *)METADATA(mp);
		mdb_env_init_meta0(mm);
		mm->mm_address = me_metas[0]->mm_address;

		mp = (MDB_page *)(my.mc_wbuf[0] + me_psize);
		mp->mp_p.p_pgno = 1;
		mp->mp_flags = P_META;
		*(MDB_meta *)METADATA(mp) = *mm;
		mm = (MDB_meta *)METADATA(mp);

		/* Count the number of NMem::fg_Free pages, subtract from lastpg to find
		 * number of active pages
		 */
		{
			MDB_ID freecount = 0;
			MDB_cursor mc;
			CLowLevelValue key, data;
			mdb_cursor_init(&mc, pInternalTransaction, FREE_DBI, nullptr);
			while ((rc = mdb_cursor_get(&mc, &key, &data, MDB_NEXT)) == 0)
				freecount += *(MDB_ID *)data.m_pData;
			freecount += pInternalTransaction->mt_dbs[FREE_DBI].md_branch_pages +
				pInternalTransaction->mt_dbs[FREE_DBI].md_leaf_pages +
				pInternalTransaction->mt_dbs[FREE_DBI].md_overflow_pages;

			/* Set metapage 1 */
			mm->mm_last_pg = pInternalTransaction->mt_next_pgno - freecount - 1;
			mm->mm_dbs[MAIN_DBI] = pInternalTransaction->mt_dbs[MAIN_DBI];
			if (mm->mm_last_pg > NUM_METAS-1) {
				mm->mm_dbs[MAIN_DBI].md_root = mm->mm_last_pg;
				mm->mm_txnid = 1;
			} else {
				mm->mm_dbs[MAIN_DBI].md_root = P_INVALID;
			}
		}
		my.mc_wlen[0] = me_psize * NUM_METAS;
		my.mc_txn = pInternalTransaction;
		pthread_mutex_lock(&my.mc_mutex);
		while(my.mc_new)
			pthread_cond_wait(&my.mc_cond, &my.mc_mutex);
		pthread_mutex_unlock(&my.mc_mutex);
		rc = mdb_env_cwalk(&my, &pInternalTransaction->mt_dbs[MAIN_DBI].md_root, 0);
		if (rc == MDB_SUCCESS && my.mc_wlen[my.mc_toggle])
			rc = mdb_env_cthr_toggle(&my, 1);
		mdb_env_cthr_toggle(&my, -1);
		pthread_mutex_lock(&my.mc_mutex);
		while(my.mc_new)
			pthread_cond_wait(&my.mc_cond, &my.mc_mutex);
		pthread_mutex_unlock(&my.mc_mutex);
		THREAD_FINISH(thr);

	#ifdef DPlatformFamily_Windows
		CloseHandle(my.mc_cond);
		CloseHandle(my.mc_mutex);
	#else
		pthread_cond_destroy(&my.mc_cond);
		pthread_mutex_destroy(&my.mc_mutex);
	#endif
	}
}
