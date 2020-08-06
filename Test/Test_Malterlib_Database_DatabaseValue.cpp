// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/String/String>
#include <Mib/Database/Database>
#include <Mib/Database/DatabaseValue>

#include "Test_Malterlib_Database_Database.h"

using namespace NMib;
using namespace NMib::NStr;
using namespace NMib::NDatabase;
using namespace NMib::NFile;
using namespace NMib::NContainer;
using namespace NMib::NStorage;
using namespace NMib::NTime;

namespace
{
	class CDatabaseValue_Tests : public NMib::NTest::CTest
	{
		static fp32 fs_ReverseEncodeFloat(uint8 _1, uint8 _2, uint8 _3, uint8 _4)
		{
			TCVector<uint8> KeyData;
			KeyData.f_Insert(_1);
			KeyData.f_Insert(_2);
			KeyData.f_Insert(_3);
			KeyData.f_Insert(_4);

			CDatabaseValue Value;
			Value.m_pData = KeyData.f_GetArray();
			Value.m_Size = KeyData.f_GetLen();
			fp32 TestValue = fg_Get<0>(Value.f_TupleFromLexicographic<fp32>());
			return TestValue;
		}

		using CTestVariant = TCStreamableVariant
			<
				uint8
				, NStorage::TCMember<CStr, uint8(0)>
				, NStorage::TCMember<uint32, uint8(1)>
			>
		;

