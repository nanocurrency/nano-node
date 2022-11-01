#pragma once

#include <cstdint>

struct stack_st_ASN1_TYPE;
typedef stack_st_ASN1_TYPE ASN1_SEQUENCE_ANY;

namespace nano::ssl::detail
{
template <typename DataT>
void deleteNoOp (DataT *);

template <typename DataT>
void deleteNoOp (const DataT *);

template <typename DataT>
constexpr auto getNoOpDeleter ();

template <typename DataT>
constexpr auto getConstNoOpDeleter ();

void deleteBuffer (std::uint8_t * data);

void deleteSequence (ASN1_SEQUENCE_ANY * sequence);

}

namespace nano::ssl::detail
{
template <typename DataT>
void deleteNoOp (DataT *)
{
}

template <typename DataT>
void deleteNoOp (const DataT *)
{
}

template <typename DataT>
constexpr auto getNoOpDeleter ()
{
	return static_cast<void (*) (DataT *)> (deleteNoOp<DataT>);
}

template <typename DataT>
constexpr auto getConstNoOpDeleter ()
{
	return static_cast<void (*) (const DataT *)> (deleteNoOp<DataT>);
}

}
