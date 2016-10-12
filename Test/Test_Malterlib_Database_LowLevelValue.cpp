// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/String/String>
#include <Mib/Database/LowLevel>
#include <Mib/Database/LowLevelValue>

#include "Test_Malterlib_Database_LowLevel.h"

using namespace NMib;
using namespace NMib::NStr;
using namespace NMib::NDatabase;
using namespace NMib::NFile;

namespace
{
	class CLowLevelValue_Tests : public NMib::NTest::CTest
	{
	public:
		
		void f_DoTests()
		{
			DMibTestSuite("Order")
			{
				auto Environment = fg_GetTestEnvironment();
				
				auto WriteTransaction = Environment.m_Environment.f_BeginWriteTransaction();
				auto MainDatabase = WriteTransaction.f_OpenDatabase();
				auto DatabaseTransaction = MainDatabase.f_Transaction(WriteTransaction);
				
				auto fReverseEncodeFloat = [&](uint8 _1, uint8 _2, uint8 _3, uint8 _4)
					{
						NMib::NContainer::TCVector<uint8> KeyData;
						KeyData.f_Insert(_1);
						KeyData.f_Insert(_2);
						KeyData.f_Insert(_3);
						KeyData.f_Insert(_4);
						
						CLowLevelValue Value;
						Value.m_pData = KeyData.f_GetArray(); 
						Value.m_Size = KeyData.f_GetLen(); 
						fp32 TestValue = NMib::NContainer::fg_Get<0>(fg_DecodeValue<fp32>(Value));
						return TestValue; 
					}
				;
				
				uint32 iOrder = 0;
				
				CLowLevelValueCache ValueCache;
				CLowLevelValueCache KeyCache;
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fReverseEncodeFloat(0,0,0,0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fReverseEncodeFloat(0x0,0x0,0x0,0x80)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_NegQNan()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_NegSNan()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_NegInf()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_LimitMin()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fReverseEncodeFloat(1,0,0,0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(-2.0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(-1.0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(-0.5)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(-0.25)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(-0.005)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_NegSmallest()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_NegSmallestDenormal()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(-0.0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(0.0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_SmallestDenormal()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_Smallest()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(0.005)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(0.25)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(0.5)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(1.0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32(2.0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_LimitMax()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_Inf()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_SNan()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fp32::fs_QNan()), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fReverseEncodeFloat(0xFF,0xFF,0xFF,0x7F)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "float", fReverseEncodeFloat(0xFF,0xFF,0xFF,0xFF)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "int  ", -int32(2)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "int  ", -int32(1)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "int  ", int32(0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "int  ", int32(1)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "int  ", int32(2)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "uint ", uint32(0)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "uint ", uint32(1)), fg_EncodeValue(ValueCache, iOrder++));
				DatabaseTransaction.f_Insert(fg_EncodeValue(KeyCache, "uint ", uint32(2)), fg_EncodeValue(ValueCache, iOrder++));
				
				auto iData = DatabaseTransaction.f_ReadCursor();
				iData.f_First();
				for (uint32 iExpectedOrder = 0; iData; ++iData, ++iExpectedOrder)
				{
					auto &Value = iData.f_Value();
					auto DecodedOrder = NContainer::fg_Get<0>(fg_DecodeValue<uint32>(Value));
					DMibExpect(DecodedOrder, ==, iExpectedOrder)(ETestFlag_Aggregated);
				}
			};
		}
	};
	
	DMibTestRegister(CLowLevelValue_Tests, Malterlib::Database);
}