		template <bool tf_bDescending>
		void fp_TestOrder(CStr const &_Name)
		{
			auto Environment = fg_GetTestEnvironment("TestDatabaseValue" + _Name);

			auto WriteTransaction = Environment.m_Environment.f_BeginWriteTransaction();
			auto MainDatabase = WriteTransaction.f_OpenDatabase();
			auto DatabaseTransaction = MainDatabase.f_Transaction(WriteTransaction);

			uint32 StartOrder = tf_bDescending ? 1000 : 0;

			auto fNextOrder = [](uint32 &o_iOrder) -> uint32
				{
					uint32 Return = o_iOrder;
					if constexpr (tf_bDescending)
						--o_iOrder;
					else
						++o_iOrder;

					return Return;
				}
			;

			uint32 iOrder = StartOrder;

			#define DInsertValue(d_Prefix, d_Value) DatabaseTransaction.f_Insert \
				( \
				 	fg_Tuple(fg_Descending<tf_bDescending>( d_Prefix ), fg_Descending<tf_bDescending>( d_Value )) \
				 	, fNextOrder(iOrder) \
				)

			DInsertValue("float", (fs_ReverseEncodeFloat(0,0,0,0)));
			DInsertValue("float", (fs_ReverseEncodeFloat(0x0,0x0,0x0,0x80)));
			DInsertValue("float", (fp32::fs_NegQNan()));
			DInsertValue("float", (fp32::fs_NegSNan()));
			DInsertValue("float", (fp32::fs_NegInf()));
			DInsertValue("float", (fp32::fs_LimitMin()));
			DInsertValue("float", (fs_ReverseEncodeFloat(1,0,0,0)));
			DInsertValue("float", (fp32(-2.0f)));
			DInsertValue("float", (fp32(-1.0f)));
			DInsertValue("float", (fp32(-0.5f)));
			DInsertValue("float", (fp32(-0.25f)));
			DInsertValue("float", (fp32(-0.005f)));
			DInsertValue("float", (fp32::fs_NegSmallest()));
			DInsertValue("float", (fp32::fs_NegSmallestDenormal()));
			DInsertValue("float", (fp32(-0.0f)));
			DInsertValue("float", (fp32(0.0f)));
			DInsertValue("float", (fp32::fs_SmallestDenormal()));
			DInsertValue("float", (fp32::fs_Smallest()));
			DInsertValue("float", (fp32(0.005f)));
			DInsertValue("float", (fp32(0.25f)));
			DInsertValue("float", (fp32(0.5f)));
			DInsertValue("float", (fp32(1.0f)));
			DInsertValue("float", (fp32(2.0f)));
			DInsertValue("float", (fp32::fs_LimitMax()));
			DInsertValue("float", (fp32::fs_Inf()));
			DInsertValue("float", (fp32::fs_SNan()));
			DInsertValue("float", (fp32::fs_QNan()));
			DInsertValue("float", (fs_ReverseEncodeFloat(0xFF,0xFF,0xFF,0x7F)));
			DInsertValue("float", (fs_ReverseEncodeFloat(0xFF,0xFF,0xFF,0xFF)));
			DInsertValue("int  ", (-int32(2)));
			DInsertValue("int  ", (-int32(1)));
			DInsertValue("int  ", (int32(0)));
			DInsertValue("int  ", (int32(1)));
			DInsertValue("int  ", (int32(2)));
			DInsertValue("map  ", (TCMap<CStr, CStr>{{"Test", "Value"}}));
			DInsertValue("map  ", (TCMap<CStr, CStr>{{"Test", "Value"}, {"Test1", "Value"}}));
			DInsertValue("map  ", (TCMap<CStr, CStr>{{"Test1", "Value"}}));
			DInsertValue("map  ", (TCMap<CStr, CStr>{{"Test3", "Value"}}));
			DInsertValue("set  ", (TCSet<CStr>{"Test"}));
			DInsertValue("set  ", (TCSet<CStr>{"Test", "Test1"}));
			DInsertValue("set  ", (TCSet<CStr>{"Test1"}));
			DInsertValue("set  ", (TCSet<CStr>{"Test3"}));
			DInsertValue("time ", (CTimeConvert::fs_CreateTime(55, 5, 6, 15, 20, 55, 0.6)));
			DInsertValue("time ", (CTimeConvert::fs_CreateTime(2007, 5, 6, 15, 20, 55, 0.6)));
			DInsertValue("time ", (CTimeConvert::fs_CreateTime(2018, 5, 6, 15, 20, 55, 0.55)));
			DInsertValue("time ", (CTimeConvert::fs_CreateTime(2018, 5, 6, 15, 20, 55, 0.6)));
			DInsertValue("time ", (CTimeConvert::fs_CreateTime(2018, 5, 6, 15, 20, 55, 0.7)));
			DInsertValue("time ", (CTimeConvert::fs_CreateTime(10004556, 5, 6, 15, 20, 55, 0.6)));
			DInsertValue("times", (CTimeSpanConvert::fs_CreateMinuteSpan(-50)));
			DInsertValue("times", (CTimeSpanConvert::fs_CreateMinuteSpan(0)));
			DInsertValue("times", (CTimeSpanConvert::fs_CreateMinuteSpan(10)));
			DInsertValue("times", (CTimeSpanConvert::fs_CreateMinuteSpan(20)));
			DInsertValue("uint ", (uint32(0)));
			DInsertValue("uint ", (uint32(1)));
			DInsertValue("uint ", (uint32(2)));
			DInsertValue("vari ", (CTestVariant{"A String"}));
			DInsertValue("vari ", (CTestVariant{"String"}));
			DInsertValue("vari ", (CTestVariant{55}));
			DInsertValue("vari ", (CTestVariant{66}));
			DInsertValue("vec  ", (CByteVector{0x00}));
			DInsertValue("vec  ", (CByteVector{0, 1, 2}));
			DInsertValue("vec  ", (CByteVector{0, 1, 2, 3}));
			DInsertValue("vec  ", (CByteVector{4, 0}));
			DInsertValue("vec  ", (CByteVector{0x7f}));
			DInsertValue("vec  ", (CByteVector{0x80}));
			DInsertValue("vec  ", (CByteVector{0xff}));
			DInsertValue("vecs ", (TCVector<CStr>{"Test"}));
			DInsertValue("vecs ", (TCVector<CStr>{"Test", "Test"}));
			DInsertValue("vecs ", (TCVector<CStr>{"Test1"}));
			DInsertValue("vecs ", (TCVector<CStr>{"Test3"}));

			if (tf_bDescending)
				StartOrder = iOrder + 1;

			auto iData = DatabaseTransaction.f_ReadCursor();
			iData.f_First();
			for (uint32 iExpectedOrder = StartOrder; iData; ++iData, ++iExpectedOrder)
			{
				auto DecodedOrder = iData.f_Value<uint32>();
				DMibExpect(DecodedOrder, ==, iExpectedOrder)(ETestFlag_Aggregated);
			}
		}

