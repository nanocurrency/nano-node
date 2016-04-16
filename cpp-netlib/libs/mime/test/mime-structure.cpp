/*
    Read in a mime structure, parse it, dump the structure to stdout

    Returns 0 for success, non-zero for failure
*/

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

template <typename Container>
void DumpContainer(std::ostream &out, const std::string &prefix,
                   const Container &c) {
  out << prefix << ' ';
  if (c.size() < 10) {
    for (typename Container::const_iterator iter = c.begin(); iter != c.end();
         ++iter)
      out << (int)*iter << ' ';
  } else {
    for (int i = 0; i < 5; i++) out << int(c.begin()[i]) << ' ';
    out << "...  ";
    for (int i = 0; i < 5; i++) out << int(c.rbegin()[i]) << ' ';
  }
  out << std::endl;
}

void DumpStructure(std::ostream &out, const char *title, const mime_part &mp,
                   std::string prefix) {
  std::string content_type = mp.get_content_type();
  if (NULL != title) out << prefix << "Data from: " << title << std::endl;
  out << prefix << "Content-Type: " << content_type << std::endl;
  out << prefix << "There are "
      << std::distance(mp.header_begin(), mp.header_end()) << " headers"
      << std::endl << std::flush;
  size_t subpart_count = std::distance(mp.subpart_begin(), mp.subpart_end());
  switch (mp.get_part_kind()) {
    case mime_part::simple_part:
      if (subpart_count != 0)
        out << str(boost::format("%s ### %d subparts on a simple (%s) type!") %
                   prefix % subpart_count % content_type) << std::endl;
      out << prefix << "The body is " << mp.body_size() << " bytes long"
          << std::endl;
      DumpContainer(out, prefix, *mp.body());
      break;

    case mime_part::multi_part:
      break;

    case mime_part::message_part:
      if (boost::iequals(content_type, "message/delivery-status"))
        out << prefix << "The body is " << mp.body_size() << " bytes long"
            << std::endl;
      else if (1 != subpart_count)
        out << str(boost::format("%s ### %d subparts on a message (%s) type!") %
                   subpart_count % prefix % content_type) << std::endl;
      break;
  }

  if (subpart_count != 0) {
    out << prefix << "There are "
        << std::distance(mp.subpart_begin(), mp.subpart_end()) << " sub parts"
        << std::endl << std::flush;
    for (mime_part::constPartIter iter = mp.subpart_begin();
         iter != mp.subpart_end(); ++iter)
      DumpStructure(out, NULL, **iter, prefix + "  ");
  }
}

int main(int argc, char *argv[]) {
  int retVal = 0;

  for (int i = 1; i < argc; ++i) {
    boost::shared_ptr<mime_part> rmp;
    try {
      std::ifstream in(argv[i]);
      if (!in) {
        std::cerr << "Can't open file " << argv[i] << std::endl;
        retVal += 100;
        continue;
      }

      in >> std::noskipws;
      std::cout << "**********************************" << std::endl;
      rmp = mime_part::parse_mime(in);
    }

    catch (const boost::mime::mime_parsing_error &err) {
      std::cout << "Caught an error parsing '" << argv[i] << "'" << std::endl;
      std::cout << "    " << err.what() << std::endl;
      retVal += 10;
      continue;
    }
    catch (const boost::exception &berr) {
      std::cout << "Caught an boost error parsing '" << argv[i] << "'"
                << std::endl;
      //	std::cout << "    " << berr.what () << std::endl;
      retVal += 10;
      continue;
    }
    catch (const std::runtime_error &rerr) {
      std::cout << "Caught an runtime error parsing '" << argv[i] << "'"
                << std::endl;
      std::cout << "    " << rerr.what() << std::endl;
      retVal += 10;
      continue;
    }

    DumpStructure(std::cout, argv[i], *rmp, std::string());
  }

  return retVal;
}
