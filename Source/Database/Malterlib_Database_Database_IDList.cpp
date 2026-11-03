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
		unsigned base = 0;
		unsigned cursor = 1;
		int val = 0;
		unsigned n = _pIDs[0];

		while (0 < n)
		{
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = DCmp(_pIDs[cursor], _ID);

			if (val < 0)
				n = pivot;
			else if (val > 0)
			{
				base = cursor;
				n -= pivot + 1;
			}
			else
				return cursor;
		}

		if (val > 0)
			++cursor;
		return cursor;
	}

	CMdbIdl fg_MidlAlloc(int _nIDs)
	{
		CMdbIdl ids = (CMdbIdl)NMib::NMemory::fg_Alloc((_nIDs + 2) * sizeof(CMdbId));
		*ids++ = _nIDs;
		*ids = 0;
		return ids;
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
		CMdbIdl ids = *_pIDList;
		umint NewSize = (DMdbIdlUmMax + 2) * sizeof(CMdbId);
		if (*(--ids) > DMdbIdlUmMax && (ids = (CMdbIdl)NMib::NMemory::fg_Resize(ids, NewSize, 0)))
		{
			*ids++ = DMdbIdlUmMax;
			*_pIDList = ids;
		}
	}

	static void fg_MidlGrow(CMdbIdl *_pIDList, int _nIDs)
	{
		CMdbIdl idn = *_pIDList - 1;
		/* grow it */
		umint NewSize = (*idn + _nIDs + 2) * sizeof(CMdbId);
		idn = (CMdbIdl)NMib::NMemory::fg_Resize(idn, NewSize, 0, EAllocationFlag_SizeNotNeeded);
		*idn++ += _nIDs;
		*_pIDList = idn;
	}

	void fg_MidlNeed(CMdbIdl *_pIDList, unsigned _nIDs)
	{
		CMdbIdl ids = *_pIDList;
		_nIDs += ids[0];
		if (_nIDs > ids[-1])
		{
			_nIDs = (_nIDs + _nIDs / 4 + (256 + 2)) & -256;
			umint NewSize = _nIDs * sizeof(CMdbId);
			ids = (CMdbIdl)NMib::NMemory::fg_Resize(ids - 1, NewSize, 0, EAllocationFlag_SizeNotNeeded);
			*ids++ = _nIDs - 2;
			*_pIDList = ids;
		}
	}

	void fg_MidlAppend(CMdbIdl *_pIDList, CMdbId _ID)
	{
		CMdbIdl ids = *_pIDList;
		/* Too big? */
		if (ids[0] >= ids[-1])
		{
			fg_MidlGrow(_pIDList, DMdbIdlUmMax);
			ids = *_pIDList;
		}
		ids[0]++;
		ids[ids[0]] = _ID;
	}

	void fg_MidlAppendList(CMdbIdl *_pIDList, CMdbIdl _pAppendIDs)
	{
		CMdbIdl ids = *_pIDList;
		/* Too big? */
		if (ids[0] + _pAppendIDs[0] >= ids[-1])
		{
			fg_MidlGrow(_pIDList, _pAppendIDs[0]);
			ids = *_pIDList;
		}
		memcpy(&ids[ids[0] + 1], &_pAppendIDs[1], _pAppendIDs[0] * sizeof(CMdbId));
		ids[0] += _pAppendIDs[0];
	}

	void fg_MidlAppendRange(CMdbIdl *_pIDList, CMdbId _ID, unsigned _nIDs)
	{
		CMdbId *ids = *_pIDList, len = ids[0];
		/* Too big? */
		if (len + _nIDs > ids[-1])
		{
			fg_MidlGrow(_pIDList, _nIDs | DMdbIdlUmMax);
			ids = *_pIDList;
		}
		ids[0] = len + _nIDs;
		ids += len;
		while (_nIDs)
			ids[_nIDs--] = _ID++;
	}

	void fg_MidlXMerge(CMdbIdl _pIDs, CMdbIdl _pMergeIDs)
	{
		CMdbId old_id, merge_id, i = _pMergeIDs[0], j = _pIDs[0], k = i + j, total = k;
		_pIDs[0] = (CMdbId)-1; // delimiter for _pIDs scan below
		old_id = _pIDs[j];
		while (i)
		{
			merge_id = _pMergeIDs[i--];
			for (; old_id < merge_id; old_id = _pIDs[--j])
				_pIDs[k--] = old_id;
			_pIDs[k--] = merge_id;
		}
		_pIDs[0] = total;
	}

	/* Quicksort + Insertion sort for small arrays */

