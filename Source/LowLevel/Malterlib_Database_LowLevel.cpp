// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:
 
 Copyright 2011-2016 Howard Chu, Symas Corp. All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted only as authorized by the OpenLDAP
 Public License.
 
 A copy of this license is available in the file LICENSE.OpenLDAP in the
 top-level directory of the distribution or, alternatively, at
 <http://www.OpenLDAP.org/license.html>.
 
 This code is derived from btree.c written by Martin Hedenfalk.
 
 Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>
 
 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
 
 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/


#include "Malterlib_Database_LowLevel.h"
#include "Malterlib_Database_LowLevel_IDList.h"
#include "Malterlib_Database_LowLevel_Internal.h"

namespace NMib::NDatabase
{
	bool CLowLevelValue::operator < (CLowLevelValue const &_Right) const
	{
		mint nData = fg_Min(m_Size, _Right.m_Size);
		auto Diff = NMem::fg_MemCmp(m_pData, _Right.m_pData, nData);
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
	
	bool CLowLevelValue::operator == (CLowLevelValue const &_Right) const
	{
		if (m_Size != _Right.m_Size)
			return false;
		return NMem::fg_MemCmp(m_pData, _Right.m_pData, m_Size) == 0;
	}
	
	bool CLowLevelValue::f_HasPrefix(CLowLevelValue const &_Right) const
	{
		if (m_Size < _Right.m_Size)
			return false;
		return NMem::fg_MemCmp(m_pData, _Right.m_pData, _Right.m_Size) == 0;
	}
}
