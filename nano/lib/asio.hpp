#pragma once

#include <nano/boost/asio.hpp>

namespace nano
{
class shared_const_buffer
{
public:
	using value_type = boost::asio::const_buffer;
	using const_iterator = const boost::asio::const_buffer *;

	explicit shared_const_buffer (std::vector<uint8_t> const & data);
	explicit shared_const_buffer (uint8_t data);
	explicit shared_const_buffer (std::string const & data);
	explicit shared_const_buffer (std::vector<uint8_t> && data);
	explicit shared_const_buffer (std::shared_ptr<std::vector<uint8_t>> const & data);

	const boost::asio::const_buffer * begin () const;
	const boost::asio::const_buffer * end () const;

	size_t size () const;

private:
	std::shared_ptr<std::vector<uint8_t>> m_data;
	boost::asio::const_buffer m_buffer;
};

template <typename AsyncWriteStream, typename WriteHandler>
BOOST_ASIO_INITFN_RESULT_TYPE (WriteHandler, void(boost::system::error_code, std::size_t)) async_write (AsyncWriteStream & s, const nano::shared_const_buffer & buffer, WriteHandler && handler)
{
	return boost::asio::async_write (s, buffer, std::move (handler));
}
}