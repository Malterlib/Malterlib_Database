// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDatabase::NPrivate
{
	/** Append ID to IDL. The IDL must be big enough. */
	inline_always void fg_MidlXAppend(CMdbIdl _pIDs, CMdbId _ID)
	{
		CMdbId *pIDs = _pIDs;
		CMdbId nIDs = ++pIDs[0];
		pIDs[nIDs] = _ID;
	}
}
