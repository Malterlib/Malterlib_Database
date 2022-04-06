// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
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
	/** Common code for #f_DatabaseStatistics() and #f_Statistics().
	 * @param[in] env the environment to operate in.
	 * @param[in] db the #MDB_db record containing the stats to return.
	 * @param[out] arg the address of an #CDatabaseStatistics structure to receive the stats.
	 * @return 0, this function always succeeds.
	 */
	CDatabaseStatistics DMibDatabaseInfrequentSection CDatabaseEnvironment_Internal::f_DatabaseStatistics(MDB_db *db)
	{
		CDatabaseStatistics Stats;
		Stats.m_PageSize = me_psize;
		Stats.m_BTreeDepth = db->md_depth;
		Stats.m_nBranchPages = db->md_branch_pages;
		Stats.m_nLeafPages = db->md_leaf_pages;
		Stats.m_nOverflowPages = db->md_overflow_pages;
		Stats.m_nDataItems = db->md_entries;

		return Stats;
	}

	CDatabaseOffset CDatabaseStatistics::f_GetTotalUsedSize() const
	{
		return (m_nBranchPages + m_nLeafPages + m_nOverflowPages) * m_PageSize;
	}
}
