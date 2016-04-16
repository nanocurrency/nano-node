
HTTP Server API
===============

General
-------

:mod:`cpp-netlib` includes and implements two distinct HTTP server
implementations that you can use and embed in your own applications. Both HTTP
Server implementations:

  * **Cannot be copied.** This means you may have to store instances of the HTTP
    Server in dynamic memory if you intend to use them as function parameters or
    pass them around in smart pointers of by reference.
  * **Assume that requests made are independent of each other.** None of the
    HTTP Server implementations support request pipelining (yet) so a single
    connection only deals with a single request.
  * **Are header-only and are compiled-into your application.** Future releases
    in case you want to upgrade the implementation you are using in your
    application will be distributed as header-only implementations, which means
    you have to re-compile your application to use a newer version of the
    implementations.

The HTTP Servers have different semantics, and in some cases require different
APIs from the supplied template parameters.

Implementations
---------------

There are two different user-facing template classes that differentiate the
`Synchronous Servers`_ from the `Asynchronous Servers`_. Both templates take a
single template parameter named ``Handler`` which describes the type of the
Handler function object.

There are two different Handler concepts, one concept for `Synchronous Servers`_
and another for `Asynchronous Servers`.

The SynchronousHandler concept for `Synchronous Servers`_ is described by the
following table:

---------------

**Legend:**

H
    The Handler type.
h
    An instance of H.
Req
    A type that models the Request Concept.
Res
    A type that models the Response Concept.
req
    An instance of Req.
res
    An instance of Res.

+----------------+-------------+----------------------------------------------+
| Construct      | Return Type | Description                                  |
+================+=============+==============================================+
| ``h(req,res)`` | ``void``    | Handle the request; res is passed in as a    |
|                |             | non-const lvalue, which represents the       |
|                |             | response to be returned to the client        |
|                |             | performing the request.                      |
+----------------+-------------+----------------------------------------------+

More information about the internals of the `Synchronous Servers`_ can be found
in the following section.

The AsynchronousHandler concept for `Asynchronous Servers`_ is described by the
following table:

---------------

**Legend:**

H
    The Handler type.
h
    An instance of H.
Req
    A type that models the Request Concept.
ConnectionPtr
    A type that models the Connection Pointer Concept.
req
    An instance of Req.
conn
    An instance of ConncetionPtr.

+------------------+-------------+--------------------------------------------+
| Construct        | Return Type | Description                                |
+==================+=============+============================================+
| ``h(req, conn)`` | ``void``    | Handle the request; conn is a shared       |
|                  |             | pointer which exposes functions for        |
|                  |             | writing to and reading from the connection.|
+------------------+-------------+--------------------------------------------+

More information about the internals of the `Asynchronous Servers`_ can be found
in the following section.

Synchronous Servers
-------------------

The synchronous server implementation is represented by the template ``server``
in namespace ``boost::network::http``. The ``server`` template takes in a single
template parameter named ``Handler`` which models the SynchronousHandler
concept (described above).

An instance of Handler is taken in by reference to the constructor of the HTTP
server. This means the Handler is not copied around and only a single instance
of the handler is used for all connections and requests performed against the
HTTP server.

.. warning:: It is important to note that the HTTP server does not implement any
   locking upon invoking the Handler. In case you have any state in the Handler
   that will be associated with the synchronous server, you would have to
   implement your own synchronization internal to the Handler implementation.
   This matters especially if you run the synchronous server in multiple
   threads.

The general pattern of usage for the HTTP Server template is shown below:

.. code-block:: c++

    struct handler;
    typedef boost::network::http::server<handler> http_server;

    struct handler {
        void operator()(
            http_server::request const & req,
            http_server::response & res
        ) {
            // do something, and then edit the res object here.
        }
    };

More information about the actual HTTP Server API follows in the next section.
It is important to understand that the HTTP Server is actually embedded in your
application, which means you can expose almost all your application logic
through the Handler type, which you can also initialize appropriately.

API Documentation
~~~~~~~~~~~~~~~~~

The following sections assume that the following file has been included:

.. code-block:: c++

    #include <boost/network/include/http/server.hpp>

And that the following typedef's have been put in place:

.. code-block:: c++

    struct handler_type;
    typedef boost::network::http::server<handler_type> http_server;

    struct handler_type {
        void operator()(http_server::request const & request,
                        http_server::response & response) {
            // do something here
        }
    };

