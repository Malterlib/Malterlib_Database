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

		template <typename tf_CStream, typename tf_CValue>
		static void fs_FeedLexicographic(tf_CStream &_Stream, tf_CValue const &_Value);

		template <typename tf_CStream, typename tf_CValue>
		static void fs_ConsumeLexicographic(tf_CStream &_Stream, tf_CValue &&o_Value);

		template <typename ...tfp_CValue>
		static CDatabaseValue fs_ToLexicographic(CDatabaseValueCache &o_StreamCache, tfp_CValue const &...p_Value);

		template <typename ...tfp_CValue>
		static CDatabaseValue fs_ToStream(CDatabaseValueCache &o_StreamCache, tfp_CValue const &...p_Value);

		template <typename ...tfp_CValue>
		static CDatabaseValue fs_ToStream(CDatabaseValueCache &o_StreamCache, NStorage::TCTuple<tfp_CValue...> const &_Value);

		template <typename ...tfp_CValue>
		auto f_TupleFromLexicographic() const;

		template <typename tf_CValue>
		auto f_FromLexicographic() const;
		template <typename tf_CValue>
		void f_FromLexicographic(tf_CValue &o_Value) const;

		template <typename ...tfp_CValue>
		auto f_TupleFromStream() const;
		template <typename tf_CValue>
		auto f_FromStream() const;
		template <typename tf_CValue>
		void f_FromStream(tf_CValue &o_Value) const;

		template <typename tf_CValue>
		static CDatabaseValue fs_Raw(tf_CValue const &_Value);

		template <typename tf_CValue>
		tf_CValue const &f_Raw() const;

		mint m_Size = 0;				///< Size of the data item */
		uint8 const *m_pData = nullptr;	///< Address of the data item */
	};

	template <typename t_CType, typename t_CEnableIf = void>
	struct TCDatabaseStreamLexicographicAutoDescending
	{
		static constexpr bool mc_bValue = true;
	};

	template <typename t_CType, bool t_bDescending, typename t_CEnableIf = void>
	struct TCDatabaseStreamLexicographic;

	template <typename t_CType>
	struct TCDescending
	{
		t_CType &m_Value;
		TCDescending(t_CType &_Value);
	};

	template <typename tf_CValue>
	TCDescending<tf_CValue> fg_Descending(tf_CValue &_Value);

	template <bool tf_bDescending, typename tf_CValue>
	decltype(auto) fg_Descending(tf_CValue &&_Value);
}

#include "Malterlib_Database_DatabaseValue.hpp"

#ifndef DMibPNoShortCuts
using namespace NMib::NDatabase;
#endif
