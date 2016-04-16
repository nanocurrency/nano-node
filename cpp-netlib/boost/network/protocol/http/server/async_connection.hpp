#ifndef BOOST_NETWORK_PROTOCOL_HTTP_SERVER_CONNECTION_HPP_20101027
#define BOOST_NETWORK_PROTOCOL_HTTP_SERVER_CONNECTION_HPP_20101027

// Copyright 2010 Dean Michael Berris.
// Copyright 2014 Jelle Van den Driessche.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/throw_exception.hpp>
#include <boost/scope_exit.hpp>
#include <boost/network/protocol/http/algorithms/linearize.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/range/adaptor/sliced.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/make_shared.hpp>
#include <boost/network/protocol/stream_handler.hpp>
#include <boost/network/protocol/http/server/request_parser.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/optional.hpp>
#include <boost/utility/typed_in_place_factory.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/utility/enable_if.hpp>
#include <list>
#include <vector>
#include <iterator>
#ifdef BOOST_NETWORK_NO_LIB
#include <boost/network/protocol/http/server/impl/parsers.ipp>
#endif

#ifndef BOOST_NETWORK_HTTP_SERVER_CONNECTION_HEADER_BUFFER_MAX_SIZE
/** Here we define a page's worth of header connection buffer data.
 *  This can be tuned to reduce the memory cost of connections, but this
 *  default size is set to be friendly to typical service applications.
 *  This is the maximum size though and Boost.Asio's internal representation
 *  of a streambuf would make appropriate decisions on how big a buffer
 *  is to begin with.
 *
 *  This kinda assumes that a page is by default 4096. Since we're using
 *  the default allocator with the static buffers, it's not guaranteed that
 *  the static buffers will be page-aligned when they are allocated.
 */
#define BOOST_NETWORK_HTTP_SERVER_CONNECTION_HEADER_BUFFER_MAX_SIZE 4096
#endif /* BOOST_NETWORK_HTTP_SERVER_CONNECTION_HEADER_BUFFER_MAX_SIZE */

