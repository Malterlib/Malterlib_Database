// Copyright © 2019 Nonna Holding AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Database_DatabaseValue.h"

namespace NMib::NDatabase
{
	bool CDatabaseValue::operator < (CDatabaseValue const &_Right) const
	{
		mint nData = fg_Min(m_Size, _Right.m_Size);
		auto Diff = NMemory::fg_MemCmp(m_pData, _Right.m_pData, nData);
		if (Diff)
		{
			if (Diff < 0)
				return true;
			else
				return false;
		}
		if (m_Size < _Right.m_Size)
			return true;
		else if (m_Size > _Right.m_Size)
			return false;
		return false;
	}

	bool CDatabaseValue::operator == (CDatabaseValue const &_Right) const
	{
		if (m_Size != _Right.m_Size)
			return false;
		return NMemory::fg_MemCmp(m_pData, _Right.m_pData, m_Size) == 0;
	}

	bool CDatabaseValue::f_HasPrefix(CDatabaseValue const &_Right) const
	{
		if (m_Size < _Right.m_Size)
			return false;
		return NMemory::fg_MemCmp(m_pData, _Right.m_pData, _Right.m_Size) == 0;
	}
}
