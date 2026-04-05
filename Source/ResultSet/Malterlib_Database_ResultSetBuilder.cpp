// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
CResultSetBuilder
^^^^^^^^^^^^^^^^^

Utility class to build up a result set piecemeal.
*/
#include "Malterlib_Database_ResultSetBuilder.h"

namespace NMib::NDatabase
{
	namespace
	{
		// Utilities to get the length of a vector contained in a variant.
		struct CVectorVariantSizeVisitor
		{
			umint m_Length;

			CVectorVariantSizeVisitor()
				: m_Length(0)
			{
			}

			template <typename tf_CVector>
			void operator ()(tf_CVector const& _Vector)
			{
				m_Length = _Vector.f_GetLen();
			}
		};

		template<typename t_VectorVariant>
		umint fg_GetVectorVariantLength(t_VectorVariant const& _VecVar)
		{
			CVectorVariantSizeVisitor Visitor;

			fg_Visit(Visitor, _VecVar);

			return Visitor.m_Length;
		}
	}

	CResultSetBuilder::CResultSetBuilder()
		: mp_nRows(0)
		, mp_bRowActive(false)
		, mp_nColumnWiseActive(0)
	{

	}

	CResultSetBuilder::CResultSetBuilder(CResultSetBuilder&& _ToMove)
		: mp_Metadata(fg_Move(_ToMove.mp_Metadata))
		, mp_KeyPartInfos(fg_Move(_ToMove.mp_KeyPartInfos))
		, mp_ColumnInfos(fg_Move(_ToMove.mp_ColumnInfos))
		, mp_KeyPartValues(fg_Move(_ToMove.mp_KeyPartValues))
		, mp_nRows(_ToMove.mp_nRows)
		, mp_KeyPartRowValueIndices(fg_Move(_ToMove.mp_KeyPartRowValueIndices))
		, mp_Columns(fg_Move(_ToMove.mp_Columns))
		, mp_bRowActive(_ToMove.mp_bRowActive)
		, mp_nColumnWiseActive(_ToMove.mp_nColumnWiseActive)
	{
		DMibCheck(!_ToMove.mp_bRowActive)("You cannot move a result set builder when you are still constructing it!");
		DMibCheck(!_ToMove.mp_nColumnWiseActive)("You cannot move a result set builder when you are still constructing it!");
	}

	CResultSetBuilder::CResultSetBuilder(NContainer::TCVector<EResultDataType> const& _lKeyPartTypes, NContainer::TCVector<NResultSet::CSubType> const& _lKeyPartSubTypes, NContainer::TCVector<EResultDataType> const& _lColumnTypes, NContainer::TCVector<NResultSet::CSubType> const& _lColumnSubTypes)
		: mp_nRows(0)
		, mp_bRowActive(false)
	{
		f_SetDescription(_lKeyPartTypes, _lKeyPartSubTypes, _lColumnTypes, _lColumnSubTypes);
	}

	CResultSetBuilder::CResultSetBuilder(CResultSetDescription const& _Desc)
		: mp_nRows(0)
		, mp_bRowActive(false)
		, mp_nColumnWiseActive(0)
	{
		f_SetDescription(_Desc);
	}

	CResultSetBuilder::~CResultSetBuilder()
	{
	}

	bool CResultSetBuilder::f_IsValid() const
	{
		// Check that each key part has the same number of rows as we think they have.
		for (auto const& KeyPartValueIndices : mp_KeyPartRowValueIndices)
		{
			if (mp_nRows != KeyPartValueIndices.f_GetLen())
				return false;
		}

		return true;
	}

	void CResultSetBuilder::f_Clear()
	{
		mp_Metadata.f_Clear();
		mp_KeyPartInfos.f_Clear();
		mp_ColumnInfos.f_Clear();

		mp_KeyPartValues.f_Clear();

		mp_nRows = 0;
		mp_KeyPartRowValueIndices.f_Clear();
		mp_Columns.f_Clear();
	}

