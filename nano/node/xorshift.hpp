#pragma once

#include <nano/lib/ubsan_warnings.hpp>

#include <array>

namespace nano
{
class xorshift1024star final
{
public:
	std::array<uint64_t, 16> s;
	unsigned p{ 0 };

	__IGNORE_ASAN_WARNINGS__
	uint64_t next ()
	{
		auto p_l (p);
		auto pn ((p_l + 1) & 15);
		p = pn;
		uint64_t s0 = s[p_l];
		uint64_t s1 = s[pn];
		s1 ^= s1 << 31; // a
		s1 ^= s1 >> 11; // b
		s0 ^= s0 >> 30; // c
		return (s[pn] = s0 ^ s1) * 1181783497276652981LL;
	}
};
}
