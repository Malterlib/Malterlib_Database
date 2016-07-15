// Copyright © 2016 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	template <typename t_CKey, typename t_CValue, bool t_bDescending>
	struct TCOrderedByteArrayCoder<NContainer::TCMap<t_CKey, t_CValue>, t_bDescending>
	{
		
	};
}
