#include <nano/node/environment.hpp>

#include <algorithm>
#include <thread>

nano::environment::environment () :
alarm (ctx),
work_impl (std::make_unique<nano::work_pool> (std::max (std::thread::hardware_concurrency (), 1u))),
work (*work_impl)
{
}
