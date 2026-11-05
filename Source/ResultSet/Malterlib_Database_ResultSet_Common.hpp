// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDatabase::NResultSet
{
	//
	// Result Set Implementation Data Types
	//

	template <typename tf_CStream>
	void CMetadataEntry::f_Feed(tf_CStream &_Stream) const
	{
		_Stream << m_ID << m_Data;
	}

	template <typename tf_CStream>
	void CMetadataEntry::f_Consume(tf_CStream &_Stream)
	{
		_Stream >> m_ID >> m_Data;
	}

	CSparseDataEntryBase::CSparseDataEntryBase(CRowIndex _iRow)
		: m_iRow(_iRow)
	{
	}

	CSparseDataEntryBase::CSparseDataEntryBase(CSparseDataEntryBase &&_ToMove)
		: m_iRow(_ToMove.m_iRow)
	{
	}

	// Represents a value of a column for a specified (by m_ID) row.

	template <typename t_CType>
	TCSparseDataEntry<t_CType>::TCSparseDataEntry()
		: CSparseDataEntryBase(0)
	{
	}

	template <typename t_CType>
	TCSparseDataEntry<t_CType>::TCSparseDataEntry(CRowIndex _iRow, t_CType &&_Data)
		: CSparseDataEntryBase(_iRow)
		, m_Data(fg_Move(_Data))
	{
	}

	template <typename t_CType>
	TCSparseDataEntry<t_CType>::TCSparseDataEntry(TCSparseDataEntry &&_ToMove)
		: CSparseDataEntryBase(_ToMove.m_iRow)
		, m_Data(fg_Move(_ToMove.m_Data))
	{
	}

	template <typename t_CType>
	template <typename tf_CStream>
	void TCSparseDataEntry<t_CType>::f_Feed(tf_CStream &_Stream) const
	{
		_Stream << m_iRow << m_Data;
	}

	template <typename t_CType>
	template <typename tf_CStream>
	void TCSparseDataEntry<t_CType>::f_Consume(tf_CStream &_Stream)
	{
		_Stream >> m_iRow >> m_Data;
	}

	// IO

	template <typename tf_CStream>
	void CElementInfo::f_Feed(tf_CStream &_Stream) const
	{
		_Stream << (uint32)m_Type << m_SubType << m_Metadata;
	}

	template <typename tf_CStream>
	void CElementInfo::f_Consume(tf_CStream &_Stream)
	{
		_Stream >> (uint32 &)m_Type >> m_SubType >> m_Metadata;
	}

	// Utility class for converting to/from result data types

#define DAODB_DefineTypeToDataType(d_InType, d_OutType, d_Enum)	\
	template<>	\
	struct TCTypeToDataType< d_InType >	\
	{	\
		using CDataType = d_OutType; \
		static constexpr EResultDataType mc_Type = d_Enum; \
		static d_OutType fs_Convert(d_InType _Value) { return d_OutType (_Value); } \
	}

	DAODB_DefineTypeToDataType(fp64, fp64, EResultDataType_fp64);
	DAODB_DefineTypeToDataType(fp64 &, fp64, EResultDataType_fp64);
	DAODB_DefineTypeToDataType(uint64, uint64, EResultDataType_uint64);

	template <>
	struct TCTypeToDataType<NStr::CStr>
	{
		using CDataType = NStr::CStr;

		static NStr::CStr fs_Convert(NStr::CStr &&_Value)
		{
			return fg_Move(_Value);
		}

		static constexpr EResultDataType mc_Type = EResultDataType_CStr;
	};

	template <>
	struct TCTypeToDataType<NContainer::CByteVector>
	{
		using CDataType = NContainer::CByteVector;

		static NContainer::CByteVector fs_Convert(NContainer::CByteVector &&_Value)
		{
			return fg_Move(_Value);
		}

		static constexpr EResultDataType mc_Type = EResultDataType_Binary;
	};

	template <>
	struct TCTypeToDataType<NContainer::CByteVector &>
	{
		using CDataType = NContainer::CByteVector;

		static NContainer::CByteVector fs_Convert(NContainer::CByteVector const &_Value)
		{
			return _Value;
		}

		static constexpr EResultDataType mc_Type = EResultDataType_Binary;
	};

	template <>
	struct TCTypeToDataType<NContainer::CByteVector const &>
	{
		using CDataType = NContainer::CByteVector;

		static NContainer::CByteVector fs_Convert(NContainer::CByteVector const &_Value)
		{
			return _Value;
		}

		static constexpr EResultDataType mc_Type = EResultDataType_Binary;
	};

	template <size_t t_Len>
	struct TCTypeToDataType<char const (&)[t_Len]>
	{
		using CDataType = NStr::CStr;

		static NStr::CStr fs_Convert(char const _Value[t_Len])
		{
			return NStr::CStr(_Value);
		}

		static constexpr EResultDataType mc_Type = EResultDataType_CStr;
	};

	template <>
	struct TCTypeToDataType<char const *>
	{
		using CDataType = NStr::CStr;

		static NStr::CStr fs_Convert(char const *_pValue)
		{
			return NStr::CStr(_pValue);
		}

		static constexpr EResultDataType mc_Type = EResultDataType_CStr;
	};

	DAODB_DefineTypeToDataType(uint32, fp64, EResultDataType_fp64);
	DAODB_DefineTypeToDataType(int32, fp64, EResultDataType_fp64);
	DAODB_DefineTypeToDataType(fp32, fp64, EResultDataType_fp64);
#ifdef DMibPUniqueType_int
	DAODB_DefineTypeToDataType(int, fp64, EResultDataType_fp64);
#endif
	DAODB_DefineTypeToDataType(float, fp64, EResultDataType_fp64);
	DAODB_DefineTypeToDataType(double, fp64, EResultDataType_fp64);
	/*
	DAODB_DefineTypeToDataType(uint32, int32, EResultDataType_int32);
	DAODB_DefineTypeToDataType(uint64, int64, EResultDataType_int64);
	DAODB_DefineTypeToDataType(float, fp32, EResultDataType_fp32);
	DAODB_DefineTypeToDataType(double, fp64, EResultDataType_fp64);
	*/
#undef DAODB_DefineTypeToDataType
}
