
HTTP Client API
===============

General
-------

:mod:`cpp-netlib` includes and implements a number of HTTP clients that you can
use and embed in your own applications. All of the HTTP client implementations:

  * **Cannot be copied.** This means you may have to store instances of the
    clients in dynamic memory if you intend to use them as function parameters
    or pass them around in smart pointers or by reference.
  * **Assume that requests made are independent of each other.** There currently
    is no cookie or session management system built-in to cpp-netlib's HTTP client
    implementations.

The HTTP clients all share the same API, but the internals are documented in
terms of what is different and what to expect with the different
implementations.

As of 0.9.1 the default implementation for the :mod:`cpp-netlib` HTTP client is
asynchronous.

As of 0.11 the `Synchronous Clients`_ are now *DEPRECATED* and will be removed
in subsequent releases.

Features
--------

The HTTP client implementation supports requesting secure HTTP (HTTPS) content
only in the following situations:

  * **Client libraries are built with ``BOOST_NETWORK_ENABLE_HTTPS``.** This
    tells the implementation to use HTTPS-specific code to handle HTTPS-based
    content when making connections associated with HTTPS URI's. This requires
    a dependency on OpenSSL_.
  * **The ``BOOST_NETWORK_ENABLE_HTTPS`` macro is set when compiling user
    code.** It is best to define this either at compile-time of all code using
    the library, or before including any of the client headers.

To use the client implementations that support HTTPS URIs, you may explicitly
do the following:

.. code-block:: c++

   #define BOOST_NETWORK_ENABLE_HTTPS
   #include <boost/network/include/http/client.hpp>

This forces HTTPS support to be enabled and forces a dependency on OpenSSL_.
This dependency is imposed by `Boost.Asio`_

.. _OpenSSL: http://www.openssl.org/
.. _`Boost.Asio`: http://www.boost.org/libs/asio

Implementations
---------------

There is a single user-facing template class named ``basic_client`` which takes
three template parameters:

  * **Tag** - which static tag you choose that defines the behavior of the client.

  * **http_version_major** - an unsigned int that defines the HTTP major version
    number, this directly affects the HTTP messages sent by the client.

  * **http_version_minor** - an unsigned int that defines the HTTP minor version
    number.

There are two major different class of implementations of the ``basic_client``
template that depend on which tag you choose: `Synchronous Clients`_ and
`Asynchronous Clients`_. These two different classes are described in their own
sections following this one. What follows is a table of all tags supported by
the HTTP client implementation provided by :mod:`cpp-netlib`.

---------------

.. include:: ../in_depth/http_client_tags.rst

In the above table the tags follow a pattern for describing the behavior
introduced by the tags. This pattern is shown below:

    <protocol>_<modifier>_<character-width>_<resolve-strategy>

For example, the tag ``http_default_8bit_tcp_resolve`` indicates the protocol
``http``, a modifier ``default``, a character width of ``8bit``, and a resolve
strategy of ``tcp_resolve``.

Synchronous Clients
~~~~~~~~~~~~~~~~~~~

Of the client tags shown in the table, the following makes the ``basic_client``
behave as a fully synchronous client.

  * **http_default_8bit_tcp_resolve**
  * **http_default_8bit_udp_resolve**
  * **http_keepalive_8bit_tcp_resolve**
  * **http_keepalive_8bit_udp_resolve**

The synchronous client implements all the operations of the client underneath
the interface all block to wait for I/O to finish. All the member methods are
synchronous and will block until the response object is ready or throws if errors
are encountered in the performance of the HTTP requests.

.. warning:: The synchronous clients are **NOT** thread safe. You will need to do
   external synchronization to use synchronous client implementations.

.. note:: As of version 0.11, all the synchronous client implementations are
   deprecated. They will be removed in the next version of the library.

Asynchronous Clients
~~~~~~~~~~~~~~~~~~~~

The following tags specify the ``basic_client`` to behave in an asynchronous
manner:

  * **http_async_8bit_tcp_resolve**
  * **http_async_8bit_udp_resolve**

An asynchronous client implementation means that``basic_client<...>`` is an
`Active Object`_. This means that the client has and manages its own lifetime
thread, and returns values that are asynchronously filled in. The response
object encapsulates Boost.Thread_ futures which get filled in once the values
are available.

.. _Boost.Thread: http://www.boost.org/libs/thread
.. _`Active Object`: http://en.wikipedia.org/wiki/Active_object

The asynchronous clients implement all operations asynchronously which are hidden
from the user. The interface is still synchronous but the fetching of data
happens on a different thread.

.. note:: The asynchronous clients are thread safe, and can be shared across
   many threads. Each request starts a sequence of asynchronous operations
   dedicated to that request. The client does not re-cycle connections and uses
   a one-request-one-connection model.

