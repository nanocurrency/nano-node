//            Copyright (c) Glyn Matthews 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "rss.hpp"
#include "../rapidxml/rapidxml.hpp"
#include <stdexcept>
#include <cassert>

namespace boost {
namespace network {
namespace rss {
channel::channel(const http::client::response &response) {
  std::string response_body = body(response);
  rapidxml::xml_document<> doc;
  doc.parse<0>(const_cast<char *>(response_body.c_str()));

  rapidxml::xml_node<> *rss = doc.first_node("rss");
  if (!rss) {
    throw std::runtime_error("Invalid RSS feed.");
  }

  rapidxml::xml_node<> *channel = rss->first_node("channel");
  if (!channel) {
    throw std::runtime_error("Invalid RSS channel.");
  }

  rapidxml::xml_node<> *title = channel->first_node("title");
  if (title) {
    title_ = title->first_node()->value();
  }

  rapidxml::xml_node<> *description = channel->first_node("description");
  if (description) {
    description_ = description->first_node()->value();
  }

  rapidxml::xml_node<> *link = channel->first_node("link");
  if (link) {
    link_ = link->first_node()->value();
  }

  rapidxml::xml_node<> *author = channel->first_node("author");
  if (author) {
    author_ = author->first_node()->value();
  }

  rapidxml::xml_node<> *item = channel->first_node("item");
  while (item) {
    items_.push_back(rss::item());

    rapidxml::xml_node<> *title = item->first_node("title");
    if (title) {
      items_.back().set_title(title->first_node()->value());
    }

    rapidxml::xml_node<> *author = item->first_node("author");
    if (author) {
      items_.back().set_author(author->first_node()->value());
    }

    rapidxml::xml_node<> *description = item->first_node("description");
    if (description) {
      items_.back().set_description(description->first_node()->value());
    }

    item = item->next_sibling();
  }
}
}  // namespace rss
}  // namespace network
}  // namespace boost
