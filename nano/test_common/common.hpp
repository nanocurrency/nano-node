#pragma once

#include <nano/lib/fwd.hpp>

namespace nano::test
{
/**
 * Logger for test code that has no node or system of its own to log through, shared by the whole test process
 */
nano::logger & default_logger ();

/**
 * Stats for test code that has no node or system of its own to count into, shared by the whole test process and logging through `default_logger`
 */
nano::stats & default_stats ();

/**
 * Factor that stretches every test deadline and timeout, read once from the DEADLINE_SCALE_FACTOR environment variable, 1 when it is unset
 */
double deadline_scaling_factor ();
}