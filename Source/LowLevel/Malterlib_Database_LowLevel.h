// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:
 
 Copyright 2011-2016 Howard Chu, Symas Corp. All rights reserved.
 
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

#pragma once

#include <Mib/Core/Core>

#if DMibPPtrBits < 64
#	define DMibDatabaseLowLevel_Always64BitSupport
#endif

namespace NMib
{
	namespace NDatabase
	{
#		ifdef DMibDatabaseLowLevel_Always64BitSupport
			using CLowLevelSize = uint64;
#		else
			using CLowLevelSize = mint;
#		endif

		/** @brief A handle for an individual database in the DB environment. */
		using CLowLevelDatabaseID = uint32;


		/** @brief Generic structure used for passing keys and data in and out
		 * of the database.
		 *
		 * Values returned from the database are valid only until a subsequent
		 * update operation, or the end of the transaction. Do not modify or
		 * free them, they commonly point into the database itself.
		 *
		 * Key sizes must be between 1 and #f_MaxKeySize() inclusive.
		 * The same applies to data sizes in databases with the #ELowLevelDatabaseFlag_SortedDuplicates flag.
		 * Other data items can in theory be from 0 to 0xffffffff bytes long.
		 */
		struct CLowLevelValue
		{
			mint m_Size = 0;				///< Size of the data item */
			uint8 const *m_pData = nullptr;	///< Address of the data item */
			
			bool operator < (CLowLevelValue const &_Right) const;
			bool operator == (CLowLevelValue const &_Right) const;
		};
		
		using FLowLevelCompare = NFunction::TCFunction<int (CLowLevelValue const &_Left, CLowLevelValue const &_Right), NFunction::CFunctionSupportCompareTag>;

		/// \enum Environment Flags
		enum ELowLevelEnvironmentFlag : uint32
		{
			ELowLevelEnvironmentFlag_None = 0
			, ELowLevelEnvironmentFlag_FixedMap = 0x01				///< Memory map at a fixed address (experimental)
			, ELowLevelEnvironmentFlag_NoSubdir = 0x4000			///< No environment directory 
			, ELowLevelEnvironmentFlag_NoSync = 0x10000				///< Don't fsync after commit  
			, ELowLevelEnvironmentFlag_ReadOnly = 0x20000			///< Read only  
			, ELowLevelEnvironmentFlag_NoMetaSync = 0x40000			///< Don't fsync metapage after commit  
			, ELowLevelEnvironmentFlag_WriteMap = 0x80000			///< Use writable mmap  
			, ELowLevelEnvironmentFlag_WriteMapAsync = 0x100000		///< Use asynchronous msync when #ELowLevelEnvironmentFlag_WriteMap is used  
			, ELowLevelEnvironmentFlag_NoTls = 0x200000				///< Tie reader locktable slots to #MDB_txn objects instead of to threads  
			, ELowLevelEnvironmentFlag_NoLock = 0x400000			///< Don't do any locking, caller must manage their own locks
			, ELowLevelEnvironmentFlag_NoReadAhead = 0x800000		///< Don't do readahead (no effect on Windows)
			, ELowLevelEnvironmentFlag_NoMemoryInit = 0x1000000		///< Don't initialize malloc'd memory before writing to datafile

			, ELowLevelEnvironmentFlag_Changable =
			(
				ELowLevelEnvironmentFlag_NoSync
				| ELowLevelEnvironmentFlag_NoMetaSync
				| ELowLevelEnvironmentFlag_WriteMapAsync
				| ELowLevelEnvironmentFlag_NoMemoryInit
			)
			, ELowLevelEnvironmentFlag_Changeless = 
			(
				ELowLevelEnvironmentFlag_FixedMap
				| ELowLevelEnvironmentFlag_NoSubdir
				| ELowLevelEnvironmentFlag_ReadOnly
				| ELowLevelEnvironmentFlag_WriteMap
				| ELowLevelEnvironmentFlag_NoTls
				| ELowLevelEnvironmentFlag_NoLock
				| ELowLevelEnvironmentFlag_NoReadAhead
			)
		};
		
