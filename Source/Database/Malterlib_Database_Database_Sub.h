// Copyright © 2016 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2019 Howard Chu, Symas Corp.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted only as authorized by the OpenLDAP
 Public License.

 A copy of this license is available in the file LICENSE in the
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

#pragma once

namespace NMib::NDatabase
{
	///	\enum	f_OpenDatabase	Database Flags
	enum EDatabaseSubFlag : uint32
	{
		EDatabaseSubFlag_None = 0
		, EDatabaseSubFlag_ReverseKeys = 0x02					///< Use reverse string keys
		, EDatabaseSubFlag_SortedDuplicates = 0x04				///< Use sorted duplicates
		, EDatabaseSubFlag_IntegerKeys = 0x08					///< Numeric keys in native byte order: either uint32 or CDatabaseOffset
																///  (expects 32-bit int <= size_t <= 32/64-bit CDatabaseOffset.)
																///  The keys must all be of the same size.
		, EDatabaseSubFlag_SortedDuplicatesFixedSize = 0x10		///< With #EDatabaseSubFlag_SortedDuplicates, sorted dup items have fixed size
		, EDatabaseSubFlag_SortedDuplicatesIntegerKeys = 0x20	///< With #EDatabaseSubFlag_SortedDuplicates, dups are #EDatabaseSubFlag_IntegerKeys-style integers
		, EDatabaseSubFlag_SortedDuplicatesReverseKeys = 0x40	///< With #EDatabaseSubFlag_SortedDuplicates, use reverse string dups
		, EDatabaseSubFlag_Create = 0x40000						///< Create DB if not already existing
	};

	using FDatabaseCompare = NFunction::TCFunction<int (CDatabaseValue const &_Left, CDatabaseValue const &_Right), NFunction::CFunctionSupportCompareTag>;

	/** @brief A handle for an individual database in the DB environment. */
	using CDatabaseSubID = uint32;

	struct CDatabaseSubProperties
	{
		FDatabaseCompare m_fKeyCompare;
		FDatabaseCompare m_fDataCompare;
	};

	struct CDatabaseSub
	{
		CDatabaseSub();

		CDatabaseSubReadTransaction f_Transaction(CDatabaseReadTransaction const &_Transaction);
		CDatabaseSubWriteTransaction f_Transaction(CDatabaseWriteTransaction const &_Transaction);

	private:
		friend struct CDatabaseTransactionCommon;

		CDatabaseSub(CDatabaseSubID _DatabaseID);

		CDatabaseSubID mp_DatabaseID;
	};
}
