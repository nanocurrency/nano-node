.. _simple_wget:

***************
 Simple `wget`
***************

This example is a very simple implementation of a ``wget`` style
clone.  It's very similar to the previous example, but introduces the
``uri`` class.

The code
========

.. code-block:: c++

   #include <boost/network/protocol/http/client.hpp>
   #include <boost/network/uri.hpp>
   #include <string>
   #include <fstream>
   #include <iostream>

   namespace http = boost::network::http;
   namespace uri = boost::network::uri;

   namespace {
   std::string get_filename(const uri::uri &url) {
       std::string path = uri::path(url);
       std::size_t index = path.find_last_of('/');
       std::string filename = path.substr(index + 1);
       return filename.empty()? "index.html" : filename;
   }
   } // namespace

   int
   main(int argc, char *argv[]) {
       if (argc != 2) {
           std::cerr << "Usage: " << argv[0] << " url" << std::endl;
           return 1;
       }

       try {
           http::client client;
           http::client::request request(argv[1]);
           http::client::response response = client.get(request);

           std::string filename = get_filename(request.uri());
           std::cout << "Saving to: " << filename << std::endl;
           std::ofstream ofs(filename.c_str());
           ofs << static_cast<std::string>(body(response)) << std::endl;
       }
       catch (std::exception &e) {
           std::cerr << e.what() << std::endl;
           return 1;
       }

       return 0;
   }

Running the example
===================

You can then run this to copy the Boost_ website:

.. code-block:: bash

    $ cd ~/cpp-netlib-build
    $ make simple_wget
    $ ./example/simple_wget http://www.boost.org/
    $ cat index.html

.. _Boost: http://www.boost.org/

Diving into the code
====================

As with ``wget``, this example simply makes an HTTP request to the
specified resource, and saves it on the filesystem.  If the file name
is not specified, it names the resultant file as ``index.html``.

The new thing to note here is use of the ``uri`` class.  The ``uri``
takes a string as a constructor argument and parses it.  The ``uri``
parser is fully-compliant with `RFC 3986`_.  The URI is provided in
the following header:

.. _`RFC 3986`: http://www.ietf.org/rfc/rfc3986.txt

.. code-block:: c++

   #include <boost/network/uri.hpp>

Most of the rest of the code is familiar from the previous example.
To retrieve the URI resource's file name, the following function is
provided:

.. code-block:: c++

   std::string get_filename(const uri::uri &url) {
       std::string path = uri::path(url);
       std::size_t index = path.find_last_of('/');
       std::string filename = path.substr(index + 1);
       return filename.empty()? "index.html" : filename;
   }

The ``uri`` interface provides access to its different components:
``scheme``, ``user_info``, ``host``, ``port``, ``path``, ``query`` and
``fragment``.  The code above takes the URI path to determine the
resource name.

Next we'll develop a simple client/server application using
``http::server`` and ``http::client``.
