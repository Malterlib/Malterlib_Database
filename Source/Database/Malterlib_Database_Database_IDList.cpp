// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
SPDX-License-Identifier: OLDAP-2.8
This code is derived from lmdb with the following copyright:

This work is part of OpenLDAP Software <http://www.openldap.org/>.

Copyright 2000-2016 The OpenLDAP Foundation.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted only as authorized by the OpenLDAP
Public License.

A copy of this license is available in the file LICENSE.OpenLDAP in the
top-level directory of the distribution or, alternatively, at
<http://www.OpenLDAP.org/license.html>.
*/

#include "Malterlib_Database_Database_IDList.h"

namespace NMib::NDatabase::NPrivate
{
	/** @defgroup internal	LMDB Internals
	 *	@{
	 */
	/** @defgroup idls	ID List Management
	 *	@{
	 */
#define DCmp(d_Left,d_Right)	 ( (d_Left) < (d_Right) ? -1 : (d_Left) > (d_Right) )

	unsigned fg_MidlSearch(CMdbIdl _pIDs, CMdbId _ID)
	{
		/*
		 * binary search of _ID in _pIDs
		 * if found, returns position of _ID
		 * if not found, returns first position greater than _ID
		 */
		unsigned iBase = 0;
		unsigned iCursor = 1;
		int Compare = 0;
		unsigned nRemaining = _pIDs[0];

		while (0 < nRemaining)
		{
			unsigned nHalf = nRemaining >> 1;
			iCursor = iBase + nHalf + 1;
			Compare = DCmp(_pIDs[iCursor], _ID);

			if (Compare < 0)
				nRemaining = nHalf;
			else if (Compare > 0)
			{
				iBase = iCursor;
				nRemaining -= nHalf + 1;
			}
			else
				return iCursor;
		}

		if (Compare > 0)
			++iCursor;
		return iCursor;
	}

	CMdbIdl fg_MidlAlloc(int _nIDs)
	{
		CMdbIdl pIDs = (CMdbIdl)NMib::NMemory::fg_Alloc((_nIDs + 2) * sizeof(CMdbId));
		*pIDs++ = _nIDs;
		*pIDs = 0;
		return pIDs;
	}

	void fg_MidlFree(CMdbIdl _pIDs)
	{
		if (_pIDs)
		{
			--_pIDs;
			umint nIds = *_pIDs;
			NMib::NMemory::fg_Free(_pIDs, (nIds + 2) * sizeof(CMdbId));
		}
	}

	void fg_MidlShrink(CMdbIdl *_pIDList)
	{
		CMdbIdl pIDs = *_pIDList;
		umint NewSize = (DMdbIdlUmMax + 2) * sizeof(CMdbId);
		if (*(--pIDs) > DMdbIdlUmMax && (pIDs = (CMdbIdl)NMib::NMemory::fg_Resize(pIDs, NewSize, 0)))
		{
			*pIDs++ = DMdbIdlUmMax;
			*_pIDList = pIDs;
		}
	}

	static void fg_MidlGrow(CMdbIdl *_pIDList, int _nIDs)
	{
		CMdbIdl pAllocation = *_pIDList - 1;
		/* grow it */
		umint NewSize = (*pAllocation + _nIDs + 2) * sizeof(CMdbId);
		pAllocation = (CMdbIdl)NMib::NMemory::fg_Resize(pAllocation, NewSize, 0, EAllocationFlag_SizeNotNeeded);
		*pAllocation++ += _nIDs;
		*_pIDList = pAllocation;
	}

	void fg_MidlNeed(CMdbIdl *_pIDList, unsigned _nIDs)
	{
		CMdbIdl pIDs = *_pIDList;
		_nIDs += pIDs[0];
		if (_nIDs > pIDs[-1])
		{
			_nIDs = (_nIDs + _nIDs / 4 + (256 + 2)) & -256;
			umint NewSize = _nIDs * sizeof(CMdbId);
			pIDs = (CMdbIdl)NMib::NMemory::fg_Resize(pIDs - 1, NewSize, 0, EAllocationFlag_SizeNotNeeded);
			*pIDs++ = _nIDs - 2;
			*_pIDList = pIDs;
		}
	}

	void fg_MidlAppend(CMdbIdl *_pIDList, CMdbId _ID)
	{
		CMdbIdl pIDs = *_pIDList;
		/* Too big? */
		if (pIDs[0] >= pIDs[-1])
		{
			fg_MidlGrow(_pIDList, DMdbIdlUmMax);
			pIDs = *_pIDList;
		}
		pIDs[0]++;
		pIDs[pIDs[0]] = _ID;
	}