		/// \enum Write transaction flags
		enum ELowLevelWriteTrasactionFlag
		{
			ELowLevelWriteTrasactionFlag_None
			, ELowLevelWriteTrasactionFlag_NoMetaSync = ELowLevelEnvironmentFlag_NoMetaSync	///< Flush system buffers but omit metadata flush when committing this transaction.
			, ELowLevelWriteTrasactionFlag_NoSync = ELowLevelEnvironmentFlag_NoSync			///< Don't flush system buffers to disk when committing this transaction.
		};

		///	\enum	f_OpenDatabase	Database Flags
		enum ELowLevelDatabaseFlag : uint32
		{
			ELowLevelDatabaseFlag_None = 0
			, ELowLevelDatabaseFlag_ReverseKeys = 0x02					///< Use reverse string keys
			, ELowLevelDatabaseFlag_SortedDuplicates = 0x04				///< Use sorted duplicates
			, ELowLevelDatabaseFlag_IntegerKeys = 0x08					///< Numeric keys in native byte order: either uint32 or mint. The keys must all be of the same size.
			, ELowLevelDatabaseFlag_SortedDuplicatesFixedSize = 0x10	///< With #ELowLevelDatabaseFlag_SortedDuplicates, sorted dup items have fixed size
			, ELowLevelDatabaseFlag_SortedDuplicatesIntegerKeys = 0x20	///< With #ELowLevelDatabaseFlag_SortedDuplicates, dups are #ELowLevelDatabaseFlag_IntegerKeys-style integers 
			, ELowLevelDatabaseFlag_SortedDuplicatesReverseKeys = 0x40	///< With #ELowLevelDatabaseFlag_SortedDuplicates, use reverse string dups
			, ELowLevelDatabaseFlag_Create = 0x40000					///< Create DB if not already existing
		};

		///	\enum Copy Flags
		enum ELowLevelEnvironmentCopyFlag
		{
			ELowLevelEnvironmentCopyFlag_None = 0 
			, ELowLevelEnvironmentCopyFlag_Compact = 0x01 ///< Compacting copy: Omit free space from copy, and renumber all pages sequentially. 
		};

		/** @brief Statistics for a database in the environment */
		struct CLowLevelStatistics
		{
			uint32 m_PageSize;				///< Size of a database page. This is currently the same for all databases.
			uint32 m_BTreeDepth;			///< Depth (height) of the B-tree
			CLowLevelSize m_nBranchPages;	///< Number of internal (non-leaf) pages
			CLowLevelSize m_nLeafPages;		///< Number of leaf pages
			CLowLevelSize m_nOverflowPages;	///< Number of overflow pages
			CLowLevelSize m_nDataItems;		///< Number of data items
		};

		/** @brief Information about the environment */
		struct CLowLevelEnvironmentInfo
		{
			void *m_pMapAddress;						///< Address of map, if fixed
			CLowLevelSize m_MapSize;					///< Size of the data memory map
			CLowLevelSize m_LastPageID;					///< ID of the last used page
			CLowLevelSize m_LastCommittedTransactionID;	///< ID of the last committed transaction
			uint32 m_MaxReaders;						///< max reader slots in the environment
			uint32 m_nReaders;							///< max reader slots used in the environment
		};

		struct CLowLevelReaderInfo
		{
			mint m_iReaderSlot;
			NStr::CStr m_Description;
		};

		struct CLowLevelEnvironment;
		struct CLowLevelEnvironment_Internal;
		struct CLowLevelDatabase;
		struct CLowLevelDatabaseReadTransaction;
		struct CLowLevelDatabaseWriteTransaction;
		
		struct CDatabaseProperties
		{
			FLowLevelCompare m_fKeyCompare;
			FLowLevelCompare m_fDataCompare;
		};
		
		struct CLowLevelTransactionCommon
		{
			struct CInternal;
			