When an asynchronous client object is destroyed, it waits for all pending
asynchronous operations to finish. Errors encountered during operations on
retrieving data from the response objects cause exceptions to be thrown --
therefore it is best that if a client object is constructed, it should outlive
the response object or be outside the try-catch block handling the errors from
operations on responses. In code, usage should look like the following:

.. code-block:: c++

    http::client client;
    try {
      http::client::response response = client.get("http://www.example.com/");
      std::cout << body(response);
    } catch (std::exception& e) {
      // deal with exceptions here
    }

A common mistake is to declare the client inside the try block which invokes
undefined behavior when errors arise from the handling of response objects.
Previous examples cited by the documentation showed the short version of the
code which didn't bother moving the ``http::client`` object outside of the same
``try`` block where the request/response objects are being used.

Member Functions
----------------

In this section we assume that the following typedef is in effect:

.. code-block:: c++

    typedef boost::network::http::basic_client<
        boost::network::http::tags::http_default_8bit_udp_resolve
        , 1
        , 1
        >
        client;

Also, that code using the HTTP client will have use the following header:

.. code-block:: c++

    #include <boost/network/include/http/client.hpp>

.. note:: Starting version 0.9, cpp-netlib clients and server implementations
   by default now have an externally-linked component. This is a breaking change
   for code that used to rely on cpp-netlib being a header-only library, but can
   inhibited by defining the ``BOOST_NETWORK_NO_LIB`` preprocessor macro before
   including any cpp-netlib header.

.. note:: Starting version 0.11, cpp-netlib clients and server implementations
   no longer support the ``BOOST_NETWORK_NO_LIB`` option.

Constructors
~~~~~~~~~~~~

The client implementation can be default constructed, or customized at
initialization.

``client()``
    Default constructor.
``explicit client(client::options const &)``
    Constructor taking a ``client_options<Tag>`` object. The following table
    shows the options you can set on a ``client_options<Tag>`` instance.

+--------------------------+----------------------------+--------------------------+
| Parameter Name           | Type                       | Description              |
+==========================+============================+==========================+
| follow_redirects         | ``bool``                   | Boolean to specify       |
|                          |                            | whether the client       |
|                          |                            | should follow HTTP       |
|                          |                            | redirects. Default is    |
|                          |                            | ``false``.               |
+--------------------------+----------------------------+--------------------------+
| cache_resolved           | ``bool``                   | Boolean to specify       |
|                          |                            | whether the client       |
|                          |                            | should cache resolved    |
|                          |                            | endpoints. The default   |
|                          |                            | is ``false``.            |
+--------------------------+----------------------------+--------------------------+
| io_service               | ``shared_ptr<io_service>`` | Shared pointer to a      |
|                          |                            | Boost.Asio               |
|                          |                            | ``io_service``.          |
+--------------------------+----------------------------+--------------------------+
| openssl_certificate      | ``string``                 | The filename of the      |
|                          |                            | certificate to load for  |
|                          |                            | the SSL connection for   |
|                          |                            | verification.            |
+--------------------------+----------------------------+--------------------------+
| openssl_verify_path      | ``string``                 | The directory from       |
|                          |                            | which the certificate    |
|                          |                            | authority files are      |
|                          |                            | located.                 |
+--------------------------+----------------------------+--------------------------+
| always_verify_peer       | ``bool``                   | Boolean to specify       |
|                          |                            | whether the client       |
|                          |                            | should always verify     |
|                          |                            | peers in SSL connections |
+--------------------------+----------------------------+--------------------------+
| openssl_certificate_file | ``string``                 | Filename of the          |
|                          |                            | certificate to use for   |
|                          |                            | client-side SSL session  |
|                          |                            | establishment.           |
+--------------------------+----------------------------+--------------------------+
| openssl_private_key_file | ``string``                 | Filename of the          |
|                          |                            | private key to use for   |
|                          |                            | client-side SSL session  |
|                          |                            | establishment.           |
+--------------------------+----------------------------+--------------------------+
| timeout                  | ``int``                    | Number of seconds to     |
|                          |                            | wait for client requests |
|                          |                            | before considering a     |
|                          |                            | timeout has occurred.    |
+--------------------------+----------------------------+--------------------------+


To use the above supported named parameters, you'll have code that looks like
the following:

.. code-block:: c++

    using namespace boost::network::http; // parameters are in this namespace
    client::options options;
    options.follow_redirects(true)
           .cache_resolved(true)
           .io_service(boost::make_shared<boost::asio::io_service>())
           .openssl_certificate("/tmp/my-cert")
           .openssl_verify_path("/tmp/ca-certs")
           .timeout(10);
    client client_(options);
    // use client_ as normal from here on out.

HTTP Methods
~~~~~~~~~~~~

