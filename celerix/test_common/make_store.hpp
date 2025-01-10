#pragma once

#include <memory>

namespace celerix::store
{
class component;
}

namespace celerix::test
{
std::unique_ptr<celerix::store::component> make_store ();
}
