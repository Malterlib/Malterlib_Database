// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

/*
CResultSet
^^^^^^^^^^

Represents a result of a database query of some kind.

A result set has two conceptual parts:
	* It's structure (/schema)
	* It's data

The structure of a result set consists of:
	A) nKeyParts Key Parts, KeyPartX
	B) nColumns Columns, ColumnX

	Each KeyPart has a type.
	Each Column has a type.

The data of a result set consists of a ordered list of nRows rows.
(Note: You don't know how the rows are ordered, just that they are)

Each row consists:
	A) nKeyParts key part values 
	B) nColumns column values

This can be viewed as a table:

ResultSet:
^^^^^^^^^^

Name:		KeyPart0	|	...		|	KeyPartN	||	Column0		|	...		|	ColumnN
Type:		<CKeyPart0>	|			|	CKeyPartN	||	CColumn0	|			|	CColumnN
			--------------------------------------------------------------------------------
Row:		<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>
Row:		<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>
...			<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>
			<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>
			<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>
			<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>
			<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>
			<Val>		|	...		|	<Val>		||	<Val>		|	...		|	<Val>


Each key part or column can be of one of the following types, represented internally by 
a member of the EResultDataType enum:

	Type:				|	CType:			| EResultDataType	
	---------------------------------------------------------------------
	String				|	CStr			|	EResultDataType_CStr
	64bit float			|	fp64			|	EResultDataType_fp64
	Binary blob			|	CByteVector		|	EResultDataType_Binary
	Unsigned 64bit int	|	uint64			|	EResultDataType_uint64

NOTE: These types have been deprecated:
	Signed 32bit int	|	int32			|	EResultDataType_int32
	Signed 64bit int	|	int64			|	EResultDataType_int64
	32bit float			|	fp32			|	EResultDataType_fp32

Meta data can be associated with various elements of a result set. Meta data is treated as 
a binary blob by the result set.

The following elements can have associated meta data:
	1) The Result set itself can have any number of meta data blobs attached, identified by a unique uint32.
	2) Each key part (of the structure, not per row) can have a meta data blob attached.
	3) Each column (of the structure, not per row) can have a meta data blob attached.

*/

#pragma once

#include "Malterlib_Database_ResultSet_Common.h"

namespace NMib::NDatabase
{

	//
	// Result Set
	//

	class CResultSetBuilder;

	class CResultSet
	{
	private:

	protected:

		//
		// Structure:
		//

		NContainer::TCVector<NResultSet::CMetaDataEntry> mp_MetaData;	// Lookup by uint32 m_ID
		NContainer::TCVector<NResultSet::CElementInfo> mp_KeyPartInfos;	// Index by iKeyPart
		NContainer::TCVector<NResultSet::CElementInfo> mp_ColumnInfos;	// Index by iColumn

		//
		// Data:
		//

		NContainer::TCVector< NResultSet::CValueVector > mp_KeyPartValues;		// Index by iKeyPart, then by iKeyPartValue

		mint mp_nRows;											// Cached for easy access, == mp_KeyParse[X].f_GetLen()
		NContainer::TCVector< NContainer::TCVector<uint32> > mp_KeyPartRowValueIndices;		// Index by iKeyPart and then by iRow to get value index.
		NContainer::TCVector< NResultSet::CSparseValueVector > mp_Columns;	// Index by iColumn, then by iterator (as values are sparse)

	public:

		//
		// Iterators
		//

		// Allows iteration over all key parts for a specific row.
		struct CKeyPartIterator
		{
			CKeyPartIterator();
			~CKeyPartIterator();

			template <typename tf_CType>
			inline tf_CType const& f_Get() const;

			inline uint32 f_GetValueIndex() const;

			inline EResultDataType f_GetType() const;
			inline NResultSet::CSubType f_GetSubType() const;

			inline mint f_GetIndex() const;

			CKeyPartIterator & operator ++ ();

			explicit operator bool () const;

		private:
			CKeyPartIterator(CResultSet const& _ResultSet, mint _iRow, mint _iKeyPart = 0);

		private:
			friend class CResultSet;

			CResultSet const* mp_pResultSet;
			mint mp_iRow;

			mint mp_nKeyParts;	// Cached from mp_pResultSet

			mint mp_iKeyPart;
		};

