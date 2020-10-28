#pragma once

#include <chrono>

namespace nano
{
template <typename>
class timestamp_generator_base;
using timestamp_generator = timestamp_generator_base<std::chrono::system_clock>;
}
