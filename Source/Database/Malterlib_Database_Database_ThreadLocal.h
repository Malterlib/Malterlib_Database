// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib::NDatabase
{
	struct CDatabaseThreadLocal
	{
		CDatabaseValueCache m_KeyCache;
		CDatabaseValueCache m_ValueCache;
	};

	extern NStorage::TCAggregate<NThread::TCThreadLocal<CDatabaseThreadLocal>> g_DatabaseThreadLocal;
}
