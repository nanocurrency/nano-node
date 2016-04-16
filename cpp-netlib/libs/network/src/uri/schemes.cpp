//              Copyright 2012 Glyn Matthews.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <boost/network/uri/schemes.hpp>
#include <boost/unordered_set.hpp>

namespace boost {
namespace network {
namespace uri {
namespace {
static boost::unordered_set<std::string> hierarchical_schemes_;
static boost::unordered_set<std::string> opaque_schemes_;

bool register_hierarchical_schemes() {
  hierarchical_schemes_.insert("http");
  hierarchical_schemes_.insert("https");
  hierarchical_schemes_.insert("shttp");
  hierarchical_schemes_.insert("ftp");
  hierarchical_schemes_.insert("file");
  hierarchical_schemes_.insert("dns");
  hierarchical_schemes_.insert("nfs");
  hierarchical_schemes_.insert("imap");
  hierarchical_schemes_.insert("nntp");
  hierarchical_schemes_.insert("pop");
  hierarchical_schemes_.insert("rsync");
  hierarchical_schemes_.insert("snmp");
  hierarchical_schemes_.insert("telnet");
  hierarchical_schemes_.insert("svn");
  hierarchical_schemes_.insert("svn+ssh");
  hierarchical_schemes_.insert("git");
  hierarchical_schemes_.insert("git+ssh");
  return true;
}

bool register_opaque_schemes() {
  opaque_schemes_.insert("mailto");
  opaque_schemes_.insert("news");
  opaque_schemes_.insert("im");
  opaque_schemes_.insert("sip");
  opaque_schemes_.insert("sms");
  opaque_schemes_.insert("xmpp");
  return true;
}

static bool hierarchical = register_hierarchical_schemes();
static bool opaque = register_opaque_schemes();
}  // namespace

bool hierarchical_schemes::exists(const std::string &scheme) {
  return hierarchical_schemes_.end() != hierarchical_schemes_.find(scheme);
}

bool opaque_schemes::exists(const std::string &scheme) {
  return opaque_schemes_.end() != opaque_schemes_.find(scheme);
}
}  // namespace uri
}  // namespace network
}  // namespace boost