			CLowLevelTransactionCommon();
			~CLowLevelTransactionCommon();
			CLowLevelTransactionCommon(CLowLevelTransactionCommon &&_Other);
			CLowLevelTransactionCommon &operator =(CLowLevelTransactionCommon &&_Other);
			
			CLowLevelSize f_TransactionID();
			
		protected:
			friend struct CLowLevelEnvironment_Internal;
			friend struct CLowLevelDatabase;
			friend struct CLowLevelEnvironment;
			friend struct CInternal;

			CLowLevelTransactionCommon(NPtr::TCUniquePointer<CInternal> &&_pTransaction);
			
			CLowLevelDatabase fp_OpenDatabase(ch8 const *_pName, ELowLevelDatabaseFlag _Flags, CDatabaseProperties const &_Properties);
			CInternal *fp_GetInternal();
			
			NPtr::TCUniquePointer<CInternal> mp_pTransaction;
		};
		
		struct CLowLevelReadTransaction;
		
		struct CLowLevelResetReadTransaction
		{
			CLowLevelReadTransaction f_Renew();
			
		private:
			friend struct CLowLevelReadTransaction;
			
			CLowLevelResetReadTransaction(NPtr::TCUniquePointer<CLowLevelTransactionCommon::CInternal> &&_pTransaction);
			
			NPtr::TCUniquePointer<CLowLevelTransactionCommon::CInternal> mp_pTransaction;
		};
		
		struct CLowLevelReadTransaction : protected CLowLevelTransactionCommon
		{
			CLowLevelReadTransaction();
			CLowLevelReadTransaction(CLowLevelReadTransaction &&_Other);
			CLowLevelReadTransaction &operator =(CLowLevelReadTransaction &&_Other);
			
			CLowLevelResetReadTransaction f_Reset();
			CLowLevelDatabaseReadTransaction f_OpenDatabaseTransaction
				(
					ch8 const *_pName = nullptr
					, ELowLevelDatabaseFlag _Flags = ELowLevelDatabaseFlag_None
					, CDatabaseProperties const &_Properties = CDatabaseProperties()
				)
			;
			
			NContainer::TCSet<NStr::CStr> f_EnumDatabases();
			
		private:
			friend struct CLowLevelResetReadTransaction;
			friend struct CLowLevelEnvironment;
			friend struct CLowLevelDatabase;
			
			CLowLevelReadTransaction(NPtr::TCUniquePointer<CInternal> &&_pTransaction);
			CLowLevelReadTransaction(CLowLevelTransactionCommon &&_Transaction);
		};
		
		struct CLowLevelWriteTransaction : protected CLowLevelTransactionCommon
		{
			CLowLevelWriteTransaction();
			CLowLevelWriteTransaction(CLowLevelWriteTransaction &&_Other);
			CLowLevelWriteTransaction &operator =(CLowLevelWriteTransaction &&_Other);
			
			void f_Commit();
			CLowLevelDatabase f_OpenDatabase
				(
					ch8 const *_pName = nullptr
					, ELowLevelDatabaseFlag _Flags = ELowLevelDatabaseFlag_Create
					, CDatabaseProperties const &_Properties = CDatabaseProperties()				 
				)
			;

			NContainer::TCSet<NStr::CStr> f_EnumDatabases();
			
			CLowLevelWriteTransaction f_BeginChildTransaction();
		private:
			friend struct CLowLevelEnvironment;
			friend struct CLowLevelDatabase;
			
			CLowLevelWriteTransaction(CLowLevelTransactionCommon &&_Transaction);
		};
		
		struct CLowLevelDatabase
		{
			CLowLevelDatabase();
			
			CLowLevelDatabaseReadTransaction f_Transaction(CLowLevelReadTransaction const &_Transaction);
			CLowLevelDatabaseWriteTransaction f_Transaction(CLowLevelWriteTransaction const &_Transaction);
			
		private:
			friend struct CLowLevelTransactionCommon;
			
			CLowLevelDatabase(CLowLevelDatabaseID _DatabaseID);
			
			CLowLevelDatabaseID mp_DatabaseID;
		};
		
