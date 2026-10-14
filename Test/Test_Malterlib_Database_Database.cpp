// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/String/String>
#include <Mib/Database/Database>
#include <Mib/Database/DatabaseActor>
#include <Mib/Database/DatabaseValue>
#include <Mib/Test/Exception>

#include "Test_Malterlib_Database_Database.h"

using namespace NMib;
using namespace NMib::NStr;
using namespace NMib::NDatabase;
using namespace NMib::NFile;

namespace NMib::NDatabase
{
	CTestEnvironment::CTestEnvironment(CTestEnvironment &&_Other) = default;
	CTestEnvironment &CTestEnvironment::operator = (CTestEnvironment &&_Other) = default;

	CTestEnvironment::CTestEnvironment()
	{
	}

	CTestEnvironment::~CTestEnvironment()
	{
		EDatabaseEnvironmentFlag Flags = EDatabaseEnvironmentFlag_None;
		if (m_Environment.f_IsOpen())
		{
			Flags = m_Environment.f_GetFlags();
			m_Environment.f_Close();
		}

		if (!(Flags & EDatabaseEnvironmentFlag_ReadOnly))
		{
			try
			{
				if (CFile::fs_FileExists(m_DatabaseDirectory))
					CFile::fs_DeleteDirectoryRecursive(m_DatabaseDirectory, true);
				if (CFile::fs_FileExists(m_DatabaseDirectory + ".lock"))
					CFile::fs_DeleteDirectoryRecursive(m_DatabaseDirectory + ".lock", true);
			}
			catch (NException::CException const &_Exception)
			{
				DMibConOut("Exception deleting test database: {}\n", _Exception);
			}
		}
	}

	CTestEnvironment fg_GetTestEnvironment(CStr const &_FileName, bool _bReset, EDatabaseEnvironmentFlag _OpenFlags, uint64 _MapSize)
	{
		CTestEnvironment Environment;

		Environment.m_FileName = _FileName;
		Environment.m_DatabaseDirectory = CFile::fs_AppendPath(CFile::fs_GetProgramDirectory(), _FileName);
		if (_bReset)
		{
			if (_OpenFlags & EDatabaseEnvironmentFlag_NoSubdir)
			{
				if (CFile::fs_FileExists(Environment.m_DatabaseDirectory))
					CFile::fs_DeleteDirectoryRecursive(Environment.m_DatabaseDirectory, true);
				if (CFile::fs_FileExists(Environment.m_DatabaseDirectory + ".lock"))
					CFile::fs_DeleteDirectoryRecursive(Environment.m_DatabaseDirectory + ".lock", true);
			}
			else
			{
				if (CFile::fs_FileExists(Environment.m_DatabaseDirectory))
					CFile::fs_DeleteDirectoryRecursive(Environment.m_DatabaseDirectory, true);
				CFile::fs_CreateDirectory(Environment.m_DatabaseDirectory);
			}
		}

		Environment.m_Environment.f_SetMaxDatabases(32);
		if (_MapSize)
			Environment.m_Environment.f_SetMapSize(_MapSize);
		Environment.m_Environment.f_Open(Environment.m_DatabaseDirectory, _OpenFlags, EFileAttrib_None);

		return fg_Move(Environment);
	}
}

namespace
{
	class CDatabase_Tests : public NMib::NTest::CTest
	{
	public:
		void fp_PopulateEnvironment(CDatabaseEnvironment &_Environment, umint _nDatabases = 16, umint _nValuesPerDatabase = 4 * 1024)
		{
			_Environment.f_SetMapSize(_nDatabases * _nValuesPerDatabase * 256 * fg_Max(NSys::fg_Mem_PageSize() / 4096u, 1u));
			auto Transaction = _Environment.f_BeginWriteTransaction();
			for (uint32 iDatabase = 0; iDatabase < _nDatabases; ++iDatabase)
			{
				auto Database = Transaction.f_OpenDatabase(fg_Format("Database{sl3,sf0}", iDatabase));
				for (uint32 iValue = 0; iValue < _nValuesPerDatabase; ++iValue)
				{
					auto DatabaseTransaction = Database.f_Transaction(Transaction);
					DatabaseTransaction.f_Insert
						(
							NStorage::fg_TupleReferences("TestKey", iDatabase, iValue)
							, NStorage::fg_TupleReferences("TestValue", iDatabase, iValue, iValue, iValue, iValue)
						)
					;
				}
			}
			Transaction.f_Commit();
		}

