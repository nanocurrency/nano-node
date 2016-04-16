.. _atom_reader:

******************
 Atom feed reader
******************

The next examples show some simple, more practical applications using
the HTTP client.  The first one reads a simple Atom_ feed and prints
the titles of each entry to the console.

.. _Atom: http://en.wikipedia.org/wiki/Atom_(standard)

The code
========

.. code-block:: c++

   #include "atom.hpp"
   #include <boost/network/protocol/http/client.hpp>
   #include <boost/foreach.hpp>
   #include <iostream>

   int main(int argc, char * argv[]) {
       using namespace boost::network;

       if (argc != 2) {
           std::cout << "Usage: " << argv[0] << " <url>" << std::endl;
           return 1;
       }

       try {
           http::client client;
           http::client::request request(argv[1]);
           request << header("Connection", "close");
           http::client::response response = client.get(request);
           atom::feed feed(response);

           std::cout << "Feed: " << feed.title()
	   	     << " (" << feed.subtitle() << ")" << std::endl;
           BOOST_FOREACH(const atom::entry &entry, feed) {
               std::cout << entry.title()
	       		 << " (" << entry.published() << ")" << std::endl;
           }
       }
       catch (std::exception &e) {
           std::cerr << e.what() << std::endl;
       }

       return 0;
   }

Building and running ``atom_reader``
====================================

.. code-block:: bash

    $ cd ~/cpp-netlib-build
    $ make atom_reader

And to run the example from the command line to access the feed that
lists of all the commits on cpp-netlib's master branch:

.. code-block:: bash

    $ ./example/atom_reader https://github.com/cpp-netlib/cpp-netlib/commits/master.atom

Diving into the code
====================

Most of this will now be familiar.  The response is passed to the
constructor to the ``atom::feed`` class, which parses the resultant
XML.  To keep this example as simple as possible, `rapidxml`_, a
header-only XML parser library, was used to parse the response.

.. _`rapidxml`: http://rapidxml.sourceforge.net/

A similar example using RSS feeds exists in
``libs/network/example/rss``.