	void fg_MidlAppendList(CMdbIdl *_pIDList, CMdbIdl _pAppendIDs)
	{
		CMdbIdl pIDs = *_pIDList;
		/* Too big? */
		if (pIDs[0] + _pAppendIDs[0] >= pIDs[-1])
		{
			fg_MidlGrow(_pIDList, _pAppendIDs[0]);
			pIDs = *_pIDList;
		}
		memcpy(&pIDs[pIDs[0] + 1], &_pAppendIDs[1], _pAppendIDs[0] * sizeof(CMdbId));
		pIDs[0] += _pAppendIDs[0];
	}

	void fg_MidlAppendRange(CMdbIdl *_pIDList, CMdbId _ID, unsigned _nIDs)
	{
		CMdbId *pIDs = *_pIDList, nExisting = pIDs[0];
		/* Too big? */
		if (nExisting + _nIDs > pIDs[-1])
		{
			fg_MidlGrow(_pIDList, _nIDs | DMdbIdlUmMax);
			pIDs = *_pIDList;
		}
		pIDs[0] = nExisting + _nIDs;
		pIDs += nExisting;
		while (_nIDs)
			pIDs[_nIDs--] = _ID++;
	}

	void fg_MidlXMerge(CMdbIdl _pIDs, CMdbIdl _pMergeIDs)
	{
		CMdbId OldID, MergeID, iMerge = _pMergeIDs[0], iOld = _pIDs[0], iDestination = iMerge + iOld, nTotal = iDestination;
		_pIDs[0] = (CMdbId)-1; // delimiter for _pIDs scan below
		OldID = _pIDs[iOld];
		while (iMerge)
		{
			MergeID = _pMergeIDs[iMerge--];
			for (; OldID < MergeID; OldID = _pIDs[--iOld])
				_pIDs[iDestination--] = OldID;
			_pIDs[iDestination--] = MergeID;
		}
		_pIDs[0] = nTotal;
	}

	/* Quicksort + Insertion sort for small arrays */

#define DSmall	8
#define	DMidlSwap(d_Left,d_Right)	{ SwapTemp=(d_Left); (d_Left)=(d_Right); (d_Right)=SwapTemp; }

	void fg_MidlSort(CMdbIdl _pIDs)
	{
		/* Max possible depth of int-indexed tree * 2 items/level */
		int IndexStack[sizeof(int) * 8 * 2];
		int iUp, iDown, iMedian, iLow, iHigh, iStack;
		CMdbId Pivot, SwapTemp;

		iHigh = (int)_pIDs[0];
		iLow = 1;
		iStack = 0;
		for (;;)
		{
			if (iHigh - iLow < DSmall)
			{ // Insertion sort
				for (iDown = iLow + 1; iDown <= iHigh; iDown++)
				{
					Pivot = _pIDs[iDown];
					for (iUp = iDown - 1; iUp >= 1; iUp--)
					{
						if (_pIDs[iUp] >= Pivot)
							break;
						_pIDs[iUp + 1] = _pIDs[iUp];
					}
					_pIDs[iUp + 1] = Pivot;
				}
				if (iStack == 0)
					break;
				iHigh = IndexStack[iStack--];
				iLow = IndexStack[iStack--];
			}
			else
			{
				iMedian = (iLow + iHigh) >> 1; // Choose median of left, center, right
				DMidlSwap(_pIDs[iMedian], _pIDs[iLow + 1]);
				if (_pIDs[iLow] < _pIDs[iHigh])
					DMidlSwap(_pIDs[iLow], _pIDs[iHigh]);
				if (_pIDs[iLow + 1] < _pIDs[iHigh])
					DMidlSwap(_pIDs[iLow + 1], _pIDs[iHigh]);
				if (_pIDs[iLow] < _pIDs[iLow + 1])
					DMidlSwap(_pIDs[iLow], _pIDs[iLow + 1]);
				iUp = iLow + 1;
				iDown = iHigh;
				Pivot = _pIDs[iLow + 1];
				for (;;)
				{
					do
						iUp++;
					while (_pIDs[iUp] > Pivot);
					do
						iDown--;
					while (_pIDs[iDown] < Pivot);
					if (iDown < iUp)
						break;
					DMidlSwap(_pIDs[iUp], _pIDs[iDown]);
				}
				_pIDs[iLow + 1] = _pIDs[iDown];
				_pIDs[iDown] = Pivot;
				iStack += 2;
				if (iHigh - iUp + 1 >= iDown - iLow)
				{
					IndexStack[iStack] = iHigh;
					IndexStack[iStack - 1] = iUp;
					iHigh = iDown - 1;
				}
				else
				{
					IndexStack[iStack] = iDown - 1;
					IndexStack[iStack - 1] = iLow;
					iLow = iUp;
				}
			}
		}
	}

