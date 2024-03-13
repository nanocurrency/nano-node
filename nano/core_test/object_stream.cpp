#include <nano/lib/numbers.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/object_stream_adapters.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include <fmt/printf.h>

namespace
{
std::string trim (std::string_view str)
{
	return boost::trim_copy (std::string{ str });
}
}

TEST (object_stream, primitive_string)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("field_name_1", "field_value");

	auto expected = R"(field_name_1: "field_value")";
	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_string_view)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("field_name_1", std::string_view{ "field_value" });

	auto expected = R"(field_name_1: "field_value")";
	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_char)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("field_name_1", 'a');

	auto expected = R"(field_name_1: "a")";
	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_bool)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("bool_field_1", true);
	obs.write ("bool_field_2", false);

	auto expected = trim (R"(
bool_field_1: true,
bool_field_2: false
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_int)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("int_field_1", 1234);
	obs.write ("int_field_2", -1234);
	obs.write ("int_field_3", std::numeric_limits<int>::max ());
	obs.write ("int_field_4", std::numeric_limits<int>::min ());

	auto expected = trim (R"(
int_field_1: 1234,
int_field_2: -1234,
int_field_3: 2147483647,
int_field_4: -2147483648
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_uint)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("uint_field_1", static_cast<unsigned int> (1234));
	obs.write ("uint_field_2", static_cast<unsigned int> (-1234));
	obs.write ("uint_field_3", std::numeric_limits<unsigned int>::max ());
	obs.write ("uint_field_4", std::numeric_limits<unsigned int>::min ());

	auto expected = trim (R"(
uint_field_1: 1234,
uint_field_2: 4294966062,
uint_field_3: 4294967295,
uint_field_4: 0
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_uint64)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("uint64_field_1", static_cast<uint64_t> (1234));
	obs.write ("uint64_field_2", static_cast<uint64_t> (-1234));
	obs.write ("uint64_field_3", std::numeric_limits<uint64_t>::max ());
	obs.write ("uint64_field_4", std::numeric_limits<uint64_t>::min ());

	auto expected = trim (R"(
uint64_field_1: 1234,
uint64_field_2: 18446744073709550382,
uint64_field_3: 18446744073709551615,
uint64_field_4: 0
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_int8)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("int8_field_1", static_cast<int8_t> (123));

	auto expected = R"(int8_field_1: 123)";
	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_uint8)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("uint8_field_1", static_cast<uint8_t> (123));

	auto expected = R"(uint8_field_1: 123)";
	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_float)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("float_field_1", 1234.5678f);
	obs.write ("float_field_2", -1234.5678f);
	obs.write ("float_field_3", std::numeric_limits<float>::max ());
	obs.write ("float_field_4", std::numeric_limits<float>::min ());
	obs.write ("float_field_5", std::numeric_limits<float>::lowest ());

	auto expected = trim (R"(
float_field_1: 1234.57,
float_field_2: -1234.57,
float_field_3: 340282346638528859811704183484516925440.00,
float_field_4: 0.00,
float_field_5: -340282346638528859811704183484516925440.00
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, primitive_double)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("double_field_1", 1234.5678f);
	obs.write ("double_field_2", -1234.5678f);
	obs.write ("double_field_3", std::numeric_limits<double>::max ());
	obs.write ("double_field_4", std::numeric_limits<double>::min ());
	obs.write ("double_field_5", std::numeric_limits<double>::lowest ());

	auto expected = trim (R"(
double_field_1: 1234.57,
double_field_2: -1234.57,
double_field_3: 179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.00,
double_field_4: 0.00,
double_field_5: -179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.00
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, object_writer_basic)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("object_field", [] (nano::object_stream & obs) {
		obs.write ("field1", "value1");
		obs.write ("field2", "value2");
		obs.write ("field3", true);
		obs.write ("field4", 1234);
	});

	auto expected = trim (R"(
object_field: {
   field1: "value1",
   field2: "value2",
   field3: true,
   field4: 1234
}
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, object_writer_nested)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("object_field", [] (nano::object_stream & obs) {
		obs.write ("field1", "value1");

		obs.write ("nested_object", [] (nano::object_stream & obs) {
			obs.write ("nested_field1", "nested_value1");
			obs.write ("nested_field2", false);
			obs.write ("nested_field3", -1234);
		});

		obs.write ("field2", "value2");
		obs.write ("field3", true);
		obs.write ("field4", 1234);
	});

	auto expected = trim (R"(
object_field: {
   field1: "value1",
   nested_object: {
      nested_field1: "nested_value1",
      nested_field2: false,
      nested_field3: -1234
   },
   field2: "value2",
   field3: true,
   field4: 1234
}
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, array_writer_basic)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	obs.write ("array_field", [] (nano::array_stream & ars) {
		ars.write (std::views::iota (0, 3));
	});

	auto expected = trim (R"(
array_field: [
   0,
   1,
   2
]
)");

	ASSERT_EQ (ss.str (), expected);
}

