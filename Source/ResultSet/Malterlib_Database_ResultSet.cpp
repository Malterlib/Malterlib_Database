// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Database_ResultSet.h"

namespace NMib::NDatabase
{
	//
	// CResultSet::CKeyPartIterator Public Implementation
	//

	CResultSet::CKeyPartIterator::CKeyPartIterator()
		: mp_pResultSet(nullptr)
		, mp_iRow(0)
		, mp_iKeyPart(0)
	{
	}

	CResultSet::CKeyPartIterator::~CKeyPartIterator()
	{
	}

	CResultSet::CKeyPartIterator &CResultSet::CKeyPartIterator::operator ++ ()
	{
		++mp_iKeyPart;
		return *this;
	}

	CResultSet::CKeyPartIterator::operator bool () const
	{
		return mp_iKeyPart < mp_nKeyParts;
	}

	//
	// CResultSet::CKeyPartIterator Private Implementation
	//

	CResultSet::CKeyPartIterator::CKeyPartIterator(CResultSet const &_ResultSet, umint _iRow, umint _iKeyPart)
		: mp_pResultSet(&_ResultSet)
		, mp_iRow(_iRow)
		, mp_nKeyParts(_ResultSet.mp_KeyPartInfos.f_GetLen())
		, mp_iKeyPart(_iKeyPart)
	{
	}

	//
	// CResultSet::CColumnValueIterator Public Implementation
	//

	CResultSet::CColumnValueIterator::CColumnValueIterator()
		: mp_pResultSet(nullptr)
		, mp_iRow(0)
		, mp_iColumn(0)
	{
	}

	CResultSet::CColumnValueIterator::~CColumnValueIterator()
	{
	}

	CResultSet::CColumnValueIterator &CResultSet::CColumnValueIterator::operator ++ ()
	{
		++mp_iColumn;
		return *this;
	}

	CResultSet::CColumnValueIterator::operator bool () const
	{
		return mp_iColumn < mp_ColumnValueIterators.f_GetLen();
	}

	//
	// CResultSet::CColumnValueIterator Private Implementation
	//

	CResultSet::CColumnValueIterator::CColumnValueIterator(CResultSet const &_ResultSet, umint _iRow, NContainer::TCVector<umint> const &_ColumnValueIterators, umint _iColumn)
		: mp_pResultSet(&_ResultSet)
		, mp_ColumnValueIterators(_ColumnValueIterators)
		, mp_iRow(_iRow)
		, mp_iColumn(0)
	{
		while (_iColumn)
		{
			operator ++ ();
			--_iColumn;
		}
	}

	//
	// CResultSet::CRowIterator Public Implementation
	//

	CResultSet::CRowIterator::CRowIterator()
		: mp_pResultSet(nullptr)
		, mp_nRows(0)
		, mp_iCurRow(0)
	{
	}

	CResultSet::CRowIterator::CRowIterator(CResultSet const &_ResultSet, umint _iRow)
		: mp_pResultSet(&_ResultSet)
		, mp_nRows(_ResultSet.mp_nRows)
		, mp_iCurRow(0)
	{
		// Start all column value iterators off at the start.
		mp_ColumnValueIterators.f_SetLen(mp_pResultSet->mp_Columns.f_GetLen());
		for (auto &Index : mp_ColumnValueIterators)
			Index = 0;
		while (_iRow)
		{
			operator ++ ();
			--_iRow;
		}
	}

	namespace
	{
		// Given a row index we are looking for column values for this is used
		// to visit each column and advance the corresponding value iterator
		struct CRowIteratorAdvanceVisitor
		{
			umint &m_ColumnValueIterator;
			umint m_iRow;

			CRowIteratorAdvanceVisitor(umint &_ColumnValueIterator, umint _iRow)
				: m_ColumnValueIterator(_ColumnValueIterator)
				, m_iRow(_iRow)
			{
			}

			template <typename tf_CVector>
			void operator () (tf_CVector const &_Vector)
			{
				umint VectorLen = _Vector.f_GetLen();
				umint Iter = m_ColumnValueIterator;
				while (Iter < VectorLen && _Vector[Iter].m_iRow < m_iRow)
					++Iter;

				m_ColumnValueIterator = Iter;
			}
		};
	}

	CResultSet::CRowIterator &CResultSet::CRowIterator::operator ++ ()
	{
		if (mp_iCurRow >= mp_nRows)
			DMibErrorListBoundCheck("Row iterator out of bounds");

		umint iCurRow = ++mp_iCurRow;

		auto iColumn = mp_pResultSet->mp_Columns.f_GetIterator();
		for (auto iColumnValueIterator = mp_ColumnValueIterators.f_GetIterator(); iColumnValueIterator; ++iColumn, ++iColumnValueIterator)
		{
			DMibCheck(iColumn);
			CRowIteratorAdvanceVisitor Visitor(*iColumnValueIterator, iCurRow);
			fg_Visit(Visitor, *iColumn);
		}

		return *this;
	}

	CResultSet::CRowIterator::operator bool () const
	{
		return mp_iCurRow < mp_nRows;
	}