	void CResultSetBuilder::f_SetDescription(NContainer::TCVector<EResultDataType> const& _lKeyPartTypes, NContainer::TCVector<NResultSet::CSubType> const& _lKeyPartSubTypes, NContainer::TCVector<EResultDataType> const& _lColumnTypes, NContainer::TCVector<NResultSet::CSubType> const& _lColumnSubTypes)
	{
		f_Clear();

		// Set the key part types.
		mp_KeyPartInfos.f_SetLen(_lKeyPartTypes.f_GetLen());
		mp_KeyPartValues.f_SetLen(_lKeyPartTypes.f_GetLen());
		mp_KeyPartRowValueIndices.f_SetLen(_lKeyPartTypes.f_GetLen());

		auto iKeyPartType = _lKeyPartTypes.f_GetIterator();
		auto iKeyPartSubType = _lKeyPartSubTypes.f_GetIterator();
		auto iKeyPartValues = mp_KeyPartValues.f_GetIterator();
		for (auto& KeyPartInfo : mp_KeyPartInfos)
		{
			KeyPartInfo.m_Type = *iKeyPartType;
			KeyPartInfo.m_SubType = *iKeyPartSubType;

			switch(*iKeyPartType)
			{
				case EResultDataType_CStr:
					(*iKeyPartValues).f_Set<EResultDataType_CStr>();
					break;
				case EResultDataType_fp64:
					(*iKeyPartValues).f_Set<EResultDataType_fp64>();
					break;
				case EResultDataType_uint64:
					(*iKeyPartValues).f_Set<EResultDataType_uint64>();
					break;
				case EResultDataType_Binary:
					(*iKeyPartValues).f_Set<EResultDataType_Binary>();
					break;
/* Deprecated:
				case EResultDataType_int32:
					(*iKeyPart).f_Set<EResultDataType_int32>();
					break;
				case EResultDataType_int64:
					(*iKeyPart).f_Set<EResultDataType_int64>();
					break;
				case EResultDataType_fp32:
					(*iKeyPart).f_Set<EResultDataType_fp32>();
					break;
*/
			}

			++iKeyPartType;
			++iKeyPartSubType;
			++iKeyPartValues;
		}


		// Set the column types.
		mp_ColumnInfos.f_SetLen(_lColumnTypes.f_GetLen());
		mp_Columns.f_SetLen(_lColumnTypes.f_GetLen());

		auto iColumnType = _lColumnTypes.f_GetIterator();
		auto iColumnSubType = _lColumnSubTypes.f_GetIterator();
		auto iColumn = mp_Columns.f_GetIterator();
		for (auto& ColumnInfo : mp_ColumnInfos)
		{
			ColumnInfo.m_Type = *iColumnType;
			ColumnInfo.m_SubType = *iColumnSubType;

			switch(*iColumnType)
			{
				case EResultDataType_CStr:
					(*iColumn).f_Set<EResultDataType_CStr>();
					break;
				case EResultDataType_fp64:
					(*iColumn).f_Set<EResultDataType_fp64>();
					break;
				case EResultDataType_uint64:
					(*iColumn).f_Set<EResultDataType_uint64>();
					break;
				case EResultDataType_Binary:
					(*iColumn).f_Set<EResultDataType_Binary>();
					break;
/*
				case EResultDataType_int32:
					(*iColumn).f_Set<EResultDataType_int32>();
					break;
				case EResultDataType_int64:
					(*iColumn).f_Set<EResultDataType_int64>();
					break;
				case EResultDataType_fp32:
					(*iColumn).f_Set<EResultDataType_fp32>();
					break;
*/
			}

			++iColumnType;
			++iColumnSubType;
			++iColumn;
		}

	}


	void CResultSetBuilder::f_SetDescription( CResultSetDescription const& _Desc )
	{
		f_SetDescription(_Desc.m_lKeyPartTypes, _Desc.m_lKeyPartSubTypes, _Desc.m_lColumnTypes, _Desc.m_lColumnSubTypes);
	}

	void CResultSetBuilder::f_AddMetadata(uint32 _ID, CResultMetadata _Data)
	{
		mp_Metadata[_ID] = fg_Move(_Data);
	}

	void CResultSetBuilder::f_SetKeyPartMetadata(umint _iKeyPart, CResultMetadata _Data)
	{
		DMibCheck( mp_KeyPartInfos.f_IsPosValid(_iKeyPart))("Invalid key part");
		mp_KeyPartInfos[_iKeyPart].m_Metadata = fg_Move(_Data);
	}

	void CResultSetBuilder::f_SetColumnMetadata(umint _iColumn, CResultMetadata _Data)
	{
		DMibCheck( mp_ColumnInfos.f_IsPosValid(_iColumn))("Invalid column");
		mp_ColumnInfos[_iColumn].m_Metadata = fg_Move(_Data);
	}

