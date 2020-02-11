#pragma once

#include <nano/lib/work.hpp>
#include <nano/secure/epoch.hpp>

namespace nano
{
nano::work_version work_version_get (nano::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a);
}
