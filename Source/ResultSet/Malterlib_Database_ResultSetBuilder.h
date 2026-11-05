// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
CResultSetBuilder
^^^^^^^^^^^^^^^^^

Utility class to build up a result set row by row.

Usage Example:

// Step 1: Describe the result set you are building:
	CResultSetDescription Desc;

	// Specify a result set with two key parts and one column.
	Desc
		.f_KeyPart(EResultDataType_fp64)
		.f_KeyPart(EResultDataType_uint64)
		.f_Column(EResultDataType_CStr)
		.f_Column(EResultDataType_CStr);

// Step 2: Create the builder
	// Add two rows to the result set
	CResultSetBuilder Builder(Desc);

// Step 3: Add each key part value:
	// Note: Indices within each key part start from 0
	umint iIndex0 = Builder.f_AddKeyPartValue(_iKeyPart, 10);
	umint iIndex1 = Builder.f_AddKeyPartValue(_iKeyPart, 1000);
	umint iIndex2 = Builder.f_AddKeyPartValue(_iKeyPart, 2.0);

	// OR

	umint iFirstIndex = Builder.f_SetKeyPartValues(_iKeyPart, lValues);

// Step 3: Add each row, specifying all key parts and columns (in order):
	Builder.f_AddRow()
		.f_KeyPart(iIndex0)
		.f_KeyPart(iIndex1)
		.f_Column("Hello")
		.f_Column(nullptr);

	Builder.f_AddRow()
		.f_KeyPart(iIndex0)
		.f_KeyPart(iIndex2)
		.f_Column(", World!")
		.f_Column(nullptr);

// Step 4: Attach metadata:
// (can be done before or after step 3)
	Builder.f_AddMetadata(0, fg_Move(MyMetadata));
	Builder.f_SetKeyPartMetadata(0, fg_Move(OtherMetadata));
	Builder.f_SetColumnMetadata(0, fg_Move(AnotherMetadata));

*/
#pragma once
#include <Mib/Core/Core>
#include "Malterlib_Database_ResultSet_Common.h"
#include "Malterlib_Database_ResultSet.h"

namespace NMib::NDatabase
{
	struct CResultSetDescription
	{
		struct CAccessor
		{
			inline CAccessor &f_KeyPart(EResultDataType _Type, NResultSet::CSubType _SubType = 0);
			inline CAccessor &f_Column(EResultDataType _Type, NResultSet::CSubType _SubType = 0);

		private:
			friend struct CResultSetDescription;

			inline CAccessor(CResultSetDescription &_Desc);

			CResultSetDescription &mp_Desc;
		};

		inline CAccessor f_KeyPart(EResultDataType _Type, NResultSet::CSubType _SubType = 0);
		inline CAccessor f_Column(EResultDataType _Type, NResultSet::CSubType _SubType = 0);

		NContainer::TCVector<EResultDataType> m_KeyPartTypes;
		NContainer::TCVector<NResultSet::CSubType> m_KeyPartSubTypes;

		NContainer::TCVector<EResultDataType> m_ColumnTypes;
		NContainer::TCVector<NResultSet::CSubType> m_ColumnSubTypes;
	};

	struct CResultSetBuilder
	{
		struct CRow
		{
			CRow(CRow &&_ToMove);
			~CRow();

			// inline CRow& f_KeyPartVariant(NResultSet::CValue const& _Value);

			// template<typename tf_CType>
			// inline CRow& f_KeyPart(tf_CType&& _Value);

			inline CRow &f_KeyPart(uint32 _iValueIndex);

			template <typename tf_CType>
			inline CRow &f_Column(tf_CType &&_Value);

			CRow &f_Column(std::nullptr_t _pNull);

		private:
			friend struct CResultSetBuilder;

			CRow(CResultSetBuilder &_Builder, uint32 _iRow);
			CRow(CRow const &_ToCopy) = delete;

			CResultSetBuilder *mp_pBuilder;

			uint32 mp_nKeyParts; // Cached from builder.
			uint32 mp_nColumns;  // Cached from builder.

			uint32 mp_iRow;

			uint32 mp_iCurKeyPart;
			uint32 mp_iCurColumn;
		};

		struct CKeyPart
		{
			CKeyPart(CKeyPart &&_ToMove);
			~CKeyPart();

			// template<typename tf_CType>
			// inline CKeyPart& f_AddValue(tf_CType&& _Value);

			inline CKeyPart &f_AddValue(uint32 _iValueIndex);

		private:
			friend struct CResultSetBuilder;