#define DSmall	8
#define	DMidlSwap(d_Left,d_Right)	{ itmp=(d_Left); (d_Left)=(d_Right); (d_Right)=itmp; }

	void fg_MidlSort(CMdbIdl _pIDs)
	{
		/* Max possible depth of int-indexed tree * 2 items/level */
		int istack[sizeof(int) * 8 * 2];
		int i, j, k, l, ir, jstack;
		CMdbId a, itmp;

		ir = (int)_pIDs[0];
		l = 1;
		jstack = 0;
		for (;;)
		{
			if (ir - l < DSmall)
			{ // Insertion sort
				for (j = l + 1; j <= ir; j++)
				{
					a = _pIDs[j];
					for (i = j - 1; i >= 1; i--)
					{
						if (_pIDs[i] >= a)
							break;
						_pIDs[i + 1] = _pIDs[i];
					}
					_pIDs[i + 1] = a;
				}
				if (jstack == 0)
					break;
				ir = istack[jstack--];
				l = istack[jstack--];
			}
			else
			{
				k = (l + ir) >> 1; // Choose median of left, center, right
				DMidlSwap(_pIDs[k], _pIDs[l + 1]);
				if (_pIDs[l] < _pIDs[ir])
					DMidlSwap(_pIDs[l], _pIDs[ir]);
				if (_pIDs[l + 1] < _pIDs[ir])
					DMidlSwap(_pIDs[l + 1], _pIDs[ir]);
				if (_pIDs[l] < _pIDs[l + 1])
					DMidlSwap(_pIDs[l], _pIDs[l + 1]);
				i = l + 1;
				j = ir;
				a = _pIDs[l + 1];
				for (;;)
				{
					do
						i++;
					while (_pIDs[i] > a);
					do
						j--;
					while (_pIDs[j] < a);
					if (j < i)
						break;
					DMidlSwap(_pIDs[i], _pIDs[j]);
				}
				_pIDs[l + 1] = _pIDs[j];
				_pIDs[j] = a;
				jstack += 2;
				if (ir - i + 1 >= j - l)
				{
					istack[jstack] = ir;
					istack[jstack - 1] = i;
					ir = j - 1;
				}
				else
				{
					istack[jstack] = j - 1;
					istack[jstack - 1] = l;
					l = i;
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
		unsigned base = 0;
		unsigned cursor = 1;
		int val = 0;
		unsigned n = (unsigned)_pIDs[0].mid;

		while (0 < n)
		{
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = DCmp(_ID, _pIDs[cursor].mid);

			if (val < 0)
				n = pivot;
			else if (val > 0)
			{
				base = cursor;
				n -= pivot + 1;
			}
			else
				return cursor;
		}

		if (val > 0)
			++cursor;
		return cursor;
	}

	int fg_Mid2lInsert(CMdbId2L _pIDs, CMdbId2 *_pID)
	{
		unsigned x, i;

		x = fg_Mid2lSearch(_pIDs, _pID->mid);

		if (x < 1)
		{
			/* internal error */
			return -2;
		}

		if (x <= _pIDs[0].mid && _pIDs[x].mid == _pID->mid)
		{
			/* duplicate */
			return -1;
		}

		if (_pIDs[0].mid >= DMdbIdlUmMax)
		{
			/* too big */
			return -2;
		}
		else
		{
			/* insert _pID */
			_pIDs[0].mid++;
			for (i = (unsigned)_pIDs[0].mid; i > x; i--)
				_pIDs[i] = _pIDs[i - 1];
			_pIDs[x] = *_pID;
		}

		return 0;
	}

	int fg_Mid2lAppend(CMdbId2L _pIDs, CMdbId2 *_pID)
	{
		/* Too big? */
		if (_pIDs[0].mid >= DMdbIdlUmMax)
			return -2;
		_pIDs[0].mid++;
		_pIDs[_pIDs[0].mid] = *_pID;
		return 0;
	}

#ifdef DMibDatabase_Always64BitSupport
	unsigned fg_Mid3lSearch(MDB_ID3L ids, CMdbId id)
	{
		/*
		 * binary search of id in ids
		 * if found, returns position of id
		 * if not found, returns first position greater than id
		 */
		unsigned base = 0;
		unsigned cursor = 1;
		int val = 0;
		unsigned n = (unsigned)ids[0].mid;

		while (0 < n)
		{
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = DCmp(id, ids[cursor].mid);

			if (val < 0)
				n = pivot;
			else if (val > 0)
			{
				base = cursor;
				n -= pivot + 1;
			}
			else
				return cursor;
		}

		if (val > 0)
			++cursor;
		return cursor;
	}

	int fg_Mid3lInsert(MDB_ID3L ids, MDB_ID3 *id)
	{
		unsigned x, i;

		x = fg_Mid3lSearch(ids, id->mid);

		if (x < 1)
		{
			/* internal error */
			return -2;
		}

		if (x <= ids[0].mid && ids[x].mid == id->mid)
		{
			/* duplicate */
			return -1;
		}

		/* insert id */
		ids[0].mid++;
		for (i = (unsigned)ids[0].mid; i > x; i--)
			ids[i] = ids[i - 1];
		ids[x] = *id;

		return 0;
	}
#endif /* DMibDatabase_Always64BitSupport */

	/** @} */
	/** @} */
}
