// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Database/ResultSet>
#include <Mib/Database/ResultSetBuilder>

using namespace NMib;
using namespace NMib::NStr;
using namespace NMib::NDatabase;

namespace
{
	class CResultSet_Tests : public NMib::NTest::CTest
	{
	public:

		void f_PopulateBuilder(CResultSetBuilder& _Builder)
		{
			CResultSetDescription Desc;

			// Specify a result set with two key parts and one column.
			Desc
				.f_KeyPart(EResultDataType_uint64, 0)
				.f_KeyPart(EResultDataType_fp64, 0)
				.f_Column(EResultDataType_CStr)
				.f_Column(EResultDataType_CStr)
				.f_Column(EResultDataType_fp64)
				.f_Column(EResultDataType_Binary);


			// Add two rows to the result set
			_Builder.f_SetDescription(Desc);

			NContainer::CByteVector lBinData;
			lBinData.f_Insert(100);

			_Builder.f_AddRow()
				.f_KeyPart(_Builder.f_AddKeyPartVariantValue(0, uint64(1)))
				.f_KeyPart(_Builder.f_AddKeyPartVariantValue(1, fp64(2.5)))
				.f_Column("Hello")
				.f_Column(nullptr)
				.f_Column(fp64(1.0 / 3.0))
				.f_Column(lBinData);

			lBinData[0] = 200;

			_Builder.f_AddRow()
				.f_KeyPart(_Builder.f_AddKeyPartVariantValue(0, uint64(2)))
				.f_KeyPart(_Builder.f_AddKeyPartVariantValue(1, fp64(5.9)))
				.f_Column(", World!")
				.f_Column(nullptr)
				.f_Column(fp64(2.0 / 3.0))
				.f_Column(lBinData);


			// Add result set metadata.
			NMib::NContainer::CByteVector Metadata;

			Metadata.f_Insert(1);
			_Builder.f_AddMetadata(1, Metadata);
			Metadata[0] = 2;
			_Builder.f_AddMetadata(2, Metadata);

			// Add key part metadata.
			Metadata[0] = 3;
			_Builder.f_SetKeyPartMetadata(0, Metadata);

			Metadata[0] = 4;
			_Builder.f_SetKeyPartMetadata(1, Metadata);

			// Add column metadata.
			Metadata[0] = 5;
			_Builder.f_SetColumnMetadata(0, Metadata);
		}


		void f_PopulateBuilderColumnWise(CResultSetBuilder& _Builder)
		{
			CResultSetDescription Desc;

			// Specify a result set with two key parts and one column.
			Desc
				.f_KeyPart(EResultDataType_uint64, 0)
				.f_KeyPart(EResultDataType_fp64, 0)
				.f_Column(EResultDataType_CStr)
				.f_Column(EResultDataType_CStr)
				.f_Column(EResultDataType_fp64)
				.f_Column(EResultDataType_Binary);

			_Builder.f_SetDescription(Desc);

			// Add two rows to the result set

			{
				auto KeyPart0 = _Builder.f_KeyPart(0);
				auto KeyPart1 = _Builder.f_KeyPart(1);
				auto Column0 = _Builder.f_Column(0);
				auto Column1 = _Builder.f_Column(1);
				auto Column2 = _Builder.f_Column(2);
				auto Column3 = _Builder.f_Column(3);


				KeyPart0.f_AddValue(_Builder.f_AddKeyPartVariantValue(0, uint64(1)));
				KeyPart0.f_AddValue(_Builder.f_AddKeyPartVariantValue(0, uint64(2)));

				KeyPart1.f_AddValue(_Builder.f_AddKeyPartVariantValue(1, fp64(2.5)));
				KeyPart1.f_AddValue(_Builder.f_AddKeyPartVariantValue(1, fp64(5.9)));

				Column0.f_AddValue(CStr("Hello"));
				Column0.f_AddValue(CStr(", World!"));

				Column1.f_AddValue(nullptr);
				Column1.f_AddValue(nullptr);

				Column2.f_AddValue(fp64(1.0 / 3.0));
				Column2.f_AddValue(fp64(2.0 / 3.0));

				NContainer::CByteVector lBinData;
				lBinData.f_Insert(100);

				Column3.f_AddValue(lBinData);
				lBinData[0] = 200;
				Column3.f_AddValue(lBinData);
			}

			// Add result set metadata.
			NMib::NContainer::CByteVector Metadata;

			Metadata.f_Insert(1);
			_Builder.f_AddMetadata(1, Metadata);
			Metadata[0] = 2;
			_Builder.f_AddMetadata(2, Metadata);

			// Add key part metadata.
			Metadata[0] = 3;
			_Builder.f_SetKeyPartMetadata(0, Metadata);

			Metadata[0] = 4;
			_Builder.f_SetKeyPartMetadata(1, Metadata);

			// Add column metadata.
			Metadata[0] = 5;
			_Builder.f_SetColumnMetadata(0, Metadata);
		}

