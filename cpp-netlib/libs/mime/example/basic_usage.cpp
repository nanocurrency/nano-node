//
//          Copyright Marshall Clow 2009-2010
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//
//

#include <boost/mime.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

struct my_traits {
  typedef std::string string_type;
  //	typedef std::pair < std::string, string_type > header_type;
  typedef std::string body_type;
};

typedef boost::mime::basic_mime<my_traits> mime_part;

int main(int argc, char* argv[]) {

  //	(1) a really simple part
  mime_part mp("text", "plain");
  mp.set_body("Hello World\n", 12);
  std::cout << mp;

  //	Three trips around the house before we go through the door.
  //	Make a part, copy it onto the heap, and wrap it into a shared pointer.
  std::cout << "*******" << std::endl;
  std::string str("<HTML><HEAD></HEAD><BODY>Hi Mom!</BODY></HTML>\n");
  boost::shared_ptr<mime_part> mp0(new mime_part(
      mime_part::make_simple_part("text", "html", str.begin(), str.end())));
  std::cout << mp0;

  std::cout << "*******" << std::endl;
  boost::shared_ptr<mime_part> mp1(new mime_part("text", "plain"));
  mp1->set_body("This is a test.....\n", 20);
  mp1->append_phrase_to_content_type("charset", "usascii");
  std::cout << mp1;

  //	Build a multipart
  mime_part mp2("multipart", "multiple");
  mp2.set_body("This is the body of a multipart\n", 32);
  mp2.append_part(mp0);
  mp2.append_part(mp1);

  //	stream it out to a string, then make a new part from the string
  std::ostringstream os1, os2;
  os1 << mp2;
  std::istringstream is(os1.str());
  is >> std::noskipws;
  boost::shared_ptr<mime_part> strmp = mime_part::parse_mime(is);
  os2 << strmp;
  if (os1.str() == os2.str())
    std::cout << "Strings match!!" << std::endl;
  else {
    //	Write the differences out to files for examination
    std::cout << "##Strings differ!!" << std::endl;
    std::ofstream t1("test1.out", std::ios::binary);
    t1 << os1.str();
    std::ofstream t2("test2.out", std::ios::binary);
    t2 << os2.str();
  }

  return 0;
}