	unsigned fg_Mid2lSearch(CMdbId2L _pIDs, CMdbId _ID)
	{
		/*
		 * binary search of _ID in _pIDs
		 * if found, returns position of _ID
		 * if not found, returns first position greater than _ID
		 */
		unsigned iBase = 0;
		unsigned iCursor = 1;
		int Compare = 0;
		unsigned nRemaining = (unsigned)_pIDs[0].m_ID;

		while (0 < nRemaining)
		{
			unsigned nHalf = nRemaining >> 1;
			iCursor = iBase + nHalf + 1;
			Compare = DCmp(_ID, _pIDs[iCursor].m_ID);

			if (Compare < 0)
				nRemaining = nHalf;
			else if (Compare > 0)
			{
				iBase = iCursor;
				nRemaining -= nHalf + 1;
			}
			else
				return iCursor;
		}

		if (Compare > 0)
			++iCursor;
		return iCursor;
	}

	int fg_Mid2lInsert(CMdbId2L _pIDs, CMdbId2 *_pID)
	{
		unsigned iInsert, iMove;

		iInsert = fg_Mid2lSearch(_pIDs, _pID->m_ID);

		if (iInsert < 1)
		{
			/* internal error */
			return -2;
		}

		if (iInsert <= _pIDs[0].m_ID && _pIDs[iInsert].m_ID == _pID->m_ID)
		{
			/* duplicate */
			return -1;
		}

		if (_pIDs[0].m_ID >= DMdbIdlUmMax)
		{
			/* too big */
			return -2;
		}
		else
		{
			/* insert _pID */
			_pIDs[0].m_ID++;
			for (iMove = (unsigned)_pIDs[0].m_ID; iMove > iInsert; iMove--)
				_pIDs[iMove] = _pIDs[iMove - 1];
			_pIDs[iInsert] = *_pID;
		}

		return 0;
	}

	int fg_Mid2lAppend(CMdbId2L _pIDs, CMdbId2 *_pID)
	{
		/* Too big? */
		if (_pIDs[0].m_ID >= DMdbIdlUmMax)
			return -2;
		_pIDs[0].m_ID++;
		_pIDs[_pIDs[0].m_ID] = *_pID;
		return 0;
	}

#ifdef DMibDatabase_Always64BitSupport
	unsigned fg_Mid3lSearch(CMdbId3L _pIDs, CMdbId _ID)
	{
		/*
		 * binary search of _ID in _pIDs
		 * if found, returns position of _ID
		 * if not found, returns first position greater than _ID
		 */
		unsigned iBase = 0;
		unsigned iCursor = 1;
		int Compare = 0;
		unsigned nRemaining = (unsigned)_pIDs[0].m_ID;

		while (0 < nRemaining)
		{
			unsigned nHalf = nRemaining >> 1;
			iCursor = iBase + nHalf + 1;
			Compare = DCmp(_ID, _pIDs[iCursor].m_ID);

			if (Compare < 0)
				nRemaining = nHalf;
			else if (Compare > 0)
			{
				iBase = iCursor;
				nRemaining -= nHalf + 1;
			}
			else
				return iCursor;
		}

		if (Compare > 0)
			++iCursor;
		return iCursor;
	}

	int fg_Mid3lInsert(CMdbId3L _pIDs, CMdbId3 *_pID)
	{
		unsigned iInsert, iMove;

		iInsert = fg_Mid3lSearch(_pIDs, _pID->m_ID);

		if (iInsert < 1)
		{
			/* internal error */
			return -2;
		}

		if (iInsert <= _pIDs[0].m_ID && _pIDs[iInsert].m_ID == _pID->m_ID)
		{
			/* duplicate */
			return -1;
		}

		/* insert _pID */
		_pIDs[0].m_ID++;
		for (iMove = (unsigned)_pIDs[0].m_ID; iMove > iInsert; iMove--)
			_pIDs[iMove] = _pIDs[iMove - 1];
		_pIDs[iInsert] = *_pID;

		return 0;
	}
#endif /* DMibDatabase_Always64BitSupport */

	/** @} */
	/** @} */
}
