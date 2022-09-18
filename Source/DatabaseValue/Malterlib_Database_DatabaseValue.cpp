// Copyright © 2019 Nonna Holding AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Database_DatabaseValue.h"

namespace NMib::NDatabase
{
	COrdering_Strong CDatabaseValue::operator <=> (CDatabaseValue const &_Right) const
	{
		mint nData = fg_Min(m_Size, _Right.m_Size);
		if (auto Diff = NMemory::fg_MemCmp(m_pData, _Right.m_pData, nData); Diff != 0)
			return Diff <=> 0;
		
		return m_Size <=> _Right.m_Size;
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

	NContainer::CByteVector CDatabaseValue::f_ToVector() const
	{
		return NContainer::CByteVector(m_pData, m_Size);
	}

	CDatabaseValue::CDatabaseValue(CDatabaseValueCache &_ValueCache, CDatabaseValue const &_CopyFromValue)
		: m_Size(_CopyFromValue.m_Size)
	{
		_ValueCache.f_SetAtLeastLen(_CopyFromValue.m_Size);
		m_pData = NMemory::fg_MemCopy(_ValueCache.f_GetArray(), _CopyFromValue.m_pData, _CopyFromValue.m_Size);
	}
}
