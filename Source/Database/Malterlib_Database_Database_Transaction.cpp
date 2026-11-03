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
	CDatabaseTransactionCommon::CDatabaseTransactionCommon() = default;

	CDatabaseTransactionCommon::~CDatabaseTransactionCommon()
	{
		if (mp_pTransaction)
			NPrivate::fg_TxnAbort(mp_pTransaction.f_Detach());
	}

	CDatabaseTransactionCommon::CDatabaseTransactionCommon(CDatabaseTransactionCommon &&_Other) = default;

	CDatabaseTransactionCommon::CDatabaseTransactionCommon(NStorage::TCUniquePointer<CInternal> &&_pTransaction)
		: mp_pTransaction(fg_Move(_pTransaction))
	{
	}

	CDatabaseTransactionCommon &CDatabaseTransactionCommon::operator = (CDatabaseTransactionCommon &&_Other) = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction() = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction(CDatabaseWriteTransaction &&_Other) = default;

	CDatabaseWriteTransaction &CDatabaseWriteTransaction::operator = (CDatabaseWriteTransaction &&_Other) = default;

	CDatabaseWriteTransaction::CDatabaseWriteTransaction(CDatabaseTransactionCommon &&_Transaction)
		: CDatabaseTransactionCommon(fg_Move(_Transaction))
	{
	}

	void CDatabaseWriteTransaction::f_Commit()
	{
		if (mp_pTransaction.f_Get()->mt_child)
			DMibError("Transaction still has a child transaction");

		NPrivate::fg_TxnCommit(mp_pTransaction.f_Detach());
	}

	CDatabaseReadTransaction::CDatabaseReadTransaction() = default;

	CDatabaseReadTransaction::CDatabaseReadTransaction(CDatabaseReadTransaction &&_Other) = default;

	CDatabaseReadTransaction &CDatabaseReadTransaction::operator = (CDatabaseReadTransaction &&_Other) = default;

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
	 * The transaction handle may be discarded using #fg_TxnAbort() or #f_Commit().
	 * @note A transaction and its cursors must only be used by a single
	 * thread, and a thread may only have a single transaction at a time.
	 * If #EDatabaseEnvironmentFlag_NoTls is in use, this does not apply to read-only transactions.
	 * @note Cursors may not span transactions.
	 * @param[in] env An environment handle returned by #CDatabaseEnvironment::CDatabaseEnvironment()
	 * @param[in] parent If this parameter is non-NULL, the new transaction
	 * will be a nested transaction, with the transaction indicated by \b parent
	 * as its parent. Transactions may be nested to any level. A parent
	 * transaction and its cursors may not issue any other operations than
	 * f_Commit and fg_TxnAbort while it has active child transactions.
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
	 * @param[out] txn Address where the new #CMdbTxn handle will be stored
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#DMdbPanic - a fatal error occurred earlier and the environment
	 *		must be shut down.
	 *	<li>#DMdbMapResized - another process wrote data beyond this CMdbEnv's
	 *		mapsize and this environment's map must be resized as well.
	 *		See #f_SetMapSize().
	 *	<li>#DMdbReadersFull - a read-only transaction was requested and
	 *		the reader lock table is full. See #f_SetMaxReaders().
	 *	<li>ENOMEM - out of memory.
	 * </ul>
	 */
	CDatabaseReadTransaction CDatabaseEnvironment::f_BeginReadTransaction()
	{
		fp_CheckOpened();

		auto Transaction = mp_pEnvironment->f_TxnBegin(nullptr, EDatabaseEnvironmentFlag_ReadOnly);
		return fg_Move(Transaction);
	}

	CDatabaseWriteTransaction CDatabaseEnvironment::f_BeginWriteTransaction(EDatabaseWriteTrasactionFlag _Flags)
	{
		fp_CheckOpened();

		static_assert(uint32(EDatabaseWriteTrasactionFlag_NoMetaSync) == uint32(EDatabaseEnvironmentFlag_NoMetaSync));
		static_assert(uint32(EDatabaseWriteTrasactionFlag_NoSync) == uint32(EDatabaseEnvironmentFlag_NoSync));

		DMibRequire((_Flags & ~(EDatabaseWriteTrasactionFlag_NoMetaSync | EDatabaseWriteTrasactionFlag_NoSync)) == 0);

		auto Transaction = mp_pEnvironment->f_TxnBegin(nullptr, fg_AutoStaticCast(_Flags));
		return fg_Move(Transaction);
	}

	CDatabaseWriteTransaction CDatabaseWriteTransaction::f_BeginChildTransaction()
	{
		auto pParentTransaction = mp_pTransaction.f_Get();
		DMibRequire(pParentTransaction);
		auto Transaction = pParentTransaction->mt_env->f_TxnBegin(mp_pTransaction.f_Get(), 0);
		return fg_Move(Transaction);
	}

	void CDatabaseTransactionCommon::CInternal::fs_CheckBlocked(uint32 _Flags)
	{
		if (_Flags & DMdbTxnFinished)
			DMibError("Transaction already finished");
		if (_Flags & DMdbTxnError)
			DMibError("Transaction in error state");
		if (_Flags & DMdbTxnHasChild)
			DMibError("Transtion has a child");
	}

	void CDatabaseTransactionCommon::CInternal::f_CheckBlocked() const
	{
		fs_CheckBlocked(mt_flags);
	}

	void CDatabaseEnvironment_Internal::f_DbiClose(CDatabaseSubID _DatabaseID)
	{
		void *ptr;
		if (_DatabaseID < DCoreDbs || _DatabaseID >= me_maxdbs)
			return;

		auto pStaticDasabaseInfo = m_StaticDasabaseInfo.f_GetArray();

		auto &StaticDasabaseInfo = pStaticDasabaseInfo[_DatabaseID];

		ptr = (ch8 *)StaticDasabaseInfo.m_DatabaseName.m_pData;
		auto Size = StaticDasabaseInfo.m_DatabaseName.m_Size;
		/* If there was no name, this was already closed */
		if (ptr)
		{
			StaticDasabaseInfo.m_DatabaseName.m_pData = NULL;
			StaticDasabaseInfo.m_DatabaseName.m_Size = 0;
			me_dbflags[_DatabaseID] = 0;
			me_dbiseqs[_DatabaseID]++;
			NMemory::fg_Free(ptr, Size + 1);
		}
	}

	void CDatabaseEnvironment_Internal::f_TxnFree(NPrivate::CMdbTxn *_pTransaction)
	{
		using namespace NPrivate;

		umint Size = 0;
		if (_pTransaction->mt_parent)
		{
			Size = me_maxdbs * (sizeof(CMdbDb) + sizeof(CMdbCursor *) + 1);
			Size += sizeof(CMdbNTxn);
		}
		else if (_pTransaction->mt_flags & EDatabaseEnvironmentFlag_ReadOnly)
		{
			Size = me_maxdbs * (sizeof(CMdbDb) + 1);
			Size += sizeof(CMdbTxn);
		}
		else
			DMibNeverGetHere;

		NMemory::fg_Free(_pTransaction, Size);
	}

	CDatabaseTransactionCommon CDatabaseEnvironment_Internal::f_TxnBegin(NPrivate::CMdbTxn *_pParent, uint32 _Flags)
	{
		using namespace NPrivate;
		CMdbTxn *txn = nullptr;
		int size, tsize;

		_Flags &= DMdbTxnBeginFlags;
		_Flags |= me_flags & EDatabaseEnvironmentFlag_WriteMap;

		if (me_flags & EDatabaseEnvironmentFlag_ReadOnly & ~_Flags) // write txn in RDONLY env
			DMibError("Cannot open write transaction in read only environment");

		if (_pParent)
		{
			/* Nested transactions: Max 1 child, write txns only, no writemap */
			_Flags |= _pParent->mt_flags;
			if (_pParent->mt_flags & DMdbTxnReadOnly)
				DMibError("Cannot create child transaction for read only transaction");
			if (_Flags & DMdbTxnReadOnly)
				DMibError("Child transactions cannot be read only");
			if (_Flags & EDatabaseEnvironmentFlag_WriteMap)
				DMibError("Child transactions are not supported for write map");

			CDatabaseTransactionCommon::CInternal::fs_CheckBlocked(_Flags);

			/* Child txns save CMdbPgState and use own copy of cursors */
			size = me_maxdbs * (sizeof(CMdbDb) + sizeof(CMdbCursor *) + 1);
			size += tsize = sizeof(CMdbNTxn);
		}
		else if (_Flags & EDatabaseEnvironmentFlag_ReadOnly)
		{
			size = me_maxdbs * (sizeof(CMdbDb) + 1);
			size += tsize = sizeof(CMdbTxn);
		}
		else
		{
			/* Reuse preallocated write txn. However, do not touch it until
			 * fg_TxnRenew0() succeeds, since it currently may be active.
			 */
			txn = me_txn0;
		}
		auto Cleanup = g_OnScopeExit / [&]
			{
				if (txn && txn != me_txn0)
					NMemory::fg_Free(txn, size);
			}
		;

		if (!txn)
		{
			txn = (CMdbTxn *)NMemory::fg_AllocInitZero(size);

#ifdef DMibDatabase_Always64BitSupport
			if (!_pParent)
			{
				txn->mt_rpages = (NPrivate::MDB_ID3L)NMemory::fg_Alloc(DMdbTrpageSize * sizeof(MDB_ID3));
				txn->mt_rpages[0].mid = 0;
				txn->mt_rpcheck = DMdbTrpageSize / 2;
			}
#endif
			txn->mt_dbxs = m_StaticDasabaseInfo.f_GetArray(); // static
			txn->mt_dbs = (CMdbDb *)((char *)txn + tsize);
			txn->mt_dbflags = (unsigned char *)txn + size - me_maxdbs;
			txn->mt_flags = _Flags;
			txn->mt_env = this;
		}

#ifdef DMibDatabase_Always64BitSupport
		auto Cleanup2 = g_OnScopeExit / [&]
			{
				if (txn->mt_rpages)
					NMemory::fg_Free(txn->mt_rpages, DMdbTrpageSize * sizeof(MDB_ID3));
			}
		;
#endif
		if (_pParent)
		{
			uint32 i;
			txn->mt_cursors = (CMdbCursor **)(txn->mt_dbs + me_maxdbs);
			txn->mt_dbiseqs = _pParent->mt_dbiseqs;
			txn->mt_u.dirty_list = (CMdbId2L)NMemory::fg_Alloc(sizeof(CMdbId2) * DMdbIdlUmSize);
			auto Cleanup3 = g_OnScopeExit / [&]
				{
					NMemory::fg_Free(txn->mt_u.dirty_list, sizeof(CMdbId2) * DMdbIdlUmSize);
				}
			;
			txn->mt_free_pgs = fg_MidlAlloc(DMdbIdlUmMax);
			auto Cleanup4 = g_OnScopeExit / [&]
				{
					fg_TxnEnd(txn, gc_MDB_END_FAIL_BEGINCHILD);
				}
			;
			Cleanup3.f_Clear();
			txn->mt_txnid = _pParent->mt_txnid;
			txn->mt_dirty_room = _pParent->mt_dirty_room;
			txn->mt_u.dirty_list[0].mid = 0;
			txn->mt_spill_pgs = nullptr;
			txn->mt_next_pgno = _pParent->mt_next_pgno;
			_pParent->mt_flags |= DMdbTxnHasChild;
			_pParent->mt_child = txn;
			txn->mt_parent = _pParent;
			txn->mt_numdbs = _pParent->mt_numdbs;
#ifdef DMibDatabase_Always64BitSupport
			txn->mt_rpages = _pParent->mt_rpages;
#endif
			memcpy(txn->mt_dbs, _pParent->mt_dbs, txn->mt_numdbs * sizeof(CMdbDb));
			/* Copy _pParent->mt_dbflags, but clear DDbNew */
			for (i = 0; i < txn->mt_numdbs; i++)
				txn->mt_dbflags[i] = _pParent->mt_dbflags[i] & ~DDbNew;
			NPrivate::CMdbNTxn *ntxn = (CMdbNTxn *)txn;
			ntxn->mnt_pgstate = me_pgstate; // save parent me_pgstate.mf_pghead & co
			if (me_pgstate.mf_pghead)
			{
				umint size = DMdbIdlSizeof(me_pgstate.mf_pghead);
				me_pgstate.mf_pghead = fg_MidlAlloc(me_pgstate.mf_pghead[0]);
				memcpy(me_pgstate.mf_pghead, ntxn->mnt_pgstate.mf_pghead, size);
			}
			NPrivate::fg_CursorShadow(_pParent, txn);

			Cleanup4.f_Clear();
			Cleanup3.f_Clear();
		}
		else
		{ // EDatabaseEnvironmentFlag_ReadOnly
			if (txn != me_txn0)
				txn->mt_dbiseqs = me_dbiseqs;
			fg_TxnRenew0(txn);
		}
#ifdef DMibDatabase_Always64BitSupport
		Cleanup2.f_Clear();
#endif
		Cleanup.f_Clear();
		txn->mt_flags |= _Flags; // could not change txn=me_txn0 earlier
		DMibDatabaseTrace
			(
				"begin txn {}{} {} on mdbenv {}, root page {}"
				, txn->mt_txnid
				, (_Flags & EDatabaseEnvironmentFlag_ReadOnly) ? "r" : "w"
				, (void *)txn
				, (void *)this
				, txn->mt_dbs[DMainDbi].md_root
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
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @return A transaction ID, valid if input is an active transaction.
	 */
	CDatabaseOffset CDatabaseTransactionCommon::f_TransactionID()
	{
		return mp_pTransaction->mt_txnid;
	}

	bool CDatabaseTransactionCommon::f_HasDatabases() const
	{
		auto &Environment = *this->mp_pTransaction->mt_env;
		return Environment.me_maxdbs > DCoreDbs;
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
	 * @param[in] _pTransaction A transaction handle returned by #f_TxnBegin()
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>EINVAL - an invalid parameter was specified.
	 *	<li>ENOSPC - no more disk space.
	 *	<li>EIO - a low-level I/O error occurred while writing.
	 *	<li>ENOMEM - out of memory.
	 * </ul>
	 */
	void NPrivate::fg_TxnCommit(CMdbTxn *_pTransaction)
	{
		DMibRequire(_pTransaction);
		using namespace NPrivate;
		uint32 i, end_mode;
		CMdbEnv *env;

		/* fg_TxnEnd() mode for a commit which writes nothing */
		end_mode = NPrivate::gc_MDB_END_EMPTY_COMMIT | DMdbEndUpdate | DMdbEndSlot | DMdbEndFree;

		if (_pTransaction->mt_child)
			DMibError("Transaction still has a child transaction");

		auto Cleanup = g_OnScopeExit / [&]
			{
				NPrivate::fg_TxnAbort(_pTransaction);
			}
		;

		env = _pTransaction->mt_env;

		if (DFIsSet(_pTransaction->mt_flags, DMdbTxnReadOnly))
			goto done;

		if (_pTransaction->mt_flags & (DMdbTxnFinished | DMdbTxnError))
		{
			if (_pTransaction->mt_parent)
				_pTransaction->mt_parent->mt_flags |= DMdbTxnError;
			if (_pTransaction->mt_flags & DMdbTxnFinished)
				DMibError("Transaction has already finished");
			if (_pTransaction->mt_flags & DMdbTxnError)
				DMibError("Transaction has already failed");
		}

		if (_pTransaction->mt_parent)
		{
			CMdbTxn *parent = _pTransaction->mt_parent;
			CMdbPage **lp;
			CMdbId2L dst, src;
			CMdbIdl pspill;
			unsigned x, y, len, ps_len;

			/* Append our free list to parent's */
			fg_MidlAppendList(&parent->mt_free_pgs, _pTransaction->mt_free_pgs);
			fg_MidlFree(_pTransaction->mt_free_pgs);
			/* Failures after this must either undo the changes
			 * to the parent or set DMdbTxnError in the parent.
			 */

			parent->mt_next_pgno = _pTransaction->mt_next_pgno;
			parent->mt_flags = _pTransaction->mt_flags;

			/* Merge our cursors into parent's and close them */
			fg_CursorsClose(_pTransaction, 1);

			/* Update parent's DB table. */
			memcpy(parent->mt_dbs, _pTransaction->mt_dbs, _pTransaction->mt_numdbs * sizeof(CMdbDb));
			parent->mt_numdbs = _pTransaction->mt_numdbs;
			parent->mt_dbflags[DFreeDbi] = _pTransaction->mt_dbflags[DFreeDbi];
			parent->mt_dbflags[DMainDbi] = _pTransaction->mt_dbflags[DMainDbi];
			for (i = DCoreDbs; i < _pTransaction->mt_numdbs; i++)
			{
				/* preserve parent's DDbNew status */
				x = parent->mt_dbflags[i] & DDbNew;
				parent->mt_dbflags[i] = _pTransaction->mt_dbflags[i] | x;
			}

			dst = parent->mt_u.dirty_list;
			src = _pTransaction->mt_u.dirty_list;
			/* Remove anything in our dirty list from parent's spill list */
			if ((pspill = parent->mt_spill_pgs) && (ps_len = pspill[0]))
			{
				x = y = ps_len;
				pspill[0] = (CPgno)-1;
				/* Mark our dirty pages as deleted in parent spill list */
				for (i = 0, len = src[0].mid; ++i <= len;)
				{
					CMdbId pn = src[i].mid << 1;
					while (pn > pspill[x])
						x--;
					if (pn == pspill[x])
					{
						pspill[x] = 1;
						y = --x;
					}
				}
				/* Squash deleted pagenums if we deleted any */
				for (x = y; ++x <= ps_len;)
				{
					if (!(pspill[x] & 1))
						pspill[++y] = pspill[x];
				}
				pspill[0] = y;
			}

			/* Remove anything in our spill list from parent's dirty list */
			if (_pTransaction->mt_spill_pgs && _pTransaction->mt_spill_pgs[0])
			{
				for (i = 1; i <= _pTransaction->mt_spill_pgs[0]; i++)
				{
					CMdbId pn = _pTransaction->mt_spill_pgs[i];
					if (pn & 1)
						continue; // deleted spillpg
					pn >>= 1;
					y = fg_Mid2lSearch(dst, pn);
					if (y <= dst[0].mid && dst[y].mid == pn)
					{
						env->f_DPageFreeMemory((CMdbPage *)dst[y].mptr);
						while (y < dst[0].mid)
						{
							dst[y] = dst[y + 1];
							y++;
						}
						dst[0].mid--;
					}
				}
			}

			/* Find len = length of merging our dirty list with parent's */
			x = dst[0].mid;
			dst[0].mid = 0; // simplify loops
			if (parent->mt_parent)
			{
				len = x + src[0].mid;
				y = fg_Mid2lSearch(src, dst[x].mid + 1) - 1;
				for (i = x; y && i; y--)
				{
					CPgno yp = src[y].mid;
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
			{ // Simplify the above for single-ancestor case
				len = DMdbIdlUmMax - _pTransaction->mt_dirty_room;
			}

			/* Merge our dirty list with parent's */
			y = src[0].mid;
			for (i = len; y; dst[i--] = src[y--])
			{
				CPgno yp = src[y].mid;
				while (yp < dst[x].mid)
					dst[i--] = dst[x--];
				if (yp == dst[x].mid)
					env->f_DPageFreeMemory((CMdbPage *)dst[x--].mptr);
			}
			DMibCheck(i == x);
			dst[0].mid = len;
			NMemory::fg_Free(_pTransaction->mt_u.dirty_list, sizeof(CMdbId2) * DMdbIdlUmSize);
			parent->mt_dirty_room = _pTransaction->mt_dirty_room;
			if (_pTransaction->mt_spill_pgs)
			{
				if (parent->mt_spill_pgs)
				{
					/* TODO: Prevent failure here, so parent does not fail */
					{
						auto Cleanup = g_OnScopeExit / [&]()
							{
								parent->mt_flags |= DMdbTxnError;
							}
						;
						fg_MidlAppendList(&parent->mt_spill_pgs, _pTransaction->mt_spill_pgs);
						Cleanup.f_Clear();
					}
					fg_MidlFree(_pTransaction->mt_spill_pgs);
					fg_MidlSort(parent->mt_spill_pgs);
				}
				else
					parent->mt_spill_pgs = _pTransaction->mt_spill_pgs;
			}

			/* Append our loose page list to parent's */
			for (lp = &parent->mt_loose_pgs; *lp; lp = &DNextLoosePage(*lp))
				;
			*lp = _pTransaction->mt_loose_pgs;
			parent->mt_loose_count += _pTransaction->mt_loose_count;

			parent->mt_child = nullptr;
			fg_MidlFree(((CMdbNTxn *)_pTransaction)->mnt_pgstate.mf_pghead);
			env->f_TxnFree(_pTransaction);
			Cleanup.f_Clear();
			return;
		}

		if (_pTransaction != env->me_txn)
			DMibError("Trying to commit write transaction that is not the shared write transaction");

		fg_CursorsClose(_pTransaction, 0);

		if (!_pTransaction->mt_u.dirty_list[0].mid && !(_pTransaction->mt_flags & (DMdbTxnDirty | DMdbTxnSpills)))
			goto done;

		DMibDatabaseTrace("committing txn {} {} on mdbenv {}, root page {}", _pTransaction->mt_txnid, (void *)_pTransaction, (void *)env, _pTransaction->mt_dbs[DMainDbi].md_root);

		/* Update DB root pointers */
		if (_pTransaction->mt_numdbs > DCoreDbs)
		{
			CMdbCursor mc;
			CDatabaseSubID i;
			CDatabaseValue data;
			data.m_Size = sizeof(CMdbDb);

			NPrivate::fg_CursorInit(&mc, _pTransaction, DMainDbi, nullptr);
			for (i = DCoreDbs; i < _pTransaction->mt_numdbs; i++)
			{
				if (_pTransaction->mt_dbflags[i] & DDbDirty)
				{
					if (DTxnDbiChanged(_pTransaction, i))
						DMibError("Stale write transaction");
					data.m_pData = (uint8 *)&_pTransaction->mt_dbs[i];
					int rc = fg_CursorPut(&mc, &_pTransaction->mt_dbxs[i].m_DatabaseName, &data, (EDatabaseWriteFlag)DFSubData);
					if (rc)
						DMibError(NStr::fg_Format("Error updating root pointers: {}", fg_StrError(rc)));
				}
			}
		}

		{
			int rc = fg_FreelistSave(_pTransaction);
			if (rc)
				DMibError(NStr::fg_Format("Error saving free list: {}", fg_StrError(rc)));
		}

		fg_MidlFree(env->me_pgstate.mf_pghead);
		env->me_pgstate.mf_pghead = nullptr;
		fg_MidlShrink(&_pTransaction->mt_free_pgs);

#if (DMibDatabaseDebug) > 2
		fg_Audit(_pTransaction);
#endif

		if (int rc = fg_PageFlush(_pTransaction, 0))
			DMibError(NStr::fg_Format("Error flushing pages: {}", fg_StrError(rc)));
		if (!DFIsSet(_pTransaction->mt_flags, DMdbTxnNoSync))
			env->f_Sync0(0, _pTransaction->mt_next_pgno);
		if (int rc = NPrivate::fg_EnvWriteMeta(_pTransaction))
			DMibError(NStr::fg_Format("Error writing metadata: {}", fg_StrError(rc)));
		end_mode = gc_MDB_END_COMMITTED | DMdbEndUpdate;
		if (env->me_flags & EDatabaseEnvironmentFlag_PreviousSnapshot)
		{
			if (!(env->me_flags & EDatabaseEnvironmentFlag_NoLock))
			{
				int excl;
				if (int rc = env->f_ShareLocks(&excl))
					DMibError(NStr::fg_Format("Error sharing locks: {}", fg_StrError(rc)));
			}
			env->me_flags ^= EDatabaseEnvironmentFlag_PreviousSnapshot;
		}

	done:
		Cleanup.f_Clear();
		fg_TxnEnd(_pTransaction, end_mode);
	}

	/** @brief Abandon all the operations of the transaction instead of saving them.
	 *
	 * The transaction handle is freed. It and its cursors must not be used
	 * again after this call.
	 * @note Earlier documentation incorrectly said all cursors would be freed.
	 * Only write-transactions free cursors.
	 * @param[in] _pTransaction A transaction handle returned by #f_TxnBegin()
	 */
	void NPrivate::fg_TxnAbort(CMdbTxn *_pTransaction)
	{
		if (_pTransaction == nullptr)
			return;

		if (_pTransaction->mt_child)
			fg_TxnAbort(_pTransaction->mt_child);

		fg_TxnEnd(_pTransaction, unsigned(NPrivate::gc_MDB_END_ABORT) | DMdbEndSlot | DMdbEndFree);
	}

	/** @brief Reset a read-only transaction.
	 *
	 * Abort the transaction like #fg_TxnAbort(), but keep the transaction
	 * handle. #f_Renew() may reuse the handle. This saves allocation
	 * overhead if the process will start a new read-only transaction soon,
	 * and also locking overhead if #EDatabaseEnvironmentFlag_NoTls is in use. The reader table
	 * lock is released, but the table slot stays tied to its thread or
	 * #CMdbTxn. Use fg_TxnAbort() to discard a reset handle, and to free
	 * its lock table slot if EDatabaseEnvironmentFlag_NoTls is in use.
	 * Cursors opened within the transaction must not be used
	 * again after this call.
	 * Reader locks generally don't interfere with writers, but they keep old
	 * versions of database pages allocated. Thus they prevent the old pages
	 * from being reused when writers commit new data, and so under heavy load
	 * the database size may grow much more rapidly than otherwise.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 */
	CDatabaseResetReadTransaction CDatabaseReadTransaction::f_Reset()
	{
		auto pTransaction = mp_pTransaction.f_Get();
		/* This call is only valid for read-only txns */
		if (!(pTransaction->mt_flags & DMdbTxnReadOnly))
			DMibError("You cannot reset a write rtransaction");

		NPrivate::fg_TxnEnd(pTransaction, NPrivate::gc_MDB_END_RESET);

		return fg_Move(mp_pTransaction);
	}

	/** @brief Renew a read-only transaction.
	 *
	 * This acquires a new reader lock for a transaction handle that had been
	 * released by #f_Reset(). It must be called before a reset transaction
	 * may be used again.
	 * @param[in] txn A transaction handle returned by #f_TxnBegin()
	 * @return A non-zero error value on failure and 0 on success. Some possible
	 * errors are:
	 * <ul>
	 *	<li>#DMdbPanic - a fatal error occurred earlier and the environment
	 *		must be shut down.
	 *	<li>EINVAL - an invalid parameter was specified.
	 * </ul>
	 */
	CDatabaseReadTransaction CDatabaseResetReadTransaction::f_Renew()
	{
		auto pTransaction = mp_pTransaction.f_Get();

		if (!(pTransaction->mt_flags & DMdbTxnReadOnly))
			DMibError("Only read only transactions can be reused");
		if (!(pTransaction->mt_flags & DMdbTxnFinished))
			DMibError("Only finished transactions can be reused");

		NPrivate::fg_TxnRenew0(pTransaction);
		DMibDatabaseTrace
			(
				"renew txn {}{} {} on mdbenv {}, root page {}"
				, pTransaction->mt_txnid
				, (pTransaction->mt_flags & DMdbTxnReadOnly) ? "r" : "w"
				, (void *)pTransaction
				, (void *)pTransaction->mt_env
				, pTransaction->mt_dbs[DMainDbi].md_root
			)
		;
		return fg_Move(mp_pTransaction);
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	// Private from here
	// ---------------------------------------------------------------------------------------------------------------------------------------

	/** Common code for #f_TxnBegin() and #f_Renew().
	 * @param[in] _pTransaction the transaction handle to initialize
	 * @return 0 on success, non-zero on failure.
	 */
	void NPrivate::fg_TxnRenew0(CMdbTxn *_pTransaction)
	{
		CMdbEnv *env = _pTransaction->mt_env;
		CLocksMemoryMap *pLocksMemoryMap = env->m_pLocksMemoryMap;
		CMdbMeta *meta;
		uint32 i, nr, flags = _pTransaction->mt_flags;
		uint16 x;
		int new_notls = 0;

		if ((flags &= DMdbTxnReadOnly) != 0)
		{
			if (!pLocksMemoryMap)
			{
				meta = env->f_PickMeta();
				_pTransaction->mt_txnid = meta->mm_txnid.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
				_pTransaction->mt_u.reader = nullptr;
			}
			else
			{
				CMdbReader *r = (env->me_flags & EDatabaseEnvironmentFlag_NoTls) ? _pTransaction->mt_u.reader : (CMdbReader *)pthread_getspecific(env->me_txkey);
				if (r)
				{
					if
					(
						r->m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) != env->me_pid
						|| r->m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) != (CTransactionID)-1
					)
					{
						DMibError("Invalid reuse of reader locktable slot");
					}
				}
				else
				{
					CProcessID pid = env->me_pid;
					CThreadID tid = pthread_self();
					CMdbMutexRef rmutex = env->me_rmutex;

					if (!env->me_live_reader)
					{
						int rc = env->f_ReaderPid(EPidLockOp_Set, pid);
						if (rc)
							DMibError(NStr::fg_Format("Failed reader pid: {}", fg_StrError(rc)));
						env->me_live_reader = 1;
					}

					{
						int rc;
						if (DLockMutex(rc, env, rmutex))
							DMibError(NStr::fg_Format("Read mutex lock failed: {}", fg_StrError(rc)));
					}
					auto Unlock = g_OnScopeExit / [&]()
						{
							DUnlockMutex(rmutex);
						}
					;
					nr = pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
					for (i = 0; i < nr; i++)
					{
						if (pLocksMemoryMap->m_Readers[i].m_OwningProcessID.f_Load(NAtomic::gc_MemoryOrder_Acquire) == 0)
							break;
					}
					if (i == env->m_MaxReaders)
						DMibError("Readers full");
					r = &pLocksMemoryMap->m_Readers[i];
					/* Claim the reader slot, carefully since other code
					 * uses the reader table un-mutexed: First reset the
					 * slot, next publish it in m_TransactionBody.m_NumReaders.  After
					 * that, it is safe for f_Close() to touch it.
					 * When it will be closed, we can finally claim it.
					 */
					r->m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
					r->m_BeginReadTransactionID.f_Store((CTransactionID)-1, NAtomic::gc_MemoryOrder_Relaxed);
					r->m_OwningThreadID.f_Store(tid, NAtomic::gc_MemoryOrder_Relaxed);
					if (i == nr)
						pLocksMemoryMap->m_TransactionBody.m_NumReaders.f_Store(++nr, NAtomic::gc_MemoryOrder_Relaxed);
					env->me_close_readers.f_Store(nr, NAtomic::gc_MemoryOrder_Relaxed);
					r->m_OwningProcessID.f_Store(pid, NAtomic::gc_MemoryOrder_Release);

					new_notls = (env->me_flags & EDatabaseEnvironmentFlag_NoTls);
					{
						int rc;
						if (!new_notls && (rc = pthread_setspecific(env->me_txkey, r)))
						{
							r->m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
							DMibError(NStr::fg_Format("pthread_setspecific failed: {}", fg_StrError(rc)));
						}
					}
				}
				do // LY: Retry on a race, ITS#7970.
					r->m_BeginReadTransactionID.f_Store(pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire), NAtomic::gc_MemoryOrder_Release);
				while (r->m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire) != pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire));
				_pTransaction->mt_txnid = r->m_BeginReadTransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
				_pTransaction->mt_u.reader = r;
				meta = env->me_metas[_pTransaction->mt_txnid & 1];
			}
		}
		else
		{
			/* Not yet touching _pTransaction == env->me_txn0, it may be active */
			if (pLocksMemoryMap)
			{
				{
					int rc;
					if (DLockMutex(rc, env, env->me_wmutex))
						DMibError(NStr::fg_Format("Write mutex lock failed: {}", fg_StrError(rc)));
					env->m_LockedThreadID = NSys::fg_Thread_GetCurrentUID();
				}
				_pTransaction->mt_txnid = pLocksMemoryMap->m_TransactionBody.m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Acquire);
				meta = env->me_metas[_pTransaction->mt_txnid & 1];
			}
			else
			{
				meta = env->f_PickMeta();
				_pTransaction->mt_txnid = meta->mm_txnid.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			}
			_pTransaction->mt_txnid++;
#if DMibDatabaseDebug
			if (_pTransaction->mt_txnid == mdb_debug_start)
				mdb_debug = 1;
#endif
			_pTransaction->mt_child = nullptr;
			_pTransaction->mt_loose_pgs = nullptr;
			_pTransaction->mt_loose_count = 0;
			_pTransaction->mt_dirty_room = DMdbIdlUmMax;
			_pTransaction->mt_u.dirty_list = env->me_dirty_list;
			_pTransaction->mt_u.dirty_list[0].mid = 0;
			_pTransaction->mt_free_pgs = env->me_free_pgs;
			_pTransaction->mt_free_pgs[0] = 0;
			_pTransaction->mt_spill_pgs = nullptr;
			env->me_txn = _pTransaction;
			memcpy(_pTransaction->mt_dbiseqs, env->me_dbiseqs, env->me_maxdbs * sizeof(uint32));
		}

		/* Copy the DB info and flags */
		memcpy(_pTransaction->mt_dbs, meta->mm_dbs, DCoreDbs * sizeof(CMdbDb));

		/* Moved to here to avoid a data race in read TXNs */
		_pTransaction->mt_next_pgno = meta->mm_last_pg + 1;
#ifdef DMibDatabase_Always64BitSupport
		_pTransaction->mt_last_pgno = _pTransaction->mt_next_pgno - 1;
#endif

		_pTransaction->mt_flags = flags;

		/* Setup db info */
		_pTransaction->mt_numdbs = env->me_numdbs;
		for (i = DCoreDbs; i < _pTransaction->mt_numdbs; i++)
		{
			x = env->me_dbflags[i];
			_pTransaction->mt_dbs[i].md_flags = x & DPersistentFlags;
			_pTransaction->mt_dbflags[i] = (x & DMdbValid) ? DDbValid | DDbUserValid | DDbStale : 0;
		}
		_pTransaction->mt_dbflags[DMainDbi] = DDbValid | DDbUserValid;
		_pTransaction->mt_dbflags[DFreeDbi] = DDbValid;

		auto Cleanup = g_OnScopeExit / [&]
			{
				NPrivate::fg_TxnEnd(_pTransaction, new_notls /*0 or DMdbEndSlot*/ | NPrivate::gc_MDB_END_FAIL_BEGIN);
			}
		;

		if (env->me_flags & DMdbFatalError)
			DMibError("Database environment had fatal error, must shutdown!");
		else if (env->me_maxpg < _pTransaction->mt_next_pgno)
			DMibError("Database size changed");
		else
			Cleanup.f_Clear();
	}

	/** Export or close DBI handles opened in this txn. */
	void NPrivate::fg_DbisUpdate(CMdbTxn *_pTransaction, int _bKeep)
	{
		int i;
		CDatabaseSubID n = _pTransaction->mt_numdbs;
		CMdbEnv *env = _pTransaction->mt_env;
		unsigned char *tdbflags = _pTransaction->mt_dbflags;

		auto pStaticDatabasesInfos = env->m_StaticDasabaseInfo.f_GetArray();
		for (i = n; --i >= DCoreDbs;)
		{
			if (tdbflags[i] & DDbNew)
			{
				if (_bKeep)
					env->me_dbflags[i] = _pTransaction->mt_dbs[i].md_flags | DMdbValid;
				else
				{
					auto &StaticDatabaseInfo = pStaticDatabasesInfos[i];
					char *ptr = (char *)StaticDatabaseInfo.m_DatabaseName.m_pData;
					umint Size = StaticDatabaseInfo.m_DatabaseName.m_Size;
					if (ptr)
					{
						StaticDatabaseInfo.m_DatabaseName.m_pData = nullptr;
						StaticDatabaseInfo.m_DatabaseName.m_Size = 0;
						env->me_dbflags[i] = 0;
						env->me_dbiseqs[i]++;
						NMemory::fg_Free(ptr, Size + 1);
					}
				}
			}
		}
		if (_bKeep && env->me_numdbs < n)
			env->me_numdbs = n;
	}

	/** End a transaction, except successful commit of a nested transaction.
	 * May be called twice for readonly txns: First reset it, then abort.
	 * @param[in] _pTransaction the transaction handle to end
	 * @param[in] _Mode why and how to end the transaction
	 */
	void NPrivate::fg_TxnEnd(CMdbTxn *_pTransaction, unsigned _Mode)
	{
		CMdbEnv *env = _pTransaction->mt_env;
#if DMibDatabaseDebug
		static char const * const names[] = DMdbEndNames;
#endif

		/* Export or close DBI handles opened in this txn */
		fg_DbisUpdate(_pTransaction, _Mode & DMdbEndUpdate);

		DMibDatabaseTrace
			(
				"{} txn {}{} {} on mdbenv {}, root page {}"
				, names[_Mode & DMdbEndOpmask]
				, _pTransaction->mt_txnid
				, (_pTransaction->mt_flags & DMdbTxnReadOnly) ? "r" : "w"
				, (void *)_pTransaction
				, (void *)env
				, _pTransaction->mt_dbs[DMainDbi].md_root
			)
		;

		if (DFIsSet(_pTransaction->mt_flags, DMdbTxnReadOnly))
		{
			if (_pTransaction->mt_u.reader)
			{
				_pTransaction->mt_u.reader->m_BeginReadTransactionID.f_Store((CTransactionID)-1, NAtomic::gc_MemoryOrder_Release);
				if (!(env->me_flags & EDatabaseEnvironmentFlag_NoTls))
					_pTransaction->mt_u.reader = nullptr; // _pTransaction does not own reader
				else if (_Mode & DMdbEndSlot)
				{
					_pTransaction->mt_u.reader->m_OwningProcessID.f_Store(0, NAtomic::gc_MemoryOrder_Release);
					_pTransaction->mt_u.reader = nullptr;
				} // else _pTransaction owns the slot until it does DMdbEndSlot
			}
			_pTransaction->mt_numdbs = 0; // prevent further DBI activity
			_pTransaction->mt_flags |= DMdbTxnFinished;
		}
		else if (!DFIsSet(_pTransaction->mt_flags, DMdbTxnFinished))
		{
			CPgno *pghead = env->me_pgstate.mf_pghead;

			if (!(_Mode & DMdbEndUpdate)) // !(already closed cursors)
				fg_CursorsClose(_pTransaction, 0);
			if (!(env->me_flags & EDatabaseEnvironmentFlag_WriteMap))
				fg_DListFree(_pTransaction);

			_pTransaction->mt_numdbs = 0;
			_pTransaction->mt_flags = DMdbTxnFinished;

			if (!_pTransaction->mt_parent)
			{
				fg_MidlShrink(&_pTransaction->mt_free_pgs);
				env->me_free_pgs = _pTransaction->mt_free_pgs;
				/* me_pgstate: */
				env->me_pgstate.mf_pghead = nullptr;
				env->me_pgstate.mf_pglast = 0;

				env->me_txn = nullptr;
				_Mode = 0; // _pTransaction == env->me_txn0, do not free() it

				/* The writer mutex was locked in f_TxnBegin. */
				if (env->m_pLocksMemoryMap)
				{
					DMibCheck(env->m_LockedThreadID == NSys::fg_Thread_GetCurrentUID())(env->m_LockedThreadID)(NSys::fg_Thread_GetCurrentUID());
					DUnlockMutex(env->me_wmutex);
				}
			}
			else
			{
				_pTransaction->mt_parent->mt_child = nullptr;
				_pTransaction->mt_parent->mt_flags &= ~DMdbTxnHasChild;
				env->me_pgstate = ((CMdbNTxn *)_pTransaction)->mnt_pgstate;
				fg_MidlFree(_pTransaction->mt_free_pgs);
				fg_MidlFree(_pTransaction->mt_spill_pgs);
				NMemory::fg_Free(_pTransaction->mt_u.dirty_list, sizeof(CMdbId2) * DMdbIdlUmSize);
			}

			fg_MidlFree(pghead);
		}

#ifdef DMibDatabase_Always64BitSupport
		if (!_pTransaction->mt_parent)
		{
			MDB_ID3L el = env->me_rpages, tl = _pTransaction->mt_rpages;
			unsigned i, x, n = tl[0].mid;
			pthread_mutex_lock(&env->me_rpmutex);
			for (i = 1; i <= n; i++)
			{
				if (tl[i].mid & (DMdbRpageChunk - 1))
				{
					/* tmp overflow pages that we didn't share in env */
					munmap(tl[i].mptr, tl[i].mcnt * env->me_psize);
				}
				else
				{
					x = fg_Mid3lSearch(el, tl[i].mid);
					if (tl[i].mptr == el[x].mptr)
						el[x].mref--;
					else
					{
						/* another tmp overflow page */
						munmap(tl[i].mptr, tl[i].mcnt * env->me_psize);
					}
				}
			}
			pthread_mutex_unlock(&env->me_rpmutex);
			tl[0].mid = 0;
			if (_Mode & DMdbEndFree)
				NMemory::fg_Free(tl, DMdbTrpageSize * sizeof(MDB_ID3));
		}
#endif
		if (_Mode & DMdbEndFree)
			env->f_TxnFree(_pTransaction);
	}

	/** Save the freelist as of this transaction to the freeDB.
	 * This changes the freelist. Keep trying until it stabilizes.
	 *
	 * When (DMdbDevel) & 2, the changes do not affect #fg_PageAlloc(),
	 * it then uses the transaction's original snapshot of the freeDB.
	 */
	int NPrivate::fg_FreelistSave(CMdbTxn *_pTransaction)
	{
#ifndef DMibClangAnalyzerWorkaround
		/* env->me_pgstate.mf_pghead[] can grow and shrink during this call.
		 * env->me_pgstate.mf_pglast and _pTransaction->mt_free_pgs[] can only grow.
		 * Page numbers cannot disappear from _pTransaction->mt_free_pgs[].
		 */
		CMdbCursor mc;
		CMdbEnv *env = _pTransaction->mt_env;
		int rc, maxfree_1pg = env->me_maxfree_1pg, more = 1;
		CTransactionID pglast = 0, head_id = 0;
		CPgno freecnt = 0, *free_pgs, *mop;
		smint head_room = 0, total_room = 0, mop_len, clean_limit;

		NPrivate::fg_CursorInit(&mc, _pTransaction, DFreeDbi, nullptr);

		if (env->me_pgstate.mf_pghead)
		{
			/* Make sure first page of freeDB is touched and on freelist */
			rc = NPrivate::fg_PageSearch(&mc, nullptr, DMdbPsFirst | DMdbPsModify);
			if (rc && rc != DMdbNotFound)
				return rc;
		}

		if (!env->me_pgstate.mf_pghead && _pTransaction->mt_loose_pgs)
		{
			/* Put loose page numbers in mt_free_pgs, since
			 * we may be unable to return them to me_pgstate.mf_pghead.
			 */
			CMdbPage *mp = _pTransaction->mt_loose_pgs;
			CMdbId2 *dl = _pTransaction->mt_u.dirty_list;
			unsigned x;
			fg_MidlNeed(&_pTransaction->mt_free_pgs, _pTransaction->mt_loose_count);

			for (; mp; mp = DNextLoosePage(mp))
			{
				fg_MidlXAppend(_pTransaction->mt_free_pgs, mp->mp_p.p_pgno);
				/* must also remove from dirty list */
				if (_pTransaction->mt_flags & DMdbTxnWriteMap)
				{
					for (x = 1; x <= dl[0].mid; x++)
					{
						if (dl[x].mid == mp->mp_p.p_pgno)
							break;
					}
					DMibCheck(x <= dl[0].mid);
				}
				else
				{
					x = fg_Mid2lSearch(dl, mp->mp_p.p_pgno);
					DMibCheck(dl[x].mid == mp->mp_p.p_pgno);
					env->f_DPageFree(mp);
				}
				dl[x].mptr = NULL;
			}
			{
				/* squash freed slots out of the dirty list */
				unsigned y;
				for (y = 1; dl[y].mptr && y <= dl[0].mid; y++);
				if (y <= dl[0].mid)
				{
					for (x = y, y++;;)
					{
						while (!dl[y].mptr && y <= dl[0].mid)
							y++;
						if (y > dl[0].mid)
							break;
						dl[x++] = dl[y++];
					}
					dl[0].mid = x - 1;
				}
				else
				{
					/* all slots freed */
					dl[0].mid = 0;
				}
			}
			_pTransaction->mt_loose_pgs = nullptr;
			_pTransaction->mt_loose_count = 0;
		}

		/* EDatabaseWriteFlag_ReserveSpace cancels meminit in ovpage NMemory::fg_Alloc (when no WRITEMAP) */
		clean_limit = (env->me_flags & (EDatabaseEnvironmentFlag_NoMemoryInit | EDatabaseEnvironmentFlag_WriteMap)) ? SSIZE_MAX : maxfree_1pg;

		for (;;)
		{
			/* Come back here after each Put() in case freelist changed */
			CDatabaseValue key, data;
			CPgno *pgs;
			smint j;

			/* If using records from freeDB which we have not yet
			 * deleted, delete them and any we reserved for me_pgstate.mf_pghead.
			 */
			while (pglast < env->me_pgstate.mf_pglast)
			{
				rc = NPrivate::fg_CursorFirst(&mc, &key, nullptr);
				if (rc)
					return rc;
				pglast = head_id = *(CTransactionID *)key.m_pData;
				total_room = head_room = 0;
				DMibCheck(pglast <= env->me_pgstate.mf_pglast);
				rc = fg_CursorDel(&mc, EDatabaseWriteFlag_None);
				if (rc)
					return rc;
			}

			/* Save the IDL of pages freed by this txn, to a single record */
			if (freecnt < _pTransaction->mt_free_pgs[0])
			{
				if (!freecnt)
				{
					/* Make sure last page of freeDB is touched and on freelist */
					rc = NPrivate::fg_PageSearch(&mc, nullptr, DMdbPsLast | DMdbPsModify);
					if (rc && rc != DMdbNotFound)
						return rc;
				}
				free_pgs = _pTransaction->mt_free_pgs;
				/* Write to last page of freeDB */
				key.m_Size = sizeof(_pTransaction->mt_txnid);
				key.m_pData = (uint8 *)&_pTransaction->mt_txnid;
				do
				{
					freecnt = free_pgs[0];
					data.m_Size = DMdbIdlSizeof(free_pgs);
					rc = fg_CursorPut(&mc, &key, &data, EDatabaseWriteFlag_ReserveSpace);
					if (rc)
						return rc;
					/* Retry if mt_free_pgs[] grew during the Put() */
					free_pgs = _pTransaction->mt_free_pgs;
				}
				while (freecnt < free_pgs[0]);
				fg_MidlSort(free_pgs);
				memcpy((ch8 *)data.m_pData, free_pgs, data.m_Size);
#	if (DMibDatabaseDebug) > 1
				{
					uint32 i = free_pgs[0];
					DMibDatabaseTrace("IDL write txn {} root {} num {}", _pTransaction->mt_txnid, _pTransaction->mt_dbs[DFreeDbi].md_root, i);
					for (; i; i--)
						DMibDatabaseTrace("IDL {}", free_pgs[i]);
				}
#	endif
				continue;
			}

			mop = env->me_pgstate.mf_pghead;
			mop_len = (mop ? mop[0] : 0) + _pTransaction->mt_loose_count;

			/* Reserve records for me_pgstate.mf_pghead[]. Split it if multi-page,
			 * to avoid searching freeDB for a page range. Use keys in
			 * range [1,me_pgstate.mf_pglast]: Smaller than txnid of oldest reader.
			 */
			if (total_room >= mop_len)
			{
				if (total_room == mop_len || --more < 0)
					break;
			}
			else if (head_room >= maxfree_1pg && head_id > 1)
			{
				/* Keep current record (overflow page), add a new one */
				head_id--;
				head_room = 0;
			}
			/* (Re)write {key = head_id, IDL length = head_room} */
			total_room -= head_room;
			head_room = mop_len - total_room;
			if (head_room > maxfree_1pg && head_id > 1)
			{
				/* Overflow multi-page for part of me_pgstate.mf_pghead */
				head_room /= head_id; // amortize page sizes
				head_room += maxfree_1pg - head_room % (maxfree_1pg + 1);
			}
			else if (head_room < 0)
			{
				/* Rare case, not bothering to delete this record */
				head_room = 0;
			}
			key.m_Size = sizeof(head_id);
			key.m_pData = (uint8 *)&head_id;
			data.m_Size = (head_room + 1) * sizeof(CPgno);
			rc = fg_CursorPut(&mc, &key, &data, EDatabaseWriteFlag_ReserveSpace);
			if (rc)
				return rc;
			/* IDL is initially empty, zero out at least the length */
			pgs = (CPgno *)data.m_pData;
			j = head_room > clean_limit ? head_room : 0;
			do
			{
				pgs[j] = 0;
			}
			while (--j >= 0);
			total_room += head_room;
		}

		/* Return loose page numbers to me_pgstate.mf_pghead, though usually none are
		 * left at this point.  The pages themselves remain in dirty_list.
		 */
		if (_pTransaction->mt_loose_pgs)
		{
			CMdbPage *mp = _pTransaction->mt_loose_pgs;
			unsigned count = _pTransaction->mt_loose_count;
			CMdbIdl loose;
			/* Room for loose pages + temp IDL with same */
			fg_MidlNeed(&env->me_pgstate.mf_pghead, 2 * count + 1);
			mop = env->me_pgstate.mf_pghead;
			loose = mop + DMdbIdlAlloclen(mop) - count;
			for (count = 0; mp; mp = DNextLoosePage(mp))
				loose[++count] = mp->mp_p.p_pgno;
			loose[0] = count;
			fg_MidlSort(loose);
			fg_MidlXMerge(mop, loose);
			_pTransaction->mt_loose_pgs = nullptr;
			_pTransaction->mt_loose_count = 0;
			mop_len = mop[0];
		}

		/* Fill in the reserved me_pgstate.mf_pghead records */
		rc = DMdbSuccess;
		if (mop_len)
		{
			CDatabaseValue key, data;

			mop += mop_len;
			rc = NPrivate::fg_CursorFirst(&mc, &key, &data);
			for (; !rc; rc = NPrivate::fg_CursorNext(&mc, &key, &data, EDatabaseCursorGet_Next))
			{
				CTransactionID id = *(CTransactionID *)key.m_pData;
				smint len = (smint)(data.m_Size / sizeof(CMdbId)) - 1;
				CMdbId save;

				DMibCheck(len >= 0 && id <= env->me_pgstate.mf_pglast);
				key.m_pData = (uint8 *)&id;
				if (len > mop_len)
				{
					len = mop_len;
					data.m_Size = (len + 1) * sizeof(CMdbId);
				}
				mop -= len;
				data.m_pData = (uint8 *)mop;
				save = mop[0];
				mop[0] = len;
				rc = fg_CursorPut(&mc, &key, &data, EDatabaseWriteFlag_OverwriteAtCurrentCursorPos);
				mop[0] = save;
				if (rc || !(mop_len -= len))
					break;
			}
		}
		return rc;
#else
		return 0;
#endif
	}
}
