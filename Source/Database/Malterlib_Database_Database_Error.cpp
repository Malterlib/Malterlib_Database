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

#include <Mib/Core/PlatformSpecific/WindowsError>

#include "Malterlib_Database_Database.h"
#include "Malterlib_Database_Database_Internal.h"

namespace NMib::NDatabase::NPrivate
{
	/** Table of descriptions for LMDB @ref errors */
	namespace
	{
		char const * const g_ErrorStrings[] =
		{
			"MDB_KEYEXIST: Key/data pair already exists"
			, "MDB_NOTFOUND: No matching key/data pair found"
			, "MDB_PAGE_NOTFOUND: Requested page not found"
			, "MDB_CORRUPTED: Located page was wrong type"
			, "MDB_PANIC: Update of meta page failed or environment had fatal error"
			, "MDB_VERSION_MISMATCH: Database environment version mismatch"
			, "MDB_INVALID: File is not an LMDB file"
			, "MDB_MAP_FULL: Environment mapsize limit reached"
			, "MDB_DBS_FULL: Environment maxdbs limit reached"
			, "MDB_READERS_FULL: Environment maxreaders limit reached"
			, "MDB_TLS_FULL: Thread-local storage keys full - too many environments open"
			, "MDB_TXN_FULL: Transaction has too many dirty pages - transaction too big"
			, "MDB_CURSOR_FULL: Internal error - cursor stack limit reached"
			, "MDB_PAGE_FULL: Internal error - page has no more space"
			, "MDB_MAP_RESIZED: Database contents grew beyond environment mapsize"
			, "MDB_INCOMPATIBLE: Operation and DB incompatible, or DB flags changed"
			, "MDB_BAD_RSLOT: Invalid reuse of reader locktable slot"
			, "MDB_BAD_TXN: Transaction must abort, has a child, or is invalid"
			, "MDB_BAD_VALSIZE: Unsupported size of key/DB name/data, or wrong DUPFIXED size"
			, "MDB_BAD_DBI: The specified DBI handle was closed/changed unexpectedly"
			, "MDB_PROBLEM: Unexpected problem - txn should abort"
		};
	}

	/** @brief Return a string describing a given error code.
	 *
	 * This function is a superset of the ANSI C X3.159-1989 (ANSI C) strerror(3)
	 * function. If the error code is greater than or equal to 0, then the string
	 * returned by the system function strerror(3) is returned. If the error code
	 * is less than 0, an error string corresponding to the LMDB library error is
	 * returned. See @ref errors for a list of LMDB-specific error codes.
	 * @param[in] err The error code
	 * @retval "error message" The description of the error
	 */
	NStr::CStr mdb_strerror(int _Error)
	{
		if (!_Error)
			return "Successful return: 0";

		if (_Error >= MDB_KEYEXIST && _Error <= MDB_LAST_ERRCODE)
		{
			int i = _Error - MDB_KEYEXIST;
			return g_ErrorStrings[i];
		}

	#ifdef DPlatformFamily_Windows
		/* These are the C-runtime error codes we use. The comment indicates
		 * their numeric value, and the Win32 error they would correspond to
		 * if the error actually came from a Win32 API. A major mess, we should
		 * have used LMDB-specific error codes for everything.
		 */
		switch(_Error)
		{
		case ENOENT:	/* 2, FILE_NOT_FOUND */
		case EIO:		/* 5, ACCESS_DENIED */
		case ENOMEM:	/* 12, INVALID_ACCESS */
		case EACCES:	/* 13, INVALID_DATA */
		case EBUSY:		/* 16, CURRENT_DIRECTORY */
		case EINVAL:	/* 22, BAD_COMMAND */
		case ENOSPC:	/* 28, OUT_OF_PAPER */
			return strerror(_Error);
		default:
			;
		}

		return NPlatform::fg_Win32_GetLastErrorStr(_Error);
	#else
		return strerror(_Error);
	#endif
	}
}
