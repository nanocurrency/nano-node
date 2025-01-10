#include <celerix/lib/logging.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/utility.hpp>
#include <celerix/store/component.hpp>
#include <celerix/test_common/make_store.hpp>

std::unique_ptr<celerix::store::component> celerix::test::make_store ()
{
	return celerix::make_store (celerix::default_logger (), celerix::unique_path (), celerix::dev::constants);
}