Constructor
```````````

``explicit http_server(options)``
    Construct an HTTP Server instance, passing in a ``server_options<Tag,
    Handler>`` object. The following table shows the supported options in
    ``server_options<Tag, Handler>``.

+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| Parameter Name        | Type                                     | Description                                                                                      |
+=======================+==========================================+==================================================================================================+
| address               | string_type                              | The hostname or IP address from which the server should be bound to. This parameter is required. |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| port                  | string_type                              | The port to which the server should bind and listen to. This parameter is required.              |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| thread_pool           | ``shared_ptr<thread_pool>``              | A shared pointer to an instance of ``boost::network::utils::thread_pool`` -- this is the         |
|                       |                                          | thread pool from where the handler is invoked. This parameter is only applicable and required    |
|                       |                                          | for ``async_server`` instances.                                                                  |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| io_service            | ``shared_ptr<io_service>``               | An optional lvalue to an instance of ``boost::asio::io_service`` which allows the server to use  |
|                       |                                          | an already-constructed ``boost::asio::io_service`` instance instead of instantiating one that it |
|                       |                                          | manages.                                                                                         |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| reuse_address         | ``bool``                                 | A boolean that specifies whether to re-use the address and port on which the server will be      |
|                       |                                          | bound to. This enables or disables the socket option for listener sockets. The default is        |
|                       |                                          | ``false``.                                                                                       |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| report_aborted        | ``bool``                                 | A boolean that specifies whether the listening socket should report aborted connection attempts  |
|                       |                                          | to the accept handler (an internal detail of cpp-netlib). This is put in place to allow for      |
|                       |                                          | future-proofing the code in case an optional error handler function is supported in later        |
|                       |                                          | releases of cpp-netlib. The default is ``false``.                                                |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| receive_buffer_size   | ``int``                                  | The size of the socket's receive buffer. The default is defined by Boost.Asio and is             |
|                       |                                          | platform-dependent.                                                                              |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| send_buffer_size      | ``int``                                  | The size of the socket's send buffer. The default is defined by Boost.Asio and is                |
|                       |                                          | platform-dependent.                                                                              |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| receive_low_watermark | ``int``                                  | The size of the socket's low watermark for its receive buffer. The default is defined by         |
|                       |                                          | Boost.Asio and is platform-dependent.                                                            |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| send_buffer_size      | ``int``                                  | The size of the socket's send low watermark for its send buffer. The default is defined by       |
|                       |                                          | Boost.Asio and is platform-dependent.                                                            |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| non_blocking_io       | ``bool``                                 | An optional bool to define whether the socket should use non-blocking I/O in case the platform   |
|                       |                                          | supports it. The default is ``true``.                                                            |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| linger                | ``bool``                                 | An optional bool to determine whether the socket should linger in case there's still data to be  |
|                       |                                          | sent out at the time of its closing. The default is ``true``.                                    |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| linger_timeout        | ``int``                                  | An optional int to define the timeout to wait for socket closes before it is set to linger.      |
|                       |                                          | The default is ``0``.                                                                            |
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+
| context               | ``shared_ptr<context>``                  | An optional shared pointer to an instance of ``boost::asio::ssl::context`` -- this contains the  |
|                       |                                          | settings needed to support SSL. This parameter is only applicable for ``async_server`` instances.|
+-----------------------+------------------------------------------+--------------------------------------------------------------------------------------------------+

To use the above supported named parameters, you'll have code that looks like the following:

.. code-block:: c++

    using namespace boost::network::http; // parameters are in this namespace
    handler handler_instance;
    sync_server<handler>::options options(handler_instance);
    options.address("0.0.0.0")
           .port("80")
           .io_service(boost::make_shared<boost::asio::io_service>())
           .reuse_address(true);
    sync_server<handler> instance(options);
    instance.run();

Public Members
``````````````

The following definitions assume that a properly constructed ``http_server``
instance has been constructed in the following manner:

.. code-block:: c++

    handler_type handler;
    http_server::options options(handler);
    http_server server(options.address("127.0.0.1").port("8000"));

``server.run()``
    Run the HTTP Server event loop. This function can be run on multiple threads
    following the example:

.. code-block:: c++

    boost::thread t1(boost::bind(&http_server::run, &server));
    boost::thread t2(boost::bind(&http_server::run, &server));
    server.run();
    t1.join();
    t2.join();

``server.stop()``
    Stop the HTTP Server acceptor and wait for all pending requests to finish.

Response Object
```````````````

The response object has its own public member functions which can be very
helpful in certain simple situations.

``response = http_server::response::stock_reply(status, body)``
    Code like the above should go inside the handler's ``operator()`` overload.
    The body parameter is an ``std::string``. The status parameter is any of
    the following values from the ``http_server::response`` enum
    ``status_type``:

