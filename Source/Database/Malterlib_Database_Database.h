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

#include <Mib/Core/Core>
#include <Mib/Database/DatabaseValue>

#if DMibPPtrBits < 64
#	define DMibDatabase_Always64BitSupport
#endif

namespace NMib::NDatabase
{
#	ifdef DMibDatabase_Always64BitSupport
#		define DMibDatabase_IDBits 64
		using CDatabaseOffset = uint64;
#	else
#		define DMibDatabase_IDBits DMibPPtrBits
		using CDatabaseOffset = mint;
#	endif

	struct CDatabaseEnvironment;
	struct CDatabaseEnvironment_Internal;
	struct CDatabaseSub;
	struct CDatabaseSubReadTransaction;
	struct CDatabaseSubWriteTransaction;
	struct CDatabaseReadTransaction;
	struct CDatabaseWriteTransaction;
	struct CDatabaseCursor;
	struct CDatabaseCursorWrite;
	struct CDatabaseCursorDuplicateData;
	struct CDatabaseCursorDuplicateDataWrite;
}

#include "Malterlib_Database_Database_Statistics.h"
#include "Malterlib_Database_Database_ThreadLocal.h"
#include "Malterlib_Database_Database_Sub.h"
#include "Malterlib_Database_Database_Transaction.h"
#include "Malterlib_Database_Database_SubTransaction.h"
#include "Malterlib_Database_Database_Cursor.h"
#include "Malterlib_Database_Database_CursorDuplicateData.h"

namespace NMib::NDatabase
{
	/// \enum Environment Flags
	enum EDatabaseEnvironmentFlag : uint32
	{
		EDatabaseEnvironmentFlag_None = 0
		, EDatabaseEnvironmentFlag_FixedMap = 0x01				///< Memory map at a fixed address (experimental)
		, EDatabaseEnvironmentFlag_NoSubdir = 0x4000			///< No environment directory
		, EDatabaseEnvironmentFlag_NoSync = 0x10000				///< Don't fsync after commit
		, EDatabaseEnvironmentFlag_ReadOnly = 0x20000			///< Read only
		, EDatabaseEnvironmentFlag_NoMetaSync = 0x40000			///< Don't fsync metapage after commit
		, EDatabaseEnvironmentFlag_WriteMap = 0x80000			///< Use writable mmap
		, EDatabaseEnvironmentFlag_WriteMapAsync = 0x100000		///< Use asynchronous msync when #EDatabaseEnvironmentFlag_WriteMap is used
		, EDatabaseEnvironmentFlag_NoTls = 0x200000				///< Tie reader locktable slots to #MDB_txn objects instead of to threads
		, EDatabaseEnvironmentFlag_NoLock = 0x400000			///< Don't do any locking, caller must manage their own locks
		, EDatabaseEnvironmentFlag_NoReadAhead = 0x800000		///< Don't do readahead (no effect on Windows)
		, EDatabaseEnvironmentFlag_NoMemoryInit = 0x1000000		///< Don't initialize malloc'd memory before writing to datafile
		, EDatabaseEnvironmentFlag_PreviousSnapshot = 0x2000000	///< Use the previous snapshot rather than the latest one

		, EDatabaseEnvironmentFlag_Changable =
		(
			EDatabaseEnvironmentFlag_NoSync
			| EDatabaseEnvironmentFlag_NoMetaSync
			| EDatabaseEnvironmentFlag_WriteMapAsync
			| EDatabaseEnvironmentFlag_NoMemoryInit
		)
		, EDatabaseEnvironmentFlag_Changeless =
		(
			EDatabaseEnvironmentFlag_FixedMap
			| EDatabaseEnvironmentFlag_NoSubdir
			| EDatabaseEnvironmentFlag_ReadOnly
			| EDatabaseEnvironmentFlag_WriteMap
			| EDatabaseEnvironmentFlag_NoTls
			| EDatabaseEnvironmentFlag_NoLock
			| EDatabaseEnvironmentFlag_NoReadAhead
			| EDatabaseEnvironmentFlag_PreviousSnapshot
		)
	};

	///	\enum Copy Flags
	enum EDatabaseEnvironmentCopyFlag
	{
		EDatabaseEnvironmentCopyFlag_None = 0
		, EDatabaseEnvironmentCopyFlag_Compact = 0x01 ///< Compacting copy: Omit free space from copy, and renumber all pages sequentially.
	};

	/** @brief Information about the environment */
	struct CDatabaseEnvironmentInfo
	{
		void *m_pMapAddress;						///< Address of map, if fixed
		CDatabaseOffset m_MapSize;					///< Size of the data memory map
		CDatabaseOffset m_LastPageID;					///< ID of the last used page
		CDatabaseOffset m_LastCommittedTransactionID;	///< ID of the last committed transaction
		uint32 m_MaxReaders;						///< max reader slots in the environment
		uint32 m_nReaders;							///< max reader slots used in the environment
	};

	struct CDatabaseReaderInfo
	{
		mint m_iReaderSlot;
		NStr::CStr m_Description;
	};

	struct CDatabaseEnvironment
	{
		CDatabaseEnvironment();
		~CDatabaseEnvironment();

		CDatabaseEnvironment(CDatabaseEnvironment &&_Other);
		CDatabaseEnvironment &operator =(CDatabaseEnvironment &&_Other);

		void f_Open(NStr::CStr const &_Path, EDatabaseEnvironmentFlag _Flags, NFile::EFileAttrib _FileAttributes);
		void f_Close();
		void f_Sync(bool _bForce);

		void f_ChangeFlags(EDatabaseEnvironmentFlag _AddFlags, EDatabaseEnvironmentFlag _RemoveFlags);
		EDatabaseEnvironmentFlag f_GetFlags();

		void f_Copy(NStr::CStr const &_Path, EDatabaseEnvironmentCopyFlag _Flags = EDatabaseEnvironmentCopyFlag_None);
		void f_Copy(void *_FileHandle, EDatabaseEnvironmentCopyFlag _Flags = EDatabaseEnvironmentCopyFlag_None);

		CDatabaseStatistics f_Statistics();
		CDatabaseEnvironmentInfo f_Info();
		mint f_MaxKeySize();

		NStr::CStr f_GetPath();
		void *f_GetFD();

		void f_SetMaxReaders(uint32 _nReaders);
		uint32 f_MaxReaders();
		mint f_CheckForStaleReaders();

		void f_SetMapSize(CDatabaseOffset _Size);
		void f_SetMaxDatabases(CDatabaseSubID _DatabaseID);

		NContainer::TCVector<CDatabaseReaderInfo> f_GetReaderInfos();

		CDatabaseReadTransaction f_BeginReadTransaction();
		CDatabaseWriteTransaction f_BeginWriteTransaction(EDatabaseWriteTrasactionFlag _Flags = EDatabaseWriteTrasactionFlag_None);

	private:
		void fp_CloseDatabase(CDatabaseSubID _DatabaseID);
		void fp_CheckOpened();

		NStorage::TCUniquePointer<CDatabaseEnvironment_Internal> mp_pEnvironment;
	};
}

#include "Malterlib_Database_Database_Cursor.hpp"
#include "Malterlib_Database_Database_CursorDuplicateData.hpp"
#include "Malterlib_Database_Database_SubTransaction.hpp"

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase;
#endif