namespace
{
class object_basic
{
public:
	nano::uint256_union uint256_union_field{ 0 };
	nano::block_hash block_hash{ 0 };

	void operator() (nano::object_stream & obs) const
	{
		obs.write ("uint256_union_field", uint256_union_field);
		obs.write ("block_hash", block_hash);
	}
};
}

TEST (object_stream, object_basic)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	object_basic test_object;
	obs.write ("test_object", test_object);

	auto expected = trim (R"(
test_object: {
   uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
   block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
}
)");

	ASSERT_EQ (ss.str (), expected);
}

TEST (object_stream, array_writer_objects)
{
	std::stringstream ss;

	std::vector<object_basic> objects;
	objects.push_back ({ .block_hash = 0 });
	objects.push_back ({ .block_hash = 1 });
	objects.push_back ({ .block_hash = 2 });

	nano::object_stream obs{ ss };
	obs.write ("array_field", [&objects] (nano::array_stream & ars) {
		ars.write (objects);
	});

	auto expected = trim (R"(
array_field: [
   {
      uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
      block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
   },
   {
      uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
      block_hash: "0000000000000000000000000000000000000000000000000000000000000001"
   },
   {
      uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
      block_hash: "0000000000000000000000000000000000000000000000000000000000000002"
   }
]
)");

	ASSERT_EQ (ss.str (), expected);
}

namespace
{
class object_array_basic
{
public:
	std::vector<int> values{ 1, 2, 3 };

	void operator() (nano::array_stream & ars) const
	{
		ars.write (values);
	}
};
}

TEST (object_stream, object_array_basic)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	object_array_basic test_object;
	obs.write ("test_object_array", test_object);

	auto expected = trim (R"(
test_object_array: [
   1,
   2,
   3
]
)");

	ASSERT_EQ (ss.str (), expected);
}

namespace
{
class object_nested
{
public:
	nano::uint256_union uint256_union_field{ 0 };
	nano::block_hash block_hash{ 0 };

	object_basic nested_object;
	object_array_basic nested_array_object;

	void operator() (nano::object_stream & obs) const
	{
		obs.write ("uint256_union_field", uint256_union_field);
		obs.write ("block_hash", block_hash);
		obs.write ("nested_object", nested_object);
		obs.write ("nested_array_object", nested_array_object);
	}
};
}

TEST (object_stream, object_nested)
{
	std::stringstream ss;

	nano::object_stream obs{ ss };
	object_nested test_object;
	obs.write ("test_object", test_object);

	auto expected = trim (R"(
test_object: {
   uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
   block_hash: "0000000000000000000000000000000000000000000000000000000000000000",
   nested_object: {
      uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
      block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
   },
   nested_array_object: [
      1,
      2,
      3
   ]
}
)");

	ASSERT_EQ (ss.str (), expected);
}