.. code-block:: c++

    enum status_type {
        ok = 200,
        created = 201,
        accepted = 202,
        no_content = 204,
        multiple_choices = 300,
        moved_permanently = 301,
        moved_temporarily = 302,
        not_modified = 304,
        bad_request = 400,
        unauthorized = 401,
        forbidden = 403,
        not_found = 404,
        not_supported = 405,
        not_acceptable = 406,
        internal_server_error = 500,
        not_implemented = 501,
        bad_gateway = 502,
        service_unavailable = 503
    };

The response object also has the following publicly accessible member values
which can be directly manipulated by the handler.

+------------------+----------------------+------------------------------------+
| Member Name      | Type                 | Description                        |
+==================+======================+====================================+
| status           | ``status_type``      | The HTTP status of the response.   |
+------------------+----------------------+------------------------------------+
| headers          | ``vector<header>``   | Vector of headers. [#]_            |
+------------------+----------------------+------------------------------------+
| content          | ``string_type`` [#]_ | The contents of the response.      |
+------------------+----------------------+------------------------------------+

.. [#] A header is a struct of type
   ``response_header<http::tags::http_server>``. An instance always has the
   members ``name`` and ``value`` both of which are of type ``string_type``.
.. [#] ``string_type`` is
   ``boost::network::string<http::tags::http_server>::type``.

Asynchronous Servers
--------------------

The asynchronous server implementation is significantly different to the
synchronous server implementation in three ways:

  #. **The Handler instance is invoked asynchronously**. This means the I/O
     thread used to handle network-related events are free to handle only the
     I/O related events. This enables the server to scale better as to the
     number of concurrent connections it can handle.
  #. **The Handler is able to schedule asynchronous actions on the thread pool
     associated with the server.** This allows handlers to perform multiple
     asynchronous computations that later on perform writes to the connection.
  #. **The Handler is able to control the (asynchronous) writes to and reads from
     the HTTP connection.** Because the connection is available to the Handler,
     that means it can write out chunks of data at a time or stream data through
     the connection continuously.

The asynchronous server is meant to allow for better scalability in terms of the
number of concurrent connections and for performing asynchronous actions within
the handlers. If your application does not need to write out information
asynchronously or perform potentially long computations, then the synchronous
server gives a generally better performance profile than the asynchronous
server.

The asynchronous server implementation is available from a single user-facing
template named ``async_server``. This template takes in a single template
parameter which is the type of the Handler to be called once a request has been
parsed from a connection.

An instance of Handler is taken as a reference to the constructor similar to the
synchronous server implementation.

.. warning:: The asynchronous server implementation, like the synchronous server
   implementation, does not perform any synchronization on the calls to the
   Handler invocation. This means if your handler contains or maintains internal
   state, you are responsible for implementing your own synchronization on
   accesses to the internal state of the Handler.

The general pattern for using the ``async_server`` template is shown below:

.. code-block:: c++

    struct handler;
    typedef boost::network::http::async_server<handler> http_server;

    struct handler {
        void operator()(
            http_server::request const & req,
            http_server::connection_ptr connection
        ) {
            // handle the request here, and use the connection to
            // either read more data or write data out to the client
        }
    };

API Documentation
~~~~~~~~~~~~~~~~~

The following sections assume that the following file has been included:

.. code-block:: c++

    #include <boost/network/include/http/server.hpp>
    #include <boost/network/utils/thread_pool.hpp>

And that the following typedef's have been put in place:

.. code-block:: c++

    struct handler_type;
    typedef boost::network::http::server<handler_type> http_server;

    struct handler_type {
        void operator()(http_server::request const & request,
                        http_server::connection_ptr connection) {
            // do something here
        }
    };

Constructor
```````````

``explicit http_server(options)``
    Construct an HTTP server instance passing in a ``server_options<Tag,
    Handler>`` instance.

Public Members
``````````````

The following definitions assume that a properly constructed ``http_server``
instance has been constructed in the following manner:

.. code-block:: c++

    handler_type handler;
    http_server::options options(handler);
    options.thread_pool(boost::make_shared<boost::network::utils::thread_pool>(2));
    http_server server(options.address("127.0.0.1").port("8000"));

``server.run()``
    Run the HTTP Server event loop. This function can be run on multiple threads
    following the example:

.. code-block:: c++

    boost::thread t1(boost::bind(&http_server::run, &server));
    boost::thread t2(boost::bind(&http_server::run, &server));
    server.run();
    t1.join();
    t2.join();

``server.stop()``
    Stop the HTTP Server acceptor and wait for all pending requests to finish.

Connection Object
`````````````````

The connection object has its own public member functions which will be the
primary means for reading from and writing to the connection.

``template <class Range> write(Range range)``
    The connection object exposes a function ``write`` that can be given a
    parameter that adheres to the Boost.Range_ ``Single Pass Range`` Concept.
    The write function, although it looks synchronous, starts of a series of
    asynchronous writes to the connection as soon as the range is serialized to
    appropriately sized buffers.

    To use this in your handler, it would look something like this:

.. code-block:: c++

    connection->write("Hello, world!");
    std::string sample = "I have a string!";
    connection->write(sample);

``template <class Range, class Callback> void write(Range range, Callback callback)``
    The connection object also exposes a function ``write`` that can be given a
    parameter that adheres to the Boost.Range_ ``Single Pass Range`` Concept, as
    well as a Callback function that returns ``void`` and takes a
    ``boost::system::error_code`` as a parameter. This overload of ``write`` is
    useful for writing streaming applications that send out chunks of data at a
    time, or for writing data that may not all fit in memory right away.

``template <class ReadCallback> void read(ReadCallback callback)``
    The connection object has a function ``read`` which can be used to read more
    information from the connection. This ``read`` function takes in a callback
    that can be assigned to a Boost.Function_ with the signature
    ``void(input_range,error_code,size_t,connection_ptr)``. The following list
    shows what the types actually mean:

      * **input_range** -- ``boost::iterator_range<char const *>`` : The range
        that denotes the data read from the connection.
      * **error_code** -- ``boost::system::error_code`` : The error code if
        there were any errors encountered from the read.
      * **size_t** -- ``std::size_t`` : The number of bytes transferred.
      * **connection_ptr** -- ``http_server::connection_ptr`` : A handle to the
        current connection, so that it is kept alive at the time of the read
        callback invocation.

    This interface is useful when doing reads of uploaded data that can be
    potentially large and may not fit in memory. The read handler is then
    responsible for dealing with the chunks of data available from the
    connection.

``void set_status(status_t new_status)``
    The ``set_status`` function takes a parameter of type ``status_t`` which is
    an enum type nested in ``http_status::connection`` which is given in the
    following code listing.

.. code-block:: c++

    enum status_t {
        ok = 200
        , created = 201
        , accepted = 202
        , no_content = 204
        , multiple_choices = 300
        , moved_permanently = 301
        , moved_temporarily = 302
        , not_modified = 304
        , bad_request = 400
        , unauthorized = 401
        , forbidden = 403
        , not_found = 404
        , not_supported = 405
        , not_acceptable = 406
        , internal_server_error = 500
        , not_implemented = 501
        , bad_gateway = 502
        , service_unavailable = 503
    };

.. note:: You may set and re-set the status several times as long as you have
   not set the headers or sent data through the connection. If you do this after
   data has already been set, the function will throw an instance of
   ``std::logic_error``.

``template <class Range> void set_headers(Range range)``
    The ``set_headers`` function takes a Single Pass Range of
    ``boost::network::http::response_header<http::tags::http_async_server>``
    instances and linearizes them to a buffer with at most
    ``BOOST_NETWORK_HTTP_SERVER_CONNECTION_HEADER_BUFFER_MAX_SIZE`` and
    immediately schedules an asynchronous write once that is done.

    The function throws an instance of ``std::logic_error`` if you try to set
    the headers for a connection more than once.

Adding SSL support to Asynchronous Server
-----------------------------------------

In order to setup SSL support for an Asynchronous Server, it is best to start from
a regular Asynchronous Server (see above). Once this server is setup, SSL can be
enabled by adding a Boost.Asio.Ssl.Context_ to the options. The settings that can be
used are defined in the link.

.. code-block:: c++

    // Initialize SSL context
    boost::shared_ptr<boost::asio::ssl::context> ctx = boost::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);
    ctx->set_options(
                boost::asio::ssl::context::default_workarounds
                | boost::asio::ssl::context::no_sslv2
                | boost::asio::ssl::context::single_dh_use);
    
    // Set keys
    ctx->set_password_callback(password_callback);
    ctx->use_certificate_chain_file("server.pem");
    ctx->use_private_key_file("server.pem", boost::asio::ssl::context::pem);
    ctx->use_tmp_dh_file("dh512.pem");
    
    handler_type handler;
    http_server::options options(handler);
    options.thread_pool(boost::make_shared<boost::network::utils::thread_pool>(2));
    http_server server(options.address("127.0.0.1").port("8442").context(ctx));

    std::string password_callback(std::size_t max_length, boost::asio::ssl::context_base::password_purpose purpose) {
        return std::string("test");
    }
    
.. _Boost.Range: http://www.boost.org/libs/range
.. _Boost.Function: http://www.boost.org/libs/function
.. _Boost.Asio.SSL.Context: http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference/ssl__context.html
