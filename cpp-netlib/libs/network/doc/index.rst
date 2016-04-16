.. _index:
.. rubric:: Straightforward network programming in modern C++

.. :Authors: Glyn Matthews <glyn.matthews@gmail.com>
.. 	  Dean Michael Berris <dberris@google.com>
.. :Date: 2014-10-01
.. :Version: 0.11.0
.. :Description: Complete user documentation, with examples, for the :mod:`cpp-netlib`.
.. :Copyright: Copyright Glyn Matthews, Dean Michael Berris 2008-2013.
..             Copyrigh 2013 Google, Inc.
..             Distributed under the Boost Software License, Version
..             1.0. (See accompanying file LICENSE_1_0.txt or copy at
..             http://www.boost.org/LICENSE_1_0.txt)

Getting cpp-netlib
==================

You can find out more about the :mod:`cpp-netlib` project at
http://cpp-netlib.org/.

**Download**

You can get the latest official version of the library from the official
project website at:

    http://cpp-netlib.org/

This version of :mod:`cpp-netlib` is tagged as cpp-netlib-0.11.0 in the GitHub_
repository. You can find more information about the progress of the development
by checking our GitHub_ project page at:

    http://github.com/cpp-netlib/cpp-netlib

**Support**

You can ask questions, join the discussion, and report issues to the
developers mailing list by joining via:

    https://groups.google.com/group/cpp-netlib

You can also file issues on the Github_ issue tracker at:

    http://github.com/cpp-netlib/cpp-netlib/issues

We are a growing community and we are happy to accept new
contributions and ideas.

C++ Network Library
===================

:mod:`cpp-netlib` is a library collection that provides application layer
protocol support using modern C++ techniques.  It is light-weight, fast,
portable and is intended to be as easy to configure as possible.

Hello, world!
=============

The :mod:`cpp-netlib` allows developers to write fast, portable
network applications with the minimum of fuss.

An HTTP server-client example can be written in tens of lines of code.
The client is as simple as this:

.. code-block:: cpp

    using namespace boost::network;
    using namespace boost::network::http;

    client::request request_("http://127.0.0.1:8000/");
    request_ << header("Connection", "close");
    client client_;
    client::response response_ = client_.get(request_);
    std::string body_ = body(response_);

And the corresponding server code is listed below:

.. code-block:: cpp

    namespace http = boost::network::http;

    struct handler;
    typedef http::server<handler> http_server;

    struct handler {
        void operator() (http_server::request const &request,
                         http_server::response &response) {
            response = http_server::response::stock_reply(
                http_server::response::ok, "Hello, world!");
        }

        void log(http_server::string_type const &info) {
            std::cerr << "ERROR: " << info << '\n';
        }
    };

    int main(int arg, char * argv[]) {
        handler handler_;
        http_server::options options(handler_);
        http_server server_(
            options.address("0.0.0.0")
                   .port("8000"));
        server_.run();
    }

Want to learn more?
===================

    * :ref:`Take a look at the getting started guide <getting_started>`
    * :ref:`Learn from some simple examples <examples>`
    * :ref:`Find out what's new <whats_new>`
    * :ref:`Study the library in more depth <in_depth>`
    * :ref:`Discover more through the full reference <reference>`
    * :ref:`Full table of contents <contents>`

.. warning:: Be aware that not all features are stable.  The generic
   	     message design is under review and the URI and HTTP
   	     client implementation will continue to undergo
   	     refactoring.  Future versions will include support for
   	     other network protocols.


.. _Boost: http://www.boost.org/
.. _GitHub: http://github.com/

