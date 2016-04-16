#ifndef BOOST_NETWORK_PROTOCOL_HTTP_POLICIES_ASYNC_RESOLVER_20100622
#define BOOST_NETWORK_PROTOCOL_HTTP_POLICIES_ASYNC_RESOLVER_20100622

// Copyright Dean Michael Berris 2010.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/strand.hpp>

namespace boost {
namespace network {
namespace http {
namespace policies {

template <class Tag>
struct async_resolver : boost::enable_shared_from_this<async_resolver<Tag> > {
  typedef typename resolver<Tag>::type resolver_type;
  typedef typename resolver_type::iterator resolver_iterator;
  typedef typename resolver_type::query resolver_query;
  typedef std::pair<resolver_iterator, resolver_iterator>
      resolver_iterator_pair;
  typedef typename string<Tag>::type string_type;
  typedef boost::unordered_map<string_type, resolver_iterator_pair>
      endpoint_cache;
  typedef boost::function<
      void(boost::system::error_code const &, resolver_iterator_pair)>
      resolve_completion_function;
  typedef boost::function<void(resolver_type &, string_type, boost::uint16_t,
                               resolve_completion_function)> resolve_function;

 protected:
  bool cache_resolved_;
  endpoint_cache endpoint_cache_;
  boost::shared_ptr<boost::asio::io_service> service_;
  boost::shared_ptr<boost::asio::io_service::strand> resolver_strand_;

  explicit async_resolver(bool cache_resolved)
      : cache_resolved_(cache_resolved), endpoint_cache_() {}

  void resolve(resolver_type &resolver_, string_type const &host,
               boost::uint16_t port,
               resolve_completion_function once_resolved) {
    if (cache_resolved_) {
      typename endpoint_cache::iterator iter =
          endpoint_cache_.find(boost::to_lower_copy(host));
      if (iter != endpoint_cache_.end()) {
        boost::system::error_code ignored;
        once_resolved(ignored, iter->second);
        return;
      }
    }

    typename resolver_type::query q(host,
                                    lexical_cast<string_type>(port));
    resolver_.async_resolve(q, resolver_strand_->wrap(boost::bind(
                                   &async_resolver<Tag>::handle_resolve,
                                   async_resolver<Tag>::shared_from_this(),
                                   boost::to_lower_copy(host), once_resolved,
                                   boost::asio::placeholders::error,
                                   boost::asio::placeholders::iterator)));
  }

  void handle_resolve(string_type const &host,
                      resolve_completion_function once_resolved,
                      boost::system::error_code const &ec,
                      resolver_iterator endpoint_iterator) {
    typename endpoint_cache::iterator iter;
    bool inserted = false;
    if (!ec && cache_resolved_) {
      boost::fusion::tie(iter, inserted) =
          endpoint_cache_.insert(std::make_pair(
              host, std::make_pair(endpoint_iterator, resolver_iterator())));
      once_resolved(ec, iter->second);
    } else {
      once_resolved(ec, std::make_pair(endpoint_iterator, resolver_iterator()));
    }
  }
};

}  // namespace policies

}  // namespace http

}  // namespace network

}  // namespace boost

#endif  // BOOST_NETWORK_PROTOCOL_HTTP_POLICIES_ASYNC_RESOLVER_20100622
