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
	int g_bDebug = 0;
	CTransactionID g_DebugStartTransactionID = -1;

	/** Return the page number of \b _pPage which may be sub-page, for debug output */
	CPgno fg_DbgPgno(CMdbPage const *_pPage)
	{
		CPgno PageNumber;
		DCopyPgno(PageNumber, _pPage->m_PageNumber);
		return PageNumber;
	}

	/** Display a key in hexadecimal and return the address of the result.
	 * @param[in] _pKey the key to display
	 * @param[in] _pBuffer the buffer to write into. Should always be #DKBuf.
	 * @return The key in hexadecimal form.
	 */
	char const *fg_DKey(CDatabaseValue const *_pKey, char *_pBuffer)
	{
		char *pOutput = _pBuffer;
		unsigned char *pByte = (unsigned char *)_pKey->m_pData;
		uint32 iByte;
		size_t BufferSize = DKBufMaxKeySize * 2;

		if (!_pKey)
			return "";

		if (_pKey->m_Size > DKBufMaxKeySize)
			return "MDB_MAXKEYSIZE";
		/* may want to make this a dynamic check: if the key is mostly
		 * printable characters, print it as-is instead of converting to hex.
		 */
#	if 1
		_pBuffer[0] = '\0';
		for (iByte = 0; iByte < _pKey->m_Size; iByte++)
		{
			auto *pPrevPtr = pOutput;
			pOutput += snprintf(pOutput, BufferSize, "%02x", *pByte++);
			BufferSize -= pOutput - pPrevPtr;
		}
#	else
		sprintf(_pBuffer, "%.*s", _pKey->m_Size, _pKey->m_pData);
#	endif
		return _pBuffer;
	}

	char const *fg_LeafNodeType(CMdbNode const *_pNode)
	{
		static char const * const s_NodeTypes[2][2] = {{"", ": DB"}, {": sub-page", ": sub-DB"}};
		return DFIsSet(_pNode->m_Flags, DFBigData) ? ": overflow page" : s_NodeTypes[DFIsSet(_pNode->m_Flags, DFDupData)][DFIsSet(_pNode->m_Flags, DFSubData)];
	}

	/** Display all the keys in the page. */
	void fg_PageList(CMdbPage const *_pPage)
	{
		CPgno PageNumber = fg_DbgPgno(_pPage);
		char const *pType, *pState = (_pPage->m_Flags & DPDirty) ? ", dirty" : "";
		CMdbNode *pNode;
		uint32 iNode, nKeys, NodeSize, TotalSize = 0;
		CDatabaseValue Key;
		DKBuf;

		switch (_pPage->m_Flags & (DPBranch | DPLeaf | DPLeaf2 | DPMeta | DPOverflow | DPSubp))
		{
		case DPBranch: pType = "Branch page"; break;
		case DPLeaf: pType = "Leaf page"; break;
		case DPLeaf | DPSubp: pType = "Sub-page"; break;
		case DPLeaf | DPLeaf2: pType = "LEAF2 page"; break;
		case DPLeaf | DPLeaf2 | DPSubp: pType = "LEAF2 sub-page"; break;
		case DPOverflow:
			DMibConErrOut("Overflow page {} pages {}{}\n", PageNumber, _pPage->m_nOverflowPages, pState);
			return;
		case DPMeta:
			DMibConErrOut("Meta-page {} txnid {}\n", PageNumber, fg_RemoveQualifiers(((CMdbMeta *)DMetaData(_pPage))->m_TransactionID.f_Load(NAtomic::gc_MemoryOrder_Relaxed)));
			return;
		default:
			DMibConErrOut("Bad page {} flags 0x{nfh,sf0,sj4}\n", PageNumber, _pPage->m_Flags);
			return;
		}

		nKeys = DNumKeys(_pPage);
		DMibConErrOut("{} {} numkeys {}{}\n", pType, PageNumber, nKeys, pState);

		for (iNode = 0; iNode < nKeys; iNode++)
		{
			if (DIsLeaf2(_pPage))
			{ // LEAF2 pages have no m_NodeOffsets[] or node headers
				Key.m_Size = NodeSize = _pPage->m_Pad;
				Key.m_pData = DLeaf2Key(_pPage, iNode, NodeSize);
				TotalSize += NodeSize;
				DMibConErrOut("key {}: nsize {}, {}\n", iNode, NodeSize, DKey(&Key));
				continue;
			}
			pNode = DNodePtr(_pPage, iNode);
			Key.m_Size = pNode->m_KeySize;
			Key.m_pData = pNode->m_Data;
			NodeSize = uint32(DNodeSize + Key.m_Size);
			if (DIsBranch(_pPage))
			{
				DMibConErrOut("key {}: page {}, {}\n", iNode, DNodePgno(pNode), DKey(&Key));
				TotalSize += NodeSize;
			}
			else
			{
				if (DFIsSet(pNode->m_Flags, DFBigData))
					NodeSize += sizeof(CPgno);
				else
					NodeSize += DNodeDataSize(pNode);
				TotalSize += NodeSize;
				NodeSize += sizeof(CIndx);
				DMibConErrOut("key {}: nsize {}, {}{}\n", iNode, NodeSize, DKey(&Key), fg_LeafNodeType(pNode));
			}
			TotalSize = DEven(TotalSize);
		}
		DMibConErrOut("Total: header {} + contents {} + unused {}\n", DIsLeaf2(_pPage) ? DPageHeaderSize : DPageBase + _pPage->m_FreeSpace.m_Lower, TotalSize, DSizeLeft(_pPage));
	}

	void fg_CursorChk(CMdbCursor const *_pCursor)
	{
		using namespace NPrivate;
		uint32 iLevel;
		CMdbNode *pNode;
		CMdbPage *pPage;

		if (!_pCursor->m_nPages || !(_pCursor->m_Flags & DCInitialized))
			return;
		for (iLevel = 0; iLevel < _pCursor->m_iTop; iLevel++)
		{
			pPage = _pCursor->m_Pages[iLevel];
			pNode = DNodePtr(pPage, _pCursor->m_NodeIndices[iLevel]);
			if (DNodePgno(pNode) != _pCursor->m_Pages[iLevel + 1]->m_PageNumber)
				printf("oops!\n");
		}
		if (_pCursor->m_NodeIndices[iLevel] >= DNumKeys(_pCursor->m_Pages[iLevel]))
			printf("ack!\n");
		if (DXCursorInited(_pCursor))
		{
			pNode = DNodePtr(_pCursor->m_Pages[_pCursor->m_iTop], _pCursor->m_NodeIndices[_pCursor->m_iTop]);
			if (((pNode->m_Flags & (DFDupData | DFSubData)) == DFDupData) && _pCursor->m_pXCursor->m_Cursor.m_Pages[0] != (CMdbPage *)DNodeData(pNode))
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
		CMdbCursor Cursor;
		CDatabaseValue Key, Data;
		CMdbId nFreePages, nUsedPages;
		CDatabaseSubID iDatabase;
		int Result;

		nFreePages = 0;
		fg_CursorInit(&Cursor, _pTransaction, DFreeDbi, nullptr);
		while ((Result = fg_CursorGet(&Cursor, &Key, &Data, EDatabaseCursorGet_Next)) == 0)
			nFreePages += *(CMdbId *)Data.m_pData;
		DMibCheck(Result == DMdbNotFound);

		nUsedPages = 0;
		for (iDatabase = 0; iDatabase < _pTransaction->m_nDatabases; iDatabase++)
		{
			CMdbXCursor XCursor;
			if (!(_pTransaction->m_pDatabaseFlags[iDatabase] & DDbValid))
				continue;
			fg_CursorInit(&Cursor, _pTransaction, iDatabase, &XCursor);
			if (_pTransaction->m_pDatabases[iDatabase].m_RootPage == DPInvalid)
				continue;
			nUsedPages
				+= _pTransaction->m_pDatabases[iDatabase].m_nBranchPages + _pTransaction->m_pDatabases[iDatabase].m_nLeafPages + _pTransaction->m_pDatabases[iDatabase].m_nOverflowPages
			;
			if (_pTransaction->m_pDatabases[iDatabase].m_Flags & EDatabaseSubFlag_SortedDuplicates)
			{
				Result = fg_PageSearch(&Cursor, nullptr, DMdbPsFirst);
				for (; Result == DMdbSuccess; Result = fg_CursorSibling(&Cursor, 1))
				{
					unsigned iNode;
					CMdbPage *pPage;
					pPage = Cursor.m_Pages[Cursor.m_iTop];
					for (iNode = 0; iNode < DNumKeys(pPage); iNode++)
					{
						CMdbNode *pLeaf = DNodePtr(pPage, iNode);
						if (pLeaf->m_Flags & DFSubData)
						{
							CMdbDb Database;
							memcpy(&Database, DNodeData(pLeaf), sizeof(Database));
							nUsedPages += Database.m_nBranchPages + Database.m_nLeafPages + Database.m_nOverflowPages;
						}
					}
				}
				DMibCheck(Result == DMdbNotFound);
			}
		}
		if (nFreePages + nUsedPages + DNumMetas != _pTransaction->m_NextPageNumber)
		{
			DMibConErrOut
				(
					"audit: {} freecount: {} count: {} total: {} next_pgno: {}\n"
					, _pTransaction->m_TransactionID
					, nFreePages
					, nUsedPages + DNumMetas
					, nFreePages + nUsedPages + DNumMetas
					, _pTransaction->m_NextPageNumber
				)
			;
		}
	}
#endif
}

