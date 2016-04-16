.. _hello_world_http_server:

***************************
 "Hello world" HTTP server
***************************

Now that we've seen how we can deal with request and response objects from the
client side, we'll see how we can then use the same abstractions on the server
side. In this example we're going to create a simple HTTP Server in C++ using
:mod:`cpp-netlib`.

The code
========

The :mod:`cpp-netlib` provides the framework to develop embedded HTTP
servers.  For this example, the server is configured to return a
simple response to any HTTP request.

.. code-block:: c++

    #include <boost/network/protocol/http/server.hpp>
    #include <string>
    #include <iostream>

    namespace http = boost::network::http;

    struct hello_world;
    typedef http::server<hello_world> server;

    struct hello_world {
        void operator() (server::request const &request,
                         server::response &response) {
            std::string ip = source(request);
            response = server::response::stock_reply(
                server::response::ok, std::string("Hello, ") + ip + "!");
        }
    };

    int
    main(int argc, char * argv[]) {

        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " address port" << std::endl;
            return 1;
        }

        try {
            hello_world handler;
            server server_(argv[1], argv[2], handler);
            server_.run();
        }
        catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }

        return 0;
    }

This is about a straightforward as server programming will get in C++.

Building and running the server
===============================

Just like with the HTTP client, we can build this example by doing the following
on the shell:

.. code-block:: bash

    $ cd ~/cpp-netlib-build
    $ make hello_world_server

The first two arguments to the ``server`` constructor are the host and
the port on which the server will listen.  The third argument is the
the handler object defined previously.  This example can be run from
a command line as follows:

.. code-block:: bash

    $ ./example/hello_world_server 0.0.0.0 8000

.. note:: If you're going to run the server on port 80, you may have to run it
   as an administrator.

Diving into the code
====================

Let's take a look at the code listing above in greater detail.

.. code-block:: c++

    #include <boost/network/protocol/http/server.hpp>

This header contains all the code needed to develop an HTTP server with
:mod:`cpp-netlib`.

.. code-block:: c++

    struct hello_world;
    typedef http::server<hello_world> server;

    struct hello_world {
        void operator () (server::request const &request,
                          server::response &response) {
            std::string ip = source(request);
            response = server::response::stock_reply(
                server::response::ok, std::string("Hello, ") + ip + "!");
        }
    };

``hello_world`` is a functor class which handles HTTP requests.  All
the operator does here is return an HTTP response with HTTP code 200
and the body ``"Hello, <ip>!"``. The ``<ip>`` in this case would be
the IP address of the client that made the request.

There are a number of pre-defined stock replies differentiated by
status code with configurable bodies.

All the supported enumeration values for the response status codes can be found
in ``boost/network/protocol/http/impl/response.ipp``.

.. code-block:: c++

    hello_world handler;
    server server_(argv[1], argv[2], handler);
    server_.run();

The first two arguments to the ``server`` constructor are the host and
the port on which the server will listen.  The third argument is the
the handler object defined previously.

.. note:: In this example, the server is specifically made to be single-threaded.
   In a multi-threaded server, you would invoke the ``hello_world::run`` member
   method in a set of threads. In a multi-threaded environment you would also
   make sure that the handler does all the necessary synchronization for shared
   resources across threads. The handler is passed by reference to the server
   constructor and you should ensure that any calls to the ``operator()`` overload
   are thread-safe.

