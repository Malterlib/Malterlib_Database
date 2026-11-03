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

	unsigned mdb_midl_search(CMdbIdl ids, CMdbId id)
	{
		/*
		 * binary search of id in ids
		 * if found, returns position of id
		 * if not found, returns first position greater than id
		 */
		unsigned base = 0;
		unsigned cursor = 1;
		int val = 0;
		unsigned n = ids[0];

		while (0 < n)
		{
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = DCmp(ids[cursor], id);

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

	CMdbIdl mdb_midl_alloc(int num)
	{
		CMdbIdl ids = (CMdbIdl)NMib::NMemory::fg_Alloc((num + 2) * sizeof(CMdbId));
		*ids++ = num;
		*ids = 0;
		return ids;
	}

	void mdb_midl_free(CMdbIdl ids)
	{
		if (ids)
		{
			--ids;
			umint nIds = *ids;
			NMib::NMemory::fg_Free(ids, (nIds + 2) * sizeof(CMdbId));
		}
	}

	void mdb_midl_shrink(CMdbIdl *idp)
	{
		CMdbIdl ids = *idp;
		umint NewSize = (DMdbIdlUmMax + 2) * sizeof(CMdbId);
		if (*(--ids) > DMdbIdlUmMax && (ids = (CMdbIdl)NMib::NMemory::fg_Resize(ids, NewSize, 0)))
		{
			*ids++ = DMdbIdlUmMax;
			*idp = ids;
		}
	}

	static void mdb_midl_grow(CMdbIdl *idp, int num)
	{
		CMdbIdl idn = *idp - 1;
		/* grow it */
		umint NewSize = (*idn + num + 2) * sizeof(CMdbId);
		idn = (CMdbIdl)NMib::NMemory::fg_Resize(idn, NewSize, 0, EAllocationFlag_SizeNotNeeded);
		*idn++ += num;
		*idp = idn;
	}

	void mdb_midl_need(CMdbIdl *idp, unsigned num)
	{
		CMdbIdl ids = *idp;
		num += ids[0];
		if (num > ids[-1])
		{
			num = (num + num / 4 + (256 + 2)) & -256;
			umint NewSize = num * sizeof(CMdbId);
			ids = (CMdbIdl)NMib::NMemory::fg_Resize(ids - 1, NewSize, 0, EAllocationFlag_SizeNotNeeded);
			*ids++ = num - 2;
			*idp = ids;
		}
	}

	void mdb_midl_append(CMdbIdl *idp, CMdbId id)
	{
		CMdbIdl ids = *idp;
		/* Too big? */
		if (ids[0] >= ids[-1])
		{
			mdb_midl_grow(idp, DMdbIdlUmMax);
			ids = *idp;
		}
		ids[0]++;
		ids[ids[0]] = id;
	}

	void mdb_midl_append_list(CMdbIdl *idp, CMdbIdl app)
	{
		CMdbIdl ids = *idp;
		/* Too big? */
		if (ids[0] + app[0] >= ids[-1])
		{
			mdb_midl_grow(idp, app[0]);
			ids = *idp;
		}
		memcpy(&ids[ids[0] + 1], &app[1], app[0] * sizeof(CMdbId));
		ids[0] += app[0];
	}

	void mdb_midl_append_range(CMdbIdl *idp, CMdbId id, unsigned n)
	{
		CMdbId *ids = *idp, len = ids[0];
		/* Too big? */
		if (len + n > ids[-1])
		{
			mdb_midl_grow(idp, n | DMdbIdlUmMax);
			ids = *idp;
		}
		ids[0] = len + n;
		ids += len;
		while (n)
			ids[n--] = id++;
	}

	void mdb_midl_xmerge(CMdbIdl idl, CMdbIdl merge)
	{
		CMdbId old_id, merge_id, i = merge[0], j = idl[0], k = i + j, total = k;
		idl[0] = (CMdbId)-1; // delimiter for idl scan below
		old_id = idl[j];
		while (i)
		{
			merge_id = merge[i--];
			for (; old_id < merge_id; old_id = idl[--j])
				idl[k--] = old_id;
			idl[k--] = merge_id;
		}
		idl[0] = total;
	}

	/* Quicksort + Insertion sort for small arrays */

#define DSmall	8
#define	DMidlSwap(d_Left,d_Right)	{ itmp=(d_Left); (d_Left)=(d_Right); (d_Right)=itmp; }

	void mdb_midl_sort(CMdbIdl ids)
	{
		/* Max possible depth of int-indexed tree * 2 items/level */
		int istack[sizeof(int) * 8 * 2];
		int i, j, k, l, ir, jstack;
		CMdbId a, itmp;

		ir = (int)ids[0];
		l = 1;
		jstack = 0;
		for (;;)
		{
			if (ir - l < DSmall)
			{ // Insertion sort
				for (j = l + 1; j <= ir; j++)
				{
					a = ids[j];
					for (i = j - 1; i >= 1; i--)
					{
						if (ids[i] >= a)
							break;
						ids[i + 1] = ids[i];
					}
					ids[i + 1] = a;
				}
				if (jstack == 0)
					break;
				ir = istack[jstack--];
				l = istack[jstack--];
			}
			else
			{
				k = (l + ir) >> 1; // Choose median of left, center, right
				DMidlSwap(ids[k], ids[l + 1]);
				if (ids[l] < ids[ir])
					DMidlSwap(ids[l], ids[ir]);
				if (ids[l + 1] < ids[ir])
					DMidlSwap(ids[l + 1], ids[ir]);
				if (ids[l] < ids[l + 1])
					DMidlSwap(ids[l], ids[l + 1]);
				i = l + 1;
				j = ir;
				a = ids[l + 1];
				for (;;)
				{
					do
						i++;
					while (ids[i] > a);
					do
						j--;
					while (ids[j] < a);
					if (j < i)
						break;
					DMidlSwap(ids[i], ids[j]);
				}
				ids[l + 1] = ids[j];
				ids[j] = a;
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

	unsigned mdb_mid2l_search(CMdbId2L ids, CMdbId id)
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

	int mdb_mid2l_insert(CMdbId2L ids, CMdbId2 *id)
	{
		unsigned x, i;

		x = mdb_mid2l_search(ids, id->mid);

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

		if (ids[0].mid >= DMdbIdlUmMax)
		{
			/* too big */
			return -2;
		}
		else
		{
			/* insert id */
			ids[0].mid++;
			for (i = (unsigned)ids[0].mid; i > x; i--)
				ids[i] = ids[i - 1];
			ids[x] = *id;
		}

		return 0;
	}

	int mdb_mid2l_append(CMdbId2L ids, CMdbId2 *id)
	{
		/* Too big? */
		if (ids[0].mid >= DMdbIdlUmMax)
			return -2;
		ids[0].mid++;
		ids[ids[0].mid] = *id;
		return 0;
	}

#ifdef DMibDatabase_Always64BitSupport
	unsigned mdb_mid3l_search(MDB_ID3L ids, CMdbId id)
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

	int mdb_mid3l_insert(MDB_ID3L ids, MDB_ID3 *id)
	{
		unsigned x, i;

		x = mdb_mid3l_search(ids, id->mid);

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
