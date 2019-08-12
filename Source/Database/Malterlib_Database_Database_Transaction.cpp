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
	CDatabaseTransactionCommon::CDatabaseTransactionCommon() = default;

	CDatabaseTransactionCommon::~CDatabaseTransactionCommon()
	{
		if (mp_pTransaction)
			NPrivate::mdb_txn_abort(mp_pTransaction.f_Detach());
	}

	CDatabaseTransactionCommon::CDatabaseTransactionCommon(CDatabaseTransactionCommon &&_Other) = default;

	CDatabaseTransactionCommon::CDatabaseTransactionCommon(NStorage::TCUniquePointer<CInternal> &&_pTransaction)
		: mp_pTransaction(fg_Move(_pTransaction))
	{
	}

	CDatabaseTransactionCommon &CDatabaseTransactionCommon::operator =(CDatabaseTransactionCommon &&_Other) = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction() = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction(CDatabaseWriteTransaction &&_Other) = default;

	CDatabaseWriteTransaction &CDatabaseWriteTransaction::operator =(CDatabaseWriteTransaction &&_Other) = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction(CDatabaseTransactionCommon &&_Transaction)
		: CDatabaseTransactionCommon(fg_Move(_Transaction))
	{
	}

	void CDatabaseWriteTransaction::f_Commit()
	{
		NPrivate::mdb_txn_commit(mp_pTransaction.f_Detach());
	}

	CDatabaseReadTransaction::CDatabaseReadTransaction() = default;

	CDatabaseReadTransaction::CDatabaseReadTransaction(CDatabaseReadTransaction &&_Other) = default;

	CDatabaseReadTransaction &CDatabaseReadTransaction::operator =(CDatabaseReadTransaction &&_Other) = default;

	CDatabaseReadTransaction::CDatabaseReadTransaction(NStorage::TCUniquePointer<CDatabaseTransactionCommon::CInternal> &&_pTransaction)
		: CDatabaseTransactionCommon(fg_Move(_pTransaction))
	{
	}

	CDatabaseReadTransaction::CDatabaseReadTransaction(CDatabaseTransactionCommon &&_Transaction)
		: CDatabaseTransactionCommon(fg_Move(_Transaction))
	{
	}

	CDatabaseResetReadTransaction::CDatabaseResetReadTransaction(NStorage::TCUniquePointer<CDatabaseTransactionCommon::CInternal> &&_pTransaction)
		: mp_pTransaction(fg_Move(_pTransaction))
	{
	}

	/** @brief Create a transaction for use with the environment.
	 *
	 * The transaction handle may be discarded using #mdb_txn_abort() or #f_Commit().
	 * @note A transaction and its cursors must only be used by a single
	 * thread, and a thread may only have a single transaction at a time.
	 * If #EDatabaseEnvironmentFlag_NoTls is in use, this does not apply to read-only transactions.
	 * @note Cursors may not span transactions.
	 * @param[in] env An environment handle returned by #CDatabaseEnvironment::CDatabaseEnvironment()
	 * @param[in] parent If this parameter is non-NULL, the new transaction
	 * will be a nested transaction, with the transaction indicated by \b parent
	 * as its parent. Transactions may be nested to any level. A parent
	 * transaction and its cursors may not issue any other operations than
	 * f_Commit and mdb_txn_abort while it has active child transactions.
	 * @param[in] flags Special options for this transaction. This parameter
	 * must be set to 0 or by bitwise OR'ing together one or more of the
	 * values described here.
	 * <ul>
	 *	<li>#EDatabaseEnvironmentFlag_ReadOnly
	 *		This transaction will not perform any write operations.
	 *	<li>#EDatabaseEnvironmentFlag_NoSync
	 *		Don't flush system buffers to disk when committing this transaction.
	 *	<li>#EDatabaseEnvironmentFlag_NoMetaSync
	 *		Flush system buffers but omit metadata flush when committing this transaction.
	 * </ul>
	 * @param[out] txn Address where the new #MDB_txn handle will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_PANIC - a fatal error occurred earlier and the environment
	 *		must be shut down.
	 *	<li>#MDB_MAP_RESIZED - another process wrote data beyond this MDB_env's
	 *		mapsize and this environment's map must be resized as well.
	 *		See #f_SetMapSize().
	 *	<li>#MDB_READERS_FULL - a read-only transaction was requested and
	 *		the reader lock table is full. See #f_SetMaxReaders().
	 *	<li>ENOMEM - out of memory.
	 * </ul>
	 */
	CDatabaseReadTransaction CDatabaseEnvironment::f_BeginReadTransaction()
	{
		fp_CheckOpened();

		auto Transaction = mp_pEnvironment->mdb_txn_begin(nullptr, EDatabaseEnvironmentFlag_ReadOnly);
		return fg_Move(Transaction);
	}

	CDatabaseWriteTransaction CDatabaseEnvironment::f_BeginWriteTransaction(EDatabaseWriteTrasactionFlag _Flags)
	{
		fp_CheckOpened();

		static_assert(uint32(EDatabaseWriteTrasactionFlag_NoMetaSync) == uint32(EDatabaseEnvironmentFlag_NoMetaSync));
		static_assert(uint32(EDatabaseWriteTrasactionFlag_NoSync) == uint32(EDatabaseEnvironmentFlag_NoSync));

		DMibRequire((_Flags & ~(EDatabaseWriteTrasactionFlag_NoMetaSync | EDatabaseWriteTrasactionFlag_NoSync)) == 0);

		auto Transaction = mp_pEnvironment->mdb_txn_begin(nullptr, fg_AutoStaticCast(_Flags));
		return fg_Move(Transaction);
	}

	CDatabaseWriteTransaction CDatabaseWriteTransaction::f_BeginChildTransaction()
	{
		auto pParentTransaction = mp_pTransaction.f_Get();
		DMibRequire(pParentTransaction);
		auto Transaction = pParentTransaction->mt_env->mdb_txn_begin(mp_pTransaction.f_Get(), 0);
		return fg_Move(Transaction);
	}

	void CDatabaseTransactionCommon::CInternal::fs_CheckBlocked(uint32 _Flags)
	{
		if (_Flags & MDB_TXN_FINISHED)
			DMibError("Transaction already finished");
		if (_Flags & MDB_TXN_ERROR)
			DMibError("Transaction in error state");
		if (_Flags & MDB_TXN_HAS_CHILD)
			DMibError("Transtion has a child");
	}

	void CDatabaseTransactionCommon::CInternal::f_CheckBlocked() const
	{
		fs_CheckBlocked(mt_flags);
	}

	void CDatabaseEnvironment_Internal::mdb_dbi_close(CDatabaseSubID dbi)
	{
		void *ptr;
		if (dbi < CORE_DBS || dbi >= me_maxdbs)
			return;

		auto pStaticDasabaseInfo = m_StaticDasabaseInfo.f_GetArray();

		ptr = (ch8 *)pStaticDasabaseInfo[dbi].m_DatabaseName.m_pData;
		/* If there was no name, this was already closed */
		if (ptr)
		{
			pStaticDasabaseInfo[dbi].m_DatabaseName.m_pData = NULL;
			pStaticDasabaseInfo[dbi].m_DatabaseName.m_Size = 0;
			me_dbflags[dbi] = 0;
			me_dbiseqs[dbi]++;
			free(ptr);
		}
	}

	CDatabaseTransactionCommon CDatabaseEnvironment_Internal::mdb_txn_begin(NPrivate::MDB_txn *parent, uint32 flags)
	{
		using namespace NPrivate;
		MDB_txn *txn = nullptr;
		int size, tsize;

		flags &= MDB_TXN_BEGIN_FLAGS;
		flags |= me_flags & EDatabaseEnvironmentFlag_WriteMap;

		if (me_flags & EDatabaseEnvironmentFlag_ReadOnly & ~flags) /* write txn in RDONLY env */
			DMibError("Cannot open write transaction in read only environment");

		if (parent)
		{
			/* Nested transactions: Max 1 child, write txns only, no writemap */
			flags |= parent->mt_flags;
			if (parent->mt_flags & MDB_TXN_RDONLY)
				DMibError("Cannot create child transaction for read only transaction");
			if (flags & MDB_TXN_RDONLY)
				DMibError("Child transactions cannot be read only");
			if (flags & EDatabaseEnvironmentFlag_WriteMap)
				DMibError("Child transactions are not supported for write map");

			CDatabaseTransactionCommon::CInternal::fs_CheckBlocked(flags);

			/* Child txns save MDB_pgstate and use own copy of cursors */
			size = me_maxdbs * (sizeof(MDB_db)+sizeof(MDB_cursor *)+1);
			size += tsize = sizeof(MDB_ntxn);
		} else if (flags & EDatabaseEnvironmentFlag_ReadOnly) {
			size = me_maxdbs * (sizeof(MDB_db)+1);
			size += tsize = sizeof(MDB_txn);
		} else {
			/* Reuse preallocated write txn. However, do not touch it until
			 * mdb_txn_renew0() succeeds, since it currently may be active.
			 */
			txn = me_txn0;
		}
		auto Cleanup = g_OnScopeExit > [&]
			{
				if (txn && txn != me_txn0)
					NMemory::fg_FreeNoSize(txn);
			}
		;

		if (!txn)
		{
			txn = (MDB_txn *)NMemory::fg_AllocInitZero(size);

		#ifdef DMibDatabase_Always64BitSupport
			if (!parent)
			{
				txn->mt_rpages = (NPrivate::MDB_ID3L)NMemory::fg_Alloc(MDB_TRPAGE_SIZE * sizeof(MDB_ID3));
				txn->mt_rpages[0].mid = 0;
				txn->mt_rpcheck = MDB_TRPAGE_SIZE/2;
			}
		#endif
			txn->mt_dbxs = m_StaticDasabaseInfo.f_GetArray();	/* static */
			txn->mt_dbs = (MDB_db *) ((char *)txn + tsize);
			txn->mt_dbflags = (unsigned char *)txn + size - me_maxdbs;
			txn->mt_flags = flags;
			txn->mt_env = this;
		}

#ifdef DMibDatabase_Always64BitSupport
		auto Cleanup2 = g_OnScopeExit > [&]
			{
				if (txn->mt_rpages)
					NMemory::fg_FreeNoSize(txn->mt_rpages);
			}
		;
#endif
		if (parent)
		{
			uint32 i;
			txn->mt_cursors = (MDB_cursor **)(txn->mt_dbs + me_maxdbs);
			txn->mt_dbiseqs = parent->mt_dbiseqs;
			txn->mt_u.dirty_list = (MDB_ID2L)NMemory::fg_Alloc(sizeof(MDB_ID2)*MDB_IDL_UM_SIZE);
			auto Cleanup3 = g_OnScopeExit > [&]
				{
					NMemory::fg_FreeNoSize(txn->mt_u.dirty_list);
				}
			;
			txn->mt_free_pgs = mdb_midl_alloc(MDB_IDL_UM_MAX);
			auto Cleanup4 = g_OnScopeExit > [&]
				{
					mdb_txn_end(txn, MDB_END_FAIL_BEGINCHILD);
				}
			;
			Cleanup3.f_Clear();
			txn->mt_txnid = parent->mt_txnid;
			txn->mt_dirty_room = parent->mt_dirty_room;
			txn->mt_u.dirty_list[0].mid = 0;
			txn->mt_spill_pgs = nullptr;
			txn->mt_next_pgno = parent->mt_next_pgno;
			parent->mt_flags |= MDB_TXN_HAS_CHILD;
			parent->mt_child = txn;
			txn->mt_parent = parent;
			txn->mt_numdbs = parent->mt_numdbs;
	#ifdef DMibDatabase_Always64BitSupport
			txn->mt_rpages = parent->mt_rpages;
	#endif
			memcpy(txn->mt_dbs, parent->mt_dbs, txn->mt_numdbs * sizeof(MDB_db));
			/* Copy parent's mt_dbflags, but clear DB_NEW */
			for (i=0; i<txn->mt_numdbs; i++)
				txn->mt_dbflags[i] = parent->mt_dbflags[i] & ~DB_NEW;
			NPrivate::MDB_ntxn *ntxn = (MDB_ntxn *)txn;
			ntxn->mnt_pgstate = me_pgstate; /* save parent me_pgstate.mf_pghead & co */
			if (me_pgstate.mf_pghead)
			{
				size = MDB_IDL_SIZEOF(me_pgstate.mf_pghead);
				me_pgstate.mf_pghead = mdb_midl_alloc(me_pgstate.mf_pghead[0]);
				memcpy(me_pgstate.mf_pghead, ntxn->mnt_pgstate.mf_pghead, size);
			}
			NPrivate::mdb_cursor_shadow(parent, txn);

			Cleanup4.f_Clear();
			Cleanup3.f_Clear();
		}
		else
		{ /* EDatabaseEnvironmentFlag_ReadOnly */
			if (txn != me_txn0)
				txn->mt_dbiseqs = me_dbiseqs;
			mdb_txn_renew0(txn);
		}
#ifdef DMibDatabase_Always64BitSupport
		Cleanup2.f_Clear();
#endif
		Cleanup.f_Clear();
		txn->mt_flags |= flags;	/* could not change txn=me_txn0 earlier */
		DMibDatabaseTrace
			(
				"begin txn {}{} {} on mdbenv {}, root page {}"
				, txn->mt_txnid
				, (flags & EDatabaseEnvironmentFlag_ReadOnly) ? "r" : "w"
				, (void *) txn
				, (void *)this
				, txn->mt_dbs[MAIN_DBI].md_root
			)
		;

		CDatabaseTransactionCommon Transaction;
		Transaction.mp_pTransaction = fg_Explicit(txn);
		return Transaction;
	}


	/** @brief Return the transaction's ID.
	 *
	 * This returns the identifier associated with this transaction. For a
	 * read-only transaction, this corresponds to the snapshot being read;
	 * concurrent readers will frequently have the same transaction ID.
	 *
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @return A transaction ID, valid if input is an active transaction.
	 */
	CDatabaseOffset CDatabaseTransactionCommon::f_TransactionID()
	{
		return mp_pTransaction->mt_txnid;
	}

	CDatabaseTransactionCommon::CInternal *CDatabaseTransactionCommon::fp_GetInternal()
	{
		return mp_pTransaction.f_Get();
	}

	/** @brief Commit all the operations of a transaction into the database.
	 *
	 * The transaction handle is freed. It and its cursors must not be used
	 * again after this call.
	 * @note Earlier documentation incorrectly said all cursors would be freed.
	 * Only write-transactions free cursors.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 *	<li>ENOSPC - no more disk space.
	 *	<li>EIO - a low-level I/O error occurred while writing.
	 *	<li>ENOMEM - out of memory.
	 * </ul>
	 */
	void NPrivate::mdb_txn_commit(MDB_txn *txn)
	{
		DMibRequire(txn);
		using namespace NPrivate;
		uint32 i, end_mode;
		MDB_env	*env;

		/* mdb_txn_end() mode for a commit which writes nothing */
		end_mode = NPrivate::MDB_END_EMPTY_COMMIT|MDB_END_UPDATE|MDB_END_SLOT|MDB_END_FREE;

		auto Cleanup = g_OnScopeExit > [&]
			{
				NPrivate::mdb_txn_abort(txn);
			}
		;

		if (txn->mt_child)
			mdb_txn_commit(txn->mt_child);

		env = txn->mt_env;

		if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY))
			goto done;

		if (txn->mt_flags & (MDB_TXN_FINISHED|MDB_TXN_ERROR))
		{
			if (txn->mt_parent)
				txn->mt_parent->mt_flags |= MDB_TXN_ERROR;
			if (txn->mt_flags & MDB_TXN_FINISHED)
				DMibError("Transaction has already finished");
			if (txn->mt_flags & MDB_TXN_ERROR)
				DMibError("Transaction has already failed");
		}

		if (txn->mt_parent)
		{
			MDB_txn *parent = txn->mt_parent;
			MDB_page **lp;
			MDB_ID2L dst, src;
			MDB_IDL pspill;
			unsigned x, y, len, ps_len;

			/* Append our free list to parent's */
			mdb_midl_append_list(&parent->mt_free_pgs, txn->mt_free_pgs);
			mdb_midl_free(txn->mt_free_pgs);
			/* Failures after this must either undo the changes
			 * to the parent or set MDB_TXN_ERROR in the parent.
			 */

			parent->mt_next_pgno = txn->mt_next_pgno;
			parent->mt_flags = txn->mt_flags;

			/* Merge our cursors into parent's and close them */
			mdb_cursors_close(txn, 1);

			/* Update parent's DB table. */
			memcpy(parent->mt_dbs, txn->mt_dbs, txn->mt_numdbs * sizeof(MDB_db));
			parent->mt_numdbs = txn->mt_numdbs;
			parent->mt_dbflags[FREE_DBI] = txn->mt_dbflags[FREE_DBI];
			parent->mt_dbflags[MAIN_DBI] = txn->mt_dbflags[MAIN_DBI];
			for (i = CORE_DBS; i < txn->mt_numdbs; i++)
			{
				/* preserve parent's DB_NEW status */
				x = parent->mt_dbflags[i] & DB_NEW;
				parent->mt_dbflags[i] = txn->mt_dbflags[i] | x;
			}

			dst = parent->mt_u.dirty_list;
			src = txn->mt_u.dirty_list;
			/* Remove anything in our dirty list from parent's spill list */
			if ((pspill = parent->mt_spill_pgs) && (ps_len = pspill[0]))
			{
				x = y = ps_len;
				pspill[0] = (pgno_t)-1;
				/* Mark our dirty pages as deleted in parent spill list */
				for (i=0, len=src[0].mid; ++i <= len; )
				{
					MDB_ID pn = src[i].mid << 1;
					while (pn > pspill[x])
						x--;
					if (pn == pspill[x])
					{
						pspill[x] = 1;
						y = --x;
					}
				}
				/* Squash deleted pagenums if we deleted any */
				for (x=y; ++x <= ps_len; )
				{
					if (!(pspill[x] & 1))
						pspill[++y] = pspill[x];
				}
				pspill[0] = y;
			}

			/* Remove anything in our spill list from parent's dirty list */
			if (txn->mt_spill_pgs && txn->mt_spill_pgs[0])
			{
				for (i=1; i<=txn->mt_spill_pgs[0]; i++)
				{
					MDB_ID pn = txn->mt_spill_pgs[i];
					if (pn & 1)
						continue;	/* deleted spillpg */
					pn >>= 1;
					y = mdb_mid2l_search(dst, pn);
					if (y <= dst[0].mid && dst[y].mid == pn)
					{
						NMemory::fg_FreeNoSize(dst[y].mptr);
						while (y < dst[0].mid)
						{
							dst[y] = dst[y+1];
							y++;
						}
						dst[0].mid--;
					}
				}
			}

			/* Find len = length of merging our dirty list with parent's */
			x = dst[0].mid;
			dst[0].mid = 0;		/* simplify loops */
			if (parent->mt_parent)
			{
				len = x + src[0].mid;
				y = mdb_mid2l_search(src, dst[x].mid + 1) - 1;
				for (i = x; y && i; y--)
				{
					pgno_t yp = src[y].mid;
					while (yp < dst[i].mid)
						i--;
					if (yp == dst[i].mid)
					{
						i--;
						len--;
					}
				}
			}
			else
			{ /* Simplify the above for single-ancestor case */
				len = MDB_IDL_UM_MAX - txn->mt_dirty_room;
			}

			/* Merge our dirty list with parent's */
			y = src[0].mid;
			for (i = len; y; dst[i--] = src[y--])
			{
				pgno_t yp = src[y].mid;
				while (yp < dst[x].mid)
					dst[i--] = dst[x--];
				if (yp == dst[x].mid)
					NMemory::fg_FreeNoSize(dst[x--].mptr);
			}
			DMibCheck(i == x);
			dst[0].mid = len;
			NMemory::fg_FreeNoSize(txn->mt_u.dirty_list);
			parent->mt_dirty_room = txn->mt_dirty_room;
			if (txn->mt_spill_pgs)
			{
				if (parent->mt_spill_pgs)
				{
					/* TODO: Prevent failure here, so parent does not fail */
					{
						auto Cleanup = g_OnScopeExit > [&]()
							{
								parent->mt_flags |= MDB_TXN_ERROR;
							}
						;
						mdb_midl_append_list(&parent->mt_spill_pgs, txn->mt_spill_pgs);
						Cleanup.f_Clear();
					}
					mdb_midl_free(txn->mt_spill_pgs);
					mdb_midl_sort(parent->mt_spill_pgs);
				}
				else
					parent->mt_spill_pgs = txn->mt_spill_pgs;
			}

			/* Append our loose page list to parent's */
			for (lp = &parent->mt_loose_pgs; *lp; lp = &NEXT_LOOSE_PAGE(*lp))
				;
			*lp = txn->mt_loose_pgs;
			parent->mt_loose_count += txn->mt_loose_count;

			parent->mt_child = nullptr;
			mdb_midl_free(((MDB_ntxn *)txn)->mnt_pgstate.mf_pghead);
			NMemory::fg_FreeNoSize(txn);
			return;
		}

		if (txn != env->me_txn)
			DMibError("Trying to commit write transaction that is not the shared write transaction");

		mdb_cursors_close(txn, 0);

		if (!txn->mt_u.dirty_list[0].mid && !(txn->mt_flags & (MDB_TXN_DIRTY|MDB_TXN_SPILLS)))
			goto done;

		DMibDatabaseTrace("committing txn {} {} on mdbenv {}, root page {}", txn->mt_txnid, (void*)txn, (void*)env, txn->mt_dbs[MAIN_DBI].md_root);

		/* Update DB root pointers */
		if (txn->mt_numdbs > CORE_DBS)
		{
			MDB_cursor mc;
			CDatabaseSubID i;
			CDatabaseValue data;
			data.m_Size = sizeof(MDB_db);

			NPrivate::mdb_cursor_init(&mc, txn, MAIN_DBI, nullptr);
			for (i = CORE_DBS; i < txn->mt_numdbs; i++)
			{
				if (txn->mt_dbflags[i] & DB_DIRTY)
				{
					if (TXN_DBI_CHANGED(txn, i))
						DMibError("Stale write transaction");
					data.m_pData = (uint8 *)&txn->mt_dbs[i];
					int rc = mdb_cursor_put(&mc, &txn->mt_dbxs[i].m_DatabaseName, &data, (EDatabaseWriteFlag)F_SUBDATA);
					if (rc)
						DMibError(NStr::fg_Format("Error updating root pointers: {}", mdb_strerror(rc)));
				}
			}
		}

		{
			int rc = mdb_freelist_save(txn);
			if (rc)
				DMibError(NStr::fg_Format("Error saving free list: {}", mdb_strerror(rc)));
		}

		mdb_midl_free(env->me_pgstate.mf_pghead);
		env->me_pgstate.mf_pghead = nullptr;
		mdb_midl_shrink(&txn->mt_free_pgs);

	#if (DMibDatabaseDebug) > 2
		mdb_audit(txn);
	#endif

		if (int rc = mdb_page_flush(txn, 0))
			DMibError(NStr::fg_Format("Error flushing pages: {}", mdb_strerror(rc)));
		if (!F_ISSET(txn->mt_flags, MDB_TXN_NOSYNC))
			env->mdb_env_sync0(0, txn->mt_next_pgno);
		if (int rc = NPrivate::mdb_env_write_meta(txn))
			DMibError(NStr::fg_Format("Error writing meta data: {}", mdb_strerror(rc)));
		end_mode = MDB_END_COMMITTED|MDB_END_UPDATE;
		if (env->me_flags & EDatabaseEnvironmentFlag_PreviousSnapshot) {
			if (!(env->me_flags & EDatabaseEnvironmentFlag_NoLock)) {
				int excl;
				if (int rc = env->mdb_env_share_locks(&excl))
					DMibError(NStr::fg_Format("Error sharing locks: {}", mdb_strerror(rc)));
			}
			env->me_flags ^= EDatabaseEnvironmentFlag_PreviousSnapshot;
		}

	done:
		Cleanup.f_Clear();
		mdb_txn_end(txn, end_mode);
	}

	/** @brief Abandon all the operations of the transaction instead of saving them.
	 *
	 * The transaction handle is freed. It and its cursors must not be used
	 * again after this call.
	 * @note Earlier documentation incorrectly said all cursors would be freed.
	 * Only write-transactions free cursors.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 */
	void NPrivate::mdb_txn_abort(MDB_txn *txn)
	{
		if (txn == nullptr)
			return;

		if (txn->mt_child)
			mdb_txn_abort(txn->mt_child);

		mdb_txn_end(txn, NPrivate::MDB_END_ABORT|MDB_END_SLOT|MDB_END_FREE);
	}

	/** @brief Reset a read-only transaction.
	 *
	 * Abort the transaction like #mdb_txn_abort(), but keep the transaction
	 * handle. #f_Renew() may reuse the handle. This saves allocation
	 * overhead if the process will start a new read-only transaction soon,
	 * and also locking overhead if #EDatabaseEnvironmentFlag_NoTls is in use. The reader table
	 * lock is released, but the table slot stays tied to its thread or
	 * #MDB_txn. Use mdb_txn_abort() to discard a reset handle, and to free
	 * its lock table slot if EDatabaseEnvironmentFlag_NoTls is in use.
	 * Cursors opened within the transaction must not be used
	 * again after this call.
	 * Reader locks generally don't interfere with writers, but they keep old
	 * versions of database pages allocated. Thus they prevent the old pages
	 * from being reused when writers commit new data, and so under heavy load
	 * the database size may grow much more rapidly than otherwise.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 */
	CDatabaseResetReadTransaction CDatabaseReadTransaction::f_Reset()
	{
		auto pTransaction = mp_pTransaction.f_Get();
		/* This call is only valid for read-only txns */
		if (!(pTransaction->mt_flags & MDB_TXN_RDONLY))
			DMibError("You cannot reset a write rtransaction");

		NPrivate::mdb_txn_end(pTransaction, NPrivate::MDB_END_RESET);

		return fg_Move(mp_pTransaction);
	}

	/** @brief Renew a read-only transaction.
	 *
	 * This acquires a new reader lock for a transaction handle that had been
	 * released by #f_Reset(). It must be called before a reset transaction
	 * may be used again.
	 * @param[in] txn A transaction handle returned by #mdb_txn_begin()
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#MDB_PANIC - a fatal error occurred earlier and the environment
	 *		must be shut down.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	CDatabaseReadTransaction CDatabaseResetReadTransaction::f_Renew()
	{
		auto pTransaction = mp_pTransaction.f_Get();

		if (!(pTransaction->mt_flags & MDB_TXN_RDONLY))
			DMibError("Only read only transactions can be reused");
		if (!(pTransaction->mt_flags & MDB_TXN_FINISHED))
			DMibError("Only finished transactions can be reused");

		NPrivate::mdb_txn_renew0(pTransaction);
		DMibDatabaseTrace
			(
				"renew txn {}{} {} on mdbenv {}, root page {}"
				, pTransaction->mt_txnid
				, (pTransaction->mt_flags & MDB_TXN_RDONLY) ? "r" : "w"
				, (void *)pTransaction
				, (void *)pTransaction->mt_env
				, pTransaction->mt_dbs[MAIN_DBI].md_root
			)
		;
		return fg_Move(mp_pTransaction);
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

	/** Common code for #mdb_txn_begin() and #f_Renew().
	 * @param[in] txn the transaction handle to initialize
	 * @return 0 on success, non-zero on failure.
	 */
	void NPrivate::mdb_txn_renew0(MDB_txn *txn)
	{
		MDB_env *env = txn->mt_env;
		CLocksMemoryMap *pLocksMemoryMap = env->m_pLocksMemoryMap;
		MDB_meta *meta;
		uint32 i, nr, flags = txn->mt_flags;
		uint16 x;
		int new_notls = 0;

		if ((flags &= MDB_TXN_RDONLY) != 0) {
			if (!pLocksMemoryMap) {
				meta = env->mdb_env_pick_meta();
				txn->mt_txnid = meta->mm_txnid;
				txn->mt_u.reader = nullptr;
			} else {
				MDB_reader *r = (env->me_flags & EDatabaseEnvironmentFlag_NoTls) ? txn->mt_u.reader : (MDB_reader *)pthread_getspecific(env->me_txkey);
				if (r)
				{
					if (r->m_OwningProcessID != env->me_pid || r->m_BeginReadTransactionID != (CTransactionID)-1)
						DMibError("Invalid reuse of reader locktable slot");
				} else {
					CProcessID pid = env->me_pid;
					CThreadID tid = pthread_self();
					mdb_mutexref_t rmutex = env->me_rmutex;

					if (!env->me_live_reader) {
						int rc = env->mdb_reader_pid(Pidset, pid);
						if (rc)
							DMibError(NStr::fg_Format("Failed reader pid: {}", mdb_strerror(rc)));
						env->me_live_reader = 1;
					}

					{
						int rc;
						if (LOCK_MUTEX(rc, env, rmutex))
							DMibError(NStr::fg_Format("Read mutex lock failed: {}", mdb_strerror(rc)));
					}
					auto Unlock = g_OnScopeExit > [&]()
						{
							UNLOCK_MUTEX(rmutex);
						}
					;
					nr = pLocksMemoryMap->mti_numreaders;
					for (i=0; i<nr; i++)
						if (pLocksMemoryMap->mti_readers[i].m_OwningProcessID == 0)
							break;
					if (i == env->m_MaxReaders)
						DMibError("Readers full");
					r = &pLocksMemoryMap->mti_readers[i];
					/* Claim the reader slot, carefully since other code
					 * uses the reader table un-mutexed: First reset the
					 * slot, next publish it in mti_numreaders.  After
					 * that, it is safe for f_Close() to touch it.
					 * When it will be closed, we can finally claim it.
					 */
					r->m_OwningProcessID = 0;
					r->m_BeginReadTransactionID = (CTransactionID)-1;
					r->m_OwningThreadID = tid;
					if (i == nr)
						pLocksMemoryMap->mti_numreaders = ++nr;
					env->me_close_readers = nr;
					r->m_OwningProcessID = pid;

					new_notls = (env->me_flags & EDatabaseEnvironmentFlag_NoTls);
					{
						int rc;
						if (!new_notls && (rc=pthread_setspecific(env->me_txkey, r)))
						{
							r->m_OwningProcessID = 0;
							DMibError(NStr::fg_Format("pthread_setspecific failed: {}", mdb_strerror(rc)));
						}
					}
				}
				do /* LY: Retry on a race, ITS#7970. */
					r->m_BeginReadTransactionID = pLocksMemoryMap->mti_txnid;
				while(r->m_BeginReadTransactionID != pLocksMemoryMap->mti_txnid);
				txn->mt_txnid = r->m_BeginReadTransactionID;
				txn->mt_u.reader = r;
				meta = env->me_metas[txn->mt_txnid & 1];
			}

		}
		else
		{
			/* Not yet touching txn == env->me_txn0, it may be active */
			if (pLocksMemoryMap)
			{
				{
					int rc;
					if (LOCK_MUTEX(rc, env, env->me_wmutex))
						DMibError(NStr::fg_Format("Write mutex lock failed: {}", mdb_strerror(rc)));
					env->m_LockedThreadID = NSys::fg_Thread_GetCurrentUID();
				}
				txn->mt_txnid = pLocksMemoryMap->mti_txnid;
				meta = env->me_metas[txn->mt_txnid & 1];
			}
			else
			{
				meta = env->mdb_env_pick_meta();
				txn->mt_txnid = meta->mm_txnid;
			}
			txn->mt_txnid++;
	#if DMibDatabaseDebug
			if (txn->mt_txnid == mdb_debug_start)
				mdb_debug = 1;
	#endif
			txn->mt_child = nullptr;
			txn->mt_loose_pgs = nullptr;
			txn->mt_loose_count = 0;
			txn->mt_dirty_room = MDB_IDL_UM_MAX;
			txn->mt_u.dirty_list = env->me_dirty_list;
			txn->mt_u.dirty_list[0].mid = 0;
			txn->mt_free_pgs = env->me_free_pgs;
			txn->mt_free_pgs[0] = 0;
			txn->mt_spill_pgs = nullptr;
			env->me_txn = txn;
			memcpy(txn->mt_dbiseqs, env->me_dbiseqs, env->me_maxdbs * sizeof(uint32));
		}

		/* Copy the DB info and flags */
		memcpy(txn->mt_dbs, meta->mm_dbs, CORE_DBS * sizeof(MDB_db));

		/* Moved to here to avoid a data race in read TXNs */
		txn->mt_next_pgno = meta->mm_last_pg+1;
	#ifdef DMibDatabase_Always64BitSupport
		txn->mt_last_pgno = txn->mt_next_pgno - 1;
	#endif

		txn->mt_flags = flags;

		/* Setup db info */
		txn->mt_numdbs = env->me_numdbs;
		for (i=CORE_DBS; i<txn->mt_numdbs; i++) {
			x = env->me_dbflags[i];
			txn->mt_dbs[i].md_flags = x & PERSISTENT_FLAGS;
			txn->mt_dbflags[i] = (x & MDB_VALID) ? DB_VALID|DB_USRVALID|DB_STALE : 0;
		}
		txn->mt_dbflags[MAIN_DBI] = DB_VALID|DB_USRVALID;
		txn->mt_dbflags[FREE_DBI] = DB_VALID;

		auto Cleanup = g_OnScopeExit > [&]
			{
				NPrivate::mdb_txn_end(txn, new_notls /*0 or MDB_END_SLOT*/ | NPrivate::MDB_END_FAIL_BEGIN);
			}
		;

		if (env->me_flags & MDB_FATAL_ERROR)
			DMibError("Database environment had fatal error, must shutdown!");
		else if (env->me_maxpg < txn->mt_next_pgno)
			DMibError("Database size changed");
		else
			Cleanup.f_Clear();
	}

	/** Export or close DBI handles opened in this txn. */
	void NPrivate::mdb_dbis_update(MDB_txn *txn, int keep)
	{
		int i;
		CDatabaseSubID n = txn->mt_numdbs;
		MDB_env *env = txn->mt_env;
		unsigned char *tdbflags = txn->mt_dbflags;

		auto pStaticDatabasesInfos = env->m_StaticDasabaseInfo.f_GetArray();
		for (i = n; --i >= CORE_DBS;) {
			if (tdbflags[i] & DB_NEW) {
				if (keep) {
					env->me_dbflags[i] = txn->mt_dbs[i].md_flags | MDB_VALID;
				} else {
					char *ptr = (char *)pStaticDatabasesInfos[i].m_DatabaseName.m_pData;
					if (ptr) {
						pStaticDatabasesInfos[i].m_DatabaseName.m_pData = nullptr;
						pStaticDatabasesInfos[i].m_DatabaseName.m_Size = 0;
						env->me_dbflags[i] = 0;
						env->me_dbiseqs[i]++;
						NMemory::fg_FreeNoSize(ptr);
					}
				}
			}
		}
		if (keep && env->me_numdbs < n)
			env->me_numdbs = n;
	}

	/** End a transaction, except successful commit of a nested transaction.
	 * May be called twice for readonly txns: First reset it, then abort.
	 * @param[in] txn the transaction handle to end
	 * @param[in] mode why and how to end the transaction
	 */
	void NPrivate::mdb_txn_end(MDB_txn *txn, unsigned mode)
	{
		MDB_env	*env = txn->mt_env;
	#if DMibDatabaseDebug
		static const char *const names[] = MDB_END_NAMES;
	#endif

		/* Export or close DBI handles opened in this txn */
		mdb_dbis_update(txn, mode & MDB_END_UPDATE);

		DMibDatabaseTrace
			(
				"{} txn {}{} {} on mdbenv {}, root page {}"
				, names[mode & MDB_END_OPMASK]
				, txn->mt_txnid
				, (txn->mt_flags & MDB_TXN_RDONLY) ? "r" : "w"
				, (void *)txn
				, (void *)env
				, txn->mt_dbs[MAIN_DBI].md_root
			)
		;

		if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY)) {
			if (txn->mt_u.reader) {
				txn->mt_u.reader->m_BeginReadTransactionID = (CTransactionID)-1;
				if (!(env->me_flags & EDatabaseEnvironmentFlag_NoTls)) {
					txn->mt_u.reader = nullptr; /* txn does not own reader */
				} else if (mode & MDB_END_SLOT) {
					txn->mt_u.reader->m_OwningProcessID = 0;
					txn->mt_u.reader = nullptr;
				} /* else txn owns the slot until it does MDB_END_SLOT */
			}
			txn->mt_numdbs = 0;		/* prevent further DBI activity */
			txn->mt_flags |= MDB_TXN_FINISHED;

		} else if (!F_ISSET(txn->mt_flags, MDB_TXN_FINISHED)) {
			pgno_t *pghead = env->me_pgstate.mf_pghead;

			if (!(mode & MDB_END_UPDATE)) /* !(already closed cursors) */
				mdb_cursors_close(txn, 0);
			if (!(env->me_flags & EDatabaseEnvironmentFlag_WriteMap)) {
				mdb_dlist_free(txn);
			}

			txn->mt_numdbs = 0;
			txn->mt_flags = MDB_TXN_FINISHED;

			if (!txn->mt_parent) {
				mdb_midl_shrink(&txn->mt_free_pgs);
				env->me_free_pgs = txn->mt_free_pgs;
				/* me_pgstate: */
				env->me_pgstate.mf_pghead = nullptr;
				env->me_pgstate.mf_pglast = 0;

				env->me_txn = nullptr;
				mode = 0;	/* txn == env->me_txn0, do not free() it */

				/* The writer mutex was locked in mdb_txn_begin. */
				if (env->m_pLocksMemoryMap)
				{
					DMibCheck(env->m_LockedThreadID == NSys::fg_Thread_GetCurrentUID());
					UNLOCK_MUTEX(env->me_wmutex);
				}
			} else {
				txn->mt_parent->mt_child = nullptr;
				txn->mt_parent->mt_flags &= ~MDB_TXN_HAS_CHILD;
				env->me_pgstate = ((MDB_ntxn *)txn)->mnt_pgstate;
				mdb_midl_free(txn->mt_free_pgs);
				mdb_midl_free(txn->mt_spill_pgs);
				NMemory::fg_FreeNoSize(txn->mt_u.dirty_list);
			}

			mdb_midl_free(pghead);
		}

	#ifdef DMibDatabase_Always64BitSupport
		if (!txn->mt_parent) {
			MDB_ID3L el = env->me_rpages, tl = txn->mt_rpages;
			unsigned i, x, n = tl[0].mid;
			pthread_mutex_lock(&env->me_rpmutex);
			for (i = 1; i <= n; i++) {
				if (tl[i].mid & (MDB_RPAGE_CHUNK-1)) {
					/* tmp overflow pages that we didn't share in env */
					munmap(tl[i].mptr, tl[i].mcnt * env->me_psize);
				} else {
					x = mdb_mid3l_search(el, tl[i].mid);
					if (tl[i].mptr == el[x].mptr) {
						el[x].mref--;
					} else {
						/* another tmp overflow page */
						munmap(tl[i].mptr, tl[i].mcnt * env->me_psize);
					}
				}
			}
			pthread_mutex_unlock(&env->me_rpmutex);
			tl[0].mid = 0;
			if (mode & MDB_END_FREE)
				NMemory::fg_FreeNoSize(tl);
		}
	#endif
		if (mode & MDB_END_FREE)
			NMemory::fg_FreeNoSize(txn);
	}

	/** Save the freelist as of this transaction to the freeDB.
	 * This changes the freelist. Keep trying until it stabilizes.
	 *
	 * When (MDB_DEVEL) & 2, the changes do not affect #mdb_page_alloc(),
	 * it then uses the transaction's original snapshot of the freeDB.
	 */
	int NPrivate::mdb_freelist_save(MDB_txn *txn)
	{
		/* env->me_pgstate.mf_pghead[] can grow and shrink during this call.
		 * env->me_pgstate.mf_pglast and txn->mt_free_pgs[] can only grow.
		 * Page numbers cannot disappear from txn->mt_free_pgs[].
		 */
		MDB_cursor mc;
		MDB_env	*env = txn->mt_env;
		int rc, maxfree_1pg = env->me_maxfree_1pg, more = 1;
		CTransactionID	pglast = 0, head_id = 0;
		pgno_t	freecnt = 0, *free_pgs, *mop;
		smint	head_room = 0, total_room = 0, mop_len, clean_limit;

		NPrivate::mdb_cursor_init(&mc, txn, FREE_DBI, nullptr);

		if (env->me_pgstate.mf_pghead) {
			/* Make sure first page of freeDB is touched and on freelist */
			rc = NPrivate::mdb_page_search(&mc, nullptr, MDB_PS_FIRST|MDB_PS_MODIFY);
			if (rc && rc != MDB_NOTFOUND)
				return rc;
		}

		if (!env->me_pgstate.mf_pghead && txn->mt_loose_pgs) {
			/* Put loose page numbers in mt_free_pgs, since
			 * we may be unable to return them to me_pgstate.mf_pghead.
			 */
			MDB_page *mp = txn->mt_loose_pgs;
			MDB_ID2 *dl = txn->mt_u.dirty_list;
			unsigned x;
			mdb_midl_need(&txn->mt_free_pgs, txn->mt_loose_count);

			for (; mp; mp = NEXT_LOOSE_PAGE(mp)) {
				mdb_midl_xappend(txn->mt_free_pgs, mp->mp_p.p_pgno);
				/* must also remove from dirty list */
				if (txn->mt_flags & MDB_TXN_WRITEMAP) {
					for (x=1; x<=dl[0].mid; x++)
						if (dl[x].mid == mp->mp_p.p_pgno)
							break;
					DMibCheck(x <= dl[0].mid);
				} else {
					x = mdb_mid2l_search(dl, mp->mp_p.p_pgno);
					DMibCheck(dl[x].mid == mp->mp_p.p_pgno);
					env->mdb_dpage_free(mp);
				}
				dl[x].mptr = NULL;
			}
			{
				/* squash freed slots out of the dirty list */
				unsigned y;
				for (y=1; dl[y].mptr && y <= dl[0].mid; y++);
				if (y <= dl[0].mid) {
					for(x=y, y++;;) {
						while (!dl[y].mptr && y <= dl[0].mid) y++;
						if (y > dl[0].mid) break;
						dl[x++] = dl[y++];
					}
					dl[0].mid = x-1;
				} else {
					/* all slots freed */
					dl[0].mid = 0;
				}
			}
			txn->mt_loose_pgs = nullptr;
			txn->mt_loose_count = 0;
		}

		/* EDatabaseWriteFlag_ReserveSpace cancels meminit in ovpage NMemory::fg_Alloc (when no WRITEMAP) */
		clean_limit = (env->me_flags & (EDatabaseEnvironmentFlag_NoMemoryInit|EDatabaseEnvironmentFlag_WriteMap))
			? SSIZE_MAX : maxfree_1pg;

		for (;;) {
			/* Come back here after each Put() in case freelist changed */
			CDatabaseValue key, data;
			pgno_t *pgs;
			smint j;

			/* If using records from freeDB which we have not yet
			 * deleted, delete them and any we reserved for me_pgstate.mf_pghead.
			 */
			while (pglast < env->me_pgstate.mf_pglast) {
				rc = NPrivate::mdb_cursor_first(&mc, &key, nullptr);
				if (rc)
					return rc;
				pglast = head_id = *(CTransactionID *)key.m_pData;
				total_room = head_room = 0;
				DMibCheck(pglast <= env->me_pgstate.mf_pglast);
				rc = mdb_cursor_del(&mc, EDatabaseWriteFlag_None);
				if (rc)
					return rc;
			}

			/* Save the IDL of pages freed by this txn, to a single record */
			if (freecnt < txn->mt_free_pgs[0]) {
				if (!freecnt) {
					/* Make sure last page of freeDB is touched and on freelist */
					rc = NPrivate::mdb_page_search(&mc, nullptr, MDB_PS_LAST|MDB_PS_MODIFY);
					if (rc && rc != MDB_NOTFOUND)
						return rc;
				}
				free_pgs = txn->mt_free_pgs;
				/* Write to last page of freeDB */
				key.m_Size = sizeof(txn->mt_txnid);
				key.m_pData = (uint8 *)&txn->mt_txnid;
				do {
					freecnt = free_pgs[0];
					data.m_Size = MDB_IDL_SIZEOF(free_pgs);
					rc = mdb_cursor_put(&mc, &key, &data, EDatabaseWriteFlag_ReserveSpace);
					if (rc)
						return rc;
					/* Retry if mt_free_pgs[] grew during the Put() */
					free_pgs = txn->mt_free_pgs;
				} while (freecnt < free_pgs[0]);
				mdb_midl_sort(free_pgs);
				memcpy((ch8 *)data.m_pData, free_pgs, data.m_Size);
	#if (DMibDatabaseDebug) > 1
				{
					uint32 i = free_pgs[0];
					DMibDatabaseTrace("IDL write txn {} root {} num {}", txn->mt_txnid, txn->mt_dbs[FREE_DBI].md_root, i);
					for (; i; i--)
						DMibDatabaseTrace("IDL {}", free_pgs[i]);
				}
	#endif
				continue;
			}

			mop = env->me_pgstate.mf_pghead;
			mop_len = (mop ? mop[0] : 0) + txn->mt_loose_count;

			/* Reserve records for me_pgstate.mf_pghead[]. Split it if multi-page,
			 * to avoid searching freeDB for a page range. Use keys in
			 * range [1,me_pgstate.mf_pglast]: Smaller than txnid of oldest reader.
			 */
			if (total_room >= mop_len) {
				if (total_room == mop_len || --more < 0)
					break;
			} else if (head_room >= maxfree_1pg && head_id > 1) {
				/* Keep current record (overflow page), add a new one */
				head_id--;
				head_room = 0;
			}
			/* (Re)write {key = head_id, IDL length = head_room} */
			total_room -= head_room;
			head_room = mop_len - total_room;
			if (head_room > maxfree_1pg && head_id > 1) {
				/* Overflow multi-page for part of me_pgstate.mf_pghead */
				head_room /= head_id; /* amortize page sizes */
				head_room += maxfree_1pg - head_room % (maxfree_1pg + 1);
			} else if (head_room < 0) {
				/* Rare case, not bothering to delete this record */
				head_room = 0;
			}
			key.m_Size = sizeof(head_id);
			key.m_pData = (uint8 *)&head_id;
			data.m_Size = (head_room + 1) * sizeof(pgno_t);
			rc = mdb_cursor_put(&mc, &key, &data, EDatabaseWriteFlag_ReserveSpace);
			if (rc)
				return rc;
			/* IDL is initially empty, zero out at least the length */
			pgs = (pgno_t *)data.m_pData;
			j = head_room > clean_limit ? head_room : 0;
			do {
				pgs[j] = 0;
			} while (--j >= 0);
			total_room += head_room;
		}

		/* Return loose page numbers to me_pgstate.mf_pghead, though usually none are
		 * left at this point.  The pages themselves remain in dirty_list.
		 */
		if (txn->mt_loose_pgs) {
			MDB_page *mp = txn->mt_loose_pgs;
			unsigned count = txn->mt_loose_count;
			MDB_IDL loose;
			/* Room for loose pages + temp IDL with same */
			mdb_midl_need(&env->me_pgstate.mf_pghead, 2*count+1);
			mop = env->me_pgstate.mf_pghead;
			loose = mop + MDB_IDL_ALLOCLEN(mop) - count;
			for (count = 0; mp; mp = NEXT_LOOSE_PAGE(mp))
				loose[ ++count ] = mp->mp_p.p_pgno;
			loose[0] = count;
			mdb_midl_sort(loose);
			mdb_midl_xmerge(mop, loose);
			txn->mt_loose_pgs = nullptr;
			txn->mt_loose_count = 0;
			mop_len = mop[0];
		}

		/* Fill in the reserved me_pgstate.mf_pghead records */
		rc = MDB_SUCCESS;
		if (mop_len)
		{
			CDatabaseValue key, data;

			mop += mop_len;
			rc = NPrivate::mdb_cursor_first(&mc, &key, &data);
			for (; !rc; rc = NPrivate::mdb_cursor_next(&mc, &key, &data, MDB_NEXT)) {
				CTransactionID id = *(CTransactionID *)key.m_pData;
				smint	len = (smint)(data.m_Size / sizeof(MDB_ID)) - 1;
				MDB_ID save;

				DMibCheck(len >= 0 && id <= env->me_pgstate.mf_pglast);
				key.m_pData = (uint8 *)&id;
				if (len > mop_len) {
					len = mop_len;
					data.m_Size = (len + 1) * sizeof(MDB_ID);
				}
				data.m_pData = (uint8 *)(mop -= len);
				save = mop[0];
				mop[0] = len;
				rc = mdb_cursor_put(&mc, &key, &data, EDatabaseWriteFlag_OverwriteAtCurrentCursorPos);
				mop[0] = save;
				if (rc || !(mop_len -= len))
					break;
			}
		}
		return rc;
	}
}