		// Allows iteration over all column values for a specific row.
		struct CColumnValueIterator
		{
			CColumnValueIterator();
			~CColumnValueIterator();

			template <typename tf_CType>
			tf_CType const* f_Get() const;

			EResultDataType f_GetType() const;
			NResultSet::CSubType f_GetSubType() const;

			mint f_GetIndex() const;

			CColumnValueIterator & operator ++ ();

			explicit operator bool () const;

		private:
			CColumnValueIterator(CResultSet const& _ResultSet, mint _iRow, NContainer::TCVector<mint> const& _ColumnValueIterators, mint _iCol = 0);

		private:
			friend class CResultSet;

			CResultSet const* mp_pResultSet;
			NContainer::TCVector<mint> mp_ColumnValueIterators;
			mint mp_iRow;

			mint mp_iColumn;
		};

		// Allows forward iteration over all rows in a result set.
		struct CRowIterator
		{
			CRowIterator();

			// Move to the next row
			CRowIterator & operator ++ ();

			// Returns:
			//	true: If the iterator points to a row within the result set.
			//	false: If the iterator points to the end of a result set.
			explicit operator bool () const;

			// Allows type-safe access to a specific key part of the current row.
			template <typename tf_CType>
			tf_CType const& f_GetKeyPart(mint _iKeyPart) const;

			inline uint32 f_GetKeyPartValueIndex(mint _iKeyPart) const;

			// Allows type-safe access to a specific column value of the current row.
			// This may return nullptr if the row does not have a value for the specified column.
			template <typename tf_CType>
			tf_CType const* f_GetColumnValue(mint _iColumn) const;

			// Returns an iterator allowing for iteration over each key part.
			CKeyPartIterator f_GetKeyPartIterator() const;

			// Returns an iterator starting at the specified key part allowing for iteration over each key part.
			CKeyPartIterator f_GetKeyPartIterator(mint _iKP) const;

			// Returns an iterator allowing for iteration over each column value.
			CColumnValueIterator f_GetColumnIterator() const;

			// Returns an iterator allowing for iteration over each column value.
			CColumnValueIterator f_GetColumnIterator(mint _iCol) const;

			mint f_GetIndex() const;
			mint f_GetNumRows() const;

		private:
			friend class CResultSet;

			CRowIterator(CResultSet const& _ResultSet, mint _iRow = 0);

			CResultSet const* mp_pResultSet;
			mint const mp_nRows;		// Cached from mp_pResultSet

			mint mp_iCurRow;	// The index of the current row , The iterator is only valid if within bounds [0, mp_pResultSet->f_GetNumRows).
			NContainer::TCVector<mint> mp_ColumnValueIterators; // The position in each of the column sparse data vectors we are currently at.
		};


	private:
		friend struct CColumnValueIterator;
		friend struct CRowIterator;
		friend class CResultSetBuilder;

	public:
		CResultSet();
		CResultSet(CResultSet&& _ToMove);
		~CResultSet();

		void f_Clear();

		bool f_IsEmpty() const;

		mint f_GetNumKeyParts() const;
		mint f_GetNumColumns() const;

		EResultDataType f_GetKeyPartType(mint _iKeyPart) const;
		NResultSet::CSubType f_GetKeyPartSubType(mint _iKeyPart) const;

		template<typename t_CType>
		NContainer::TCVector<t_CType> const& f_GetKeyPartValues(mint _iKeyPart) const;

		EResultDataType f_GetColumnType(mint _iColumn) const;
		NResultSet::CSubType f_GetColumnSubType(mint _iColumn) const;

		mint f_GetNumRows() const;

		CResultMetaData const*f_GetMetaData(uint32 _ID) const;
		CResultMetaData const& f_GetKeyPartMetaData(mint _iKeyPart) const;
		CResultMetaData const& f_GetColumnMetaData(mint _iKeyPart) const;

		CRowIterator f_GetRowIterator() const;
		// Returns an iterator starting at the specified row allowing for iteration over each row.
		CRowIterator f_GetRowIterator(mint _iRow) const;

		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const;

		template <typename tf_CStream>
		void f_Consume(tf_CStream &_Stream);
	};
}


#include "Malterlib_Database_ResultSet.hpp"

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase;
#endif
