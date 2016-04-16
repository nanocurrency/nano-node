HTTP implementation
===================

HTTP client
```````````

At the heart of the HTTP client implementation is a single class aptly named
``basic_client``, which is also a template. The template ``basic_client`` takes
three template parameters:

.. code-block:: c++

    namespace boost { namespace http {

        template <class Tag, unsigned version_major, unsigned version_minor>
        struct basic_client;

    } // namespace http

    } // namespace boost

The ``Tag`` template parameter follows the same tag-dispatch mechanism to
determine the behavior of the ``basic_client``. The interface of
``basic_client`` may change depending on certain properties defined for the tag
you provide. Below is a table of predefined supported tags you can use in your
overload of the ``basic_client``:

------------

.. include:: http_client_tags.rst

.. _Boost.Thread: http://www.boost.org/libs/thread


The default typedef for the HTTP client that is provided uses the
``http_async_8bit_udp_resolve`` tag, and implements HTTP 1.1. The exact
typedef is in the ``boost::network::http`` namespace as the following:

.. code-block:: c++

    namespace boost { namespace network { namespace http {

        typedef basic_client<tags::http_async_8bit_udp_resolve, 1, 1>
            client;

    }}}


This type has nested typedefs for the correct types for the ``basic_request``
and ``basic_response`` templates. To use the correct types for ``basic_request``
or ``basic_response`` you can use these nested typedefs like so:


.. code-block:: c++

    boost::network::http::client::request request;
    boost::network::http::client::response response;

    // or...
    using namespace boost::network;
    http::client::request request;
    http::client::response response;


Typical use cases for the HTTP client would look something like the following:


.. code-block:: c++

    using namespace boost::network;
    http::request request("http://www.boost.org/");
    request << header("Connection", "close");


The ``basic_client`` implements all HTTP methods as member functions
(HEAD, GET, POST, PUT, DELETE).  Therefore, the code to make an HTTP
request looks trivially simple:


.. code-block:: c++

    using namespace boost::network;
    http::client client;
    http::client::request request("http://www.boost.org/");
    http::client::response response = client.get(request);


Accessing data from ``http::response`` is done using wrappers.
To get the response headers, we use the ``headers`` wrapper which
returns, in the default case, a multimap of strings to strings:


.. code-block:: c++

    using namespace boost::network;
    typedef headers_range<http_client::response>::type response_headers;
    boost::range_iterator<response_headers>::type iterator;

    response_headers headers_ = headers(response);
    for (iterator it = headers_.begin(); it != headers_.end(); ++it) {
        std::cout << it->first << ": " << it->second << std::endl;
    }
    std::cout << std::endl;


HTTP server
```````````

As with the HTTP client, the HTTP server that is provided with
cpp-netlib is extensible through the tag mechanism and is embeddable.
The template class declaration of ``basic_server`` is given below:


.. code-block:: c++

    namespace boost { namespace network { namespace http {

        template <class Tag, class RequestHandler> basic_server;

    }}}


The second template argument is used to specify the request handler
type. The request handler type is a functor type which should overload
the function call operator (``RequestHandler::operator()`` should be
overloaded) that takes two parameters: the first one being a reference
to a ``const basic_request<Tag>`` and the second being a reference to
a ``basic_response<Tag>`` instance.

All the logic for parsing the HTTP request and building the ``const
basic_request<Tag>`` object resides internally in the ``basic_server``
template.  Processing the request is delegated to the
``RequestHandler`` type, and the assumption of which would be that the
response is formed inside the ``RequestHandler`` function call
operator overload.

The ``basic_server`` template however is only an underlying
implementation while the user-visible implementation is the
``http::server`` template. This simply specializes the
``basic_server`` template to use the ``default_`` tag and forwards the
``RequestHandler`` parameter:

.. code-block:: c++

    namespace boost { namespace network { namespace http {

        template <class RequestHandler>
        class server :
            public basic_server<default_, RequestHandler> {};

    }}}

To use the forwarding server type you just supply the request handler
implementation as the parameter. For example, an "echo" server example
might look something like this:


.. code-block:: c++

    using namespace boost::network;
    struct echo;
    typedef http::server<echo> echo_server;

    struct echo {
        void operator () (const echo_server::request &request,
                          echo_server::response &response) const {
            std::string ip = source(request);
            response = echo_server::response::stock_reply(
                echo_server::response::ok,
		body(request));
            std::cerr << "[" << ip << "]: " << request.uri <<
                " status = " << echo_server::response::ok << '\n';
        }
    };


Here, all we're doing is returning the original request body with an
HTTP OK response (200). We are also printing the IP address from where the
request came from. Notice that we are using a wrapper to access the source of
the request.