		template <bool tf_bDescending>
		void fp_TestRoundtrip()
		{
			auto fTestCoding = [](CStr const &_Path, auto const &_Value)
				{
					DMibTestPath(_Path);
					CDatabaseValueCache Cache;
					auto RecodedValue = CDatabaseValue::fs_ToLexicographic(Cache, fg_Descending<tf_bDescending>(_Value))
						.template f_FromLexicographic<typename NTraits::TCRemoveReferenceAndQualifiers<decltype(fg_Descending<tf_bDescending>(_Value))>::CType>()
					;

					DMibExpect(RecodedValue, ==, _Value);

					auto RecodedTuple = CDatabaseValue::fs_ToLexicographic(Cache, fg_Descending<tf_bDescending>(_Value))
						.template f_TupleFromLexicographic<typename NTraits::TCRemoveReferenceAndQualifiers<decltype(fg_Descending<tf_bDescending>(_Value))>::CType>()
					;
					DMibExpect(fg_Get<0>(RecodedTuple), ==, _Value);
				}
			;
			#define DTestEncoding(d_Value) fTestCoding(#d_Value, d_Value)

			auto fTestCodingFloat = [](CStr const &_Path, auto const &_Value)
				{
					DMibTestPath(_Path);
					CDatabaseValueCache Cache;
					auto RecodedValue = CDatabaseValue::fs_ToLexicographic(Cache, fg_Descending<tf_bDescending>(_Value))
						.template f_FromLexicographic<typename NTraits::TCRemoveReferenceAndQualifiers<decltype(fg_Descending<tf_bDescending>(_Value))>::CType>()
					;
					using CInteger = typename NTraits::TCIntFromSize<sizeof(_Value)>::CType;
					DMibExpect((CInteger const &)RecodedValue, ==, (CInteger const &)_Value);

					auto RecodedTuple = CDatabaseValue::fs_ToLexicographic(Cache, fg_Descending<tf_bDescending>(_Value))
						.template f_TupleFromLexicographic<typename NTraits::TCRemoveReferenceAndQualifiers<decltype(fg_Descending<tf_bDescending>(_Value))>::CType>()
					;
					DMibExpect((CInteger const &)fg_Get<0>(RecodedTuple), ==, (CInteger const &)_Value);
				}
			;
			#define DTestEncodingFloat(d_Value) fTestCodingFloat(#d_Value, d_Value)

			DTestEncodingFloat(fs_ReverseEncodeFloat(0,0,0,0));
			DTestEncodingFloat(fs_ReverseEncodeFloat(0x0,0x0,0x0,0x80));
			DTestEncodingFloat(fp32::fs_NegQNan());
			DTestEncodingFloat(fp32::fs_NegSNan());
			DTestEncodingFloat(fp32::fs_NegInf());
			DTestEncodingFloat(fp32::fs_LimitMin());
			DTestEncodingFloat(fs_ReverseEncodeFloat(1,0,0,0));
			DTestEncodingFloat(fp32(-2.0f));
			DTestEncodingFloat(fp32(-1.0f));
			DTestEncodingFloat(fp32(-0.5f));
			DTestEncodingFloat(fp32(-0.25f));
			DTestEncodingFloat(fp32(-0.005f));
			DTestEncodingFloat(fp32::fs_NegSmallest());
			DTestEncodingFloat(fp32::fs_NegSmallestDenormal());
			DTestEncodingFloat(fp32(-0.0f));
			DTestEncodingFloat(fp32(0.0f));
			DTestEncodingFloat(fp32::fs_SmallestDenormal());
			DTestEncodingFloat(fp32::fs_Smallest());
			DTestEncodingFloat(fp32(0.005f));
			DTestEncodingFloat(fp32(0.25f));
			DTestEncodingFloat(fp32(0.5f));
			DTestEncodingFloat(fp32(1.0f));
			DTestEncodingFloat(fp32(2.0f));
			DTestEncodingFloat(fp32::fs_LimitMax());
			DTestEncodingFloat(fp32::fs_Inf());
			DTestEncodingFloat(fp32::fs_SNan());
			DTestEncodingFloat(fp32::fs_QNan());
			DTestEncodingFloat(fs_ReverseEncodeFloat(0xFF,0xFF,0xFF,0x7F));
			DTestEncodingFloat(fs_ReverseEncodeFloat(0xFF,0xFF,0xFF,0xFF));

			DTestEncoding(-int32(2));
			DTestEncoding(-int32(1));
			DTestEncoding(int32(0));
			DTestEncoding(int32(1));
			DTestEncoding(int32(2));

			DTestEncoding(uint32(0));
			DTestEncoding(uint32(1));
			DTestEncoding(uint32(2));

			DTestEncoding((CByteVector{0x00}));
			DTestEncoding((CByteVector{0, 1, 2}));
			DTestEncoding((CByteVector{0, 1, 2, 3}));
			DTestEncoding((CByteVector{4, 0}));
			DTestEncoding((CByteVector{0x7f}));
			DTestEncoding((CByteVector{0x80}));
			DTestEncoding((CByteVector{0xff}));
			DTestEncoding((CByteVector{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0xff}));

			DTestEncoding((TCVector<CStr>{"Test"}));
			DTestEncoding((TCVector<CStr>{"Test", "Test"}));
			DTestEncoding((TCVector<CStr>{"Test1"}));
			DTestEncoding((TCVector<CStr>{"Test3"}));

			DTestEncoding((TCSet<CStr>{"Test"}));
			DTestEncoding((TCSet<CStr>{"Test", "Test1"}));
			DTestEncoding((TCSet<CStr>{"Test1"}));
			DTestEncoding((TCSet<CStr>{"Test3"}));

			DTestEncoding((TCMap<CStr, CStr>{{"Test", "Value"}}));
			DTestEncoding((TCMap<CStr, CStr>{{"Test", "Value"}, {"Test1", "Value"}}));
			DTestEncoding((TCMap<CStr, CStr>{{"Test1", "Value"}}));
			DTestEncoding((TCMap<CStr, CStr>{{"Test3", "Value"}}));

			DTestEncoding((CTimeConvert::fs_CreateTime(55, 5, 6, 15, 20, 55, 0.6)));
			DTestEncoding((CTimeConvert::fs_CreateTime(2007, 5, 6, 15, 20, 55, 0.6)));
			DTestEncoding((CTimeConvert::fs_CreateTime(2018, 5, 6, 15, 20, 55, 0.55)));
			DTestEncoding((CTimeConvert::fs_CreateTime(2018, 5, 6, 15, 20, 55, 0.6)));
			DTestEncoding((CTimeConvert::fs_CreateTime(10004556, 5, 6, 15, 20, 55, 0.6)));

			DTestEncoding((CTimeSpanConvert::fs_CreateMinuteSpan(-50)));
			DTestEncoding((CTimeSpanConvert::fs_CreateMinuteSpan(0)));
			DTestEncoding((CTimeSpanConvert::fs_CreateMinuteSpan(10)));
			DTestEncoding((CTimeSpanConvert::fs_CreateMinuteSpan(20)));

			DTestEncoding((CTestVariant{"A String"}));
			DTestEncoding((CTestVariant{"String"}));
			DTestEncoding((CTestVariant{55}));
			DTestEncoding((CTestVariant{66}));
		}

	public:

		void f_DoTests()
		{
			DMibTestSuite("Order")
			{
				fp_TestOrder<false>("Order");
			};
			DMibTestSuite("Order Descending")
			{
				fp_TestOrder<true>("OrderDescending");
			};
			DMibTestSuite("Roundtrip")
			{
				fp_TestRoundtrip<false>();
			};
			DMibTestSuite("Roundtrip Descending")
			{
				fp_TestRoundtrip<true>();
			};
		}
	};

	DMibTestRegister(CDatabaseValue_Tests, Malterlib::Database);
}