		struct CLowLevelCursor;
		struct CLowLevelCursorWrite;
		struct CLowLevelCursorDuplicateData;
		struct CLowLevelCursorDuplicateDataWrite;
		
		struct CLowLevelDatabaseReadTransaction 
		{
			CLowLevelStatistics f_DatabaseStatistics();
			uint32 f_DatabaseFlags();

			void f_Get(CLowLevelValue *_pKey, CLowLevelValue *_pValue);
			int f_KeyCompare(CLowLevelValue const &_Left, CLowLevelValue const &_Right);
			int f_DataCompare(CLowLevelValue const &_Left, CLowLevelValue const &_Right);

			CLowLevelCursor f_ReadCursor();
			CLowLevelCursorDuplicateData f_ReadCursorDuplicateData();
			
		protected:
			friend struct CLowLevelDatabase;
			
			CLowLevelDatabaseReadTransaction(CLowLevelTransactionCommon::CInternal *_pTransaction, CLowLevelDatabaseID _DatabaseID);
							  
			CLowLevelTransactionCommon::CInternal *mp_pTransaction;
			CLowLevelDatabaseID mp_DatabaseID;
		};

		struct CLowLevelDatabaseWriteTransaction : public CLowLevelDatabaseReadTransaction
		{
			void f_DropDatabase(bool _bDelete);
			
			void f_Insert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend = false);
			CLowLevelValue f_Insert(CLowLevelValue const &_Key, mint _DataSize, bool _bAppend = false);
			
			void f_Upsert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend = false);
			CLowLevelValue f_Upsert(CLowLevelValue const &_Key, mint _DataSize, bool _bAppend = false);

			void f_Delete(CLowLevelValue const &_Key);
			void f_DeleteData(CLowLevelValue const &_Key, CLowLevelValue const &_Data); // Only for duplicate data databases

			CLowLevelCursorWrite f_WriteCursor();
			CLowLevelCursorDuplicateDataWrite f_WriteCursorDuplicateData();
			
		private:
			friend struct CLowLevelDatabase;
			