		void fp_PopulateEnvironmentSingleDB(CDatabaseEnvironment &_Environment, umint _nValuesPerDatabase = 4 * 1024)
		{
			_Environment.f_SetMapSize(_nValuesPerDatabase * 256 * fg_Max(NSys::fg_Mem_PageSize() / 4096u, 1u));
			auto Transaction = _Environment.f_BeginWriteTransaction();
			auto Database = Transaction.f_OpenDatabase();
			for (uint32 iValue = 0; iValue < _nValuesPerDatabase; ++iValue)
			{
				auto DatabaseTransaction = Database.f_Transaction(Transaction);
				DatabaseTransaction.f_Insert(NStorage::fg_TupleReferences("TestKey", iValue), NStorage::fg_TupleReferences("TestValue", iValue, iValue, iValue, iValue));
			}
			Transaction.f_Commit();
		}

		void fp_DeleteEverySecond(CDatabaseEnvironment &_Environment)
		{
			auto Transaction = _Environment.f_BeginWriteTransaction();
			auto Databases = Transaction.f_EnumDatabases();

			for (auto &DatabaseName : Databases)
			{
				auto Database = Transaction.f_OpenDatabase(DatabaseName);
				auto DatabaseTransaction = Database.f_Transaction(Transaction);
				auto iCursor = DatabaseTransaction.f_WriteCursor();
				for (iCursor.f_First(); iCursor; ++iCursor)
				{
					++iCursor;
					if (!iCursor)
						break;
					auto Decoded = iCursor.f_KeyTuple<CStr, uint32, uint32>();
					iCursor.f_Delete();
					if (iCursor)
						Decoded = iCursor.f_KeyTuple<CStr, uint32, uint32>();
				}
			}
			Transaction.f_Commit();
		}

		bool fp_EnvironmentsSame(CDatabaseEnvironment &_Environment0, CDatabaseEnvironment &_Environment1)
		{
			auto TransactionLeft = _Environment0.f_BeginReadTransaction();
			auto TransactionRight = _Environment1.f_BeginReadTransaction();

			auto LeftDatabases = TransactionLeft.f_EnumDatabases();
			auto ReightDatabases = TransactionRight.f_EnumDatabases();

			if (LeftDatabases != ReightDatabases)
				return false;

			for (auto &DatabaseName : LeftDatabases)
			{
				auto LeftDatabase = TransactionLeft.f_OpenDatabaseTransaction(DatabaseName);
				auto iLeftCursor = LeftDatabase.f_ReadCursor();
				iLeftCursor.f_First();

				auto RightDatabase = TransactionRight.f_OpenDatabaseTransaction(DatabaseName);
				auto iRightCursor = RightDatabase.f_ReadCursor();
				iRightCursor.f_First();

				while (iLeftCursor && iRightCursor)
				{
					auto &LeftKey = iLeftCursor.f_Key();
					auto &RightKey = iRightCursor.f_Key();
					if (LeftKey != RightKey)
						return false;

					auto &LeftValue = iLeftCursor.f_Value();
					auto &RightValue = iRightCursor.f_Value();
					if (LeftValue != RightValue)
						return false;

					++iLeftCursor;
					++iRightCursor;
				}
				if (!!iLeftCursor != !!iRightCursor)
					return false;
			}
			return true;
		}

		void fp_DoCopy(CStr const &_Name, EDatabaseEnvironmentCopyFlag _Flags, CTestEnvironment &_Environment)
		{
			if constexpr (sizeof(void *) < 8)
				_Flags |= EDatabaseEnvironmentCopyFlag_Compact;

			DMibTestPath(_Name);
			CStr CopyPath = _Environment.m_DatabaseDirectory + _Name;
			if (CFile::fs_FileExists(CopyPath))
				CFile::fs_DeleteDirectoryRecursive(CopyPath);
			CFile::fs_CreateDirectory(CopyPath);
			_Environment.m_Environment.f_Copy(CopyPath, _Flags);

			DMibExpectExceptionType(_Environment.m_Environment.f_Copy(CopyPath), NException::CException);

			auto EnvironmentCopy = fg_GetTestEnvironment(_Environment.m_FileName + _Name, false);

			DMibExpectTrue(fp_EnvironmentsSame(_Environment.m_Environment, EnvironmentCopy.m_Environment));
		}