	// Returns an iterator allowing for iteration over each key part.
	CResultSet::CKeyPartIterator CResultSet::CRowIterator::f_GetKeyPartIterator() const
	{
		return CKeyPartIterator(*mp_pResultSet, mp_iCurRow);
	}

	// Returns an iterator allowing for iteration over each key part.
	CResultSet::CKeyPartIterator CResultSet::CRowIterator::f_GetKeyPartIterator(umint _iKP) const
	{
		return CKeyPartIterator(*mp_pResultSet, mp_iCurRow, _iKP);
	}

	// Returns an iterator allowing for iteration over each column value.
	CResultSet::CColumnValueIterator CResultSet::CRowIterator::f_GetColumnIterator() const
	{
		return CColumnValueIterator(*mp_pResultSet, mp_iCurRow, mp_ColumnValueIterators);
	}

	// Returns an iterator allowing for iteration over each column value.
	CResultSet::CColumnValueIterator CResultSet::CRowIterator::f_GetColumnIterator(umint _iCol) const
	{
		return CColumnValueIterator(*mp_pResultSet, mp_iCurRow, mp_ColumnValueIterators, _iCol);
	}

	//
	// CResultSet Public Implementation
	//

	CResultSet::CResultSet()
		: mp_nRows(0)
	{
	}

	CResultSet::CResultSet(CResultSet &&_ToMove)
		: mp_Metadata(fg_Move(_ToMove.mp_Metadata))
		, mp_KeyPartInfos(fg_Move(_ToMove.mp_KeyPartInfos))
		, mp_ColumnInfos(fg_Move(_ToMove.mp_ColumnInfos))
		, mp_KeyPartValues(fg_Move(_ToMove.mp_KeyPartValues))
		, mp_nRows(_ToMove.mp_nRows)
		, mp_KeyPartRowValueIndices(fg_Move(_ToMove.mp_KeyPartRowValueIndices))
		, mp_Columns(fg_Move(_ToMove.mp_Columns))
	{
	}

	CResultSet::~CResultSet()
	{
	}

	void CResultSet::f_Clear()
	{
		mp_Metadata.f_Clear();
		mp_KeyPartInfos.f_Clear();
		mp_ColumnInfos.f_Clear();
		mp_KeyPartValues.f_Clear();
		mp_nRows = 0;
		mp_KeyPartRowValueIndices.f_Clear();
		mp_Columns.f_Clear();
	}

	bool CResultSet::f_IsEmpty() const
	{
		return mp_nRows == 0;
	}

	umint CResultSet::f_GetNumKeyParts() const
	{
		return mp_KeyPartInfos.f_GetLen();
	}

	umint CResultSet::f_GetNumColumns() const
	{
		return mp_ColumnInfos.f_GetLen();
	}

	EResultDataType CResultSet::f_GetKeyPartType(umint _iKeyPart) const
	{
		return mp_KeyPartInfos[_iKeyPart].m_Type;
	}

	NResultSet::CSubType CResultSet::f_GetKeyPartSubType(umint _iKeyPart) const
	{
		return mp_KeyPartInfos[_iKeyPart].m_SubType;
	}

	EResultDataType CResultSet::f_GetColumnType(umint _iColumn) const
	{
		return mp_ColumnInfos[_iColumn].m_Type;
	}

	NResultSet::CSubType CResultSet::f_GetColumnSubType(umint _iColumn) const
	{
		return mp_ColumnInfos[_iColumn].m_SubType;
	}

	umint CResultSet::f_GetNumRows() const
	{
		return mp_nRows;
	}

	struct CMetadataCompare
	{
		COrdering_Strong operator () (NResultSet::CMetadataEntry const &_A, NResultSet::CMetadataEntry const &_B) const
		{
			return _A.m_ID <=> _B.m_ID;
		}

		COrdering_Strong operator () (NResultSet::CMetadataEntry const &_A, uint32 _B) const
		{
			return _A.m_ID <=> _B;
		}

		COrdering_Strong operator () (uint32 _A, NResultSet::CMetadataEntry const &_B) const
		{
			return _A <=> _B.m_ID;
		}
	};

	CResultMetadata const *CResultSet::f_GetMetadata(uint32 _ID) const
	{
		CMetadataCompare Compare;
		aint iMetadata = mp_Metadata.f_BinarySearch(Compare, _ID);
		if (iMetadata == -1)
			return nullptr;
		else
			return &mp_Metadata[iMetadata].m_Data;
	}

	CResultMetadata const &CResultSet::f_GetKeyPartMetadata(umint _iKeyPart) const
	{
		return mp_KeyPartInfos[_iKeyPart].m_Metadata;
	}

	CResultMetadata const &CResultSet::f_GetColumnMetadata(umint _iKeyPart) const
	{
		return mp_ColumnInfos[_iKeyPart].m_Metadata;
	}

	CResultSet::CRowIterator CResultSet::f_GetRowIterator() const
	{
		return CRowIterator(*this);
	}

	CResultSet::CRowIterator CResultSet::f_GetRowIterator(umint _iRow) const
	{
		return CRowIterator(*this, _iRow);
	}
}