The client implementation supports various HTTP methods. The following
constructs assume that a client has been properly constructed named ``client_``
and that there is an appropriately constructed request object named ``request_``
and that there is an appropriately constructed response object named
``response_`` like the following:

.. code-block:: c++

    using namespace boost::network::http;  // parameters are here
    client client_();
    client::request request_("http://cpp-netib.github.com/");
    client::response response_;

``response_ = client_.get(request_)``
    Perform an HTTP GET request.
``response_ = client_.get(request_, callback)``
    Perform an HTTP GET request, and have the body chunks be handled by the
    ``callback`` parameter. The signature of ``callback`` should be the following:
    ``void(iterator_range<char const *> const &, boost::system::error_code const
    &)``.
``response_ = client_.head(request_)``
    Perform an HTTP HEAD request.
``response_ = client_.post(request_)``
    Perform an HTTP POST, use the data already set in the request object which
    includes the headers, and the body.
``response_ = client_.post(request_, callback)``
    Perform an HTTP POST request, and have the body chunks be handled by the
    ``callback`` parameter. The signature of ``callback`` should be the following:
    ``void(iterator_range<char const *> const &, boost::system::error_code const
    &)``.
``response_ = client_.post(request_, body)``
    Body is a string of type ``boost::network::string<Tag>::type`` where ``Tag``
    is the HTTP Client's ``Tag``. The default content-type used is
    ``x-application/octet-stream``.
``response_ = client_.post(request_, body, callback)``
    Body is a string of type ``boost::network::string<Tag>::type`` where ``Tag``
    is the HTTP Client's ``Tag``. The default content-type used is
    ``x-application/octet-stream``. Have the response body chunks be handled by
    the ``callback`` parameter. The signature of ``callback`` should be the
    following: ``void(iterator_range<char const *> const &,
    boost::system::error_code const &)``.
``response_ = client_.post(request_, body, content_type)``
    The body and content_type parameters are of type
    ``boost::network::string<Tag>::type`` where ``Tag`` is the HTTP Client's
    ``Tag``. This uses the request object's other headers.
``response_ = client_.post(request_, body, content_type, callback)``
    The body and content_type parameters are of type
    ``boost::network::string<Tag>::type`` where ``Tag`` is the HTTP Client's
    ``Tag``. This uses the request object's other headers. Have the response
    body chunks be handled by the ``callback`` parameter. The signature of
    ``callback`` should be the following: ``void(iterator_range<char const *> const
    &, boost::system::error_code const &)``.
``response_ = client_.post(request_, body, content_type, callback, streaming_callback)``
    The body and content_type parameters are of type
    ``boost::network::string<Tag>::type`` where ``Tag`` is the HTTP Client's
    ``Tag``. This uses the request object's other headers. Have the response
    body chunks be handled by the ``callback`` parameter. The signature of
    ``callback`` should be the following: ``void(iterator_range<char const *> const
    &, boost::system::error_code const &)``. The ``streaming_callback``
    argument should have a which has a signature of the form:
    ``bool(string_type&)``. The provided ``string_type&`` will be streamed as
    soon as the function returns. A return value of ``false`` signals the
    client that the most recent invocation is the last chunk to be sent.
``response_ = client_.post(request_, streaming_callback)``
    Perform and HTTP POST request, and have the request's body chunks be
    generated by the ``streaming_callback`` which has a signature of the form:
    ``bool(string_type&)``. The provided ``string_type&`` will be streamed as
    soon as the function returns. A return value of ``false`` signals the client
    that the most recent invocation is the last chunk to be sent.
``response_ = client_.post(request_, callback, streaming_callback)``
    Perform an HTTP POST request, and have the body chunks be handled by the
    ``callback`` parameter. The signature of ``callback`` should be the
    following: ``void(iterator_range<char const *> const &,
    boost::system::error_code const &)``. This form also has the request's body
    chunks be generated by the ``streaming_callback`` which has a signature of
    the form: ``bool(string_type&)``. The provided ``string_type&`` will be
    streamed as soon as the function returns. A return value of ``false``
    signals the client that the most recent invocation is the last chunk to be
    sent.
``response_ = client_.put(request_)``
    Perform an HTTP PUT, use the data already set in the request object which
    includes the headers, and the body.
``response_ = client_.put(request_, callback)``
    Perform an HTTP PUT request, and have the body chunks be handled by the
    ``callback`` parameter. The signature of ``callback`` should be the following:
    ``void(iterator_range<char const *> const &, boost::system::error_code const
    &)``.
``response_ = client_.put(request_, body)``
    Body is a string of type ``boost::network::string<Tag>::type`` where ``Tag``
    is the HTTP Client's ``Tag``. The default content-type used is
    ``x-application/octet-stream``.
