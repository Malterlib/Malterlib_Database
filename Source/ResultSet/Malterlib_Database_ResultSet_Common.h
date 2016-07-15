// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

/*
Types common to CResultSet and it's writers etc...
*/
#pragma once
#include <Mib/Core/Core>
#include <Mib/Storage/Variant>

namespace NMib::NDatabase
{
	enum EResultDataType
	{
		EResultDataType_CStr
		, EResultDataType_fp64
		, EResultDataType_uint64
		, EResultDataType_Binary
/* Deprecated:
		, EResultDataType_int32
		, EResultDataType_int64
		, EResultDataType_fp32
*/			
	};


	// Represents a single bit of meta data in a binary blob
	typedef NContainer::CByteVector CResultMetaData;

	namespace NResultSet
	{

		//
		// Result Set Implementation Data Types
		//

		enum
		{
			EResultSetFormatVersion = 2
		};


		// Represents a bit of meta data with a specified ID
		// Used for meta data attached to the result set itself.
		struct CMetaDataEntry
		{
			uint32 m_ID;
			CResultMetaData m_Data;

			template <typename tf_CStream>
			inline void f_Feed(tf_CStream &_Stream) const;

			template <typename tf_CStream>
			inline void f_Consume(tf_CStream &_Stream);
		};

		// Represents the subtype of a key part or column
		typedef uint8 CSubType;

		// Represents information about either a key part or a column.
		struct CElementInfo
		{
			EResultDataType m_Type;		// Type of key part or column
			CSubType m_SubType;			// Subtype of key part or column
			CResultMetaData m_MetaData;		// Meta data for the key part or column

			template <typename tf_CStream>
			inline void f_Feed(tf_CStream &_Stream) const;

			template <typename tf_CStream>
			inline void f_Consume(tf_CStream &_Stream);
		};

		// Represents a binary blob
		typedef NContainer::CByteVector CBlob;

		// Represents a value that can be stored in either a row key part or a row column.
		typedef NStorage::TCStreamableVariant
			<
				EResultDataType
				, NStorage::TCMember<NStr::CStr, EResultDataType_CStr>
				, NStorage::TCMember<fp64, EResultDataType_fp64>
				, NStorage::TCMember<uint64, EResultDataType_uint64>
				, NStorage::TCMember<CBlob, EResultDataType_Binary>
				/* Deprecated:
				, NStorage::TCMember<int32, EResultDataType_int32>
				, NStorage::TCMember<int64, EResultDataType_int64>
				, NStorage::TCMember<fp32, EResultDataType_fp32>
				*/
			> CValue
		;

		// Represents a vector of values.
		// Used to store the value of a specific key part for all rows.
		typedef NStorage::TCStreamableVariant
			<
				EResultDataType
				, NStorage::TCMember<NContainer::TCVector<NStr::CStr>, EResultDataType_CStr>
				, NStorage::TCMember<NContainer::TCVector<fp64>, EResultDataType_fp64>
				, NStorage::TCMember<NContainer::TCVector<uint64>, EResultDataType_uint64>
				, NStorage::TCMember<NContainer::TCVector<CBlob>, EResultDataType_Binary>
			/* Deprecated:
				, NStorage::TCMember<NContainer::TCVector<int32>, EResultDataType_int32>
				, NStorage::TCMember<NContainer::TCVector<int64>, EResultDataType_int64>
				, NStorage::TCMember<NContainer::TCVector<fp32>, EResultDataType_fp32>
			*/
			> CValueVector
		;

		typedef NContainer::TCVector<mint> CRowKeyPartValueIndices;

		// Represents the index of a row (from 0)
		typedef uint32 CRowIndex;

		// Base class for storing a column value
		struct CSparseDataEntryBase
		{
			CRowIndex m_iRow;	// The row the value stored in a subclass is for.

			inline CSparseDataEntryBase(CRowIndex _iRow);

			inline CSparseDataEntryBase(CSparseDataEntryBase&& _ToMove);
		};

		// Represents a value of a column for a specified (by m_ID) row.
		template<typename t_CType>
		struct TCSparseDataEntry : public CSparseDataEntryBase
		{
			t_CType m_Data;

			inline TCSparseDataEntry();
			inline TCSparseDataEntry(CRowIndex _iRow, t_CType&& _Data);
			inline TCSparseDataEntry(TCSparseDataEntry&& _ToMove);

			template <typename tf_CStream>
			inline void f_Feed(tf_CStream &_Stream) const;

			template <typename tf_CStream>
			inline void f_Consume(tf_CStream &_Stream);
		};

		// Represents the list of values for each row of a specific column in a sparse manner.
		typedef NStorage::TCStreamableVariant
			<
				EResultDataType
				, NStorage::TCMember<NContainer::TCVector<TCSparseDataEntry<NStr::CStr>>, EResultDataType_CStr>
				, NStorage::TCMember<NContainer::TCVector<TCSparseDataEntry<fp64>>, EResultDataType_fp64>
				, NStorage::TCMember<NContainer::TCVector<TCSparseDataEntry<uint64>>, EResultDataType_uint64>
				, NStorage::TCMember<NContainer::TCVector<TCSparseDataEntry<CBlob>>, EResultDataType_Binary>
		/*	Deprecated
				, NStorage::TCMember<NContainer::TCVector<TCSparseDataEntry<int32>>, EResultDataType_int32>
				, NStorage::TCMember<NContainer::TCVector<TCSparseDataEntry<int64>>, EResultDataType_int64>
				, NStorage::TCMember<NContainer::TCVector<TCSparseDataEntry<fp32>>, EResultDataType_fp32>
		*/
			> CSparseValueVector
		;

		// Utility class for converting to/from result data types


		template<typename t_CType>
		struct TCTypeToDataType
		{
//				static_assert( false, "The type you pass in must be one of: CStr, int32, int64, fp32, fp64, TCVector<uin8> or convertible to one.");

//				typedef _OutType CDataType;
//				static constexpr EResultDataType mc_Type = <DataTypeEnum>; 
//				static _OutType fs_Convert(_InType _V) { return _OutType (_V); }
		};
	}
}

#include "Malterlib_Database_ResultSet_Common.hpp"

#ifndef DMibPNoShortCuts
	using namespace NMib::NDatabase::NResultSet;
#endif
