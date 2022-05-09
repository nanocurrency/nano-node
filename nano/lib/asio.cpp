#include <nano/lib/asio.hpp>

nano::shared_const_buffer::shared_const_buffer (std::vector<uint8_t> const & data) :
	m_data (std::make_shared<std::vector<uint8_t>> (data)),
	m_buffer (boost::asio::buffer (*m_data))
{
}

nano::shared_const_buffer::shared_const_buffer (std::vector<uint8_t> && data) :
	m_data (std::make_shared<std::vector<uint8_t>> (std::move (data))),
	m_buffer (boost::asio::buffer (*m_data))
{
}

nano::shared_const_buffer::shared_const_buffer (uint8_t data) :
	shared_const_buffer (std::vector<uint8_t>{ data })
{
}

nano::shared_const_buffer::shared_const_buffer (std::string const & data) :
	m_data (std::make_shared<std::vector<uint8_t>> (data.begin (), data.end ())),
	m_buffer (boost::asio::buffer (*m_data))
{
}

nano::shared_const_buffer::shared_const_buffer (std::shared_ptr<std::vector<uint8_t>> const & data) :
	m_data (data),
	m_buffer (boost::asio::buffer (*m_data))
{
}

boost::asio::const_buffer const * nano::shared_const_buffer::begin () const
{
	return &m_buffer;
}

boost::asio::const_buffer const * nano::shared_const_buffer::end () const
{
	return &m_buffer + 1;
}

std::size_t nano::shared_const_buffer::size () const
{
	return m_buffer.size ();
}

std::vector<uint8_t> nano::shared_const_buffer::to_bytes () const
{
	std::vector<uint8_t> bytes;
	for (auto const & buffer : *this)
	{
		bytes.resize (bytes.size () + buffer.size ());
		std::copy ((uint8_t const *)buffer.data (), (uint8_t const *)buffer.data () + buffer.size (), bytes.data () + bytes.size () - buffer.size ());
	}
	return bytes;
}