namespace boost {
namespace network {
namespace http {

#ifndef BOOST_NETWORK_NO_LIB
extern void parse_version(std::string const& partial_parsed,
                          fusion::tuple<uint8_t, uint8_t>& version_pair);
extern void parse_headers(std::string const& input,
                          std::vector<request_header_narrow>& container);
#endif

template <class Tag, class Handler>
struct async_connection
    : boost::enable_shared_from_this<async_connection<Tag, Handler> > {

  enum status_t {
    ok = 200,
    created = 201,
    accepted = 202,
    no_content = 204,
    partial_content = 206,
    multiple_choices = 300,
    moved_permanently = 301,
    moved_temporarily = 302,
    not_modified = 304,
    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    not_supported = 405,
    not_acceptable = 406,
    request_timeout = 408,
    precondition_failed = 412,
    unsatisfiable_range = 416,
    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    space_unavailable = 507
  };

  typedef typename string<Tag>::type string_type;
  typedef basic_request<Tag> request;
  typedef shared_ptr<async_connection> connection_ptr;

 private:
  static char const* status_message(status_t status) {
    static char const ok_[] = "OK", created_[] = "Created",
                      accepted_[] = "Accepted", no_content_[] = "No Content",
                      multiple_choices_[] = "Multiple Choices",
                      moved_permanently_[] = "Moved Permanently",
                      moved_temporarily_[] = "Moved Temporarily",
                      not_modified_[] = "Not Modified",
                      bad_request_[] = "Bad Request",
                      unauthorized_[] = "Unauthorized",
                      forbidden_[] = "Fobidden", not_found_[] = "Not Found",
                      not_supported_[] = "Not Supported",
                      not_acceptable_[] = "Not Acceptable",
                      internal_server_error_[] = "Internal Server Error",
                      not_implemented_[] = "Not Implemented",
                      bad_gateway_[] = "Bad Gateway",
                      service_unavailable_[] = "Service Unavailable",
                      unknown_[] = "Unknown",
                      partial_content_[] = "Partial Content",
                      request_timeout_[] = "Request Timeout",
                      precondition_failed_[] = "Precondition Failed",
                      unsatisfiable_range_[] =
                          "Requested Range Not Satisfiable",
                      space_unavailable_[] =
                          "Insufficient Space to Store Resource";
    switch (status) {
      case ok:
        return ok_;
      case created:
        return created_;
      case accepted:
        return accepted_;
      case no_content:
        return no_content_;
      case multiple_choices:
        return multiple_choices_;
      case moved_permanently:
        return moved_permanently_;
      case moved_temporarily:
        return moved_temporarily_;
      case not_modified:
        return not_modified_;
      case bad_request:
        return bad_request_;
      case unauthorized:
        return unauthorized_;
      case forbidden:
        return forbidden_;
      case not_found:
        return not_found_;
      case not_supported:
        return not_supported_;
      case not_acceptable:
        return not_acceptable_;
      case internal_server_error:
        return internal_server_error_;
      case not_implemented:
        return not_implemented_;
      case bad_gateway:
        return bad_gateway_;
      case service_unavailable:
        return service_unavailable_;
      case partial_content:
        return partial_content_;
      case request_timeout:
        return request_timeout_;
      case precondition_failed:
        return precondition_failed_;
      case unsatisfiable_range:
        return unsatisfiable_range_;
      case space_unavailable:
        return space_unavailable_;
      default:
        return unknown_;
    }
  }

 public:
  async_connection(asio::io_service& io_service, Handler& handler,
                   utils::thread_pool& thread_pool,
                   boost::shared_ptr<ssl_context> ctx =
                       boost::shared_ptr<ssl_context>())
      : strand(io_service),
        handler(handler),
        thread_pool_(thread_pool),
        headers_buffer(
            BOOST_NETWORK_HTTP_SERVER_CONNECTION_HEADER_BUFFER_MAX_SIZE),
#ifdef BOOST_NETWORK_ENABLE_HTTPS
        socket_(io_service, ctx),
#else
        socket_(io_service),
#endif
        handshake_done(false),
        headers_already_sent(false),
        headers_in_progress(false) {
    new_start = read_buffer_.begin();
  }

  ~async_connection() throw() {
    boost::system::error_code ignored;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_receive, ignored);
  }

  /** Function: template <class Range> set_headers(Range headers)
   *  Precondition: headers have not been sent yet
   *  Postcondition: headers have been linearized to a buffer,
   *                 and assumed to have been sent already when the
   *function exits
   *  Throws: std::logic_error in case the headers have already been sent.
   *
   *  A call to set_headers takes a Range where each element models the
   *  Header concept. This Range will be linearized onto a buffer, which
   *is
   *  then sent as soon as the first call to `write` or `flush` commences.
   */
  template <class Range>
  void set_headers(Range headers) {
    lock_guard lock(headers_mutex);
    if (headers_in_progress || headers_already_sent)
      boost::throw_exception(
          std::logic_error("Headers have already been sent."));

    if (error_encountered)
      boost::throw_exception(boost::system::system_error(*error_encountered));

    typedef constants<Tag> consts;
    {
      std::ostream stream(&headers_buffer);
      stream << consts::http_slash() << 1 << consts::dot() << 1
             << consts::space() << status << consts::space()
             << status_message(status) << consts::crlf();
      if (!boost::empty(headers)) {
        typedef typename string<Tag>::type string_type;
        boost::transform(headers, std::ostream_iterator<string_type>(stream),
                         linearize_header<Tag>());
      } else {
        stream << consts::crlf();
      }
      stream << consts::crlf();
    }

    write_headers_only(
        boost::bind(&async_connection<Tag, Handler>::do_nothing,
                    async_connection<Tag, Handler>::shared_from_this()));
  }

