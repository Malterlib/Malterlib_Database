// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
SPDX-License-Identifier: OLDAP-2.8
SPDX-License-Identifier: ISC
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
	static constexpr umint gc_CopyBufferSize = 1024 * 1024;

	struct CCompactingCopyState
	{
		CCompactingCopyState(CMdbEnv *_pEnvironment, HANDLE _DestinationFile) // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
			: m_pEnvironment{_pEnvironment}
			, m_OutputFileHandle{_DestinationFile}
			, m_BlockingActorCheckout(fg_BlockingActor())
		{
		}

		~CCompactingCopyState()
		{
			if (m_WriteBuffers[0])
				NMemory::fg_Free(m_WriteBuffers[0], gc_CopyBufferSize * 2);
		}

		void f_Init(umint _PageSize)
		{
			m_WriteBuffers[0] = (uint8 *)NMemory::fg_AllocAligned(gc_CopyBufferSize * 2, _PageSize);
			NMemory::fg_MemClear(m_WriteBuffers[0], gc_CopyBufferSize * 2);
			m_WriteBuffers[1] = m_WriteBuffers[0] + gc_CopyBufferSize;
		}

		CBlockingActorCheckout m_BlockingActorCheckout;

		uint8 *m_WriteBuffers[2] = {nullptr, nullptr};
		uint8 *m_OverflowPages[2] = {nullptr, nullptr};
		umint m_WriteBuffersLength[2] = {0, 0};
		umint m_OverflowPagesLength[2] = {0, 0};
		CMdbEnv *m_pEnvironment = nullptr;
		CMdbTxn *m_pInternalTransaction = nullptr;
		CPgno m_NextPageNo = DNumMetas;
		HANDLE m_OutputFileHandle = INVALID_HANDLE_VALUE; // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
		int m_Status = 0;
		int m_iBuffer = 0;
	};

	/** Dedicated writer thread for compacting copy. */
	void DMibDatabaseInfrequentSection fg_DoBufferWrite(CCompactingCopyState &_CopyState, umint _iBuffer)
	{
		CCompactingCopyState &CopyState = _CopyState;
		int WriteSize;
		int Result = 0;
#ifdef DPlatformFamily_Windows
		DWORD nWritten;
#	define DDoWrite(d_Rc, d_Fd, d_Ptr, d_W2, d_Len)	d_Rc = WriteFile(d_Fd, d_Ptr, d_W2, &d_Len, nullptr)
#else
		int nWritten;
#	define DDoWrite(d_Rc, d_Fd, d_Ptr, d_W2, d_Len)	d_Len = write(d_Fd, d_Ptr, d_W2); d_Rc = (d_Len >= 0)
#endif

		WriteSize = CopyState.m_WriteBuffersLength[_iBuffer];
		uint8 *pWriteBuffer = CopyState.m_WriteBuffers[_iBuffer];
	again:
		while (WriteSize > 0)
		{
			DDoWrite(Result, CopyState.m_OutputFileHandle, pWriteBuffer, WriteSize, nWritten);
			if (!Result)
			{
				Result = DErrCode();
				break;
			}
			else if (nWritten > 0)
			{
				Result = DMdbSuccess;
				pWriteBuffer += nWritten;
				WriteSize -= nWritten;
				continue;
			}
			else
			{
				Result = EIO;
				break;
			}
		}
		if (Result)
		{
			CopyState.m_Status = Result;
			return;
		}
		/* If there's an overflow page tail, write it too */
		if (CopyState.m_OverflowPagesLength[_iBuffer])
		{
			WriteSize = CopyState.m_OverflowPagesLength[_iBuffer];
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
		umint iBuffer = CopyState.m_iBuffer;
		// Block on last write finishing
		fg_Dispatch
			(
				CopyState.m_BlockingActorCheckout.f_Actor()
				, []
				{
				}
			)
			.f_CallSync()
		;

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
	int DMibDatabaseInfrequentSection fg_TraverseTreeCopyRecursive(CCompactingCopyState &_CopyState, CPgno *_pPageNumber, int _Flags)
	{
		CMdbCursor Cursor = {0};
		CMdbNode *pNode;
		CMdbPage *pOutputPage, *pPage, *pLeafPage;
		char *pBuffer, *pBufferPosition;
		int Result;
		uint32 iIndex;

		/* Empty DB, nothing to do */
		if (*_pPageNumber == DPInvalid)
			return DMdbSuccess;

		auto &CopyState = _CopyState;

		Cursor.m_nPages = 1;
		Cursor.m_pTransaction = CopyState.m_pInternalTransaction;
		Cursor.m_Flags = CopyState.m_pInternalTransaction->m_Flags & (DCOrigReadOnly | DCWriteMap);

		Result = fg_PageGet(&Cursor, *_pPageNumber, &Cursor.m_Pages[0], nullptr);
		if (Result)
			return Result;
		Result = fg_PageSearchRoot(&Cursor, nullptr, DMdbPsFirst);
		if (Result)
			return Result;

		/* Make cursor pages writable */
		umint BuffAllocSize = CopyState.m_pEnvironment->m_PageSize * Cursor.m_nPages;
		pBuffer = pBufferPosition = (char *)NMemory::fg_Alloc(BuffAllocSize);

		for (iIndex = 0; iIndex < Cursor.m_iTop; iIndex++)
		{
			fg_PageCopy((CMdbPage *)pBufferPosition, Cursor.m_Pages[iIndex], CopyState.m_pEnvironment->m_PageSize);
			Cursor.m_Pages[iIndex] = (CMdbPage *)pBufferPosition;
			pBufferPosition += CopyState.m_pEnvironment->m_PageSize;
		}

		/* This is writable space for a leaf page. Usually not needed. */
		pLeafPage = (CMdbPage *)pBufferPosition;

		auto iBuffer = CopyState.m_iBuffer;
		while (Cursor.m_nPages > 0)
		{
			unsigned nKeys;
			pPage = Cursor.m_Pages[Cursor.m_iTop];
			nKeys = DNumKeys(pPage);

			if (DIsLeaf(pPage))
			{
				if (!DIsLeaf2(pPage) && !(_Flags & DFDupData))
				{
					for (iIndex = 0; iIndex < nKeys; iIndex++)
					{
						pNode = DNodePtr(pPage, iIndex);
						if (pNode->m_Flags & DFBigData)
						{
							CMdbPage *pOverflowPage;
							CPgno PageNumber;

							/* Need writable leaf */
							if (pPage != pLeafPage)
							{
								Cursor.m_Pages[Cursor.m_iTop] = pLeafPage;
								fg_PageCopy(pLeafPage, pPage, CopyState.m_pEnvironment->m_PageSize);
								pPage = pLeafPage;
								pNode = DNodePtr(pPage, iIndex);
							}

							memcpy(&PageNumber, DNodeData(pNode), sizeof(PageNumber));
							memcpy(DNodeData(pNode), &CopyState.m_NextPageNo, sizeof(CPgno));
							Result = fg_PageGet(&Cursor, PageNumber, &pOverflowPage, nullptr);
							if (Result)
								goto done;
							if (CopyState.m_WriteBuffersLength[iBuffer] >= gc_CopyBufferSize)
							{
								Result = fg_DispatchCopyBuffer(CopyState);
								if (Result)
									goto done;
								iBuffer = CopyState.m_iBuffer;
							}
							pOutputPage = (CMdbPage *)(CopyState.m_WriteBuffers[iBuffer] + CopyState.m_WriteBuffersLength[iBuffer]);
							memcpy(pOutputPage, pOverflowPage, CopyState.m_pEnvironment->m_PageSize);
							pOutputPage->m_PageNumber = CopyState.m_NextPageNo;
							CopyState.m_NextPageNo += pOverflowPage->m_nOverflowPages;
							CopyState.m_WriteBuffersLength[iBuffer] += CopyState.m_pEnvironment->m_PageSize;
							if (pOverflowPage->m_nOverflowPages > 1)
							{
								CopyState.m_OverflowPagesLength[iBuffer] = CopyState.m_pEnvironment->m_PageSize * (pOverflowPage->m_nOverflowPages - 1);
								CopyState.m_OverflowPages[iBuffer] = (uint8 *)pOverflowPage + CopyState.m_pEnvironment->m_PageSize;
								Result = fg_DispatchCopyBuffer(CopyState);
								if (Result)
									goto done;
								iBuffer = CopyState.m_iBuffer;
							}
						}
						else if (pNode->m_Flags & DFSubData)
						{
							CMdbDb Database;

							/* Need writable leaf */
							if (pPage != pLeafPage)
							{
								Cursor.m_Pages[Cursor.m_iTop] = pLeafPage;
								fg_PageCopy(pLeafPage, pPage, CopyState.m_pEnvironment->m_PageSize);
								pPage = pLeafPage;
								pNode = DNodePtr(pPage, iIndex);
							}

							memcpy(&Database, DNodeData(pNode), sizeof(Database));
							CopyState.m_iBuffer = iBuffer;
							Result = fg_TraverseTreeCopyRecursive(CopyState, &Database.m_RootPage, pNode->m_Flags & DFDupData);
							if (Result)
								goto done;
							iBuffer = CopyState.m_iBuffer;
							memcpy(DNodeData(pNode), &Database, sizeof(Database));
						}
					}
				}
			}
			else
			{
				Cursor.m_NodeIndices[Cursor.m_iTop]++;
				if (Cursor.m_NodeIndices[Cursor.m_iTop] < nKeys)
				{
					CPgno PageNumber;
				again:
					pNode = DNodePtr(pPage, Cursor.m_NodeIndices[Cursor.m_iTop]);
					PageNumber = DNodePgno(pNode);
					Result = fg_PageGet(&Cursor, PageNumber, &pPage, nullptr);
					if (Result)
						goto done;
					Cursor.m_iTop++;
					Cursor.m_nPages++;
					Cursor.m_NodeIndices[Cursor.m_iTop] = 0;
					if (DIsBranch(pPage))
					{
						/* Whenever we advance to a sibling branch page,
						 * we must proceed all the way down to its first leaf.
						 */
						fg_PageCopy(Cursor.m_Pages[Cursor.m_iTop], pPage, CopyState.m_pEnvironment->m_PageSize);
						goto again;
					}
					else
						Cursor.m_Pages[Cursor.m_iTop] = pPage;
					continue;
				}
			}
			if (CopyState.m_WriteBuffersLength[iBuffer] >= gc_CopyBufferSize)
			{
				Result = fg_DispatchCopyBuffer(CopyState);
				if (Result)
					goto done;
				iBuffer = CopyState.m_iBuffer;
			}
			pOutputPage = (CMdbPage *)(CopyState.m_WriteBuffers[iBuffer] + CopyState.m_WriteBuffersLength[iBuffer]);
			fg_PageCopy(pOutputPage, pPage, CopyState.m_pEnvironment->m_PageSize);
			pOutputPage->m_PageNumber = CopyState.m_NextPageNo++;
			CopyState.m_WriteBuffersLength[iBuffer] += CopyState.m_pEnvironment->m_PageSize;
			if (Cursor.m_iTop)
			{
				/* Update parent if there is one */
				pNode = DNodePtr(Cursor.m_Pages[Cursor.m_iTop - 1], Cursor.m_NodeIndices[Cursor.m_iTop - 1]);
				DSetPgno(pNode, pOutputPage->m_PageNumber);
				fg_CursorPop(&Cursor);
			}
			else
			{
				/* Otherwise we're done */
				*_pPageNumber = pOutputPage->m_PageNumber;
				break;
			}
		}
	done:
		NMemory::fg_Free(pBuffer, BuffAllocSize);
		return Result;
	}
}

namespace NMib::NDatabase
{
	/** Copy environment with compaction. */
	// NOLINTNEXTLINE(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
	void DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_CopyCompact(HANDLE _FileHandle)
	{
		using namespace NPrivate;
		CCompactingCopyState CopyState{this, _FileHandle};
		int Result;
		CPgno RootPage, NewRootPage;

		CopyState.f_Init(m_OSPageSize);

		auto Transaction = f_TxnBegin(nullptr, EDatabaseEnvironmentFlag_ReadOnly);
		auto pInternalTransaction = Transaction.fp_GetInternal();

		CMdbPage *pPage = (CMdbPage *)CopyState.m_WriteBuffers[0];
		memset(pPage, 0, DNumMetas * m_PageSize);
		pPage->m_PageNumber = 0;
		pPage->m_Flags = DPMeta;
		CMdbMeta *pMeta = (CMdbMeta *)DMetaData(pPage);
		f_InitMeta0(pMeta);
		pMeta->m_pAddress = m_Metas[0]->m_pAddress;

		pPage = (CMdbPage *)(CopyState.m_WriteBuffers[0] + m_PageSize);
		pPage->m_PageNumber = 1;
		pPage->m_Flags = DPMeta;
		*(CMdbMeta *)DMetaData(pPage) = *pMeta;
		pMeta = (CMdbMeta *)DMetaData(pPage);

		/* Set metapage 1 with current main DB */
		RootPage = NewRootPage = pInternalTransaction->m_pDatabases[DMainDbi].m_RootPage;
		if (RootPage != DPInvalid)
		{
			/* Count the number of free pages, subtract from lastpg to find
			 * number of active pages
			 */
			CMdbId nFreePages = 0;
			CMdbCursor Cursor;
			CDatabaseValue Key;
			CDatabaseValue Data;
			fg_CursorInit(&Cursor, pInternalTransaction, DFreeDbi, nullptr);
			while ((Result = fg_CursorGet(&Cursor, &Key, &Data, EDatabaseCursorGet_Next)) == 0)
				nFreePages += *(CMdbId *)Data.m_pData;
			if (Result != DMdbNotFound)
				DMibError(NStr::fg_Format("Error compacting database (read cursor): {}", fg_StrError(Result)));
			nFreePages
				+= pInternalTransaction->m_pDatabases[DFreeDbi].m_nBranchPages
				+ pInternalTransaction->m_pDatabases[DFreeDbi].m_nLeafPages
				+ pInternalTransaction->m_pDatabases[DFreeDbi].m_nOverflowPages
			;

			/* Set metapage 1 */
			NewRootPage = pInternalTransaction->m_NextPageNumber - 1 - nFreePages;
			pMeta->m_LastPageNumber = NewRootPage;
			pMeta->m_Databases[DMainDbi] = pInternalTransaction->m_pDatabases[DMainDbi];
			pMeta->m_Databases[DMainDbi].m_RootPage = NewRootPage;
		}
		else
		{
			/* When the DB is empty, handle it specially to
			 * fix any breakage like page leaks from ITS#8174.
			 */
			pMeta->m_Databases[DMainDbi].m_Flags = pInternalTransaction->m_pDatabases[DMainDbi].m_Flags;
		}

		if (RootPage != DPInvalid || pMeta->m_Databases[DMainDbi].m_Flags)
			pMeta->m_TransactionID.f_Store(1, NAtomic::gc_MemoryOrder_Relaxed); // use metapage 1

		CopyState.m_WriteBuffersLength[0] = m_PageSize * DNumMetas;
		CopyState.m_pInternalTransaction = pInternalTransaction;
		fg_DispatchCopyBuffer(CopyState);
		fg_Dispatch
			(
				CopyState.m_BlockingActorCheckout.f_Actor()
				, []
				{
				}
			)
			.f_CallSync()
		;
		Result = fg_TraverseTreeCopyRecursive(CopyState, &RootPage, 0);

		if (Result == DMdbSuccess && RootPage != NewRootPage)
			Result = DMdbIncompatible; // page leak or corrupt DB

		if (Result)
			DMibError(NStr::fg_Format("Error compacting database: {}", fg_StrError(Result)));

		if (Result == DMdbSuccess && CopyState.m_WriteBuffersLength[CopyState.m_iBuffer])
			Result = fg_DispatchCopyBuffer(CopyState);

		// Block
		fg_Dispatch
			(
				CopyState.m_BlockingActorCheckout.f_Actor()
				, []
				{
				}
			)
			.f_CallSync()
		;
		if (Result)
			DMibError(NStr::fg_Format("Error compacting database: {}", fg_StrError(Result)));

		Result = fg_DispatchCopyBuffer(CopyState); // Get status
		if (Result)
			DMibError(NStr::fg_Format("Error compacting database: {}", fg_StrError(Result)));
		fg_Dispatch
			(
				CopyState.m_BlockingActorCheckout.f_Actor()
				, []
				{
				}
			)
			.f_CallSync()
		;
	}
}
