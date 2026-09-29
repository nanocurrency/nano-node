#pragma once

// Force-included into the cryptopp target on MSVC.
// Crypto++ 8.7.0 (integer.cpp) calls stdext::make_checked_array_iterator whenever _MSC_VER >= 1500,
// but the MSVC STL shipped with Visual Studio 2026 (_MSVC_STL_VERSION 145) removed the stdext checked iterators.
// Upstream fixed this after the 8.9.0 release; until the submodule is updated, provide a pass-through replacement.

#include <cstddef>

#if defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION >= 145
namespace stdext
{
template <class T>
T * make_checked_array_iterator (T * array, std::size_t)
{
	return array;
}
}
#endif