  void set_status(status_t new_status) {
    lock_guard lock(headers_mutex);
    if (headers_already_sent)
      boost::throw_exception(std::logic_error(
          "Headers have already been sent, cannot reset status."));
    if (error_encountered)
      boost::throw_exception(boost::system::system_error(*error_encountered));

    status = new_status;
  }

  template <class Range>
  void write(Range const& range) {
    lock_guard lock(headers_mutex);
    if (error_encountered)
      boost::throw_exception(boost::system::system_error(*error_encountered));

    boost::function<void(boost::system::error_code)> f = boost::bind(
        &async_connection<Tag, Handler>::default_error,
        async_connection<Tag, Handler>::shared_from_this(), boost::arg<1>());

    write_impl(boost::make_iterator_range(range), f);
  }

  template <class Range, class Callback>
  typename disable_if<
      is_base_of<asio::const_buffer, typename Range::value_type>, void>::type
  write(Range const& range, Callback const& callback) {
    lock_guard lock(headers_mutex);
    if (error_encountered)
      boost::throw_exception(boost::system::system_error(*error_encountered));
    write_impl(boost::make_iterator_range(range), callback);
  }

  template <class ConstBufferSeq, class Callback>
  typename enable_if<
      is_base_of<asio::const_buffer, typename ConstBufferSeq::value_type>,
      void>::type
  write(ConstBufferSeq const& seq, Callback const& callback) {
    write_vec_impl(seq, callback, shared_array_list(), shared_buffers());
  }

 private:
  typedef boost::array<char, BOOST_NETWORK_HTTP_SERVER_CONNECTION_BUFFER_SIZE>
      buffer_type;

 public:
  typedef iterator_range<buffer_type::const_iterator> input_range;
  typedef boost::function<
      void(input_range, boost::system::error_code, std::size_t, connection_ptr)>
      read_callback_function;

  void read(read_callback_function callback) {
    if (error_encountered)
      boost::throw_exception(boost::system::system_error(*error_encountered));
    if (new_start != read_buffer_.begin()) {
      input_range input =
          boost::make_iterator_range(new_start, read_buffer_.end());
      buffer_type::iterator start_tmp = new_start;
      new_start = read_buffer_.begin();
      thread_pool().post(
          boost::bind(callback, input, boost::system::error_code(),
                      std::distance(start_tmp, data_end),
                      async_connection<Tag, Handler>::shared_from_this()));
      return;
    }

    socket().async_read_some(
        asio::buffer(read_buffer_),
        strand.wrap(boost::bind(
            &async_connection<Tag, Handler>::wrap_read_handler,
            async_connection<Tag, Handler>::shared_from_this(), callback,
            asio::placeholders::error, asio::placeholders::bytes_transferred)));
  }

  boost::network::stream_handler& socket() { return socket_; }
  utils::thread_pool& thread_pool() { return thread_pool_; }
  bool has_error() { return (!!error_encountered); }
  optional<boost::system::system_error> error() { return error_encountered; }

 private:
  void wrap_read_handler(read_callback_function callback,
                         boost::system::error_code const& ec,
                         std::size_t bytes_transferred) {
    if (ec) error_encountered = in_place<boost::system::system_error>(ec);
    buffer_type::const_iterator data_start = read_buffer_.begin(),
                                data_end = read_buffer_.begin();
    std::advance(data_end, bytes_transferred);
    thread_pool().post(boost::bind(
        callback, boost::make_iterator_range(data_start, data_end), ec,
        bytes_transferred, async_connection<Tag, Handler>::shared_from_this()));
  }

  void default_error(boost::system::error_code const& ec) {
    error_encountered = in_place<boost::system::system_error>(ec);
  }