	umint CResultSetBuilder::f_AddKeyPartVariantValue(uint32 _iKeyPart, NResultSet::CValue const& _Value)
	{
		DMibCheck( mp_KeyPartInfos.f_IsPosValid(_iKeyPart) )("Values for too many key parts are being specified!" );

		umint iIndex = 0;

		switch(mp_KeyPartInfos[_iKeyPart].m_Type)
		{
			case EResultDataType_CStr:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_CStr >();

					lValues.f_Insert(
									_Value.f_Get<EResultDataType_CStr>()
							);
					iIndex = lValues.f_GetLen() - 1;
					break;
				}
			case EResultDataType_fp64:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_fp64 >();

					lValues.f_Insert(
									_Value.f_Get<EResultDataType_fp64>()
							);
					iIndex = lValues.f_GetLen() - 1;
					break;
				}
			case EResultDataType_uint64:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_uint64 >();

					lValues.f_Insert(
									_Value.f_Get<EResultDataType_uint64>()
							);
					iIndex = lValues.f_GetLen() - 1;
					break;
				}
			case EResultDataType_Binary:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_Binary >();

					lValues.f_Insert(
									_Value.f_Get<EResultDataType_Binary>()
							);
					iIndex = lValues.f_GetLen() - 1;
					break;
				}

			default:
				DMibNeverGetHere;
				break;
		}

		return iIndex;
	}

	umint CResultSetBuilder::f_SetKeyPartVariantValues(uint32 _iKeyPart, NContainer::TCVector<NResultSet::CValue>  const& _lValues)
	{
		DMibCheck( mp_KeyPartInfos.f_IsPosValid(_iKeyPart) )("Values for too many key parts are being specified!" );

		switch(mp_KeyPartInfos[_iKeyPart].m_Type)
		{
			case EResultDataType_CStr:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_CStr >();

					lValues.f_Clear();
					for (auto const& CurValue : _lValues)
						lValues.f_Insert(CurValue.f_Get<EResultDataType_CStr>());
					break;
				}
			case EResultDataType_fp64:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_fp64 >();

					lValues.f_Clear();
					for (auto const& CurValue : _lValues)
						lValues.f_Insert(CurValue.f_Get<EResultDataType_fp64>());

					break;
				}
			case EResultDataType_uint64:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_uint64 >();

					lValues.f_Clear();
					for (auto const& CurValue : _lValues)
						lValues.f_Insert(CurValue.f_Get<EResultDataType_uint64>());

					break;
				}
			case EResultDataType_Binary:
				{
					auto & lValues = mp_KeyPartValues[_iKeyPart]
							.f_Get< EResultDataType_Binary >();

					lValues.f_Clear();
					for (auto const& CurValue : _lValues)
						lValues.f_Insert(CurValue.f_Get<EResultDataType_Binary>());

					break;
				}

			default:
				DMibNeverGetHere;
				break;
		}

		return 0;
	}

	CResultSetBuilder::CRow CResultSetBuilder::f_AddRow()
	{
		DMibCheck(!mp_bRowActive)("You can only add one row at a time.");
		DMibCheck(mp_nColumnWiseActive == 0)("You can only add a row when not adding columns or key parts..");

		mp_bRowActive = true;

		uint32 iRow = (uint32)mp_nRows;
		++mp_nRows;

		return CRow(*this, iRow);
	}


	CResultSetBuilder::CKeyPart CResultSetBuilder::f_KeyPart(uint32 _iKeyPart)
	{
		DMibCheck(!mp_bRowActive)("You cannot mix and match row and column wise building!");

		DMibCheck(_iKeyPart < mp_KeyPartInfos.f_GetLen() )("Invalid key part");

		++mp_nColumnWiseActive;

		return CKeyPart(*this, _iKeyPart);
	}

	CResultSetBuilder::CColumn CResultSetBuilder::f_Column(uint32 _iColumn)
	{
		DMibCheck(!mp_bRowActive)("You cannot mix and match row and column wise building!");

		DMibCheck(_iColumn < mp_ColumnInfos.f_GetLen() )("Invalid column");

		++mp_nColumnWiseActive;

		return CColumn(*this, _iColumn);
	}

	void CResultSetBuilder::f_ExtractTo(CResultSet& _oRS)
	{
		DMibCheck(!mp_bRowActive)("No row must be active when extracting.");

		_oRS.f_Clear();

		for (auto iMetadata = mp_Metadata.f_GetIterator(); iMetadata; ++iMetadata)
		{
			_oRS.mp_Metadata.f_Insert( { iMetadata.f_GetKey(), fg_Move(*iMetadata) } );
		}

		_oRS.mp_KeyPartInfos = fg_Move(mp_KeyPartInfos);
		_oRS.mp_ColumnInfos = fg_Move(mp_ColumnInfos);
		_oRS.mp_KeyPartValues = fg_Move(mp_KeyPartValues);
		_oRS.mp_nRows = mp_nRows;
		_oRS.mp_KeyPartRowValueIndices = fg_Move(mp_KeyPartRowValueIndices);
		_oRS.mp_Columns = fg_Move(mp_Columns);

		f_Clear();
	}

	//
	// CResultSetBuilder::CRow Private Implementation
	//

	CResultSetBuilder::CRow::CRow(CResultSetBuilder& _Builder, uint32 _iRow)
		: mp_pBuilder(&_Builder)
		, mp_nKeyParts(_Builder.mp_KeyPartInfos.f_GetLen())
		, mp_nColumns(_Builder.mp_ColumnInfos.f_GetLen())
		, mp_iRow(_iRow)
		, mp_iCurKeyPart(0)
		, mp_iCurColumn(0)
	{
	}

	//
	// CResultSetBuilder::CRow Public Implementation
	//


	CResultSetBuilder::CRow::CRow(CRow&& _ToMove)
		: mp_pBuilder(_ToMove.mp_pBuilder)
		, mp_nKeyParts(_ToMove.mp_nKeyParts)
		, mp_nColumns(_ToMove.mp_nColumns)
		, mp_iRow(_ToMove.mp_iRow)
		, mp_iCurKeyPart(_ToMove.mp_iCurKeyPart)
		, mp_iCurColumn(_ToMove.mp_iCurColumn)
	{
		_ToMove.mp_pBuilder = nullptr;
		_ToMove.mp_nKeyParts = 0;
		_ToMove.mp_nColumns = 0;
		_ToMove.mp_iRow = 0;
		_ToMove.mp_iCurKeyPart = 0;
		_ToMove.mp_iCurColumn = 0;
	}

	CResultSetBuilder::CRow::~CRow()
	{
		if (mp_pBuilder)
		{
			DMibCheck(NException::fg_UncaughtExceptions() || mp_iCurKeyPart == mp_nKeyParts)("Row not complete! There are still key parts and columns to fill in!");
			DMibCheck(NException::fg_UncaughtExceptions() || mp_iCurColumn == mp_nColumns)("Row not complete! There are still columns to fill in!");
			mp_pBuilder->mp_bRowActive = false;
		}
	}

	CResultSetBuilder::CRow& CResultSetBuilder::CRow::f_Column(std::nullptr_t)
	{
		DMibCheck( mp_pBuilder)("Adding a column value to an invalid row." );
		DMibCheck( mp_iCurKeyPart == mp_nKeyParts)("A column value is being specified before all key parts have been." );
		DMibCheck( mp_iCurColumn < mp_nColumns)("Too many column values are being specified!" );

		++mp_iCurColumn;

		return *this;
	}

	//
	// CResultSetBuilder::CKeyPart Private Implementation
	//

	CResultSetBuilder::CKeyPart::CKeyPart(CResultSetBuilder& _Builder, uint32 _iKeyPart)
		: mp_pBuilder(&_Builder)
		, mp_iKeyPart(_iKeyPart)
	{
	}

	//
	// CResultSetBuilder::CKeyPart Public Implementation
	//

	CResultSetBuilder::CKeyPart::CKeyPart(CKeyPart&& _ToMove)
		: mp_pBuilder(_ToMove.mp_pBuilder)
		, mp_iKeyPart(_ToMove.mp_iKeyPart)
	{
		_ToMove.mp_pBuilder = nullptr;
	}

	CResultSetBuilder::CKeyPart::~CKeyPart()
	{
		if (mp_pBuilder)
		{
			--mp_pBuilder->mp_nColumnWiseActive;
		}
	}


	//
	// CResultSetBuilder::CColumn Private Implementation
	//

	CResultSetBuilder::CColumn::CColumn(CResultSetBuilder& _Builder, uint32 _iColumn)
		: mp_pBuilder(&_Builder)
		, mp_iColumn(_iColumn)
		, mp_iCurRow(0)
	{
	}

	//
	// CResultSetBuilder::CColumn Public Implementation
	//

	CResultSetBuilder::CColumn::CColumn(CColumn&& _ToMove)
		: mp_pBuilder(_ToMove.mp_pBuilder)
		, mp_iColumn(_ToMove.mp_iColumn)
		, mp_iCurRow(0)
	{
		_ToMove.mp_pBuilder = nullptr;
	}

	CResultSetBuilder::CColumn::~CColumn()
	{
		if (mp_pBuilder)
		{
			--mp_pBuilder->mp_nColumnWiseActive;
		}
	}
}
