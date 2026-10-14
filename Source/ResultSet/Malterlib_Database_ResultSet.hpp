// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDatabase
{
	//
	// CResultSet::CKeyPartIterator Public Implementation
	//

	template <typename t_CType>
	t_CType const &CResultSet::CKeyPartIterator::f_Get() const
	{
		auto const &KeyPartRowValueIndices = mp_pResultSet->mp_KeyPartRowValueIndices;
		auto const &KeyPartValues = mp_pResultSet->mp_KeyPartValues;

		static constexpr EResultDataType KeyPartType = NResultSet::TCTypeToDataType<t_CType>::mc_Type;

		umint iValue = KeyPartRowValueIndices[mp_iKeyPart][mp_iRow];

		auto const &lValuesArray = KeyPartValues[mp_iKeyPart].f_Get<KeyPartType>();

		return lValuesArray[iValue];
	}

	uint32 CResultSet::CKeyPartIterator::f_GetValueIndex() const
	{
		auto const &KeyPartRowValueIndices = mp_pResultSet->mp_KeyPartRowValueIndices;

		umint iValue = KeyPartRowValueIndices[mp_iKeyPart][mp_iRow];

		return iValue;
	}

	EResultDataType CResultSet::CKeyPartIterator::f_GetType() const
	{
		return mp_pResultSet->mp_KeyPartInfos[mp_iKeyPart].m_Type;
	}

	NResultSet::CSubType CResultSet::CKeyPartIterator::f_GetSubType() const
	{
		return mp_pResultSet->mp_KeyPartInfos[mp_iKeyPart].m_SubType;
	}

	umint CResultSet::CKeyPartIterator::f_GetIndex() const
	{
		return mp_iKeyPart;
	}

	//
	// CResultSet::CColumnValueIterator Public Implementation
	//

	template <typename t_CType>
	t_CType const *CResultSet::CColumnValueIterator::f_Get() const
	{
		auto const &Columns = mp_pResultSet->mp_Columns;

		umint iCurColPos = mp_ColumnValueIterators[mp_iColumn];

		static constexpr EResultDataType ColumnType = NResultSet::TCTypeToDataType<t_CType>::mc_Type;

		auto const &lValueArray = Columns[mp_iColumn].f_Get<ColumnType>();

		if (iCurColPos < lValueArray.f_GetLen() && lValueArray[iCurColPos].m_iRow == mp_iRow)
			return &lValueArray[iCurColPos].m_Data;
		else
			return nullptr;
	}

	inline EResultDataType CResultSet::CColumnValueIterator::f_GetType() const
	{
		return mp_pResultSet->mp_ColumnInfos[mp_iColumn].m_Type;
	}

	inline NResultSet::CSubType CResultSet::CColumnValueIterator::f_GetSubType() const
	{
		return mp_pResultSet->mp_ColumnInfos[mp_iColumn].m_SubType;
	}

	inline umint CResultSet::CColumnValueIterator::f_GetIndex() const
	{
		return mp_iColumn;
	}

	//
	// CResultSet::CRowIterator Public Implementation
	//

	template <typename t_CType>
	t_CType const &CResultSet::CRowIterator::f_GetKeyPart(umint _iKeyPart) const
	{
		static constexpr EResultDataType KeyPartType = NResultSet::TCTypeToDataType<t_CType>::mc_Type;

		umint iValue = mp_pResultSet->mp_KeyPartRowValueIndices[_iKeyPart][mp_iCurRow];

		return mp_pResultSet->mp_KeyPartValues[_iKeyPart].f_Get<KeyPartType>()[iValue];
	}

	uint32 CResultSet::CRowIterator::f_GetKeyPartValueIndex(umint _iKeyPart) const
	{
		return mp_pResultSet->mp_KeyPartRowValueIndices[_iKeyPart][mp_iCurRow];
	}

	template <typename t_CType>
	t_CType const *CResultSet::CRowIterator::f_GetColumnValue(umint _iColumn) const
	{
		auto const &Columns = mp_pResultSet->mp_Columns;

		umint iCurColPos = mp_ColumnValueIterators[_iColumn];

		static constexpr EResultDataType ColumnType = NResultSet::TCTypeToDataType<t_CType>::mc_Type;

		auto const &lValueArray = Columns[_iColumn].f_Get<ColumnType>();

		if (iCurColPos < lValueArray.f_GetLen() && lValueArray[iCurColPos].m_iRow == mp_iCurRow)
			return &lValueArray[iCurColPos].m_Data;
		else
			return nullptr;
	}

	inline umint CResultSet::CRowIterator::f_GetIndex() const
	{
		return mp_iCurRow;
	}

	inline umint CResultSet::CRowIterator::f_GetNumRows() const
	{
		return mp_nRows;
	}

	//
	// ResultSet Public Implementation
	//

	template <typename t_CType>
	NContainer::TCVector<t_CType> const &CResultSet::f_GetKeyPartValues(umint _iKeyPart) const
	{
		auto const &KeyPartValues = mp_KeyPartValues;

		static constexpr EResultDataType KeyPartType = NResultSet::TCTypeToDataType<t_CType>::mc_Type;

		auto const &lValueArray = KeyPartValues[_iKeyPart].f_Get<KeyPartType>();

		return lValueArray;
	}

	/*
	template <typename tf_CStream>
	void CResultSet::f_Feed(tf_CStream &_Stream) const
	{
		// Version
		_Stream << (uint32)NResultSet::EResultSetFormatVersion;

		// Structure & Metadata
		_Stream << mp_Metadata;
		_Stream << mp_KeyPartInfos;
		_Stream << mp_ColumnInfos;

		// Data
		_Stream << (uint32)mp_nRows;
		_Stream << mp_KeyParts;
		_Stream << mp_Columns;
	}
	*/
	template <typename tf_CStream>
	void CResultSet::f_Consume(tf_CStream &_Stream)
	{
		f_Clear();

		// Version
		uint32 Version = 0;
		_Stream >> Version;

		if (Version != NResultSet::EResultSetFormatVersion)
			DMibErrorStreamVersionMismatch("Unsupported result set version!");

		DMibBinaryStreamVersion(_Stream, Version);

		// Structure & Metadata
		_Stream >> mp_Metadata;
		_Stream >> mp_KeyPartInfos;
		_Stream >> mp_ColumnInfos;

		// Data
		_Stream >> mp_KeyPartValues;

		uint32 nRows;
		_Stream >> nRows;
		mp_nRows = nRows;
		_Stream >> mp_KeyPartRowValueIndices;
		_Stream >> mp_Columns;
	}
}
