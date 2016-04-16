.. _twitter_search:

****************
 Twitter search
****************

This example uses `Twitter's search API`_ to list recent tweets given
a user query.  New features introduced here include the URI builder
and ``uri::encoded`` function.

.. _`Twitter's search API`: https://dev.twitter.com/docs/using-search

The code
========

.. code-block:: c++

   #include <boost/network/protocol/http/client.hpp>
   #include "rapidjson/rapidjson.h"
   #include "rapidjson/document.h"
   #include <iostream>

   int main(int argc, char *argv[]) {
       using namespace boost::network;
       using namespace rapidjson;

       if (argc != 2) {
           std::cout << "Usage: " << argv[0] << " <query>" << std::endl;
           return 1;
       }

       try {
           http::client client;

           uri::uri base_uri("http://search.twitter.com/search.json");

           std::cout << "Searching Twitter for query: " << argv[1] << std::endl;
           uri::uri search;
           search << base_uri << uri::query("q", uri::encoded(argv[1]));
           http::client::request request(search);
           http::client::response response = client.get(request);

           Document d;
           if (!d.Parse<0>(response.body().c_str()).HasParseError()) {
               const Value &results = d["results"];
               for (SizeType i = 0; i < results.Size(); ++i)
               {
                   const Value &user = results[i]["from_user_name"];
                   const Value &text = results[i]["text"];
                   std::cout << "From: " << user.GetString() << std::endl
                             << "  " << text.GetString() << std::endl
                             << std::endl;
               }
           }
       }
       catch (std::exception &e) {
           std::cerr << e.what() << std::endl;
       }

       return 0;
   }

.. note:: To parse the results of these queries, this example uses
          `rapidjson`_, a header-only library that is released under
          the `MIT License`_.

.. _`rapidjson`: http://code.google.com/p/rapidjson/
.. _`MIT License`: http://www.opensource.org/licenses/mit-license.php

Building and running ``twitter_search``
=======================================

.. code-block:: bash

    $ cd ~/cpp-netlib-build
    $ make twitter_search

Twitter provides a powerful set of operators to modify the behaviour
of search queries.  Some examples are provided below:

.. code-block:: bash

   $ ./example/twitter_search "Lady Gaga"

Returns any results that contain the exact phrase "Lady Gaga".

.. code-block:: bash

   $ ./example/twitter_search "#olympics"

Returns any results with the #olympics hash tag.

.. code-block:: bash

   $ ./example/twitter_search "flight :("

Returns any results that contain "flight" and have a negative
attitude.

More examples can be found on `Twitter's search API`_ page.

Diving into the code
====================

.. code-block:: c++

   uri::uri base_uri("http://search.twitter.com/search.json");

   std::cout << "Searching Twitter for query: " << argv[1] << std::endl;
   uri::uri search;
   search << base_uri << uri::query("q", uri::encoded(argv[1]));

The :mod:`cpp-netlib` URI builder uses a stream-like syntax to allow
developers to construct more complex URIs.  The example above re-uses
the same base URI and allows the command line argument to be used as
part of the URI query.  The builder also supports percent encoding
using the ``encoded`` directive.
