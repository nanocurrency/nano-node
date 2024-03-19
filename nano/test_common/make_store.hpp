#pragma once

#include <memory>

namespace nano::store
{
class component;
}

namespace nano::test
{
std::unique_ptr<nano::store::component> make_store ();
}