		void f_DoTests()
		{
			DMibTestSuite("Basics")
			{
				auto Environment = fg_GetTestEnvironment("TestDatabase0");

				{
					auto WriteTransaction = Environment.m_Environment.f_BeginWriteTransaction();
					auto MainDatabase = WriteTransaction.f_OpenDatabase();
					auto DatabaseTransaction = MainDatabase.f_Transaction(WriteTransaction);
					DatabaseTransaction.f_Insert("TestKey", "TestValue");
					auto iData = DatabaseTransaction.f_ReadCursor();
					iData.f_First();
					{
						DMibTestPath("Found");
						DMibExpectTrue(iData);
						++iData;
						DMibExpectFalse(iData);
					}
					{
						DMibTestPath("Not found");
						DMibExpectFalse(iData);
						++iData;
					}
				}
			};
			DMibTestSuite("Write Transaction")
			{
				auto Environment = fg_GetTestEnvironment("TestDatabase15");

				{
					{
						auto WriteTransaction = Environment.m_Environment.f_BeginWriteTransaction();
						{
							auto MainDatabase = WriteTransaction.f_OpenDatabase();
							auto DatabaseTransaction = MainDatabase.f_Transaction(WriteTransaction);
							DatabaseTransaction.f_Insert("TestKey", "TestValue");
						}
						WriteTransaction.f_Commit();
					}

					{
						auto ReadTransaction = Environment.m_Environment.f_BeginReadTransaction();
						auto DatabaseTransaction = ReadTransaction.f_OpenDatabaseTransaction();
						auto iData = DatabaseTransaction.f_ReadCursor();
						iData.f_First();
						{
							DMibTestPath("Found");
							DMibExpectTrue(iData);
							++iData;
							DMibExpectFalse(iData);
						}
						{
							DMibTestPath("Not found");
							DMibExpectFalse(iData);
							++iData;
						}
					}
				}
			};
			DMibTestSuite("Child Write Transaction")
			{
				auto Environment = fg_GetTestEnvironment("TestDatabase14", true, EDatabaseEnvironmentFlag_None, 128 * 1024 * 1024);

				{
					{
						auto WriteTransaction = Environment.m_Environment.f_BeginWriteTransaction();
						{
							auto MainDatabase = WriteTransaction.f_OpenDatabase();
							auto DatabaseTransaction = MainDatabase.f_Transaction(WriteTransaction);
							DatabaseTransaction.f_Insert("TestKey", "TestValue");
						}
						{
							auto ChildTransaction = WriteTransaction.f_BeginChildTransaction();
							DMibExpectException(WriteTransaction.f_BeginChildTransaction(), DMibErrorInstance("Transtion has a child"));
							auto MainDatabase = ChildTransaction.f_OpenDatabase();
							auto DatabaseTransaction = MainDatabase.f_Transaction(ChildTransaction);
							DatabaseTransaction.f_Insert("TestKey2", "TestValue2");
							DMibExpectException(WriteTransaction.f_Commit(), DMibErrorInstance("Transaction still has a child transaction"));
							ChildTransaction.f_Commit();
						}
						WriteTransaction.f_Commit();
					}

					{
						auto ReadTransaction = Environment.m_Environment.f_BeginReadTransaction();
						auto DatabaseTransaction = ReadTransaction.f_OpenDatabaseTransaction();
						auto iData = DatabaseTransaction.f_ReadCursor();
						iData.f_First();
						{
							DMibTestPath("Found");
							DMibExpectTrue(iData && "1");
							++iData;
							DMibExpectTrue(iData && "2");
							++iData;
							DMibExpectFalse(iData);
						}
						{
							DMibTestPath("Not found");
							DMibExpectFalse(iData);
							++iData;
						}
					}
				}
			};
			DMibTestCategory("Environment")
			{
				DMibTestSuite("Copy")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase1", true, EDatabaseEnvironmentFlag_None);
					fp_PopulateEnvironment(Environment.m_Environment);
					fp_DoCopy("Copy", EDatabaseEnvironmentCopyFlag_None, Environment);
					fp_DeleteEverySecond(Environment.m_Environment);
					fp_DoCopy("CopyCompact", EDatabaseEnvironmentCopyFlag_Compact, Environment);
				};
				DMibTestSuite("Stats")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase2");
					fp_PopulateEnvironment(Environment.m_Environment, 16, 128);
					DMibExpect(Environment.m_Environment.f_Statistics().m_nDataItems, ==, 16);

					auto Transaction = Environment.m_Environment.f_BeginReadTransaction();
					DMibExpect(Transaction.f_OpenDatabaseTransaction().f_DatabaseStatistics().m_nDataItems, ==, 16);
					for (auto &Database : Transaction.f_EnumDatabases())
						DMibExpect(Transaction.f_OpenDatabaseTransaction(Database).f_DatabaseStatistics().m_nDataItems, ==, 128)(ETestFlag_Aggregated);
				};
				DMibTestSuite("Info")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase3");
					auto Info = Environment.m_Environment.f_Info();
					DMibExpect(Info.m_LastCommittedTransactionID, ==, 0);
					fp_PopulateEnvironment(Environment.m_Environment, 16, 128);
					Info = Environment.m_Environment.f_Info();
					DMibExpect(Info.m_LastCommittedTransactionID, ==, 1);
					DMibExpect(Environment.m_Environment.f_GetPath(), ==, CFile::fs_GetProgramDirectory() + "/TestDatabase3");
					DMibExpect(Environment.m_Environment.f_GetFD(), !=, nullptr);