		void f_TestResultSet(CResultSet const& _RS)
		{
			DMibTest(DMibExpr(false) == DMibExpr(_RS.f_IsEmpty()));
			DMibTest(DMibExpr(2) == DMibExpr(_RS.f_GetNumRows()));
			
			DMibTest(DMibExpr(2) == DMibExpr(_RS.f_GetNumKeyParts()));
			DMibTest(DMibExpr(EResultDataType_uint64) == DMibExpr(_RS.f_GetKeyPartType(0)));
			DMibTest(DMibExpr(EResultDataType_fp64) == DMibExpr(_RS.f_GetKeyPartType(1)));
			
			DMibTest(DMibExpr(4) == DMibExpr(_RS.f_GetNumColumns()));
			DMibTest(DMibExpr(EResultDataType_CStr) == DMibExpr(_RS.f_GetColumnType(0)));
			DMibTest(DMibExpr(EResultDataType_CStr) == DMibExpr(_RS.f_GetColumnType(1)));
			DMibTest(DMibExpr(EResultDataType_fp64) == DMibExpr(_RS.f_GetColumnType(2)));
			DMibTest(DMibExpr(EResultDataType_Binary) == DMibExpr(_RS.f_GetColumnType(3)));


			{ // Check we can get the result set metadata OK, and that lookup by ID works.
				DMibTestPath("Check Metadata 0");
				NMib::NContainer::CByteVector const* pMetadata = nullptr;

				pMetadata = _RS.f_GetMetadata(0);
				DMibTest(DMibExpr(nullptr) == DMibExpr(pMetadata));
			}

			{
				DMibTestPath("Check Metadata 1");
				NMib::NContainer::CByteVector const* pMetadata = nullptr;
				pMetadata = _RS.f_GetMetadata(1);
				DMibTest(DMibExpr(nullptr) != DMibExpr(pMetadata));
				DMibTest(DMibExpr(1) == DMibExpr(pMetadata->f_GetLen()));
				DMibTest(DMibExpr(1) == DMibExpr(pMetadata->operator[](0)));
			}

			{
				DMibTestPath("Check Metadata 2");
				NMib::NContainer::CByteVector const* pMetadata = nullptr;
				pMetadata = _RS.f_GetMetadata(2);
				DMibTest(DMibExpr(nullptr) != DMibExpr(pMetadata));
				DMibTest(DMibExpr(1) == DMibExpr(pMetadata->f_GetLen()));
				DMibTest(DMibExpr(2) == DMibExpr(pMetadata->operator[](0)));
			}

			{
				DMibTestPath("Check KeyPart 0 Metadata");
				NMib::NContainer::CByteVector const& Metadata = _RS.f_GetKeyPartMetadata(0);
				DMibTest(DMibExpr(1) == DMibExpr(Metadata.f_GetLen()));
				DMibTest(DMibExpr(3) == DMibExpr(Metadata.operator[](0)));
			}

			{
				DMibTestPath("Check KeyPart 1 Metadata");
				NMib::NContainer::CByteVector const& Metadata = _RS.f_GetKeyPartMetadata(1);
				DMibTest(DMibExpr(1) == DMibExpr(Metadata.f_GetLen()));
				DMibTest(DMibExpr(4) == DMibExpr(Metadata.operator[](0)));
			}

			{
				DMibTestPath("Check Column 0 Metadata");
				NMib::NContainer::CByteVector const& Metadata = _RS.f_GetColumnMetadata(0);
				DMibTest(DMibExpr(1) == DMibExpr(Metadata.f_GetLen()));
				DMibTest(DMibExpr(5) == DMibExpr(Metadata.operator[](0)));
			}

			// Check we have access to each row
			{
				auto iRow = _RS.f_GetRowIterator();

				{
					DMibTestPath("Check Row 0");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iRow));
					DMibTest(DMibExpr(1) == DMibExpr(iRow.f_GetKeyPart<uint64>(0)));
					DMibTest(DMibExpr(2.5) == DMibExpr(iRow.f_GetKeyPart<fp64>(1)));

					DMibTest(DMibExpr(nullptr) != DMibExpr(iRow.f_GetColumnValue<CStr>(0)));
					DMibTest(DMibExpr(CStr("Hello")) == DMibExpr(*iRow.f_GetColumnValue<CStr>(0)));

					DMibTest(DMibExpr(nullptr) == DMibExpr(iRow.f_GetColumnValue<CStr>(1)));

					DMibTest(DMibExpr(nullptr) != DMibExpr(iRow.f_GetColumnValue<fp64>(2)));
					DMibTest(DMibExpr(fp64(1.0 / 3.0)) == DMibExpr(*iRow.f_GetColumnValue<fp64>(2)));

					NContainer::CByteVector lBinData;
					lBinData.f_Insert(100);

					DMibTest(DMibExpr(nullptr) != DMibExpr(iRow.f_GetColumnValue<NContainer::CByteVector>(3)));
					DMibTest(DMibExpr(lBinData) == DMibExpr(*iRow.f_GetColumnValue<NContainer::CByteVector>(3)));
				}
				
