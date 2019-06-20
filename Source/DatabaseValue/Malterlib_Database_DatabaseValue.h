// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Type/Traits>

namespace NMib::NDatabase
{
	using CDatabaseValueCache = NContainer::TCGrowingVector<uint8>;

	/** @brief Generic structure used for passing keys and data in and out
	 * of the database.
	 *
	 * Values returned from the database are valid only until a subsequent
	 * update operation, or the end of the transaction. Do not modify or
	 * free them, they commonly point into the database itself.
	 *
	 * Key sizes must be between 1 and #f_MaxKeySize() inclusive.
	 * The same applies to data sizes in databases with the #EDatabaseSubFlag_SortedDuplicates flag.
	 * Other data items can in theory be from 0 to 0xffffffff bytes long.
	 */
	struct CDatabaseValue
	{
		CDatabaseValue() = default;
		CDatabaseValue(CDatabaseValueCache const &_Value);

		bool operator < (CDatabaseValue const &_Right) const;
		bool operator == (CDatabaseValue const &_Right) const;
		bool f_HasPrefix(CDatabaseValue const &_Right) const;

		template <typename tf_CStr>
		void f_Format(tf_CStr &o_String) const;

		bool f_IsValid() const;

		mint m_Size = 0;				///< Size of the data item */
		uint8 const *m_pData = nullptr;	///< Address of the data item */
	};

	template <typename t_CType, typename t_CEnableIf = void>
	struct TCOrderedByteArrayCoderAutoDescending
	{
		static constexpr bool mc_bValue = true;
	};

	template <typename t_CType, bool t_bDescending, typename t_CEnableIf = void>
	struct TCOrderedByteArrayCoder;

	template <typename t_CType>
	struct TCDescending
	{
		t_CType &m_Value;
		TCDescending(t_CType &_Value);
	};

	template <typename tf_CValue>
	TCDescending<tf_CValue> fg_Descending(tf_CValue &_Value);

	template <typename tf_CStream, typename tf_CValue>
	void fg_EncodeLexicographic(tf_CStream &_Stream, tf_CValue const &_Value);

	template <typename tf_CStream, typename tf_CValue>
	void fg_DecodeLexicographic(tf_CStream &_Stream, tf_CValue &o_Value);

	template <typename ...tfp_CValue>
	CDatabaseValue fg_EncodeLexicographicValue(CDatabaseValueCache &o_StreamCache, tfp_CValue const &...p_Value);

	template <typename ...tfp_CValue>
	auto fg_DecodeLexicographicTuple(CDatabaseValue const &_Value);

	template <typename tf_CValue>
	void fg_DecodeLexicographicValue(CDatabaseValue const &_Value, tf_CValue &o_Value);

	template <typename tf_CValue>
	CDatabaseValue fg_RawDatabaseValue(tf_CValue const &_Value);

	template <typename tf_CValue>
	tf_CValue const &fg_RawDatabaseValue(CDatabaseValue const &_Value);
}

#include "Malterlib_Database_DatabaseValue.hpp"

#ifndef DMibPNoShortCuts
using namespace NMib::NDatabase;
#endif
