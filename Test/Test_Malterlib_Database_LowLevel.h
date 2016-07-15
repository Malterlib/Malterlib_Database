// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/String/String>
#include <Mib/Database/LowLevel>

namespace NMib::NDatabase
{
	struct CTestEnvironment
	{
		NStr::CStr m_DatabaseDirectory;
		CLowLevelEnvironment m_Environment;
		
		CTestEnvironment(CTestEnvironment &&_Other);
		CTestEnvironment &operator =(CTestEnvironment &&_Other);
		CTestEnvironment();
		
		~CTestEnvironment();
	};
	
	CTestEnvironment fg_GetTestEnvironment(NStr::CStr const &_FileName = "TestDatabase", bool _bReset = true, ELowLevelEnvironmentFlag _OpenFlags = ELowLevelEnvironmentFlag_None);
}

