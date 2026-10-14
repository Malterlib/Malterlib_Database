// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/String/String>
#include <Mib/Database/Database>

namespace NMib::NDatabase
{
	struct CTestEnvironment
	{
		NStr::CStr m_DatabaseDirectory;
		NStr::CStr m_FileName;
		CDatabaseEnvironment m_Environment;

		CTestEnvironment(CTestEnvironment &&_Other);
		CTestEnvironment &operator = (CTestEnvironment &&_Other);
		CTestEnvironment();

		~CTestEnvironment();
	};

	CTestEnvironment fg_GetTestEnvironment(NStr::CStr const &_FileName, bool _bReset = true, EDatabaseEnvironmentFlag _OpenFlags = EDatabaseEnvironmentFlag_None, uint64 _MapSize = 0);
}