  typedef boost::array<char, BOOST_NETWORK_HTTP_SERVER_CONNECTION_BUFFER_SIZE>
      array;
  typedef std::list<shared_ptr<array> > array_list;
  typedef boost::shared_ptr<array_list> shared_array_list;
  typedef boost::shared_ptr<std::vector<asio::const_buffer> > shared_buffers;
  typedef request_parser<Tag> request_parser_type;
  typedef boost::lock_guard<boost::recursive_mutex> lock_guard;
  typedef std::list<boost::function<void()> > pending_actions_list;

  asio::io_service::strand strand;
  Handler& handler;
  utils::thread_pool& thread_pool_;
  asio::streambuf headers_buffer;
  boost::network::stream_handler socket_;
  bool handshake_done;
  volatile bool headers_already_sent, headers_in_progress;

  boost::recursive_mutex headers_mutex;
  buffer_type read_buffer_;
  status_t status;
  request_parser_type parser;
  request request_;
  buffer_type::iterator new_start, data_end;
  string_type partial_parsed;
  optional<boost::system::system_error> error_encountered;
  pending_actions_list pending_actions;

  template <class, class>
  friend struct async_server_base;

  enum state_t {
    method,
    uri,
    version,
    headers
  };

  void start() {
    typename ostringstream<Tag>::type ip_stream;
    ip_stream << socket_.remote_endpoint().address().to_string() << ':'
              << socket_.remote_endpoint().port();
    request_.source = ip_stream.str();
    read_more(method);
  }

  void read_more(state_t state) {
#ifdef BOOST_NETWORK_ENABLE_HTTPS
    if (socket_.is_ssl_enabled() && !handshake_done) {
      socket_.async_handshake(
          boost::asio::ssl::stream_base::server,
          boost::bind(&async_connection::handle_handshake,
                      async_connection<Tag, Handler>::shared_from_this(),
                      boost::asio::placeholders::error, state));
    } else {
#endif
      socket_.async_read_some(
          asio::buffer(read_buffer_),
          strand.wrap(
              boost::bind(&async_connection<Tag, Handler>::handle_read_data,
                          async_connection<Tag, Handler>::shared_from_this(),
                          state, boost::asio::placeholders::error,
                          boost::asio::placeholders::bytes_transferred)));
#ifdef BOOST_NETWORK_ENABLE_HTTPS
    }
#endif
  }

