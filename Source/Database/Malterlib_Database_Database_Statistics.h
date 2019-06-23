// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>

namespace NMib::NDatabase
{
	/** @brief Statistics for a database in the environment */
	struct CDatabaseStatistics
	{
		uint32 m_PageSize;				///< Size of a database page. This is currently the same for all databases.
		uint32 m_BTreeDepth;			///< Depth (height) of the B-tree
		CDatabaseOffset m_nBranchPages;	///< Number of internal (non-leaf) pages
		CDatabaseOffset m_nLeafPages;		///< Number of leaf pages
		CDatabaseOffset m_nOverflowPages;	///< Number of overflow pages
		CDatabaseOffset m_nDataItems;		///< Number of data items
	};
}
