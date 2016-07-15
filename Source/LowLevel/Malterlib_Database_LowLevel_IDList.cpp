// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
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

#include "Malterlib_Database_LowLevel_IDList.h"

namespace NMib::NDatabase::NPrivate
{

	/** @defgroup internal	LMDB Internals
	 *	@{
	 */
	/** @defgroup idls	ID List Management
	 *	@{
	 */
	#define CMP(x,y)	 ( (x) < (y) ? -1 : (x) > (y) )

	unsigned mdb_midl_search( MDB_IDL ids, MDB_ID id )
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

		while( 0 < n ) {
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = CMP( ids[cursor], id );

			if( val < 0 ) {
				n = pivot;

			} else if ( val > 0 ) {
				base = cursor;
				n -= pivot + 1;

			} else {
				return cursor;
			}
		}

		if( val > 0 ) {
			++cursor;
		}
		return cursor;
	}

	MDB_IDL mdb_midl_alloc(int num)
	{
		MDB_IDL ids = (MDB_IDL)NMib::NMem::fg_AllocNoSize((num+2) * sizeof(MDB_ID));
		*ids++ = num;
		*ids = 0;
		return ids;
	}

	void mdb_midl_free(MDB_IDL ids)
	{
		if (ids)
			NMib::NMem::fg_Free(ids-1);
	}

	void mdb_midl_shrink( MDB_IDL *idp )
	{
		MDB_IDL ids = *idp;
		mint NewSize = (MDB_IDL_UM_MAX+2) * sizeof(MDB_ID);
		if (*(--ids) > MDB_IDL_UM_MAX && (ids = (MDB_IDL)NMib::NMem::fg_Resize(ids, NewSize)))
		{
			*ids++ = MDB_IDL_UM_MAX;
			*idp = ids;
		}
	}

	static void mdb_midl_grow( MDB_IDL *idp, int num )
	{
		MDB_IDL idn = *idp-1;
		/* grow it */
		mint NewSize = (*idn + num + 2) * sizeof(MDB_ID);
		idn = (MDB_IDL)NMib::NMem::fg_Resize(idn, NewSize);
		*idn++ += num;
		*idp = idn;
	}

	void mdb_midl_need( MDB_IDL *idp, unsigned num )
	{
		MDB_IDL ids = *idp;
		num += ids[0];
		if (num > ids[-1]) {
			num = (num + num/4 + (256 + 2)) & -256;
			mint NewSize = num * sizeof(MDB_ID);
			ids = (MDB_IDL)NMib::NMem::fg_Resize(ids-1, NewSize);
			*ids++ = num - 2;
			*idp = ids;
		}
	}

	void mdb_midl_append( MDB_IDL *idp, MDB_ID id )
	{
		MDB_IDL ids = *idp;
		/* Too big? */
		if (ids[0] >= ids[-1]) 
		{
			mdb_midl_grow(idp, MDB_IDL_UM_MAX);
			ids = *idp;
		}
		ids[0]++;
		ids[ids[0]] = id;
	}

	void mdb_midl_append_list( MDB_IDL *idp, MDB_IDL app )
	{
		MDB_IDL ids = *idp;
		/* Too big? */
		if (ids[0] + app[0] >= ids[-1]) {
			mdb_midl_grow(idp, app[0]);
			ids = *idp;
		}
		memcpy(&ids[ids[0]+1], &app[1], app[0] * sizeof(MDB_ID));
		ids[0] += app[0];
	}

	void mdb_midl_append_range( MDB_IDL *idp, MDB_ID id, unsigned n )
	{
		MDB_ID *ids = *idp, len = ids[0];
		/* Too big? */
		if (len + n > ids[-1]) {
			mdb_midl_grow(idp, n | MDB_IDL_UM_MAX);
			ids = *idp;
		}
		ids[0] = len + n;
		ids += len;
		while (n)
			ids[n--] = id++;
	}

	void mdb_midl_xmerge( MDB_IDL idl, MDB_IDL merge )
	{
		MDB_ID old_id, merge_id, i = merge[0], j = idl[0], k = i+j, total = k;
		idl[0] = (MDB_ID)-1;		/* delimiter for idl scan below */
		old_id = idl[j];
		while (i) {
			merge_id = merge[i--];
			for (; old_id < merge_id; old_id = idl[--j])
				idl[k--] = old_id;
			idl[k--] = merge_id;
		}
		idl[0] = total;
	}

	/* Quicksort + Insertion sort for small arrays */

	#define SMALL	8
	#define	MIDL_SWAP(a,b)	{ itmp=(a); (a)=(b); (b)=itmp; }

	void
	mdb_midl_sort( MDB_IDL ids )
	{
		/* Max possible depth of int-indexed tree * 2 items/level */
		int istack[sizeof(int)*8 * 2];
		int i,j,k,l,ir,jstack;
		MDB_ID a, itmp;

		ir = (int)ids[0];
		l = 1;
		jstack = 0;
		for(;;) {
			if (ir - l < SMALL) {	/* Insertion sort */
				for (j=l+1;j<=ir;j++) {
					a = ids[j];
					for (i=j-1;i>=1;i--) {
						if (ids[i] >= a) break;
						ids[i+1] = ids[i];
					}
					ids[i+1] = a;
				}
				if (jstack == 0) break;
				ir = istack[jstack--];
				l = istack[jstack--];
			} else {
				k = (l + ir) >> 1;	/* Choose median of left, center, right */
				MIDL_SWAP(ids[k], ids[l+1]);
				if (ids[l] < ids[ir]) {
					MIDL_SWAP(ids[l], ids[ir]);
				}
				if (ids[l+1] < ids[ir]) {
					MIDL_SWAP(ids[l+1], ids[ir]);
				}
				if (ids[l] < ids[l+1]) {
					MIDL_SWAP(ids[l], ids[l+1]);
				}
				i = l+1;
				j = ir;
				a = ids[l+1];
				for(;;) {
					do i++; while(ids[i] > a);
					do j--; while(ids[j] < a);
					if (j < i) break;
					MIDL_SWAP(ids[i],ids[j]);
				}
				ids[l+1] = ids[j];
				ids[j] = a;
				jstack += 2;
				if (ir-i+1 >= j-l) {
					istack[jstack] = ir;
					istack[jstack-1] = i;
					ir = j-1;
				} else {
					istack[jstack] = j-1;
					istack[jstack-1] = l;
					l = i;
				}
			}
		}
	}

	unsigned mdb_mid2l_search( MDB_ID2L ids, MDB_ID id )
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

		while( 0 < n ) {
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = CMP( id, ids[cursor].mid );

			if( val < 0 ) {
				n = pivot;

			} else if ( val > 0 ) {
				base = cursor;
				n -= pivot + 1;

			} else {
				return cursor;
			}
		}

		if( val > 0 ) {
			++cursor;
		}
		return cursor;
	}

	int mdb_mid2l_insert( MDB_ID2L ids, MDB_ID2 *id )
	{
		unsigned x, i;

		x = mdb_mid2l_search( ids, id->mid );

		if( x < 1 ) {
			/* internal error */
			return -2;
		}

		if ( x <= ids[0].mid && ids[x].mid == id->mid ) {
			/* duplicate */
			return -1;
		}

		if ( ids[0].mid >= MDB_IDL_UM_MAX ) {
			/* too big */
			return -2;

		} else {
			/* insert id */
			ids[0].mid++;
			for (i=(unsigned)ids[0].mid; i>x; i--)
				ids[i] = ids[i-1];
			ids[x] = *id;
		}

		return 0;
	}

	int mdb_mid2l_append( MDB_ID2L ids, MDB_ID2 *id )
	{
		/* Too big? */
		if (ids[0].mid >= MDB_IDL_UM_MAX) {
			return -2;
		}
		ids[0].mid++;
		ids[ids[0].mid] = *id;
		return 0;
	}

	#ifdef DMibDatabaseLowLevel_Always64BitSupport
	unsigned mdb_mid3l_search( MDB_ID3L ids, MDB_ID id )
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

		while( 0 < n ) {
			unsigned pivot = n >> 1;
			cursor = base + pivot + 1;
			val = CMP( id, ids[cursor].mid );

			if( val < 0 ) {
				n = pivot;

			} else if ( val > 0 ) {
				base = cursor;
				n -= pivot + 1;

			} else {
				return cursor;
			}
		}

		if( val > 0 ) {
			++cursor;
		}
		return cursor;
	}

	int mdb_mid3l_insert( MDB_ID3L ids, MDB_ID3 *id )
	{
		unsigned x, i;

		x = mdb_mid3l_search( ids, id->mid );

		if( x < 1 ) {
			/* internal error */
			return -2;
		}

		if ( x <= ids[0].mid && ids[x].mid == id->mid ) {
			/* duplicate */
			return -1;
		}

		/* insert id */
		ids[0].mid++;
		for (i=(unsigned)ids[0].mid; i>x; i--)
			ids[i] = ids[i-1];
		ids[x] = *id;

		return 0;
	}
	#endif /* DMibDatabaseLowLevel_Always64BitSupport */

	/** @} */
	/** @} */
}
