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
		uint32 iNode = 0, nKeys;
		int iLow, iHigh;
		int Result = 0;
		CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		CMdbNode *pNode = nullptr;
		CDatabaseValue NodeKey;
		FDatabaseCompare *pCompare;
		DKBuf;

		nKeys = DNumKeys(pPage);

		DMibDatabaseTrace("searching {} keys in {} {}page {}", nKeys, DIsLeaf(pPage) ? "leaf" : "branch", DIsSubp(pPage) ? "sub-" : "", fg_DbgPgno(pPage));

		iLow = DIsLeaf(pPage) ? 0 : 1;
		iHigh = nKeys - 1;
		pCompare = &(_pCursor->m_pDatabaseInfo->m_fKeyCompare);

		/* Branch pages have no data, so if using integer keys,
		 * alignment is guaranteed. Use faster fg_CmpInt.
		 */
		if (DIsBranch(pPage) && *pCompare == g_fCompareCInt)
		{
			if (DNodePtr(pPage, 1)->m_KeySize == sizeof(CDatabaseOffset))
				pCompare = &g_fCompareLong;
			else
				pCompare = &g_fCompareInt;
		}

		if (DIsLeaf2(pPage))
		{
			NodeKey.m_Size = _pCursor->m_pDatabase->m_Pad;
			pNode = DNodePtr(pPage, 0); // fake
			while (iLow <= iHigh)
			{
				iNode = (iLow + iHigh) >> 1;
				NodeKey.m_pData = DLeaf2Key(pPage, iNode, NodeKey.m_Size);
				Result = (*pCompare)(*_pKey, NodeKey);
				DMibDatabaseTrace("found leaf index {} [{}], rc = {}", iNode, DKey(&NodeKey), Result);
				if (Result == 0)
					break;
				if (Result > 0)
					iLow = iNode + 1;
				else
					iHigh = iNode - 1;
			}
		}
		else
		{
			while (iLow <= iHigh)
			{
				iNode = (iLow + iHigh) >> 1;

				pNode = DNodePtr(pPage, iNode);
				NodeKey.m_Size = DNodeKeySize(pNode);
				NodeKey.m_pData = DNodeKey(pNode);

				Result = (*pCompare)(*_pKey, NodeKey);
#if DMibDatabaseDebug
				if (DIsLeaf(pPage))
					DMibDatabaseTrace("found leaf index {} [{}], rc = {}", iNode, DKey(&NodeKey), Result);
				else
					DMibDatabaseTrace("found branch index {} [{} -> {}], rc = {}", iNode, DKey(&NodeKey), DNodePgno(pNode), Result);
#endif
				if (Result == 0)
					break;
				if (Result > 0)
					iLow = iNode + 1;
				else
					iHigh = iNode - 1;
			}
		}

		if (Result > 0)
		{ // Found entry is less than the key.
			iNode++; // Skip to get the smallest entry larger than key.
			if (!DIsLeaf2(pPage))
				pNode = DNodePtr(pPage, iNode);
		}
		if (o_pExact)
			*o_pExact = (Result == 0 && nKeys > 0);
		/* store the key index */
		_pCursor->m_NodeIndices[_pCursor->m_iTop] = iNode;
		if (iNode >= nKeys)
			/* There is no entry larger or equal to the key. */
			return nullptr;

		/* nodeptr is fake for LEAF2 */
		return pNode;
	}

	/** Return the data associated with a given node.
	 * @param[in] _pCursor The cursor for this operation.
	 * @param[in] _pLeaf The node being read.
	 * @param[out] _pData Updated to point to the node's data.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_NodeRead(CMdbCursor *_pCursor, CMdbNode *_pLeaf, CDatabaseValue *_pData)
	{
		CMdbPage *pOverflowPage; // overflow page
		CPgno PageNumber;
		int Result;

		if (DMcOvpg(_pCursor))
		{
			DMdbPageUnref(_pCursor->m_pTransaction, DMcOvpg(_pCursor));
			DMcSetOvpg(_pCursor, NULL);
		}
		if (!DFIsSet(_pLeaf->m_Flags, DFBigData))
		{
			_pData->m_Size = DNodeDataSize(_pLeaf);
			_pData->m_pData = DNodeData(_pLeaf);
			return DMdbSuccess;
		}

		/* Read overflow data.
		*/
		_pData->m_Size = DNodeDataSize(_pLeaf);
		memcpy(&PageNumber, DNodeData(_pLeaf), sizeof(PageNumber));
		if ((Result = fg_PageGet(_pCursor, PageNumber, &pOverflowPage, nullptr)) != 0)
		{
			DMibDatabaseTrace("read overflow page {} failed", PageNumber);
			return Result;
		}
		_pData->m_pData = DMetaData(pOverflowPage);
		DMcSetOvpg(_pCursor, pOverflowPage);

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
		uint32 iNode;
		umint NodeSize = DNodeSize;
		smint Room;
		CIndx Offset;
		CMdbNode *pNode;
		CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		CMdbPage *pOverflowPage = nullptr; // overflow page
		void *pNodeData;
		DKBuf;

		DMibCheck(pPage->m_FreeSpace.m_Upper >= pPage->m_FreeSpace.m_Lower);

		DMibDatabaseTrace
			(
				"add to {} {}page {} index {}, data size {} key size {} [{}]"
				, DIsLeaf(pPage) ? "leaf" : "branch"
				, DIsSubp(pPage) ? "sub-" : ""
				, fg_DbgPgno(pPage)
				, _iNode
				, _pData ? _pData->m_Size : 0
				, _pKey ? _pKey->m_Size : 0
				, _pKey ? DKey(_pKey) : "null"
			)
		;

		if (DIsLeaf2(pPage))
		{
			/* Move higher keys up one slot. */
			int KeySize = _pCursor->m_pDatabase->m_Pad, nAfter;
			uint8 *pLeaf2Key = DLeaf2Key(pPage, _iNode, KeySize);
			nAfter = DNumKeys(pPage) - _iNode;
			if (nAfter > 0)
				memmove(pLeaf2Key + KeySize, pLeaf2Key, nAfter*KeySize);
			/* insert new key */
			memcpy(pLeaf2Key, _pKey->m_pData, KeySize);

			/* Just using these for counting */
			pPage->m_FreeSpace.m_Lower += sizeof(CIndx);
			pPage->m_FreeSpace.m_Upper -= CIndx(KeySize - sizeof(CIndx));
			return DMdbSuccess;
		}

		Room = (smint)DSizeLeft(pPage) - (smint)sizeof(CIndx);
		if (_pKey != nullptr)
			NodeSize += _pKey->m_Size;
		if (DIsLeaf(pPage))
		{
			DMibCheck(_pKey && _pData);
			if (DFIsSet(_Flags, DFBigData))
			{
				/* Data already on overflow page. */
				NodeSize += sizeof(CPgno);
			}
			else if (NodeSize + _pData->m_Size > _pCursor->m_pTransaction->m_pEnvironment->m_MaxNodeSize)
			{
				int nOverflowPages = DOverflowPages(_pData->m_Size, _pCursor->m_pTransaction->m_pEnvironment->m_PageSize);
				int Result;
				/* Put data on overflow page. */
				DMibDatabaseTrace("data size is {}, node would be {}, put data on overflow page", _pData->m_Size, NodeSize + _pData->m_Size);
				NodeSize = DEven(NodeSize + sizeof(CPgno));
				if ((smint)NodeSize > Room)
					goto full;
				if ((Result = fg_PageNew(_pCursor, DPOverflow, nOverflowPages, &pOverflowPage)))
					return Result;
				DMibDatabaseTrace("allocated overflow page {}", pOverflowPage->m_PageNumber);
				_Flags |= DFBigData;
				goto update;
			}
			else
				NodeSize += _pData->m_Size;
		}
		NodeSize = DEven(NodeSize);
		if ((smint)NodeSize > Room)
			goto full;

	update:
		/* Move higher pointers up one slot. */
		for (iNode = DNumKeys(pPage); iNode > _iNode; iNode--)
			pPage->m_NodeOffsets[iNode] = pPage->m_NodeOffsets[iNode - 1];

		/* Adjust free space offsets. */
		Offset = pPage->m_FreeSpace.m_Upper - NodeSize;
		DMibCheck(Offset >= pPage->m_FreeSpace.m_Lower + sizeof(CIndx));
		pPage->m_NodeOffsets[_iNode] = Offset;
		pPage->m_FreeSpace.m_Upper = Offset;
		pPage->m_FreeSpace.m_Lower += sizeof(CIndx);

		/* Write the node data. */
		pNode = DNodePtr(pPage, _iNode);
		pNode->m_KeySize = (_pKey == nullptr) ? 0 : _pKey->m_Size;
		pNode->m_Flags = _Flags;
		if (DIsLeaf(pPage))
			DSetDataSize(pNode, _pData->m_Size);
		else
			DSetPgno(pNode, _PageNumber);

		if (_pKey)
			memcpy(DNodeKey(pNode), _pKey->m_pData, _pKey->m_Size);

		if (DIsLeaf(pPage))
		{
			pNodeData = (CMdbPage *)DNodeData(pNode);
			if (pOverflowPage == nullptr)
			{
				if (DFIsSet(_Flags, DFBigData))
					memcpy(pNodeData, _pData->m_pData, sizeof(CPgno));
				else if (DFIsSet(_Flags, EDatabaseWriteFlag_ReserveSpace))
					_pData->m_pData = (uint8 *)pNodeData;
				else if (_pData->m_Size)
					memcpy(pNodeData, _pData->m_pData, _pData->m_Size);
			}
			else
			{
				memcpy(pNodeData, &pOverflowPage->m_PageNumber, sizeof(CPgno));
				pNodeData = DMetaData(pOverflowPage);
				if (DFIsSet(_Flags, EDatabaseWriteFlag_ReserveSpace))
					_pData->m_pData = (uint8 *)pNodeData;
				else if (_pData->m_Size)
					memcpy(pNodeData, _pData->m_pData, _pData->m_Size);
			}
		}

		return DMdbSuccess;

	full:
		DMibDatabaseTrace("not enough room in page {}, got {} ptrs", fg_DbgPgno(pPage), DNumKeys(pPage));
		DMibDatabaseTrace("upper-lower = {} - {} = {}", pPage->m_FreeSpace.m_Upper, pPage->m_FreeSpace.m_Lower, Room);
		DMibDatabaseTrace("node size = {}", NodeSize);
		_pCursor->m_pTransaction->m_Flags |= DMdbTxnError;
		return DMdbPageFull;
	}

	/** Delete the specified node from a page.
	 * @param[in] _pCursor Cursor pointing to the node to delete.
	 * @param[in] _KeySize The size of a node. Only used if the page is
	 * part of a #EDatabaseSubFlag_SortedDuplicatesFixedSize database.
	 */
	void NPrivate::fg_NodeDel(CMdbCursor *_pCursor, int _KeySize)
	{
		CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		CIndx iNode = _pCursor->m_NodeIndices[_pCursor->m_iTop];
		uint32 Size;
		CIndx iSource, iDestination, nKeys, NodeOffset;
		CMdbNode *pNode;
		uint8 *pBase;

		DMibDatabaseTrace("delete node {} on {} page {}", iNode, DIsLeaf(pPage) ? "leaf" : "branch", fg_DbgPgno(pPage));
		nKeys = DNumKeys(pPage);
		DMibCheck(iNode < nKeys);

		if (DIsLeaf2(pPage))
		{
			int nAfter = nKeys - 1 - iNode;
			pBase = DLeaf2Key(pPage, iNode, _KeySize);
			if (nAfter)
				memmove(pBase, pBase + _KeySize, nAfter * _KeySize);
			pPage->m_FreeSpace.m_Lower -= sizeof(CIndx);
			pPage->m_FreeSpace.m_Upper += CIndx(_KeySize - sizeof(CIndx));
			return;
		}

		pNode = DNodePtr(pPage, iNode);
		Size = DNodeSize + pNode->m_KeySize;
		if (DIsLeaf(pPage))
		{
			if (DFIsSet(pNode->m_Flags, DFBigData))
				Size += sizeof(CPgno);
			else
				Size += DNodeDataSize(pNode);
		}
		Size = DEven(Size);

		NodeOffset = pPage->m_NodeOffsets[iNode];
		for (iSource = iDestination = 0; iSource < nKeys; iSource++)
		{
			if (iSource != iNode)
			{
				pPage->m_NodeOffsets[iDestination] = pPage->m_NodeOffsets[iSource];
				if (pPage->m_NodeOffsets[iSource] < NodeOffset)
					pPage->m_NodeOffsets[iDestination] += Size;
				iDestination++;
			}
		}

		pBase = (uint8 *)pPage + pPage->m_FreeSpace.m_Upper + DPageBase;
		memmove(pBase + Size, pBase, NodeOffset - pPage->m_FreeSpace.m_Upper);

		pPage->m_FreeSpace.m_Lower -= sizeof(CIndx);
		pPage->m_FreeSpace.m_Upper += Size;
	}

	/** Compact the main page after deleting a node on a subpage.
	 * @param[in] _pPage The main page to operate on.
	 * @param[in] _iNode The index of the subpage on the main page.
	 */
	void NPrivate::fg_NodeShrink(CMdbPage *_pPage, CIndx _iNode)
	{
		CMdbNode *pNode;
		CMdbPage *pSubPage, *pNewSubPage;
		char *pBase;
		CIndx Delta, NodeSize, Length, NodeOffset;
		int iNode;

		pNode = DNodePtr(_pPage, _iNode);
		pSubPage = (CMdbPage *)DNodeData(pNode);
		Delta = DSizeLeft(pSubPage);
		NodeSize = DNodeDataSize(pNode) - Delta;

		/* Prepare to shift upward, set len = length(subpage part to shift) */
		if (DIsLeaf2(pSubPage))
		{
			Length = NodeSize;
			if (NodeSize & 1)
				return; // do not make the node uneven-sized
		}
		else
		{
			pNewSubPage = (CMdbPage *)((char *)pSubPage + Delta); // destination subpage
			for (iNode = DNumKeys(pSubPage); --iNode >= 0;)
				pNewSubPage->m_NodeOffsets[iNode] = pSubPage->m_NodeOffsets[iNode] - Delta;
			Length = DPageHeaderSize;
		}
		pSubPage->m_FreeSpace.m_Upper = pSubPage->m_FreeSpace.m_Lower;
		DCopyPgno(pSubPage->m_PageNumber, _pPage->m_PageNumber);
		DSetDataSize(pNode, uint32(NodeSize));

		/* Shift <lower nodes...initial part of subpage> upward */
		pBase = (char *)_pPage + _pPage->m_FreeSpace.m_Upper + DPageBase;
		memmove(pBase + Delta, pBase, (char *)pSubPage + Length - pBase);

		NodeOffset = _pPage->m_NodeOffsets[_iNode];
		for (iNode = DNumKeys(_pPage); --iNode >= 0;)
		{
			if (_pPage->m_NodeOffsets[iNode] <= NodeOffset)
				_pPage->m_NodeOffsets[iNode] += Delta;
		}
		_pPage->m_FreeSpace.m_Upper += Delta;
	}

	/** Move a node from _pSourceCursor to _pDestinationCursor.
	 */
	int NPrivate::fg_NodeMove(CMdbCursor *_pSourceCursor, CMdbCursor *_pDestinationCursor, int _bFromLeft)
	{
		CMdbNode *pSourceNode;
		CDatabaseValue Key, Data;
		CPgno SourcePageNumber;
		CMdbCursor CopyCursor;
		int Result;
		unsigned short Flags;

		DKBuf;

		/* Mark src and dst as dirty. */
		if ((Result = fg_PageTouch(_pSourceCursor)) || (Result = fg_PageTouch(_pDestinationCursor)))
			return Result;

		if (DIsLeaf2(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop]))
		{
			Key.m_Size = _pSourceCursor->m_pDatabase->m_Pad;
			Key.m_pData = DLeaf2Key(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop], _pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop], Key.m_Size);
			Data.m_Size = 0;
			Data.m_pData = nullptr;
			SourcePageNumber = 0;
			Flags = 0;
		}
		else
		{
			pSourceNode = DNodePtr(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop], _pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop]);
			DMibCheck(!((umint)pSourceNode & 1));
			SourcePageNumber = DNodePgno(pSourceNode);
			Flags = pSourceNode->m_Flags;
			if (_pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop] == 0 && DIsBranch(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop]))
			{
				uint32 nPages = _pSourceCursor->m_nPages;
				CMdbNode *pLowestNode;
				/* must find the lowest key below src */
				Result = fg_PageSearchLowest(_pSourceCursor);
				if (Result)
					return Result;
				if (DIsLeaf2(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop]))
				{
					Key.m_Size = _pSourceCursor->m_pDatabase->m_Pad;
					Key.m_pData = DLeaf2Key(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop], 0, Key.m_Size);
				}
				else
				{
					pLowestNode = DNodePtr(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop], 0);
					Key.m_Size = DNodeKeySize(pLowestNode);
					Key.m_pData = DNodeKey(pLowestNode);
				}
				_pSourceCursor->m_nPages = nPages--;
				_pSourceCursor->m_iTop = nPages;
			}
			else
			{
				Key.m_Size = DNodeKeySize(pSourceNode);
				Key.m_pData = DNodeKey(pSourceNode);
			}
			Data.m_Size = DNodeDataSize(pSourceNode);
			Data.m_pData = DNodeData(pSourceNode);
		}
		CopyCursor.m_pXCursor = nullptr;
		if (DIsBranch(_pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop]) && _pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop] == 0)
		{
			uint32 nPages = _pDestinationCursor->m_nPages;
			CMdbNode *pLowestNode;
			CDatabaseValue BranchKey;
			/* must find the lowest key below dst */
			fg_CursorCopy(_pDestinationCursor, &CopyCursor);
			Result = fg_PageSearchLowest(&CopyCursor);
			if (Result)
				return Result;
			if (DIsLeaf2(CopyCursor.m_Pages[CopyCursor.m_iTop]))
			{
				BranchKey.m_Size = CopyCursor.m_pDatabase->m_Pad;
				BranchKey.m_pData = DLeaf2Key(CopyCursor.m_Pages[CopyCursor.m_iTop], 0, BranchKey.m_Size);
			}
			else
			{
				pLowestNode = DNodePtr(CopyCursor.m_Pages[CopyCursor.m_iTop], 0);
				BranchKey.m_Size = DNodeKeySize(pLowestNode);
				BranchKey.m_pData = DNodeKey(pLowestNode);
			}
			CopyCursor.m_nPages = nPages--;
			CopyCursor.m_iTop = nPages;
			CopyCursor.m_NodeIndices[nPages] = 0;
			Result = fg_UpdateKey(&CopyCursor, &BranchKey);
			if (Result)
				return Result;
		}

		DMibDatabaseTrace
			(
				"moving {} node {} [{}] on page {} to node {} on page {}"
				, DIsLeaf(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop]) ? "leaf" : "branch"
				, _pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop]
				, DKey(&Key)
				, _pSourceCursor->m_Pages[_pSourceCursor->m_iTop]->m_PageNumber
				, _pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop]
				, _pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop]->m_PageNumber
			)
		;

		/* Add the node to the destination page.
		*/
		Result = fg_NodeAdd(_pDestinationCursor, _pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop], &Key, &Data, SourcePageNumber, Flags);
		if (Result != DMdbSuccess)
			return Result;

		/* Delete the node from the source page.
		*/
		fg_NodeDel(_pSourceCursor, Key.m_Size);

		{
			/* Adjust other cursors pointing to mp */
			CMdbCursor *pOther, *pOtherCursor;
			CDatabaseSubID DatabaseID = _pSourceCursor->m_DatabaseID;
			CMdbPage *pDestinationPage, *pSourcePage;

			pSourcePage = _pSourceCursor->m_Pages[_pSourceCursor->m_iTop];
			/* If we're adding on the left, bump others up */
			if (_bFromLeft)
			{
				pDestinationPage = _pDestinationCursor->m_Pages[_pSourceCursor->m_iTop];
				for (pOther = _pSourceCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
				{
					if (_pSourceCursor->m_Flags & DCSub)
						pOtherCursor = &pOther->m_pXCursor->m_Cursor;
					else
						pOtherCursor = pOther;
					if (!(pOtherCursor->m_Flags & DCInitialized) || pOtherCursor->m_iTop < _pSourceCursor->m_iTop)
						continue;
					if
					(
						pOtherCursor != _pDestinationCursor
						&& pOtherCursor->m_Pages[_pSourceCursor->m_iTop] == pDestinationPage
						&& pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop] >= _pDestinationCursor->m_NodeIndices[_pSourceCursor->m_iTop]
					)
					{
						pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop]++;
					}
					if
					(
						pOtherCursor != _pSourceCursor
						&& pOtherCursor->m_Pages[_pSourceCursor->m_iTop] == pSourcePage
						&& pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop] == _pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop]
					)
					{
						pOtherCursor->m_Pages[_pSourceCursor->m_iTop] = _pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop];
						pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop] = _pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop];
						pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop - 1]++;
					}
					if (DIsLeaf(pSourcePage))
						DXCursorRefresh(pOtherCursor, _pSourceCursor->m_iTop, pOtherCursor->m_Pages[_pSourceCursor->m_iTop]);
				}
			}
			else
			/* Adding on the right, bump others down */
			{
				for (pOther = _pSourceCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
				{
					if (_pSourceCursor->m_Flags & DCSub)
						pOtherCursor = &pOther->m_pXCursor->m_Cursor;
					else
						pOtherCursor = pOther;
					if (pOtherCursor == _pSourceCursor)
						continue;
					if (!(pOtherCursor->m_Flags & DCInitialized) || pOtherCursor->m_iTop < _pSourceCursor->m_iTop)
						continue;
					if (pOtherCursor->m_Pages[_pSourceCursor->m_iTop] == pSourcePage)
					{
						if (!pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop])
						{
							pOtherCursor->m_Pages[_pSourceCursor->m_iTop] = _pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop];
							pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop] = _pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop];
							pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop - 1]--;
						}
						else
							pOtherCursor->m_NodeIndices[_pSourceCursor->m_iTop]--;
						if (DIsLeaf(pSourcePage))
							DXCursorRefresh(pOtherCursor, _pSourceCursor->m_iTop, pOtherCursor->m_Pages[_pSourceCursor->m_iTop]);
					}
				}
			}
		}

		/* Update the parent separators.
		*/
		if (_pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop] == 0)
		{
			if (_pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop - 1] != 0)
			{
				if (DIsLeaf2(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop]))
					Key.m_pData = DLeaf2Key(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop], 0, Key.m_Size);
				else
				{
					pSourceNode = DNodePtr(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop], 0);
					Key.m_Size = DNodeKeySize(pSourceNode);
					Key.m_pData = DNodeKey(pSourceNode);
				}
				DMibDatabaseTrace("update separator for source page {} to [{}]", _pSourceCursor->m_Pages[_pSourceCursor->m_iTop]->m_PageNumber, DKey(&Key));
				fg_CursorCopy(_pSourceCursor, &CopyCursor);
				CopyCursor.m_nPages--;
				CopyCursor.m_iTop--;
				/* We want fg_Rebalance to find CopyCursor when doing fixups */
				DWithCursorTracking(CopyCursor, Result = fg_UpdateKey(&CopyCursor, &Key));
				if (Result)
					return Result;
			}
			if (DIsBranch(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop]))
			{
				CDatabaseValue NullKey;
				CIndx iNode = _pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop];
				NullKey.m_Size = 0;
				_pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop] = 0;
				Result = fg_UpdateKey(_pSourceCursor, &NullKey);
				_pSourceCursor->m_NodeIndices[_pSourceCursor->m_iTop] = iNode;
				DMibCheck(Result == DMdbSuccess);
			}
		}

		if (_pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop] == 0)
		{
			if (_pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop - 1] != 0)
			{
				if (DIsLeaf2(_pSourceCursor->m_Pages[_pSourceCursor->m_iTop]))
					Key.m_pData = DLeaf2Key(_pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop], 0, Key.m_Size);
				else
				{
					pSourceNode = DNodePtr(_pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop], 0);
					Key.m_Size = DNodeKeySize(pSourceNode);
					Key.m_pData = DNodeKey(pSourceNode);
				}
				DMibDatabaseTrace("update separator for destination page {} to [{}]", _pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop]->m_PageNumber, DKey(&Key));
				fg_CursorCopy(_pDestinationCursor, &CopyCursor);
				CopyCursor.m_nPages--;
				CopyCursor.m_iTop--;
				/* We want fg_Rebalance to find CopyCursor when doing fixups */
				DWithCursorTracking(CopyCursor, Result = fg_UpdateKey(&CopyCursor, &Key));
				if (Result)
					return Result;
			}
			if (DIsBranch(_pDestinationCursor->m_Pages[_pDestinationCursor->m_iTop]))
			{
				CDatabaseValue NullKey;
				CIndx iNode = _pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop];
				NullKey.m_Size = 0;
				_pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop] = 0;
				Result = fg_UpdateKey(_pDestinationCursor, &NullKey);
				_pDestinationCursor->m_NodeIndices[_pDestinationCursor->m_iTop] = iNode;
				DMibCheck(Result == DMdbSuccess);
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
		umint Size;

		Size = DLeafSize(_pKey, _pData);
		if (Size > m_MaxNodeSize)
		{
			/* put on overflow page */
			Size -= _pData->m_Size - sizeof(CPgno);
		}

		return DEven(Size + sizeof(CIndx));
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
		umint Size;

		Size = DIndexSize(_pKey);
		if (Size > m_MaxNodeSize)
		{
			/* put on overflow page */
			/* not implemented */
			/* Size -= _pKey->size - sizeof(CPgno); */
		}

		return Size + sizeof(CIndx);
	}

	/** Replace the key for a branch node with a new key.
	 * Set #DMdbTxnError on failure.
	 * @param[in] _pCursor Cursor pointing to the node to operate on.
	 * @param[in] _pKey The new key to use.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_UpdateKey(CMdbCursor *_pCursor, CDatabaseValue *_pKey)
	{
		CMdbPage *pPage;
		CMdbNode *pNode;
		char *pBase;
		umint Length;
		int Delta, KeySize, OldKeySize;
		CIndx NodeOffset, iNode, nKeys, iTarget;
		DKBuf;

		iTarget = _pCursor->m_NodeIndices[_pCursor->m_iTop];
		pPage = _pCursor->m_Pages[_pCursor->m_iTop];
		pNode = DNodePtr(pPage, iTarget);
		NodeOffset = pPage->m_NodeOffsets[iTarget];
#if DMibDatabaseDebug
		{
			CDatabaseValue OldKey;
			char OldKeyBuffer[DKBufMaxKeySize * 2 + 1];
			OldKey.m_pData = DNodeKey(pNode);
			OldKey.m_Size = pNode->m_KeySize;
			DMibDatabaseTrace("update key {} (ofs {}) [{}] to [{}] on page {}", iTarget, NodeOffset, fg_DKey(&OldKey, OldKeyBuffer), DKey(_pKey), pPage->m_PageNumber);
		}
#endif

		/* Sizes must be 2-byte aligned. */
		KeySize = DEven(_pKey->m_Size);
		OldKeySize = DEven(pNode->m_KeySize);
		Delta = KeySize - OldKeySize;

		/* Shift node contents if DEven(key length) changed. */
		if (Delta)
		{
			if (Delta > 0 && DSizeLeft(pPage) < Delta)
			{
				CPgno PageNumber;
				/* not enough space left, do a delete and split */
				DMibDatabaseTrace("Not enough room, delta = {}, splitting...", Delta);
				PageNumber = DNodePgno(pNode);
				fg_NodeDel(_pCursor, 0);
				return fg_PageSplit(_pCursor, _pKey, nullptr, PageNumber, DMdbSplitReplace);
			}

			nKeys = DNumKeys(pPage);
			for (iNode = 0; iNode < nKeys; iNode++)
			{
				if (pPage->m_NodeOffsets[iNode] <= NodeOffset)
					pPage->m_NodeOffsets[iNode] -= Delta;
			}

			pBase = (char *)pPage + pPage->m_FreeSpace.m_Upper + DPageBase;
			Length = NodeOffset - pPage->m_FreeSpace.m_Upper + DNodeSize;
			memmove(pBase - Delta, pBase, Length);
			pPage->m_FreeSpace.m_Upper -= Delta;

			pNode = DNodePtr(pPage, iTarget);
		}

		/* But even if no shift was needed, update KeySize */
		if (pNode->m_KeySize != _pKey->m_Size)
			pNode->m_KeySize = _pKey->m_Size;

		if (_pKey->m_Size)
			memcpy(DNodeKey(pNode), _pKey->m_pData, _pKey->m_Size);

		return DMdbSuccess;
	}

	/** Rebalance the tree after a delete operation.
	 * @param[in] _pCursor Cursor pointing to the page where rebalancing
	 * should begin.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_Rebalance(CMdbCursor *_pCursor)
	{
		CMdbNode *pNode;
		int Result, bFromLeft;
		uint32 iParent, MinKeys, Threshold;
		CMdbCursor NeighborCursor;
		CIndx iOldNode;

		if (DIsBranch(_pCursor->m_Pages[_pCursor->m_iTop]))
		{
			MinKeys = 2;
			Threshold = 1;
		}
		else
		{
			MinKeys = 1;
			Threshold = DFillThreshold;
		}
		DMibDatabaseTrace
			(
				"rebalancing {} page {} (has {} keys, {fe1} full)"
				, DIsLeaf(_pCursor->m_Pages[_pCursor->m_iTop]) ? "leaf" : "branch"
				, fg_DbgPgno(_pCursor->m_Pages[_pCursor->m_iTop])
				, DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop])
				, (float)DPageFill(_pCursor->m_pTransaction->m_pEnvironment, _pCursor->m_Pages[_pCursor->m_iTop]) / 10
			)
		;

		if (DPageFill(_pCursor->m_pTransaction->m_pEnvironment, _pCursor->m_Pages[_pCursor->m_iTop]) >= Threshold && DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop]) >= MinKeys)
		{
			DMibDatabaseTrace("no need to rebalance page {}, above fill threshold", fg_DbgPgno(_pCursor->m_Pages[_pCursor->m_iTop]));
			return DMdbSuccess;
		}

		if (_pCursor->m_nPages < 2)
		{
			CMdbPage *pPage = _pCursor->m_Pages[0];
			if (DIsSubp(pPage))
			{
				DPuts("Can't rebalance a subpage, ignoring");
				return DMdbSuccess;
			}
			if (DNumKeys(pPage) == 0)
			{
				DPuts("tree is completely empty");
				_pCursor->m_pDatabase->m_RootPage = DPInvalid;
				_pCursor->m_pDatabase->m_Depth = 0;
				_pCursor->m_pDatabase->m_nLeafPages = 0;
				fg_MidlAppend(&_pCursor->m_pTransaction->m_pFreePages, pPage->m_PageNumber);
				/* Adjust cursors pointing to pPage */
				_pCursor->m_nPages = 0;
				_pCursor->m_iTop = 0;
				_pCursor->m_Flags &= ~DCInitialized;
				{
					CMdbCursor *pOther, *pOtherCursor;
					CDatabaseSubID DatabaseID = _pCursor->m_DatabaseID;

					for (pOther = _pCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
					{
						if (_pCursor->m_Flags & DCSub)
							pOtherCursor = &pOther->m_pXCursor->m_Cursor;
						else
							pOtherCursor = pOther;
						if (!(pOtherCursor->m_Flags & DCInitialized) || (pOtherCursor->m_nPages < _pCursor->m_nPages))
							continue;
						if (pOtherCursor->m_Pages[0] == pPage)
						{
							pOtherCursor->m_nPages = 0;
							pOtherCursor->m_iTop = 0;
							pOtherCursor->m_Flags &= ~DCInitialized;
						}
					}
				}
			}
			else if (DIsBranch(pPage) && DNumKeys(pPage) == 1)
			{
				int iLevel;
				DPuts("collapsing root page!");
				fg_MidlAppend(&_pCursor->m_pTransaction->m_pFreePages, pPage->m_PageNumber);
				_pCursor->m_pDatabase->m_RootPage = DNodePgno(DNodePtr(pPage, 0));
				Result = fg_PageGet(_pCursor, _pCursor->m_pDatabase->m_RootPage, &_pCursor->m_Pages[0], nullptr);
				if (Result)
					return Result;
				_pCursor->m_pDatabase->m_Depth--;
				_pCursor->m_pDatabase->m_nBranchPages--;
				_pCursor->m_NodeIndices[0] = _pCursor->m_NodeIndices[1];
				for (iLevel = 1; iLevel < _pCursor->m_pDatabase->m_Depth; iLevel++)
				{
					_pCursor->m_Pages[iLevel] = _pCursor->m_Pages[iLevel + 1];
					_pCursor->m_NodeIndices[iLevel] = _pCursor->m_NodeIndices[iLevel + 1];
				}
				{
					/* Adjust other cursors pointing to pPage */
					CMdbCursor *pOther, *pOtherCursor;
					CDatabaseSubID DatabaseID = _pCursor->m_DatabaseID;

					for (pOther = _pCursor->m_pTransaction->m_pCursors[DatabaseID]; pOther; pOther = pOther->m_pNext)
					{
						if (_pCursor->m_Flags & DCSub)
							pOtherCursor = &pOther->m_pXCursor->m_Cursor;
						else
							pOtherCursor = pOther;
						if (pOtherCursor == _pCursor)
							continue;
						if (!(pOtherCursor->m_Flags & DCInitialized))
							continue;
						if (pOtherCursor->m_Pages[0] == pPage)
						{
							for (iLevel = 0; iLevel < _pCursor->m_pDatabase->m_Depth; iLevel++)
							{
								pOtherCursor->m_Pages[iLevel] = pOtherCursor->m_Pages[iLevel + 1];
								pOtherCursor->m_NodeIndices[iLevel] = pOtherCursor->m_NodeIndices[iLevel + 1];
							}
							pOtherCursor->m_nPages--;
							pOtherCursor->m_iTop--;
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
		iParent = _pCursor->m_iTop - 1;
		DMibCheck(DNumKeys(_pCursor->m_Pages[iParent]) > 1);

		/* Leaf page fill factor is below the threshold.
		 * Try to move keys from left or right neighbor, or
		 * merge with a neighbor page.
		 */

		/* Find neighbors.
		*/
		fg_CursorCopy(_pCursor, &NeighborCursor);
		NeighborCursor.m_pXCursor = nullptr;

		iOldNode = _pCursor->m_NodeIndices[_pCursor->m_iTop];
		if (_pCursor->m_NodeIndices[iParent] == 0)
		{
			/* We're the leftmost leaf in our parent.
			*/
			DPuts("reading right neighbor");
			NeighborCursor.m_NodeIndices[iParent]++;
			pNode = DNodePtr(_pCursor->m_Pages[iParent], NeighborCursor.m_NodeIndices[iParent]);
			Result = fg_PageGet(_pCursor, DNodePgno(pNode), &NeighborCursor.m_Pages[NeighborCursor.m_iTop], nullptr);
			if (Result)
				return Result;
			NeighborCursor.m_NodeIndices[NeighborCursor.m_iTop] = 0;
			_pCursor->m_NodeIndices[_pCursor->m_iTop] = DNumKeys(_pCursor->m_Pages[_pCursor->m_iTop]);
			bFromLeft = 0;
		}
		else
		{
			/* There is at least one neighbor to the left.
			*/
			DPuts("reading left neighbor");
			NeighborCursor.m_NodeIndices[iParent]--;
			pNode = DNodePtr(_pCursor->m_Pages[iParent], NeighborCursor.m_NodeIndices[iParent]);
			Result = fg_PageGet(_pCursor, DNodePgno(pNode), &NeighborCursor.m_Pages[NeighborCursor.m_iTop], nullptr);
			if (Result)
				return Result;
			NeighborCursor.m_NodeIndices[NeighborCursor.m_iTop] = DNumKeys(NeighborCursor.m_Pages[NeighborCursor.m_iTop]) - 1;
			_pCursor->m_NodeIndices[_pCursor->m_iTop] = 0;
			bFromLeft = 1;
		}

		DMibDatabaseTrace
			(
				"found neighbor page {} ({} keys, {fe1} full)"
				, NeighborCursor.m_Pages[NeighborCursor.m_iTop]->m_PageNumber
				, DNumKeys(NeighborCursor.m_Pages[NeighborCursor.m_iTop])
				, (float)DPageFill(_pCursor->m_pTransaction->m_pEnvironment, NeighborCursor.m_Pages[NeighborCursor.m_iTop]) / 10
			)
		;

		/* If the neighbor page is above threshold and has enough keys,
		 * move one key from it. Otherwise we should try to merge them.
		 * (A branch page must never have less than 2 keys.)
		 */
		if
		(
			DPageFill(_pCursor->m_pTransaction->m_pEnvironment, NeighborCursor.m_Pages[NeighborCursor.m_iTop]) >= Threshold
			&& DNumKeys(NeighborCursor.m_Pages[NeighborCursor.m_iTop]) > MinKeys
		)
		{
			Result = fg_NodeMove(&NeighborCursor, _pCursor, bFromLeft);
			if (bFromLeft)
			{
				/* if we inserted on left, bump position up */
				iOldNode++;
			}
		}
		else
		{
			if (!bFromLeft)
				Result = fg_PageMerge(&NeighborCursor, _pCursor);
			else
			{
				iOldNode += DNumKeys(NeighborCursor.m_Pages[NeighborCursor.m_iTop]);
				NeighborCursor.m_NodeIndices[NeighborCursor.m_iTop] += _pCursor->m_NodeIndices[NeighborCursor.m_iTop] + 1;
				/* We want fg_Rebalance to find NeighborCursor when doing fixups */
				DWithCursorTracking(NeighborCursor, Result = fg_PageMerge(_pCursor, &NeighborCursor));
				fg_CursorCopy(&NeighborCursor, _pCursor);
			}
			_pCursor->m_Flags &= ~DCEof;
		}
		_pCursor->m_NodeIndices[_pCursor->m_iTop] = iOldNode;
		return Result;
	}

	/** Add all the DB's pages to the free list.
	 * @param[in] _pCursor Cursor on the DB to free.
	 * @param[in] _bSubDatabases non-Zero to check for sub-DBs in this DB.
	 * @return 0 on success, non-zero on failure.
	 */
	int NPrivate::fg_Drop0(CMdbCursor *_pCursor, int _bSubDatabases)
	{
		int Result;

		Result = fg_PageSearch(_pCursor, nullptr, DMdbPsFirst);
		if (Result == DMdbSuccess)
		{
			CMdbTxn *pTransaction = _pCursor->m_pTransaction;
			CMdbNode *pNode;
			CMdbCursor ReadCursor;
			uint32 iIndex;

			/* DUPSORT sub-DBs have no ovpages/DBs. Omit scanning leaves.
			 * This also avoids any DPLeaf2 pages, which have no nodes.
			 * Also if the DB doesn't have sub-DBs and has no overflow
			 * pages, omit scanning leaves.
			 */
			if ((_pCursor->m_Flags & DCSub) || (!_bSubDatabases && !_pCursor->m_pDatabase->m_nOverflowPages))
				fg_CursorPop(_pCursor);

			fg_CursorCopy(_pCursor, &ReadCursor);
#ifdef DMibDatabase_Always64BitSupport
			/* bump refcount for ReadCursor's pages */
			for (iIndex = 0; iIndex < _pCursor->m_nPages; iIndex++)
				fg_PageGet(&ReadCursor, _pCursor->m_Pages[iIndex]->m_PageNumber, &ReadCursor.m_Pages[iIndex], nullptr);
#endif
			while (_pCursor->m_nPages > 0)
			{
				CMdbPage *pPage = _pCursor->m_Pages[_pCursor->m_iTop];
				unsigned nKeys = DNumKeys(pPage);
				if (DIsLeaf(pPage))
				{
					for (iIndex = 0; iIndex < nKeys; iIndex++)
					{
						pNode = DNodePtr(pPage, iIndex);
						if (pNode->m_Flags & DFBigData)
						{
							CMdbPage *pOverflowPage;
							CPgno PageNumber;
							memcpy(&PageNumber, DNodeData(pNode), sizeof(PageNumber));
							Result = fg_PageGet(_pCursor, PageNumber, &pOverflowPage, nullptr);
							if (Result != 0)
								goto done;
							DMibCheck(DIsOverflow(pOverflowPage));
							fg_MidlAppendRange(&pTransaction->m_pFreePages, PageNumber, pOverflowPage->m_nOverflowPages);
							_pCursor->m_pDatabase->m_nOverflowPages -= pOverflowPage->m_nOverflowPages;
							if (!_pCursor->m_pDatabase->m_nOverflowPages && !_bSubDatabases)
								break;
						}
						else if (_bSubDatabases && (pNode->m_Flags & DFSubData))
						{
							fg_XCursorInit1(_pCursor, pNode);
							Result = fg_Drop0(&_pCursor->m_pXCursor->m_Cursor, 0);
							if (Result)
								goto done;
						}
					}
					if (!_bSubDatabases && !_pCursor->m_pDatabase->m_nOverflowPages)
						goto pop;
				}
				else
				{
					fg_MidlNeed(&pTransaction->m_pFreePages, nKeys);
					for (iIndex = 0; iIndex < nKeys; iIndex++)
					{
						CPgno PageNumber;
						pNode = DNodePtr(pPage, iIndex);
						PageNumber = DNodePgno(pNode);
						/* free it */
						fg_MidlXAppend(pTransaction->m_pFreePages, PageNumber);
					}
				}
				if (!_pCursor->m_iTop)
					break;
				_pCursor->m_NodeIndices[_pCursor->m_iTop] = iIndex;
				Result = fg_CursorSibling(_pCursor, 1);
				if (Result)
				{
					if (Result != DMdbNotFound)
						goto done;
					/* no more siblings, go back to beginning
					 * of previous level.
					 */
				pop:
					fg_CursorPop(_pCursor);
					_pCursor->m_NodeIndices[0] = 0;
					for (iIndex = 1; iIndex < _pCursor->m_nPages; iIndex++)
					{
						_pCursor->m_NodeIndices[iIndex] = 0;
						_pCursor->m_Pages[iIndex] = ReadCursor.m_Pages[iIndex];
					}
				}
			}
			/* free it */
			fg_MidlAppend(&pTransaction->m_pFreePages, _pCursor->m_pDatabase->m_RootPage);
		done:
			if (Result)
				pTransaction->m_Flags |= DMdbTxnError;
#ifdef DMibDatabase_Always64BitSupport
			/* drop refcount for ReadCursor's pages */
			fg_CursorUnref(&ReadCursor);
#endif
		}
		else if (Result == DMdbNotFound)
			Result = DMdbSuccess;
		_pCursor->m_Flags &= ~DCInitialized;
		return Result;
	}
}
