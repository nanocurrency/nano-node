//            Copyright (c) Glyn Matthews 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "atom.hpp"
#include "../rapidxml/rapidxml.hpp"
#include <stdexcept>
#include <cassert>

namespace boost {
namespace network {
namespace atom {
feed::feed(const http::client::response &response) {
  std::string response_body = body(response);
  rapidxml::xml_document<> doc;
  doc.parse<0>(const_cast<char *>(response_body.c_str()));

  rapidxml::xml_node<> *feed = doc.first_node("feed");
  if (!feed) {
    throw std::runtime_error("Invalid atom feed.");
  }

  rapidxml::xml_node<> *title = feed->first_node("title");
  if (title) {
    title_ = title->first_node()->value();
  }

  rapidxml::xml_node<> *subtitle = feed->first_node("subtitle");
  if (subtitle) {
    subtitle_ = subtitle->first_node()->value();
  }

  rapidxml::xml_node<> *id = feed->first_node("id");
  if (id) {
    id_ = id->first_node()->value();
  }

  rapidxml::xml_node<> *updated = feed->first_node("updated");
  if (updated) {
    updated_ = updated->first_node()->value();
  }

  rapidxml::xml_node<> *author = feed->first_node("author");
  if (author) {
    rapidxml::xml_node<> *name = author->first_node("name");
    rapidxml::xml_node<> *email = author->first_node("email");
    if (name && email) {
      author_ = atom::author(name->first_node()->value(),
                             email->first_node()->value());
    } else if (name) {
      author_ = atom::author(name->first_node()->value());
    }
  }

  rapidxml::xml_node<> *entry = feed->first_node("entry");
  while (entry) {
    entries_.push_back(atom::entry());

    rapidxml::xml_node<> *title = entry->first_node("title");
    if (title) {
      entries_.back().set_title(title->first_node()->value());
    }

    rapidxml::xml_node<> *id = entry->first_node("id");
    if (id) {
      entries_.back().set_id(id->first_node()->value());
    }

    rapidxml::xml_node<> *published = entry->first_node("published");
    if (published) {
      entries_.back().set_published(published->first_node()->value());
    }

    rapidxml::xml_node<> *updated = entry->first_node("updated");
    if (updated) {
      entries_.back().set_updated(updated->first_node()->value());
    }

    rapidxml::xml_node<> *summary = entry->first_node("summary");
    if (summary) {
      entries_.back().set_summary(summary->first_node()->value());
    }

    rapidxml::xml_node<> *content = entry->first_node("content");
    if (content) {
      entries_.back().set_content(content->first_node()->value());
    }

    entry = entry->next_sibling();
  }
}
}  // namespace atom
}  // namespace network
}  // namespace boost