namespace nano
{
using builtin_array_with_pair = std::vector<std::pair<nano::block_hash, int>>;

void stream_as (std::pair<nano::block_hash, int> const & entry, nano::object_stream & obs)
{
	auto const & [hash, value] = entry;
	obs.write ("hash", hash);
	obs.write ("value", value);
}
}

TEST (object_stream, builtin_array)
{
	using namespace nano;

	std::stringstream ss;

	builtin_array_with_pair array;
	array.push_back ({ nano::block_hash{ 1 }, 1 });
	array.push_back ({ nano::block_hash{ 2 }, 2 });
	array.push_back ({ nano::block_hash{ 3 }, 3 });

	nano::object_stream obs{ ss };
	obs.write_range ("array_field", array);

	auto expected = trim (R"(
array_field: [
   {
      hash: "0000000000000000000000000000000000000000000000000000000000000001",
      value: 1
   },
   {
      hash: "0000000000000000000000000000000000000000000000000000000000000002",
      value: 2
   },
   {
      hash: "0000000000000000000000000000000000000000000000000000000000000003",
      value: 3
   }
]
)");

	ASSERT_EQ (ss.str (), expected);
}

namespace
{
class streamable_object
{
public:
	nano::uint256_union uint256_union_field{ 0 };
	nano::block_hash block_hash{ 0 };

	void operator() (nano::object_stream & obs) const
	{
		obs.write ("uint256_union_field", uint256_union_field);
		obs.write ("block_hash", block_hash);
	}
};
}

TEST (object_stream, ostream_adapter)
{
	using namespace nano::object_stream_adapters;

	std::stringstream ss1, ss2;

	streamable_object test_object;
	ss1 << test_object; // Using automatic ostream adapter (in `nano::ostream_operators`)
	ss2 << nano::streamed (test_object); // Using explicit ostream adapter

	auto expected = trim (R"(
{
   uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
   block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
}
)");

	ASSERT_EQ (ss1.str (), expected);
	ASSERT_EQ (ss2.str (), expected);
}

TEST (object_stream, fmt_adapter)
{
	streamable_object test_object;
	auto str1 = fmt::format ("{}", test_object); // Using automatic fmt adapter
	auto str2 = fmt::format ("{}", nano::streamed (test_object)); // Using explicit fmt adapter

	auto expected = trim (R"(
{
   uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
   block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
}
)");

	ASSERT_EQ (str1, expected);
	ASSERT_EQ (str2, expected);
}

TEST (object_stream, to_string)
{
	using namespace nano::object_stream_adapters;

	streamable_object test_object;
	auto str = to_string (test_object); // Using automatic to_string adapter

	auto expected = trim (R"(
{
   uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000000",
   block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
}
)");

	ASSERT_EQ (str, expected);
}

TEST (object_stream, to_json)
{
	using namespace nano::object_stream_adapters;

	streamable_object test_object;
	auto str = to_json (test_object); // Using automatic to_string adapter

	auto expected = trim (R"(
{"uint256_union_field":"0000000000000000000000000000000000000000000000000000000000000000","block_hash":"0000000000000000000000000000000000000000000000000000000000000000"}
)");

	ASSERT_EQ (str, expected);
}

TEST (object_stream, print_range)
{
	std::deque<streamable_object> objects;
	objects.push_back ({ 1 });
	objects.push_back ({ 2 });
	objects.push_back ({ 3 });

	std::stringstream ss1, ss2;
	ss1 << nano::streamed_range (objects);
	ss2 << fmt::format ("{}", nano::streamed_range (objects));

	auto expected = trim (R"(
[
   {
      uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000001",
      block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
   },
   {
      uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000002",
      block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
   },
   {
      uint256_union_field: "0000000000000000000000000000000000000000000000000000000000000003",
      block_hash: "0000000000000000000000000000000000000000000000000000000000000000"
   }
]
)");

	ASSERT_EQ (ss1.str (), expected);
	ASSERT_EQ (ss2.str (), expected);
}
