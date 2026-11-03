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

#pragma once

#include <Mib/Core/Core>
#include "Malterlib_Database_Database.h"

namespace NMib::NDatabase::NPrivate
{
	/** @defgroup internal	LMDB Internals
	 *	@{
	 */

	/** @defgroup idls	ID List Management
	 *	@{
	 */
	/** A generic unsigned ID number. These were entryIDs in back-bdb.
	 *	Preferably it should have the same size as a pointer.
	 */

	using CMdbId = CDatabaseOffset;

	/** An IDL is an ID List, a sorted array of IDs. The first
	 * element of the array is a counter for how many actual
	 * IDs are in the list. In the original back-bdb code, IDLs are
	 * sorted in ascending order. For libmdb IDLs are sorted in
	 * descending order.
	 */
	typedef CMdbId *CMdbIdl;

	/* IDL sizes - likely should be even bigger
	 *   limiting factors: sizeof(ID), thread stack size
	 */
#define	DMdbIdlLogn	16	/* DB_SIZE is 2^16, UM_SIZE is 2^17 */
#define DMdbIdlDbSize		(1<<DMdbIdlLogn)
#define DMdbIdlUmSize		(1<<(DMdbIdlLogn+1))

#define DMdbIdlDbMax		(DMdbIdlDbSize-1)
#define DMdbIdlUmMax		(DMdbIdlUmSize-1)

#define DMdbIdlSizeof(d_Ids)		(((d_Ids)[0]+1) * sizeof(CMdbId))
#define DMdbIdlIsZero(d_Ids) ( (d_Ids)[0] == 0 )
#define DMdbIdlCpy( d_Dst, d_Src ) (memcpy( d_Dst, d_Src, DMdbIdlSizeof( d_Src ) ))
#define DMdbIdlFirst( d_Ids )	( (d_Ids)[1] )
#define DMdbIdlLast( d_Ids )		( (d_Ids)[(d_Ids)[0]] )

	/** Current max length of an #fg_MidlAlloc()ed IDL */
#define DMdbIdlAlloclen( d_Ids )	( (d_Ids)[-1] )

	/** Append ID to IDL. The IDL must be big enough. */
	inline_always void fg_MidlXAppend(CMdbIdl _pIDs, CMdbId _ID)
	{
		CMdbId *xidl = _pIDs;
		CMdbId xlen = ++xidl[0];
		xidl[xlen] = _ID;
	}

	/** Search for an ID in an IDL.
	 * @param[in] _pIDs	The IDL to search.
	 * @param[in] _ID	The ID to search for.
	 * @return	The index of the first ID greater than or equal to \b _ID.
	 */
	unsigned fg_MidlSearch(CMdbIdl _pIDs, CMdbId _ID);

	/** Allocate an IDL.
	 * Allocates memory for an IDL of the given size.
	 * @return	IDL on success, nullptr on failure.
	 */
	CMdbIdl fg_MidlAlloc(int _nIDs);

	/** Free an IDL.
	 * @param[in] _pIDs	The IDL to free.
	 */
	void fg_MidlFree(CMdbIdl _pIDs);

	/** Shrink an IDL.
	 * Return the IDL to the default size if it has grown larger.
	 * @param[in,out] _pIDList	Address of the IDL to shrink.
	 */
	void fg_MidlShrink(CMdbIdl *_pIDList);

	/** Make room for _nIDs additional elements in an IDL.
	 * @param[in,out] _pIDList	Address of the IDL.
	 * @param[in] _nIDs	Number of elements to make room for.
	 * @return	0 on success, ENOMEM on failure.
	 */
	void fg_MidlNeed(CMdbIdl *_pIDList, unsigned _nIDs);

	/** Append an ID onto an IDL.
	 * @param[in,out] _pIDList	Address of the IDL to append to.
	 * @param[in] _ID	The ID to append.
	 * @return	0 on success, ENOMEM if the IDL is too large.
	 */
	void fg_MidlAppend(CMdbIdl *_pIDList, CMdbId _ID);

	/** Append an IDL onto an IDL.
	 * @param[in,out] _pIDList	Address of the IDL to append to.
	 * @param[in] _pAppendIDs	The IDL to append.
	 * @return	0 on success, ENOMEM if the IDL is too large.
	 */
	void fg_MidlAppendList(CMdbIdl *_pIDList, CMdbIdl _pAppendIDs);

	/** Append an ID range onto an IDL.
	 * @param[in,out] _pIDList	Address of the IDL to append to.
	 * @param[in] _ID	The lowest ID to append.
	 * @param[in] _nIDs		Number of IDs to append.
	 * @return	0 on success, ENOMEM if the IDL is too large.
	 */
	void fg_MidlAppendRange(CMdbIdl *_pIDList, CMdbId _ID, unsigned _nIDs);

	/** Merge an IDL onto an IDL. The destination IDL must be big enough.
	 * @param[in] _pIDs	The IDL to merge into.
	 * @param[in] _pMergeIDs	The IDL to merge.
	 */
	void fg_MidlXMerge(CMdbIdl _pIDs, CMdbIdl _pMergeIDs);

	/** Sort an IDL.
	 * @param[in,out] _pIDs	The IDL to sort.
	 */
	void fg_MidlSort(CMdbIdl _pIDs);

	/** An ID2 is an ID/pointer pair.
	 */
	struct CMdbId2
	{
		CMdbId mid; ///< The ID
		void *mptr; ///< The pointer
	};

	/** An ID2L is an ID2 List, a sorted array of ID2s.
	 * The first element's \b mid member is a count of how many actual
	 * elements are in the array. The \b mptr member of the first element is unused.
	 * The array is sorted in ascending order by \b mid.
	 */
	typedef CMdbId2 *CMdbId2L;

	/** Search for an ID in an ID2L.
	 * @param[in] _pIDs	The ID2L to search.
	 * @param[in] _ID	The ID to search for.
	 * @return	The index of the first ID2 whose \b mid member is greater than or equal to \b _ID.
	 */
	unsigned fg_Mid2lSearch(CMdbId2L _pIDs, CMdbId _ID);

	/** Insert an ID2 into a ID2L.
	 * @param[in,out] _pIDs	The ID2L to insert into.
	 * @param[in] _pID	The ID2 to insert.
	 * @return	0 on success, -1 if the ID was already present in the ID2L.
	 */
	int fg_Mid2lInsert(CMdbId2L _pIDs, CMdbId2 *_pID);

	/** Append an ID2 into a ID2L.
	 * @param[in,out] _pIDs	The ID2L to append into.
	 * @param[in] _pID	The ID2 to append.
	 * @return	0 on success, -2 if the ID2L is too big.
	 */
	int fg_Mid2lAppend(CMdbId2L _pIDs, CMdbId2 *_pID);

#ifdef DMibDatabase_Always64BitSupport
	struct MDB_ID3
	{
		CMdbId mid;  ///< The ID
		void *mptr;  ///< The pointer
		uint32 mcnt; ///< Number of pages
		uint32 mref; ///< Refcounter
	};

	typedef MDB_ID3 *MDB_ID3L;

	unsigned fg_Mid3lSearch(MDB_ID3L ids, CMdbId id);
	int fg_Mid3lInsert(MDB_ID3L ids, MDB_ID3 *id);

#endif /* DMibDatabase_Always64BitSupport */
	/** @} */
	/** @} */
}
