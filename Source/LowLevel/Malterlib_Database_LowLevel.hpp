// Copyright © 2016 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>

namespace NMib
{
	namespace NDatabase
	{
		template <typename tf_CStr>
		void CLowLevelValue::f_Format(tf_CStr &o_String) const
		{
			uint8 const *pDataEnd = m_pData + m_Size;
			typename tf_CStr::CFormat Formatter{"{nfh,sf0,sj2}"};
			uint8 ValueToFormat;
			Formatter << ValueToFormat;

			o_String += "[";

			for (uint8 const *pData = m_pData; pData != pDataEnd; ++pData)
			{
				ValueToFormat = *pData;
				o_String += Formatter;
			}

			o_String += "]";
		}
	}
}