  void handle_read_data(state_t state, boost::system::error_code const& ec,
                        std::size_t bytes_transferred) {
    if (!ec) {
      logic::tribool parsed_ok;
      iterator_range<buffer_type::iterator> result_range, input_range;
      data_end = read_buffer_.begin();
      std::advance(data_end, bytes_transferred);
      switch (state) {
        case method:
          input_range = boost::make_iterator_range(new_start, data_end);
          fusion::tie(parsed_ok, result_range) =
              parser.parse_until(request_parser_type::method_done, input_range);
          if (!parsed_ok) {
            client_error();
            break;
          } else if (parsed_ok == true) {
            swap(partial_parsed, request_.method);
            request_.method.append(boost::begin(result_range),
                                   boost::end(result_range));
            trim(request_.method);
            new_start = boost::end(result_range);
          } else {
            partial_parsed.append(boost::begin(result_range),
                                  boost::end(result_range));
            new_start = read_buffer_.begin();
            read_more(method);
            break;
          }
        case uri:
          input_range = boost::make_iterator_range(new_start, data_end);
          fusion::tie(parsed_ok, result_range) =
              parser.parse_until(request_parser_type::uri_done, input_range);
          if (!parsed_ok) {
            client_error();
            break;
          } else if (parsed_ok == true) {
            swap(partial_parsed, request_.destination);
            request_.destination.append(boost::begin(result_range),
                                        boost::end(result_range));
            trim(request_.destination);
            new_start = boost::end(result_range);
          } else {
            partial_parsed.append(boost::begin(result_range),
                                  boost::end(result_range));
            new_start = read_buffer_.begin();
            read_more(uri);
            break;
          }
        case version:
          input_range = boost::make_iterator_range(new_start, data_end);
          fusion::tie(parsed_ok, result_range) = parser.parse_until(
              request_parser_type::version_done, input_range);
          if (!parsed_ok) {
            client_error();
            break;
          } else if (parsed_ok == true) {
            fusion::tuple<uint8_t, uint8_t> version_pair;
            partial_parsed.append(boost::begin(result_range),
                                  boost::end(result_range));
            parse_version(partial_parsed, version_pair);
            request_.http_version_major = fusion::get<0>(version_pair);
            request_.http_version_minor = fusion::get<1>(version_pair);
            new_start = boost::end(result_range);
            partial_parsed.clear();
          } else {
            partial_parsed.append(boost::begin(result_range),
                                  boost::end(result_range));
            new_start = read_buffer_.begin();
            read_more(version);
            break;
          }
        case headers:
          input_range = boost::make_iterator_range(new_start, data_end);
          fusion::tie(parsed_ok, result_range) = parser.parse_until(
              request_parser_type::headers_done, input_range);
          if (!parsed_ok) {
            client_error();
            break;
          } else if (parsed_ok == true) {
            partial_parsed.append(boost::begin(result_range),
                                  boost::end(result_range));
            try {
              parse_headers(partial_parsed, request_.headers);
            } catch (...) {
              client_error();
              break;
            }
            new_start = boost::end(result_range);
            thread_pool().post(boost::bind(
                &Handler::operator(), &handler, cref(request_),
                async_connection<Tag, Handler>::shared_from_this()));
            return;
          } else {
            partial_parsed.append(boost::begin(result_range),
                                  boost::end(result_range));
            new_start = read_buffer_.begin();
            read_more(headers);
            break;
          }
        default:
          BOOST_ASSERT(false &&
                       "This is a bug, report to the cpp-netlib devel "
                       "mailing list!");
          std::abort();
      }
    } else {
      error_encountered = in_place<boost::system::system_error>(ec);
    }
  }

  void client_error() {
    static char const* bad_request =
        "HTTP/1.0 400 Bad Request\r\nConnection: close\r\nContent-Type: "
        "text/plain\r\nContent-Length: 12\r\n\r\nBad Request.";

    asio::async_write(
        socket(), asio::buffer(bad_request, strlen(bad_request)),
        strand.wrap(boost::bind(
            &async_connection<Tag, Handler>::client_error_sent,
            async_connection<Tag, Handler>::shared_from_this(),
            asio::placeholders::error, asio::placeholders::bytes_transferred)));
  }

  void client_error_sent(boost::system::error_code const& ec,
                         std::size_t bytes_transferred) {
    if (!ec) {
      boost::system::error_code ignored;
      socket().shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
      socket().close(ignored);
    } else {
      error_encountered = in_place<boost::system::system_error>(ec);
    }
  }

  void do_nothing() {}

  void write_headers_only(boost::function<void()> callback) {
    if (headers_in_progress) return;
    headers_in_progress = true;
    asio::async_write(
        socket(), headers_buffer,
        strand.wrap(boost::bind(
            &async_connection<Tag, Handler>::handle_write_headers,
            async_connection<Tag, Handler>::shared_from_this(), callback,
            asio::placeholders::error, asio::placeholders::bytes_transferred)));
  }

  void handle_write_headers(boost::function<void()> callback,
                            boost::system::error_code const& ec,
                            std::size_t bytes_transferred) {
    lock_guard lock(headers_mutex);
    if (!ec) {
      headers_buffer.consume(headers_buffer.size());
      headers_already_sent = true;
      thread_pool().post(callback);
      pending_actions_list::iterator start = pending_actions.begin(),
                                     end = pending_actions.end();
      while (start != end) {
        thread_pool().post(*start++);
      }
      pending_actions_list().swap(pending_actions);
    } else {
      error_encountered = in_place<boost::system::system_error>(ec);
    }
  }

