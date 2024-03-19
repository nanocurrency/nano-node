#include <nano/lib/logging.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/utility.hpp>
#include <nano/store/component.hpp>
#include <nano/test_common/make_store.hpp>

std::unique_ptr<nano::store::component> nano::test::make_store ()
{
	return nano::make_store (nano::default_logger (), nano::unique_path (), nano::dev::constants);
}
