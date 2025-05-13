// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NDatabase
{
	//
	// CResultSetDescription::CAccessor Public Implementation
	//

	CResultSetDescription::CAccessor::CAccessor(CResultSetDescription& _Desc)
		: mp_Desc(_Desc)
	{
	}

	//
	// CResultSetDescription::CAccessor Public Implementation
	//

	CResultSetDescription::CAccessor& CResultSetDescription::CAccessor::f_KeyPart(EResultDataType _Type, NResultSet::CSubType _SubType)
	{
		DMibCheck(mp_Desc.m_lColumnTypes.f_IsEmpty())("You must specify all key part types before specifying any column types" );
		mp_Desc.m_lKeyPartTypes.f_Insert(_Type);
		mp_Desc.m_lKeyPartSubTypes.f_Insert(_SubType);

		return *this;
	}

	CResultSetDescription::CAccessor& CResultSetDescription::CAccessor::f_Column(EResultDataType _Type, NResultSet::CSubType _SubType)
	{
		DMibCheck(!mp_Desc.m_lKeyPartTypes.f_IsEmpty())("Specifying column type before key part type." );
		mp_Desc.m_lColumnTypes.f_Insert(_Type);
		mp_Desc.m_lColumnSubTypes.f_Insert(_SubType);

		return *this;
	}

	//
	// CResultSetDescription Public Implementation
	//

	CResultSetDescription::CAccessor CResultSetDescription::f_KeyPart(EResultDataType _Type, NResultSet::CSubType _SubType)
	{
		DMibCheck(m_lColumnTypes.f_IsEmpty())("You must specify all key part types before specifying any column types" );
		m_lKeyPartTypes.f_Insert(_Type);
		m_lKeyPartSubTypes.f_Insert(_SubType);

		return CAccessor(*this);
	}

	CResultSetDescription::CAccessor CResultSetDescription::f_Column(EResultDataType _Type, NResultSet::CSubType _SubType)
	{
		DMibCheck(!m_lKeyPartTypes.f_IsEmpty())("Specifying column type before key part type." );
		m_lColumnTypes.f_Insert(_Type);
		m_lColumnSubTypes.f_Insert(_SubType);

		return CAccessor(*this);
	}

	//
	// CResultSetBuilder::CRow Public Implementation
	//
/*
	CResultSetBuilder::CRow& CResultSetBuilder::CRow::f_KeyPartVariant(NResultSet::CValue const& _Value)
	{
		DMibCheck( mp_pBuilder)("Adding a keypart to an invalid row." );
		DMibCheck( mp_iCurKeyPart < mp_nKeyParts)("Too many key parts are being specified!" );

		DMibCheck( mp_pBuilder->mp_KeyPartInfos[mp_iCurKeyPart].m_Type ==  _Value.f_GetTypeID())("Invalid type used for key part");

		switch(mp_pBuilder->mp_KeyPartInfos[mp_iCurKeyPart].m_Type)
		{
			case EResultDataType_CStr:
				mp_pBuilder->mp_KeyParts[mp_iCurKeyPart]
						.f_Get< EResultDataType_CStr >()
						.f_Insert(
								_Value.f_Get<EResultDataType_CStr>()
						);
				break;
			case EResultDataType_fp64:
				mp_pBuilder->mp_KeyParts[mp_iCurKeyPart]
						.f_Get< EResultDataType_fp64 >()
						.f_Insert(
								_Value.f_Get<EResultDataType_fp64>()
						);
				break;
			case EResultDataType_uint64:
				mp_pBuilder->mp_KeyParts[mp_iCurKeyPart]
						.f_Get< EResultDataType_uint64 >()
						.f_Insert(
								_Value.f_Get<EResultDataType_uint64>()
						);
				break;
			case EResultDataType_Binary:
				mp_pBuilder->mp_KeyParts[mp_iCurKeyPart]
						.f_Get< EResultDataType_Binary >()
						.f_Insert(
								_Value.f_Get<EResultDataType_Binary>()
						);
				break;

			default:
				DMibNeverGetHere;
				break;
		}

		++mp_iCurKeyPart;

		return *this;
	}
*/

	CResultSetBuilder::CRow& CResultSetBuilder::CRow::f_KeyPart(uint32 _iValue)
	{
		using NResultSet::TCTypeToDataType;

		DMibCheck( mp_pBuilder)("Adding a keypart to an invalid row." );
		DMibCheck( mp_iCurKeyPart < mp_nKeyParts)("Too many key parts are being specified!" );

		mp_pBuilder->mp_KeyPartRowValueIndices[mp_iCurKeyPart].f_Insert(_iValue);

		++mp_iCurKeyPart;

		return *this;
	}

	template<typename t_CType>
	CResultSetBuilder::CRow& CResultSetBuilder::CRow::f_Column(t_CType&& _Value)
	{
		using NResultSet::TCTypeToDataType;

		static constexpr EResultDataType Type = TCTypeToDataType<t_CType>::mc_Type;

		DMibCheck( mp_pBuilder)("Adding a column value to an invalid row." );
		DMibCheck( mp_iCurKeyPart == mp_nKeyParts)("A column value is being specified before all key parts have been." );
		DMibCheck( mp_iCurColumn < mp_nColumns)("Too many column values are being specified!" );
		DMibCheck( mp_pBuilder->mp_ColumnInfos[mp_iCurColumn].m_Type ==  Type)("Invalid type used for column value");

		mp_pBuilder->mp_Columns[mp_iCurColumn]
				.f_Get<Type>()
				.f_Insert( { mp_iRow, TCTypeToDataType<t_CType>::fs_Convert( std::forward<t_CType>(_Value) ) } );

		++mp_iCurColumn;

		return *this;
	}


	//
	// CResultSetBuilder::CKeyPart Public Implementation
	//

	inline CResultSetBuilder::CKeyPart& CResultSetBuilder::CKeyPart::f_AddValue(uint32 _iIndex)
	{

		DMibCheck(mp_pBuilder)("Adding a key part value to an invalid key part.");
		DMibCheck( mp_iKeyPart < mp_pBuilder->mp_KeyPartInfos.f_GetLen())("Invalid key part");

		mp_pBuilder->mp_KeyPartRowValueIndices[mp_iKeyPart].f_Insert(_iIndex);

		mp_pBuilder->mp_nRows = fg_Max( mp_pBuilder->mp_nRows , mp_pBuilder->mp_KeyPartRowValueIndices[mp_iKeyPart].f_GetLen() );

		return *this;
	}

	//
	// CResultSetBuilder::CColumn Public Implementation
	//

	template<typename t_CType>
	inline CResultSetBuilder::CColumn& CResultSetBuilder::CColumn::f_AddValue(t_CType&& _Value)
	{
		using NResultSet::TCTypeToDataType;

		static constexpr EResultDataType Type = TCTypeToDataType<t_CType>::mc_Type;

		DMibCheck(mp_pBuilder)("Adding a key part value to an invalid column.");
		DMibCheck( mp_iColumn < mp_pBuilder->mp_ColumnInfos.f_GetLen())("Invalid column");

		auto& lArray = mp_pBuilder->mp_Columns[mp_iColumn].f_Get<Type>();

		if (mp_iCurRow == 0 && !lArray.f_IsEmpty())
			mp_iCurRow = lArray.f_GetLast().m_iRow;

		DMibCheck( mp_iCurRow < mp_pBuilder->mp_nRows )("Attempting to add a column value for a row that has no key parts specified.");

		lArray.f_Insert( { mp_iCurRow, TCTypeToDataType<t_CType>::fs_Convert( std::forward<t_CType>(_Value) ) } );

		++mp_iCurRow;

		return *this;
	}

	inline CResultSetBuilder::CColumn& CResultSetBuilder::CColumn::f_AddValue(std::nullptr_t)
	{

		if (mp_iCurRow == 0)
		{
			auto const& ArrayVariant = mp_pBuilder->mp_Columns[mp_iColumn];

			switch(ArrayVariant.f_GetTypeID())
			{
				case EResultDataType_CStr:
					if (!ArrayVariant.f_Get<EResultDataType_CStr>().f_IsEmpty())
						mp_iCurRow = ArrayVariant.f_Get<EResultDataType_CStr>().f_GetLast().m_iRow;
					break;
				case EResultDataType_fp64:
					if (!ArrayVariant.f_Get<EResultDataType_fp64>().f_IsEmpty())
						mp_iCurRow = ArrayVariant.f_Get<EResultDataType_fp64>().f_GetLast().m_iRow;
					break;
				case EResultDataType_uint64:
					if (!ArrayVariant.f_Get<EResultDataType_uint64>().f_IsEmpty())
						mp_iCurRow = ArrayVariant.f_Get<EResultDataType_uint64>().f_GetLast().m_iRow;
					break;
				case EResultDataType_Binary:
					if (!ArrayVariant.f_Get<EResultDataType_Binary>().f_IsEmpty())
						mp_iCurRow = ArrayVariant.f_Get<EResultDataType_Binary>().f_GetLast().m_iRow;
					break;
/* Deprecated:
				case EResultDataType_int32:
					if (!ArrayVariant.f_Get<EResultDataType_int32>().f_IsEmpty())
						mp_iCurRow = ArrayVariant.f_Get<EResultDataType_int32>().f_GetLast().m_iRow;
					break;
				case EResultDataType_int64:
					if (!ArrayVariant.f_Get<EResultDataType_int64>().f_IsEmpty())
						mp_iCurRow = ArrayVariant.f_Get<EResultDataType_int64>().f_GetLast().m_iRow;
					break;
				case EResultDataType_fp32:
					if (!ArrayVariant.f_Get<EResultDataType_fp32>().f_IsEmpty())
						mp_iCurRow = ArrayVariant.f_Get<EResultDataType_fp32>().f_GetLast().m_iRow;
					break;
*/						
			}
		}

		DMibCheck( mp_iCurRow < mp_pBuilder->mp_nRows )("Attempting to add a column value for a row that has no key parts specified.");

		++mp_iCurRow;
		return *this;
	}

	//
	// CResultSetBuilder Public Implementation
	//

	template<typename t_CType>
	mint CResultSetBuilder::f_AddKeyPartValue(uint32 _iKeyPart, t_CType && _Value)
	{
		using NResultSet::TCTypeToDataType;

		static constexpr EResultDataType Type = TCTypeToDataType<t_CType>::mc_Type;

		DMibCheck( _iKeyPart < mp_KeyPartInfos.f_GetLen())("Values for too many key parts are being specified!" );
		DMibCheck( mp_KeyPartInfos[_iKeyPart].m_Type ==  Type)("Invalid type used for key part values");

		auto & lKeyPartValues = mp_KeyPartValues[_iKeyPart].f_Get< Type >();

		lKeyPartValues.f_Insert(
						TCTypeToDataType<t_CType>::fs_Convert( std::forward<t_CType>(_Value) )
				);

		return lKeyPartValues.f_GetLen();
	}

	template<typename t_CType>
	mint CResultSetBuilder::f_SetKeyPartValues(uint32 _iKeyPart, NContainer::TCVector<t_CType> && _lValues)
	{
		using NResultSet::TCTypeToDataType;

		static constexpr EResultDataType Type = TCTypeToDataType<t_CType>::mc_Type;

		DMibCheck( _iKeyPart < mp_KeyPartInfos.f_GetLen())("Values for too many key parts are being specified!" );
		DMibCheck( mp_KeyPartInfos[_iKeyPart].m_Type ==  Type)("Invalid type used for key part values");

		auto & lKeyPartValues = mp_KeyPartValues[_iKeyPart].f_Get< Type >();

		lKeyPartValues = fg_Move(_lValues);

		return 0;
	}

	mint CResultSetBuilder::f_GetNumRows() const
	{
		return mp_nRows;
	}

	template <typename tf_CStream>
	void CResultSetBuilder::f_Feed(tf_CStream &_Stream) const
	{
		DMibCheck( f_IsValid() )("Attempting to stream a result set that is not valid.");

		// Version
		_Stream << (uint32)NResultSet::EResultSetFormatVersion;

		// Structure & Metadata
		_Stream << mp_Metadata;
		_Stream << mp_KeyPartInfos;
		_Stream << mp_ColumnInfos;

		// Data
		_Stream << mp_KeyPartValues;

		_Stream << (uint32)mp_nRows;
		_Stream << mp_KeyPartRowValueIndices;
		_Stream << mp_Columns;
	}

	template <typename tf_CStream>
	void CResultSetBuilder::f_Consume(tf_CStream &_Stream)
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