  void handle_write(
      boost::function<void(boost::system::error_code const&)> callback,
      shared_array_list temporaries, shared_buffers buffers,
      boost::system::error_code const& ec, std::size_t bytes_transferred) {
    // we want to forget the temporaries and buffers
    thread_pool().post(boost::bind(callback, ec));
  }

  template <class Range>
  void write_impl(Range range,
                  boost::function<void(boost::system::error_code)> callback) {
    // linearize the whole range into a vector
    // of fixed-sized buffers, then schedule an asynchronous
    // write of these buffers -- make sure they are live
    // by making these linearized buffers shared and made
    // part of the completion handler.
    //
    // once the range has been linearized and sent, schedule
    // a wrapper to be called in the io_service's thread, that
    // will re-schedule the given callback into the thread pool
    // referred to here so that the io_service's thread can concentrate
    // on doing I/O.
    //

    static std::size_t const connection_buffer_size =
        BOOST_NETWORK_HTTP_SERVER_CONNECTION_BUFFER_SIZE;
    shared_array_list temporaries = boost::make_shared<array_list>();
    shared_buffers buffers =
        boost::make_shared<std::vector<asio::const_buffer> >(0);

    std::size_t range_size = boost::distance(range);
    buffers->reserve((range_size / connection_buffer_size) +
                     ((range_size % connection_buffer_size) ? 1 : 0));
    std::size_t slice_size = std::min(range_size, connection_buffer_size);
    typename boost::range_iterator<Range>::type start = boost::begin(range),
                                                end = boost::end(range);
    while (slice_size != 0) {
      using boost::adaptors::sliced;
      shared_ptr<array> new_array = make_shared<array>();
      boost::copy(range | sliced(0, slice_size), new_array->begin());
      temporaries->push_back(new_array);
      buffers->push_back(asio::buffer(new_array->data(), slice_size));
      std::advance(start, slice_size);
      range = boost::make_iterator_range(start, end);
      range_size = boost::distance(range);
      slice_size = std::min(range_size, connection_buffer_size);
    }

    if (!buffers->empty()) {
      write_vec_impl(*buffers, callback, temporaries, buffers);
    }
  }

  template <class ConstBufferSeq, class Callback>
  void write_vec_impl(ConstBufferSeq const& seq, Callback const& callback,
                      shared_array_list temporaries, shared_buffers buffers) {
    lock_guard lock(headers_mutex);
    if (error_encountered)
      boost::throw_exception(boost::system::system_error(*error_encountered));

    boost::function<void(boost::system::error_code)> callback_function =
        callback;

    boost::function<void()> continuation = boost::bind(
        &async_connection<Tag, Handler>::template write_vec_impl<
             ConstBufferSeq, boost::function<void(boost::system::error_code)> >,
        async_connection<Tag, Handler>::shared_from_this(), seq,
        callback_function, temporaries, buffers);

    if (!headers_already_sent && !headers_in_progress) {
      write_headers_only(continuation);
      return;
    } else if (headers_in_progress && !headers_already_sent) {
      pending_actions.push_back(continuation);
      return;
    }

    asio::async_write(
        socket_, seq,
        boost::bind(&async_connection<Tag, Handler>::handle_write,
                    async_connection<Tag, Handler>::shared_from_this(),
                    callback_function, temporaries, buffers,
                    asio::placeholders::error,
                    asio::placeholders::bytes_transferred));
  }
  void handle_handshake(const boost::system::error_code& ec, state_t state) {
    if (!ec) {
      handshake_done = true;
      read_more(state);
    } else {
      error_encountered = in_place<boost::system::system_error>(ec);
    }
  }
};

} /* http */

} /* network */

} /* boost */

#endif /* BOOST_NETWORK_PROTOCOL_HTTP_SERVER_CONNECTION_HPP_20101027 */
