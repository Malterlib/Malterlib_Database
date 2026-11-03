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

	/** Current max length of an #mdb_midl_alloc()ed IDL */
#define DMdbIdlAlloclen( d_Ids )	( (d_Ids)[-1] )

	/** Append ID to IDL. The IDL must be big enough. */
	inline_always void mdb_midl_xappend(CMdbIdl idl, CMdbId id)
	{
		CMdbId *xidl = idl;
		CMdbId xlen = ++xidl[0];
		xidl[xlen] = id;
	}

	/** Search for an ID in an IDL.
	 * @param[in] ids	The IDL to search.
	 * @param[in] id	The ID to search for.
	 * @return	The index of the first ID greater than or equal to \b id.
	 */
	unsigned mdb_midl_search(CMdbIdl ids, CMdbId id);

	/** Allocate an IDL.
	 * Allocates memory for an IDL of the given size.
	 * @return	IDL on success, nullptr on failure.
	 */
	CMdbIdl mdb_midl_alloc(int num);

	/** Free an IDL.
	 * @param[in] ids	The IDL to free.
	 */
	void mdb_midl_free(CMdbIdl ids);

	/** Shrink an IDL.
	 * Return the IDL to the default size if it has grown larger.
	 * @param[in,out] idp	Address of the IDL to shrink.
	 */
	void mdb_midl_shrink(CMdbIdl *idp);

	/** Make room for num additional elements in an IDL.
	 * @param[in,out] idp	Address of the IDL.
	 * @param[in] num	Number of elements to make room for.
	 * @return	0 on success, ENOMEM on failure.
	 */
	void mdb_midl_need(CMdbIdl *idp, unsigned num);

	/** Append an ID onto an IDL.
	 * @param[in,out] idp	Address of the IDL to append to.
	 * @param[in] id	The ID to append.
	 * @return	0 on success, ENOMEM if the IDL is too large.
	 */
	void mdb_midl_append(CMdbIdl *idp, CMdbId id);

	/** Append an IDL onto an IDL.
	 * @param[in,out] idp	Address of the IDL to append to.
	 * @param[in] app	The IDL to append.
	 * @return	0 on success, ENOMEM if the IDL is too large.
	 */
	void mdb_midl_append_list(CMdbIdl *idp, CMdbIdl app);

	/** Append an ID range onto an IDL.
	 * @param[in,out] idp	Address of the IDL to append to.
	 * @param[in] id	The lowest ID to append.
	 * @param[in] n		Number of IDs to append.
	 * @return	0 on success, ENOMEM if the IDL is too large.
	 */
	void mdb_midl_append_range(CMdbIdl *idp, CMdbId id, unsigned n);

	/** Merge an IDL onto an IDL. The destination IDL must be big enough.
	 * @param[in] idl	The IDL to merge into.
	 * @param[in] merge	The IDL to merge.
	 */
	void mdb_midl_xmerge(CMdbIdl idl, CMdbIdl merge);

	/** Sort an IDL.
	 * @param[in,out] ids	The IDL to sort.
	 */
	void mdb_midl_sort(CMdbIdl ids);

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
	 * @param[in] ids	The ID2L to search.
	 * @param[in] id	The ID to search for.
	 * @return	The index of the first ID2 whose \b mid member is greater than or equal to \b id.
	 */
	unsigned mdb_mid2l_search(CMdbId2L ids, CMdbId id);

	/** Insert an ID2 into a ID2L.
	 * @param[in,out] ids	The ID2L to insert into.
	 * @param[in] id	The ID2 to insert.
	 * @return	0 on success, -1 if the ID was already present in the ID2L.
	 */
	int mdb_mid2l_insert(CMdbId2L ids, CMdbId2 *id);

	/** Append an ID2 into a ID2L.
	 * @param[in,out] ids	The ID2L to append into.
	 * @param[in] id	The ID2 to append.
	 * @return	0 on success, -2 if the ID2L is too big.
	 */
	int mdb_mid2l_append(CMdbId2L ids, CMdbId2 *id);

#ifdef DMibDatabase_Always64BitSupport
	struct MDB_ID3
	{
		CMdbId mid;  ///< The ID
		void *mptr;  ///< The pointer
		uint32 mcnt; ///< Number of pages
		uint32 mref; ///< Refcounter
	};

	typedef MDB_ID3 *MDB_ID3L;

	unsigned mdb_mid3l_search(MDB_ID3L ids, CMdbId id);
	int mdb_mid3l_insert(MDB_ID3L ids, MDB_ID3 *id);

#endif /* DMibDatabase_Always64BitSupport */
	/** @} */
	/** @} */
}