			CKeyPart(CResultSetBuilder &_Builder, uint32 _iKeyPart);
			CKeyPart(CKeyPart const &_ToCopy) = delete;

			CResultSetBuilder *mp_pBuilder;

			uint32 mp_iKeyPart;
		};

		struct CColumn
		{
			CColumn(CColumn &&_ToMove);
			~CColumn();

			template <typename tf_CType>
			inline CColumn &f_AddValue(tf_CType &&_Value);

			CColumn &f_AddValue(std::nullptr_t _pNull);

		private:
			friend struct CResultSetBuilder;

			CColumn(CResultSetBuilder &_Builder, uint32 _iKeyPart);
			CColumn(CColumn const &_ToCopy) = delete;

			CResultSetBuilder *mp_pBuilder;

			uint32 mp_iColumn;

			uint32 mp_iCurRow;
		};

		CResultSetBuilder();
		CResultSetBuilder(CResultSetBuilder &&_ToMove);
		CResultSetBuilder
			(
				NContainer::TCVector<EResultDataType> const &_KeyPartTypes
				, NContainer::TCVector<NResultSet::CSubType> const &_KeyPartSubTypes
				, NContainer::TCVector<EResultDataType> const &_ColumnTypes
				, NContainer::TCVector<NResultSet::CSubType> const &_ColumnSubTypes
			)
		;
		CResultSetBuilder(CResultSetDescription const &_Desc);
		~CResultSetBuilder();

		bool f_IsValid() const;

		void f_Clear();

		void f_SetDescription
			(
				NContainer::TCVector<EResultDataType> const &_KeyPartTypes
				, NContainer::TCVector<NResultSet::CSubType> const &_KeyPartSubTypes
				, NContainer::TCVector<EResultDataType> const &_ColumnTypes
				, NContainer::TCVector<NResultSet::CSubType> const &_ColumnSubTypes
			)
		;
		void f_SetDescription(CResultSetDescription const &_Desc);

		void f_AddMetadata(uint32 _ID, CResultMetadata &&_Data);
		void f_SetKeyPartMetadata(umint _iKeyPart, CResultMetadata &&_Data);
		void f_SetColumnMetadata(umint _iColumn, CResultMetadata &&_Data);

		template <typename tf_CType>
		inline umint f_AddKeyPartValue(uint32 _iKeyPart, tf_CType &&_Value);

		template <typename tf_CType>
		inline umint f_SetKeyPartValues(uint32 _iKeyPart, NContainer::TCVector<tf_CType> &&_Values);

		umint f_AddKeyPartVariantValue(uint32 _iKeyPart, NResultSet::CValue const &_Value);
		umint f_SetKeyPartVariantValues(uint32 _iKeyPart, NContainer::TCVector<NResultSet::CValue> const &_Values);

		CRow f_AddRow();
		inline umint f_GetNumRows() const;

		CKeyPart f_KeyPart(uint32 _iKeyPart);
		CColumn f_Column(uint32 _iColumn);

		// Mostly useful for test code.
		void f_ExtractTo(CResultSet &o_ResultSet);

		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const;

		template <typename tf_CStream>
		void f_Consume(tf_CStream &_Stream);

	protected:
		//
		// Structure:
		//

		NContainer::TCMap<uint32, CResultMetadata> mp_Metadata;         // Lookup by uint32 m_ID
		NContainer::TCVector<NResultSet::CElementInfo> mp_KeyPartInfos; // Index by iKeyPart
		NContainer::TCVector<NResultSet::CElementInfo> mp_ColumnInfos;  // Index by iColumn

		//
		// Data:
		//

		NContainer::TCVector<NResultSet::CValueVector> mp_KeyPartValues; // Index by iKeyPart, then by iKeyPartValue

		umint mp_nRows;                                                               // Cached for easy access, == mp_KeyParse[X].f_GetLen()
		NContainer::TCVector<NContainer::TCVector<uint32>> mp_KeyPartRowValueIndices; // Index by iKeyPart and then by iRow to get value index.
		NContainer::TCVector<NResultSet::CSparseValueVector> mp_Columns;              // Index by iColumn, then by iterator (as values are sparse)

		//
		// Editing
		//

		bool mp_bRowActive;         // We are adding rows
		umint mp_nColumnWiseActive; // We are adding values by keypart/column
	};
}

#include "Malterlib_Database_ResultSetBuilder.hpp"

#ifndef DMibPNoShortCuts
using namespace NMib::NDatabase;
#endif
