//            Copyright (c) Glyn Matthews 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef ___ATOM_INC__
#define ___ATOM_INC__

#include <string>
#include <vector>
#include <boost/network/protocol/http.hpp>

namespace boost {
namespace network {
namespace atom {
class entry {

 public:
  void set_title(const std::string &title) { title_ = title; }

  std::string title() const { return title_; }

  void set_id(const std::string &id) { id_ = id; }

  std::string id() const { return id_; }

  void set_published(const std::string &published) { published_ = published; }

  std::string published() const { return published_; }

  void set_updated(const std::string &updated) { updated_ = updated; }

  std::string updated() const { return updated_; }

  void set_summary(const std::string &summary) { summary_ = summary; }

  std::string summary() const { return summary_; }

  void set_content(const std::string &content) { content_ = content; }

  std::string content() const { return content_; }

 private:
  std::string title_;
  std::string id_;
  std::string published_;
  std::string updated_;
  std::string summary_;
  std::string content_;
};

class author {

 public:
  author() {}

  author(const std::string &name) : name_(name) {}

  author(const std::string &name, const std::string &email)
      : name_(name), email_(email) {}

  std::string name() const { return name_; }

  std::string email() const { return email_; }

 private:
  std::string name_;
  std::string email_;
};

class feed {

 public:
  typedef entry value_type;
  typedef std::vector<entry>::iterator iterator;
  typedef std::vector<entry>::const_iterator const_iterator;

  feed(const http::client::response &response);

  std::string title() const { return title_; }

  std::string subtitle() const { return subtitle_; }

  std::string id() const { return id_; }

  std::string updated() const { return updated_; }

  atom::author author() const { return author_; }

  unsigned int entry_count() const { return entries_.size(); }

  iterator begin() { return entries_.begin(); }

  iterator end() { return entries_.end(); }

  const_iterator begin() const { return entries_.begin(); }

  const_iterator end() const { return entries_.end(); }

 private:
  std::string title_;
  std::string subtitle_;
  std::string id_;
  std::string updated_;
  atom::author author_;
  std::vector<entry> entries_;
};
}  // namespace atom
}  // namespace network
}  // namespace boost

#endif  // ___ATOM_INC__
