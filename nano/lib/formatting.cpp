#include <nano/lib/files.hpp>
#include <nano/lib/formatting.hpp>
#include <nano/lib/ratios.hpp>

#include <array>
#include <string_view>

namespace nano::log
{
std::ostream & operator<< (std::ostream & os, as_nano_formatter const & wrapper)
{
	nano::encode_balance (os, wrapper.value, nano::nano_ratio, wrapper.precision, true);
	return os;
}

std::ostream & operator<< (std::ostream & os, as_raw_nano_formatter const & wrapper)
{
	os << wrapper.value;
	return os;
}

std::ostream & operator<< (std::ostream & os, as_size_formatter const & wrapper)
{
	constexpr std::array<std::string_view, 5> units{ "B", "KiB", "MiB", "GiB", "TiB" };

	std::size_t unit = 0;
	double value = static_cast<double> (wrapper.value);
	while (value >= 1024.0 && unit + 1 < units.size ())
	{
		value /= 1024.0;
		++unit;
	}

	if (unit == 0)
	{
		os << fmt::format ("{} {}", wrapper.value, units[unit]);
	}
	else
	{
		os << fmt::format ("{:.2f} {}", value, units[unit]);
	}
	return os;
}

std::ostream & operator<< (std::ostream & os, as_disk_space_formatter const & wrapper)
{
	auto const & info = wrapper.info;
	auto const free_percent = info.capacity > 0 ? 100.0 * static_cast<double> (info.available) / static_cast<double> (info.capacity) : 0.0;

	os << fmt::format ("{} available of {} ({:.1f}% free)",
	as_size (info.available),
	as_size (info.capacity),
	free_percent);

	return os;
}
}
