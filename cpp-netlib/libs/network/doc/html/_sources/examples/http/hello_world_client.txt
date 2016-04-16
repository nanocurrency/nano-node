.. _hello_world_http_client:

***************************
 "Hello world" HTTP client
***************************

Since we have a "Hello World" HTTP server, let's then create an HTTP client to
access that server. This client will be similar to the HTTP client we made
earlier in the documentation.

The code
========

We want to create a simple HTTP client that just makes a request to the HTTP
server that we created earlier. This really simple client will look like this:

.. code-block:: c++

    #include <boost/network/protocol/http/client.hpp>
    #include <string>
    #include <sstream>
    #include <iostream>

    namespace http = boost::network::http;

    int main(int argc, char * argv[]) {
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " address port" << std::endl;
            return 1;
        }

        try {
            http::client client;
            std::ostringstream url;
            url << "http://" << argv[1] << ":" << argv[2] << "/";
            http::client::request request(url.str());
            http::client::response response =
                client.get(request);
            std::cout << body(response) << std::endl;
        } catch (std::exception & e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
        return 0;
    }

Building and running the client
===============================

Just like with the HTTP Server and HTTP client example before, we can build this
example by doing the following on the shell:

.. code-block:: bash

    $ cd ~/cpp-netlib-build
    $ make hello_world_client

This example can be run from the command line as follows:

.. code-block:: bash

    $ ./example/hello_world_client http://127.0.0.1:8000

.. note:: This assumes that you have the ``hello_world_server`` running on
   localhost port 8000.

Diving into the code
====================

All this example shows is how easy it is to write an HTTP client that connects
to an HTTP server, and gets the body of the response. The relevant lines are:

.. code-block:: c++

    http::client client;
    http::client::request request(url.str());
    http::client::response response =
        client.get(request);
    std::cout << body(response) << std::endl;

You can then imagine using this in an XML-RPC client, where you can craft the
XML-RPC request as payload which you can pass as the body to a request, then
perform the request via HTTP:

.. code-block:: c++

    http::client client;
    http::client::request request("http://my.webservice.com/");
    http::client::response =
        client.post(request, some_xml_string, "application/xml");
    std::data = body(response);

The next set of examples show some more practical applications using
the :mod:`cpp-netlib` HTTP client.
