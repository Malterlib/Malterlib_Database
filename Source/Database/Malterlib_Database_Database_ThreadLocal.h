// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