				++iRow;
				
				{
					DMibTestPath("Check Row 1");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iRow));						
					DMibTest(DMibExpr(2) == DMibExpr(iRow.f_GetKeyPart<uint64>(0)));
					DMibTest(DMibExpr(5.9) == DMibExpr(iRow.f_GetKeyPart<fp64>(1)));

					DMibTest(DMibExpr(nullptr) != DMibExpr(iRow.f_GetColumnValue<CStr>(0)));

					DMibTest(DMibExpr(CStr(", World!")) == DMibExpr(*iRow.f_GetColumnValue<CStr>(0)));
					DMibTest(DMibExpr(nullptr) == DMibExpr(iRow.f_GetColumnValue<CStr>(1)));

					DMibTest(DMibExpr(nullptr) != DMibExpr(iRow.f_GetColumnValue<fp64>(2)));
					DMibTest(DMibExpr(fp64(2.0 / 3.0)) == DMibExpr(*iRow.f_GetColumnValue<fp64>(2)));

					NContainer::CByteVector lBinData;
					lBinData.f_Insert(200);

					DMibTest(DMibExpr(nullptr) != DMibExpr(iRow.f_GetColumnValue<NContainer::CByteVector>(3)));
					DMibTest(DMibExpr(lBinData) == DMibExpr(*iRow.f_GetColumnValue<NContainer::CByteVector>(3)));

				}

				++iRow;
				{
					DMibTestPath("Check End");
					DMibTest(DMibExpr(false) == DMibExpr((bool)iRow));
				}
			}

			// Check we can iterate key parts
			{
				DMibTestPath("KeyPart Iterator");
				auto iRow = _RS.f_GetRowIterator();
				auto iKeyPart = iRow.f_GetKeyPartIterator();

				{							
					DMibTestPath("KeyPart 0");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iKeyPart));
					DMibTest(DMibExpr(1) == DMibExpr(iKeyPart.f_Get<uint64>()));
				}

				++iKeyPart;
				{
					DMibTestPath("KeyPart 1");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iKeyPart));
					DMibTest(DMibExpr(2.5) == DMibExpr(iKeyPart.f_Get<fp64>()));
				}

				++iKeyPart;
				DMibTest(DMibExpr(false) == DMibExpr((bool)iKeyPart));
			}

			// Check we can iterate columns
			{
				DMibTestPath("Column Iterator");
				auto iRow = _RS.f_GetRowIterator();
				auto iColumn = iRow.f_GetColumnIterator();

				{							
					DMibTestPath("Column 0");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iColumn));
					DMibTest(DMibExpr(nullptr) != DMibExpr(iColumn.f_Get<CStr>()));
					DMibTest(DMibExpr(CStr("Hello")) == DMibExpr(*iColumn.f_Get<CStr>()));
				}

				++iColumn;
				{							
					DMibTestPath("Column 1");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iColumn));
					DMibTest(DMibExpr(nullptr) == DMibExpr(iColumn.f_Get<CStr>()));
				}

				++iColumn;
				{							
					DMibTestPath("Column 2");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iColumn));
					DMibTest(DMibExpr(nullptr) != DMibExpr(iColumn.f_Get<fp64>()));
					DMibTest(DMibExpr(fp64(1.0 / 3.0)) == DMibExpr(*iColumn.f_Get<fp64>()));
				}

				++iColumn;
				{							
					DMibTestPath("Column 3");
					DMibTest(DMibExpr(true) == DMibExpr((bool)iColumn));
					DMibTest(DMibExpr(nullptr) != DMibExpr(iColumn.f_Get<NContainer::CByteVector>()));
					
					NContainer::CByteVector lBinData;
					lBinData.f_Insert(100);

					DMibTest(DMibExpr(lBinData) == DMibExpr(*iColumn.f_Get<NContainer::CByteVector>()));
				}

				++iColumn;
				DMibTest(DMibExpr(false) == DMibExpr((bool)iColumn));
			}			
		}

		void f_DoTests()
		{
//			DMibTestCategory("Dynamic")
			{
				DMibTestSuite("BuildingAndReading")
				{
					CResultSet RS;
					{
						CResultSetBuilder Builder;
						f_PopulateBuilder(Builder);

						DMibTest(DMibExpr(true) == DMibExpr(RS.f_IsEmpty()));
						Builder.f_ExtractTo(RS);
					}

					{
						f_TestResultSet(RS);
					}
				};

				DMibTestSuite("BuildingColumnWiseAndReading")
				{
					CResultSet RS;
					{
						CResultSetBuilder Builder;
						f_PopulateBuilderColumnWise(Builder);

						DMibTest(DMibExpr(true) == DMibExpr(RS.f_IsEmpty()));
						Builder.f_ExtractTo(RS);
					}

					{
						f_TestResultSet(RS);
					}
				};

				DMibTestSuite("Streaming")
				{
					NMib::NContainer::CByteVector RawData;

					{
						CResultSetBuilder Builder;
						f_PopulateBuilder(Builder);

						NStream::CBinaryStreamMemory<> OStream;

						OStream << Builder;

						RawData = OStream.f_GetVector();
					}

					{
						NStream::CBinaryStreamMemoryRef<> IStream(RawData);

						CResultSet RS;

						IStream >> RS;

						f_TestResultSet(RS);
					}

				};

			};
		}	
	};
	
	DMibTestRegister(CResultSet_Tests, Malterlib::Database);
}

