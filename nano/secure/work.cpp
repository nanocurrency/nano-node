#include <nano/secure/work.hpp>

nano::work_version nano::work_version_get (nano::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a)
{
	switch (epoch_a)
	{
		case nano::epoch::epoch_0:
		case nano::epoch::epoch_1:
		case nano::epoch::epoch_2:
			return nano::work_version::work_1;
		default:
			release_assert (false);
			return nano::work_version::unspecified;
	}
}
