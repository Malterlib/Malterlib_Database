// Copyright © 2019 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Encoding/Bin128>

namespace NMib::NDatabase
{
	template <typename t_CType>
	TCDescending<t_CType>::TCDescending(t_CType &_Value)
		: m_Value(_Value)
	{
	}

	template <typename tf_CValue>
	TCDescending<tf_CValue> fg_Descending(tf_CValue &&_Value)
	{
		return TCDescending<NTraits::TCRemoveReference<tf_CValue>>(_Value);
	}

	template <bool tf_bDescending, typename tf_CValue>
	decltype(auto) fg_Descending(tf_CValue &&_Value)
	{
		if constexpr (tf_bDescending)
			return (TCDescending<NTraits::TCRemoveReference<tf_CValue>>(_Value));
		else
			return fg_Forward<tf_CValue>(_Value);
	}

	template <typename t_CValue>
	struct TCGetDescending
	{
		static constexpr bool mc_bDescending = false;
		using CType = NTraits::TCRemoveQualifiers<t_CValue>;

		template <typename tf_CType>
		static tf_CType &&fs_GetValue(tf_CType &&_Value)
		{
			return fg_Forward<tf_CType>(_Value);
		}
	};

	template <typename t_CValue>
	struct TCGetDescending<TCDescending<t_CValue>>
	{
		static constexpr bool mc_bDescending = true;
		using CType = NTraits::TCRemoveQualifiers<t_CValue>;

		template <typename tf_CType>
		static decltype(auto) fs_GetValue(tf_CType &&_Value)
		{
			return _Value.m_Value;
		}
	};

	template <typename t_CType>
	struct TCDatabaseStreamLexicographic<t_CType, true, TCEnableIf<TCDatabaseStreamLexicographicAutoDescending<t_CType>::mc_bValue>>
	{
		template <typename t_CStream>
		struct TCBinaryStream_Invert : public NStream::CBinaryStreamBigEndian
		{
			DMibStreamImplementOperators(TCBinaryStream_Invert);

			TCBinaryStream_Invert(t_CStream *_pStream)
				: mp_pStream(_pStream)
			{
			}

			void f_FeedBytes(void const *_pMem, mint _nBytes)
			{
				uint8 const *pSource = (uint8 const *)_pMem;
				if (_nBytes <= 1024)
				{
					uint8 Buffer[1024];
					for (mint i = 0; i < _nBytes; ++i)
						Buffer[i] = ~pSource[i];

					mp_pStream->f_FeedBytes(Buffer, _nBytes);
				}
				else
				{
					NContainer::CByteVector Buffer;
					Buffer.f_SetLen(_nBytes);
					auto pDestination = Buffer.f_GetArray();
					for (mint i = 0; i < _nBytes; ++i)
						pDestination[i] = ~pSource[i];

					mp_pStream->f_FeedBytes(pDestination, _nBytes);
				}
			}

			void f_ConsumeBytes(void *_pMem, mint _nBytes)
			{
				uint8 *pDestination = (uint8 *)_pMem;
				if (_nBytes <= 1024)
				{
					uint8 Buffer[1024];
					mp_pStream->f_ConsumeBytes(Buffer, _nBytes);
					for (mint i = 0; i < _nBytes; ++i)
						pDestination[i] = ~Buffer[i];
				}
				else
				{
					NContainer::CByteVector Buffer;
					Buffer.f_SetLen(_nBytes);
					auto pSource = Buffer.f_GetArray();
					mp_pStream->f_ConsumeBytes(pSource, _nBytes);

					for (mint i = 0; i < _nBytes; ++i)
						pDestination[i] = ~pSource[i];
				}
			}

			bool f_IsValid() const
			{
				return mp_pStream->f_IsValid();
			}

			bool f_IsAtEndOfStream() const
			{
				return mp_pStream->f_IsAtEndOfStream();
			}

			NStream::CFilePos f_GetPosition() const
			{
				return mp_pStream->f_GetPosition();
			}

			void f_SetPosition(NStream::CFilePos _Pos)
			{
				return mp_pStream->f_SetPosition(_Pos);
			}

			void f_SetPositionFromEnd(NStream::CFilePos _Pos)
			{
				return mp_pStream->f_SetPosition(_Pos);
			}

			void f_AddPosition(NStream::CFilePos _Pos)
			{
				return mp_pStream->f_SetPosition(_Pos);
			}

			bool f_IsValidReadPosition(NStream::CFilePos _Pos) const
			{
				return mp_pStream->f_IsValidReadPosition(_Pos);
			}

			void f_Flush(bool _bLocalCacheOnly)
			{
				mp_pStream->f_Flush(_bLocalCacheOnly);
			}

			void f_SetCacheSize(mint _CacheSize)
			{
				mp_pStream->f_SetCacheSize(_CacheSize);
			}

			NStream::CFilePos f_GetLength() const
			{
				return mp_pStream->f_GetLength();
			}

			mint f_ContainerLengthLimit() const
			{
				return NStream::fg_CapLengthLimit(f_GetLength() - f_GetPosition());
			}

			void f_SetLength(NStream::CFilePos _Length)
			{
				return mp_pStream->f_SetLength(_Length);
			}

			t_CStream &f_Stream()
			{
				return *mp_pStream;
			}

		protected:
			DMibStreamImplementProtected(TCBinaryStream_Invert);

		private:
			t_CStream *mp_pStream;
		};

		template <typename tf_CStream>
		static void fs_Encode(TCBinaryStream_Invert<tf_CStream> &_Stream, t_CType const &_Value)
		{
			TCDatabaseStreamLexicographic<t_CType, false>::fs_Encode(_Stream.f_Stream(), _Value);
		}

		template <typename tf_CStream>
		static void fs_Encode(tf_CStream &_Stream, t_CType const &_Value)
		{
			TCBinaryStream_Invert<tf_CStream> Stream(&_Stream);
			TCDatabaseStreamLexicographic<t_CType, false>::fs_Encode(Stream, _Value);
		}

		template <typename tf_CStream>
		static void fs_Decode(TCBinaryStream_Invert<tf_CStream> &_Stream, t_CType &o_Value)
		{
			TCDatabaseStreamLexicographic<t_CType, false>::fs_Decode(_Stream.f_Stream(), o_Value);
		}

		template <typename tf_CStream>
		static void fs_Decode(tf_CStream &_Stream, t_CType &o_Value)
		{
			TCBinaryStream_Invert<tf_CStream> Stream(&_Stream);
			TCDatabaseStreamLexicographic<t_CType, false>::fs_Decode(Stream, o_Value);
		}
	};
}
