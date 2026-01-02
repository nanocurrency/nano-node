#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/messages/bulk_pull.hpp>
#include <nano/messages/message_visitor.hpp>

#include <boost/endian/conversion.hpp>

namespace nano::messages
{
bulk_pull::bulk_pull (nano::network_constants const & constants) :
	message (constants, message_type::bulk_pull)
{
}

bulk_pull::bulk_pull (bool & error_a, nano::stream & stream_a, message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void bulk_pull::visit (message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

void bulk_pull::serialize (nano::stream & stream_a) const
{
	/*
	 * Ensure the "count_present" flag is set if there
	 * is a limit specifed.  Additionally, do not allow
	 * the "count_present" flag with a value of 0, since
	 * that is a sentinel which we use to mean "all blocks"
	 * and that is the behavior of not having the flag set
	 * so it is wasteful to do this.
	 */
	debug_assert ((count == 0 && !is_count_present ()) || (count != 0 && is_count_present ()));

	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);

	if (is_count_present ())
	{
		std::array<uint8_t, extended_parameters_size> count_buffer{ { 0 } };
		decltype (count) count_little_endian;
		static_assert (sizeof (count_little_endian) < (count_buffer.size () - 1), "count must fit within buffer");

		count_little_endian = boost::endian::native_to_little (count);
		memcpy (count_buffer.data () + 1, &count_little_endian, sizeof (count_little_endian));

		write (stream_a, count_buffer);
	}
}

bool bulk_pull::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == message_type::bulk_pull);
	auto error (false);
	try
	{
		nano::read (stream_a, start);
		nano::read (stream_a, end);

		if (is_count_present ())
		{
			std::array<uint8_t, extended_parameters_size> extended_parameters_buffers;
			static_assert (sizeof (count) < (extended_parameters_buffers.size () - 1), "count must fit within buffer");

			nano::read (stream_a, extended_parameters_buffers);
			if (extended_parameters_buffers.front () != 0)
			{
				error = true;
			}
			else
			{
				memcpy (&count, extended_parameters_buffers.data () + 1, sizeof (count));
				boost::endian::little_to_native_inplace (count);
			}
		}
		else
		{
			count = 0;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool bulk_pull::is_count_present () const
{
	return header.extensions.test (count_present_flag);
}

void bulk_pull::set_count_present (bool value_a)
{
	header.extensions.set (count_present_flag, value_a);
}

void bulk_pull::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("start", start);
	obs.write ("end", end);
	obs.write ("count", count);
}
}