			CLowLevelDatabaseWriteTransaction(CLowLevelTransactionCommon::CInternal *_pTransaction, CLowLevelDatabaseID _DatabaseID);
		};
		
		struct CLowLevelEnvironment
		{
			CLowLevelEnvironment();
			~CLowLevelEnvironment();
			
			CLowLevelEnvironment(CLowLevelEnvironment &&_Other);
			CLowLevelEnvironment &operator =(CLowLevelEnvironment &&_Other);
			
			void f_Open(NStr::CStr const &_Path, ELowLevelEnvironmentFlag _Flags, NFile::EFileAttrib _FileAttributes);
			void f_Copy(NStr::CStr const &_Path, ELowLevelEnvironmentCopyFlag _Flags = ELowLevelEnvironmentCopyFlag_None);
			void f_Copy(void *_FileHandle, ELowLevelEnvironmentCopyFlag _Flags = ELowLevelEnvironmentCopyFlag_None);
			CLowLevelStatistics f_Statistics();
			CLowLevelEnvironmentInfo f_Info();
			void f_Sync(bool _bForce);
			void f_Close();
			void f_ChangeFlags(ELowLevelEnvironmentFlag _AddFlags, ELowLevelEnvironmentFlag _RemoveFlags);
			ELowLevelEnvironmentFlag f_GetFlags();
			NStr::CStr f_GetPath();
			void *f_GetFD();
			void f_SetMapSize(CLowLevelSize size);
			void f_SetMaxReaders(uint32 _nReaders);
			uint32 f_MaxReaders();
			void f_SetMaxDatabases(CLowLevelDatabaseID _DatabaseID);
			mint f_MaxKeySize();
			NContainer::TCVector<CLowLevelReaderInfo> f_GetReaderInfos();
			mint f_CheckForStaleReaders();
			CLowLevelReadTransaction f_BeginReadTransaction();
			CLowLevelWriteTransaction f_BeginWriteTransaction(ELowLevelWriteTrasactionFlag _Flags = ELowLevelWriteTrasactionFlag_None);
			
		private:
			void fp_CloseDatabase(CLowLevelDatabaseID _DatabaseID);
			void fp_CheckOpened();
			
			NPtr::TCUniquePointer<CLowLevelEnvironment_Internal> mp_pEnvironment;
		};

		struct CLowLevelCursorCommon
		{
			CLowLevelCursorCommon();
			~CLowLevelCursorCommon();
			CLowLevelCursorCommon(CLowLevelCursorCommon &&_Other);
			CLowLevelCursorCommon &operator =(CLowLevelCursorCommon &&_Other);
			
			struct CInternal;
			
			operator bool() const;
			
			CLowLevelValue const &f_Key() const;
			
			bool f_First();
			bool f_Last();
			
			bool f_FindEqual(CLowLevelValue const &_Key);
			bool f_FindLowerBound(CLowLevelValue const &_Key);
			
			void operator ++();
			void operator --();
			
		protected:
			
			CLowLevelCursorCommon(NPtr::TCUniquePointer<CInternal> &&_pInternal);
			
			void fp_SetToCurrent();
			
			CLowLevelValue mp_Key;
			CLowLevelValue mp_Value;
			
			NPtr::TCUniquePointer<CInternal> mp_pInternal;
			
			bool mp_bValid = false;
		};

		struct CLowLevelCursor : public CLowLevelCursorCommon 
		{
			CLowLevelValue const &f_Value() const;

		protected:
			friend struct CLowLevelDatabaseReadTransaction;
			
			CLowLevelCursor(NPtr::TCUniquePointer<CInternal> &&_pInternal);
		};
		
		struct CLowLevelCursorWrite : public CLowLevelCursor
		{
			void f_SetValue(CLowLevelValue const &_Value, bool _bAppend = false);
			CLowLevelValue f_SetValue(mint _DataSize, bool _bAppend = false); 
			
			void f_Insert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend = false);
			CLowLevelValue f_Insert(CLowLevelValue const &_Key, mint _DataSize, bool _bAppend = false);
			
			void f_Upsert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend = false);
			CLowLevelValue f_Upsert(CLowLevelValue const &_Key, mint _DataSize, bool _bAppend = false);
			
			void f_Delete();
			
		protected:
			friend struct CLowLevelDatabaseWriteTransaction;
			
			CLowLevelCursorWrite(NPtr::TCUniquePointer<CInternal> &&_pInternal);
		};		
		
		struct CLowLevelCursorDuplicateData : public CLowLevelCursorCommon
		{
			CLowLevelSize f_DataCount();
			bool f_NextData(); // Returns 'true' if new data is available
			bool f_PrevData(); 
			
			bool f_FirstData();
			bool f_LastData();
			
			bool f_FindEqual(CLowLevelValue const &_Key, CLowLevelValue const &_Data);
			bool f_FindLowerBound(CLowLevelValue const &_Key, CLowLevelValue const &_Data);

		protected:
			friend struct CLowLevelDatabaseReadTransaction;
			
			CLowLevelCursorDuplicateData(NPtr::TCUniquePointer<CInternal> &&_pInternal);
		};

		struct CLowLevelCursorDuplicateDataWrite : public CLowLevelCursorDuplicateData
		{
			void f_SetValue(CLowLevelValue const &_Value, bool _bAppend = false);
			void f_Insert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend = false);
			void f_Upsert(CLowLevelValue const &_Key, CLowLevelValue const &_Value, bool _bAppend = false);

			void f_DeleteKey();
			void f_DeleteData();
			
		protected:
			friend struct CLowLevelDatabaseWriteTransaction;
			
			CLowLevelCursorDuplicateDataWrite(NPtr::TCUniquePointer<CInternal> &&_pInternal);
		};
	}
}

#include "Malterlib_Database_LowLevel.hpp"

#ifndef DMibPNoShortCuts
using namespace NMib::NDatabase;
#endif
