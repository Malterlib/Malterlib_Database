// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CType, bool t_bDescending>
	struct TCOrderedByteArrayCoder<NContainer::TCVector<t_CType>, t_bDescending>
	{
		
	};
}