					auto ReadOnly0 = fg_GetTestEnvironment("TestDatabase3", false, EDatabaseEnvironmentFlag_ReadOnly | EDatabaseEnvironmentFlag_NoTls);
					auto ReadOnly1 = fg_GetTestEnvironment("TestDatabase3", false, EDatabaseEnvironmentFlag_ReadOnly | EDatabaseEnvironmentFlag_NoTls);
					auto ReadTransaciton00 = ReadOnly0.m_Environment.f_BeginReadTransaction();
					auto ReadTransaciton01 = ReadOnly0.m_Environment.f_BeginReadTransaction();
					auto ReadTransaciton10 = ReadOnly1.m_Environment.f_BeginReadTransaction();
					auto ReadTransaciton11 = ReadOnly1.m_Environment.f_BeginReadTransaction();
					auto ReaderInfos = Environment.m_Environment.f_GetReaderInfos();

					DMibExpect(ReaderInfos.f_GetLen(), ==, 4);
				};
				DMibTestSuite("Sync")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase4");
					fp_PopulateEnvironment(Environment.m_Environment, 16, 128);
					Environment.m_Environment.f_Sync(true);
				};
				DMibTestSuite("Close")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase5");
					Environment.m_Environment.f_Close();
					DMibExpectException(fp_PopulateEnvironment(Environment.m_Environment, 16, 128), DMibErrorInstance("Environment has not been opened"));
				};
				DMibTestSuite("Change Flags")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase6");
					Environment.m_Environment.f_ChangeFlags(EDatabaseEnvironmentFlag_Changable, EDatabaseEnvironmentFlag_Changable);
					DMibExpect(Environment.m_Environment.f_GetFlags(), ==, EDatabaseEnvironmentFlag_Changable);
					DMibExpectException
						(
							Environment.m_Environment.f_ChangeFlags(EDatabaseEnvironmentFlag_Changeless, EDatabaseEnvironmentFlag_Changeless)
							, DMibErrorInstance("Trying to change non-changeable flag")
						)
					;
				};
				DMibTestSuite("Flags-NoSubdir")
				{
					fg_GetTestEnvironment("TestDatabase7", true, EDatabaseEnvironmentFlag_NoSubdir);
				};
				DMibTestSuite("Flags-ReadOnly")
				{
					auto Writable = fg_GetTestEnvironment("TestDatabase8", true, EDatabaseEnvironmentFlag_None);
					fp_PopulateEnvironment(Writable.m_Environment);
					auto ReadOnly = fg_GetTestEnvironment("TestDatabase8", false, EDatabaseEnvironmentFlag_ReadOnly);
					DMibExpectTrue(fp_EnvironmentsSame(Writable.m_Environment, ReadOnly.m_Environment));

					DMibExpectException(ReadOnly.m_Environment.f_BeginWriteTransaction(), DMibErrorInstance("Cannot open write transaction in read only environment"));
				};
				DMibTestSuite("Flags-WriteMap")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase9", true, EDatabaseEnvironmentFlag_WriteMap);
					fp_PopulateEnvironment(Environment.m_Environment);
					fp_DoCopy("Copy", EDatabaseEnvironmentCopyFlag_None, Environment);
				};
				DMibTestSuite("Tls")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase10", true, EDatabaseEnvironmentFlag_NoTls);
					fp_PopulateEnvironment(Environment.m_Environment);
					fp_DoCopy("Copy", EDatabaseEnvironmentCopyFlag_None, Environment);
				};
				DMibTestSuite("Lock")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase11", true, EDatabaseEnvironmentFlag_NoLock);
					fp_PopulateEnvironment(Environment.m_Environment);
					fp_DoCopy("Copy", EDatabaseEnvironmentCopyFlag_None, Environment);
				};
				DMibTestSuite("ReadAhead")
				{
					auto Environment = fg_GetTestEnvironment("TestDatabase12", true, EDatabaseEnvironmentFlag_NoReadAhead);
					fp_PopulateEnvironment(Environment.m_Environment);
					fp_DoCopy("Copy", EDatabaseEnvironmentCopyFlag_None, Environment);
				};

				DMibTestSuite("SetMapSize")
				{
					CTestEnvironment TestEnvironment;
					TestEnvironment.m_FileName = "TestDatabase_MapSize";
					TestEnvironment.m_DatabaseDirectory = CFile::fs_GetProgramDirectory() / TestEnvironment.m_FileName;
					if (CFile::fs_FileExists(TestEnvironment.m_DatabaseDirectory))
						CFile::fs_DeleteDirectoryRecursive(TestEnvironment.m_DatabaseDirectory, true);
					if (CFile::fs_FileExists(TestEnvironment.m_DatabaseDirectory + ".lock"))
						CFile::fs_DeleteDirectoryRecursive(TestEnvironment.m_DatabaseDirectory + ".lock", true);
					CFile::fs_CreateDirectory(TestEnvironment.m_DatabaseDirectory);

					CDatabaseOffset const MapSize = 16u * 1024u * 1024u; // 16MB
					TestEnvironment.m_Environment.f_SetMapSize(MapSize);
					TestEnvironment.m_Environment.f_Open(TestEnvironment.m_DatabaseDirectory, EDatabaseEnvironmentFlag_None, EFileAttrib_None);

					auto EnvironmentInfo = TestEnvironment.m_Environment.f_Info();
					DMibExpect(EnvironmentInfo.m_MapSize, ==, MapSize);

					{
						// Cannot change mapsize while a write transaction is open
						auto WriteTransaction = TestEnvironment.m_Environment.f_BeginWriteTransaction();
						DMibExpectException(TestEnvironment.m_Environment.f_SetMapSize(MapSize * 2), DMibErrorInstance("Cannot set map size when write transaction is open"));
						WriteTransaction.f_Commit();
					}
				};

				DMibTestSuite("MaxReaders")
				{
					CTestEnvironment TestEnvironment;
					TestEnvironment.m_FileName = "TestDatabase_MaxReaders";
					TestEnvironment.m_DatabaseDirectory = CFile::fs_GetProgramDirectory() / TestEnvironment.m_FileName;
					if (CFile::fs_FileExists(TestEnvironment.m_DatabaseDirectory))
						CFile::fs_DeleteDirectoryRecursive(TestEnvironment.m_DatabaseDirectory, true);
					if (CFile::fs_FileExists(TestEnvironment.m_DatabaseDirectory + ".lock"))
						CFile::fs_DeleteDirectoryRecursive(TestEnvironment.m_DatabaseDirectory + ".lock", true);
					CFile::fs_CreateDirectory(TestEnvironment.m_DatabaseDirectory);

					DMibExpectException(TestEnvironment.m_Environment.f_SetMaxReaders(0), DMibErrorInstance("A minimum of 1 reader is required"));
					TestEnvironment.m_Environment.f_SetMaxReaders(64);
					TestEnvironment.m_Environment.f_Open(TestEnvironment.m_DatabaseDirectory, EDatabaseEnvironmentFlag_None, EFileAttrib_None);

					DMibExpect(TestEnvironment.m_Environment.f_MaxReaders(), ==, 64u);

					DMibExpectException(TestEnvironment.m_Environment.f_SetMaxReaders(32), DMibErrorInstance("Max number of readers can only be set before environment has been opened"));
				};

				DMibTestSuite("MaxDatabases")
				{
					CTestEnvironment TestEnvironment;
					TestEnvironment.m_FileName = "TestDatabase_MaxDatabases";
					TestEnvironment.m_DatabaseDirectory = CFile::fs_GetProgramDirectory() / TestEnvironment.m_FileName;
					if (CFile::fs_FileExists(TestEnvironment.m_DatabaseDirectory))
						CFile::fs_DeleteDirectoryRecursive(TestEnvironment.m_DatabaseDirectory, true);
					if (CFile::fs_FileExists(TestEnvironment.m_DatabaseDirectory + ".lock"))
						CFile::fs_DeleteDirectoryRecursive(TestEnvironment.m_DatabaseDirectory + ".lock", true);
					CFile::fs_CreateDirectory(TestEnvironment.m_DatabaseDirectory);

					TestEnvironment.m_Environment.f_SetMaxDatabases(2);
					TestEnvironment.m_Environment.f_Open(TestEnvironment.m_DatabaseDirectory, EDatabaseEnvironmentFlag_None, EFileAttrib_None);

					{
						auto WriteTransaction = TestEnvironment.m_Environment.f_BeginWriteTransaction();
						{
							WriteTransaction.f_OpenDatabase("DB0");
							WriteTransaction.f_OpenDatabase("DB1");
							DMibExpectException(WriteTransaction.f_OpenDatabase("DB2"), DMibErrorInstance("Out of database IDs"));
						}
						WriteTransaction.f_Commit();
					}
				};

				DMibTestSuite("MaxKeySize")
				{
					auto TestEnvironment = fg_GetTestEnvironment("TestDatabase_MaxKeySize", true);
					DMibExpect(TestEnvironment.m_Environment.f_MaxKeySize(), >, 0u);
				};

				DMibTestSuite("CheckForStaleReaders")
				{
					auto TestEnvironment = fg_GetTestEnvironment("TestDatabase_StaleReaders", true);
					umint ClearedReaderCount = TestEnvironment.m_Environment.f_CheckForStaleReaders();
					DMibExpect(ClearedReaderCount, >=, 0u);
				};

				DMibTestSuite("BeginTransactions")
				{
					auto TestEnvironment = fg_GetTestEnvironment("TestDatabase_BeginTransactions", true);
					{
						auto ReadTransaction = TestEnvironment.m_Environment.f_BeginReadTransaction();
						auto DatabaseSubTransaction = ReadTransaction.f_OpenDatabaseTransaction();
						auto ReadCursor = DatabaseSubTransaction.f_ReadCursor();
						ReadCursor.f_First();
						DMibExpectFalse(ReadCursor);
					}

					{
						auto WriteTransaction = TestEnvironment.m_Environment.f_BeginWriteTransaction(EDatabaseWriteTrasactionFlag_None);
						auto Database = WriteTransaction.f_OpenDatabase();
						auto DatabaseTransaction = Database.f_Transaction(WriteTransaction);
						DatabaseTransaction.f_Insert("Key", "Value");
						WriteTransaction.f_Commit();

						// Verify write persisted
						{
							auto ReadBackTransaction = TestEnvironment.m_Environment.f_BeginReadTransaction();
							auto DatabaseSubTransactionVerify = ReadBackTransaction.f_OpenDatabaseTransaction();
							auto ReadCursorVerify = DatabaseSubTransactionVerify.f_ReadCursor();
							DMibExpectTrue(ReadCursorVerify.f_FindEqual("Key"));
							DMibExpect(ReadCursorVerify.f_Value<NStr::CStr>(), ==, NStr::CStr("Value"));
						}
					}

					{
						auto WriteTransaction = TestEnvironment.m_Environment.f_BeginWriteTransaction(EDatabaseWriteTrasactionFlag_NoSync);
						auto Database = WriteTransaction.f_OpenDatabase();
						auto DatabaseTransaction = Database.f_Transaction(WriteTransaction);
						DatabaseTransaction.f_Insert("Key2", "Value2");
						WriteTransaction.f_Commit();

						// Verify write persisted with NoSync
						{
							auto ReadBackTransaction = TestEnvironment.m_Environment.f_BeginReadTransaction();
							auto DatabaseSubTransactionVerify = ReadBackTransaction.f_OpenDatabaseTransaction();
							auto ReadCursorVerify = DatabaseSubTransactionVerify.f_ReadCursor();
							DMibExpectTrue(ReadCursorVerify.f_FindEqual("Key2"));
							DMibExpect(ReadCursorVerify.f_Value<NStr::CStr>(), ==, NStr::CStr("Value2"));
						}
					}

					{
						auto WriteTransaction = TestEnvironment.m_Environment.f_BeginWriteTransaction(EDatabaseWriteTrasactionFlag_NoMetaSync);
						auto Database = WriteTransaction.f_OpenDatabase();
						auto DatabaseTransaction = Database.f_Transaction(WriteTransaction);
						DatabaseTransaction.f_Insert("Key3", "Value3");
						WriteTransaction.f_Commit();

						// Verify write persisted with NoMetaSync
						{
							auto ReadBackTransaction = TestEnvironment.m_Environment.f_BeginReadTransaction();
							auto DatabaseSubTransactionVerify = ReadBackTransaction.f_OpenDatabaseTransaction();
							auto ReadCursorVerify = DatabaseSubTransactionVerify.f_ReadCursor();
							DMibExpectTrue(ReadCursorVerify.f_FindEqual("Key3"));
							DMibExpect(ReadCursorVerify.f_Value<NStr::CStr>(), ==, NStr::CStr("Value3"));
						}
					}
				};
			};
			DMibTestSuite("Actor Compact") -> NConcurrency::TCFuture<void>
			{
				auto TestEnvironment = fg_GetTestEnvironment("TestDatabase13", true, EDatabaseEnvironmentFlag_NoReadAhead);
				fp_PopulateEnvironmentSingleDB(TestEnvironment.m_Environment);
				TestEnvironment.m_Environment.f_Close();

				NConcurrency::TCActor<CDatabaseActor> DatabaseActor{fg_Construct()};

				{
					co_await DatabaseActor(&CDatabaseActor::f_OpenDatabase, CFile::fs_GetProgramDirectory() / "TestDatabase13", 0);

					auto WriteTrasaction = co_await DatabaseActor(&CDatabaseActor::f_OpenTransactionWrite);

					umint nStartEntries = 0;
					{
						auto iData = WriteTrasaction.m_Transaction.f_ReadCursor();
						iData.f_First();
						while (iData)
						{
							++nStartEntries;
							++iData;
						}
					}
					{
						auto iData = WriteTrasaction.m_Transaction.f_WriteCursor();
						iData.f_First();
						iData.f_Delete();
					}

					co_await DatabaseActor(&CDatabaseActor::f_Compact, fg_Move(WriteTrasaction), 0);

					WriteTrasaction = co_await DatabaseActor(&CDatabaseActor::f_OpenTransactionWrite);
					umint nEndEntries = 0;
					{
						auto iData = WriteTrasaction.m_Transaction.f_ReadCursor();
						iData.f_First();
						while (iData)
						{
							++nEndEntries;
							++iData;
						}
					}

					DMibExpect(nEndEntries, ==, nStartEntries - 1);
				}

				co_await fg_Move(DatabaseActor).f_Destroy();

				co_return {};
			};
		}
	};

	DMibTestRegister(CDatabase_Tests, Malterlib::Database);
}