``response_ = client_.put(request_, body, callback)``
    Body is a string of type ``boost::network::string<Tag>::type`` where ``Tag``
    is the HTTP Client's ``Tag``. The default content-type used is
    ``x-application/octet-stream``. Have the response body chunks be handled by
    the ``callback`` parameter. The signature of ``callback`` should be the
    following: ``void(iterator_range<char const *> const &,
    boost::system::error_code const &)``.
``response_ = client_.put(request_, body, content_type)``
    The body and content_type parameters are of type
    ``boost::network::string<Tag>::type`` where ``Tag`` is the HTTP Client's
    ``Tag``. This uses the request object's other headers.
``response_ = client_.put(request_, body, content_type, callback)``
    The body and content_type parameters are of type
    ``boost::network::string<Tag>::type`` where ``Tag`` is the HTTP Client's
    ``Tag``. This uses the request object's other headers. Have the response
    body chunks be handled by the ``callback`` parameter. The signature of
    ``callback`` should be the following: ``void(iterator_range<char const *> const
    &, boost::system::error_code const &)``.
``response_ = client_.put(request_, body, content_type, callback, streaming_callback)``
    The body and content_type parameters are of type
    ``boost::network::string<Tag>::type`` where ``Tag`` is the HTTP Client's
    ``Tag``. This uses the request object's other headers. Have the response
    body chunks be handled by the ``callback`` parameter. The signature of
    ``callback`` should be the following: ``void(iterator_range<char const *> const
    &, boost::system::error_code const &)``. This form also has the request's body
    chunks be generated by the ``streaming_callback`` which has a signature of
    the form: ``bool(string_type&)``. The provided ``string_type&`` will be
    streamed as soon as the function returns. A return value of ``false``
    signals the client that the most recent invocation is the last chunk to be
    sent
``response_ = client_.put(request_, streaming_callback)``
    Perform and HTTP PUT request, and have the request's body chunks be
    generated by the ``streaming_callback`` which has a signature of the form:
    ``bool(string_type&)``. The provided ``string_type&`` will be streamed as
    soon as the function returns. A return value of ``false`` signals the client
    that the most recent invocation is the last chunk to be sent.
``response_ = client_.put(request_, callback, streaming_callback)``
    Perform an HTTP PUT request, and have the body chunks be handled by the
    ``callback`` parameter. The signature of ``callback`` should be the
    following: ``void(iterator_range<char const *> const &,
    boost::system::error_code const &)``. This form also has the request's body
    chunks be generated by the ``streaming_callback`` which has a signature of
    the form: ``bool(string_type&)``. The provided ``string_type&`` will be
    streamed as soon as the function returns. A return value of ``false``
    signals the client that the most recent invocation is the last chunk to be
    sent.
``response_ = client_.delete_(request_)``
    Perform an HTTP DELETE request.
``response_ = client_.delete_(request_, body_handler=callback)``
    Perform an HTTP DELETE request, and have the response body chunks be handled
    by the ``callback`` parameter. The signature of ``callback`` should be the
    following: ``void(iterator_range<char const *> const &,
    boost::system::error_code const &)``.

Client-Specific
~~~~~~~~~~~~~~~

``client_.clear_resolved_cache()``
    Clear the cache of resolved endpoints.


Streaming Body Handler
~~~~~~~~~~~~~~~~~~~~~~

As of v0.9.1 the library now offers a way to support a streaming body callback
function in all HTTP requests that expect a body part (GET, PUT, POST, DELETE).
A convenience macro is also provided to make callback handlers easier to write.
This macro is called ``BOOST_NETWORK_HTTP_BODY_CALLBACK`` which allows users to
write the following code to easily create functions or function objects that
are compatible with the callback function requirements.

An example of how to use the macro is shown below:

.. code-block:: c++

    struct body_handler {
        explicit body_handler(std::string & body)
        : body(body) {}

        BOOST_NETWORK_HTTP_BODY_CALLBACK(operator(), range, error) {
            // in here, range is the Boost.Range iterator_range, and error is
            // the Boost.System error code.
            if (!error)
                body.append(boost::begin(range), boost::end(range));
        }

        std::string & body;
    };

    // somewhere else
    std::string some_string;
    response_ = client_.get(request("http://cpp-netlib.github.com/"),
                            body_handler(some_string));

You can also use if for standalone functions instead if you don't want or need
to create a function object.

.. code-block:: c++

    BOOST_NETWORK_HTTP_BODY_CALLBACK(print_body, range, error) {
        if (!error)
            std::cout << "Received " << boost::distance(range) << "bytes."
                      << std::endl;
        else
            std::cout << "Error: " << error << std::endl;
    }

    // somewhere else
    response_ = client_.get(request("http://cpp-netlib.github.com/"),
                            print_body);

The ``BOOST_NETWORK_HTTP_BODY_CALLBACK`` macro is defined in
``boost/network/protocol/http/client/macros.hpp``.
