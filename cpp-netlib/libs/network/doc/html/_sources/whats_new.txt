.. _whats_new:

************
 What's New
************

:mod:`cpp-netlib` 0.11
----------------------

v0.11.2
~~~~~~~
* Support a source_port setting for connections made by the client per-request.
* Allow using cpp-netlib without OpenSSL.
* Fix build breakage for Visual Studio 2015.
* Add more options for HTTP client use of SSL/TLS options/ciphers.
* Made client_get_timeout_test less flaky.
* Fixes to URI encoding issues with multibyte strings.
* Make cpp-netlib not crash on unstable networks.
* Allow parsing empty query parameters (`#499`_).
* CMake build changes to simplify dependencies on cppnetlib-client-connections.
* Handle EOF correctly (`#496`_).
* Fix fileserver example to chunk data correctly.
* Copy hostname to avoid dangling reference to a temporary request object. (`#482`_)
* Catch exceptions in parse_headers to avoid propagating issues in parsing upwards.
* Fix some GCC warnings on signed/unsigned comparison.
* Support environment variable-based peer verification (via OpenSSL).
* Support IPv6 connections.
* Support certificate-based verification, and option to always verify hosts.

.. _`#499`: https://github.com/cpp-netlib/cpp-netlib/issues/499
.. _`#496`: https://github.com/cpp-netlib/cpp-netlib/issues/496
.. _`#482`: https://github.com/cpp-netlib/cpp-netlib/issues/482


v0.11.1
~~~~~~~
* Add support for request timeouts.
* Build configuration fixes.
* Support for Travis CI in-project config.
* Make the response parser more flexible to support older/ad-hoc servers that don't have standard format responses.
* Fix some instability in the client destructor.
* MSVC 2010 specific fixes.

v0.11.0
~~~~~~~
* Fix thread leak in DNS resolution failure (`#245`_)
* Remove unsupported `client_fwd.hpp` header (`#277`_)
* Remove support for header-only usage (`#129`_) -- this means that the BOOST_NETWORK_NO_LIB option is no longer actually supported.
* Deprecate Synchronous Client implementations (`#279`_)
* Support streaming body chunks for PUT/POST client requests (`#27`_)
* Fix non-case-sensitive header parsing for some client tags (`#313`_)
* Remove unsupported Jamfiles from the whole project (`#316`_)
* Add ``make install`` for Linux and OS X (`#285`_) 
* Fix incorrect Body processing (`#69`_)
* Support chunked transfer encoding from HTTP responses (`#86`_)
* Make OS X Clang builds use C++11 and libc++. 
* Update Boost requirement to 1.54.0.
* Experimental Base64 encoding/decoding library (`#287`_)
* *Known test failure:* OS X Xcode Clang 5.0 + Boost 1.54.0 + libc++ don't play
  well with Boost.Serialization issues, mitigate test breakage but
  ``cpp-netlib-utils_base64_test`` still fails in this platform. (`#287`_) 
* Provide a client option to always validate peers for HTTPS requests made by
  the client. (`#349`_)
* Back-port fix for `#163`_ for improved URI parsing.
* Added support for client-side certificates and private keys (`#361`_).

.. _`#129`: https://github.com/cpp-netlib/cpp-netlib/issues/129
.. _`#163`: https://github.com/cpp-netlib/cpp-netlib/issues/163
.. _`#245`: https://github.com/cpp-netlib/cpp-netlib/issues/245
.. _`#277`: https://github.com/cpp-netlib/cpp-netlib/issues/277
.. _`#279`: https://github.com/cpp-netlib/cpp-netlib/issues/279
.. _`#27`: https://github.com/cpp-netlib/cpp-netlib/issues/27
.. _`#285`: https://github.com/cpp-netlib/cpp-netlib/issues/285
.. _`#287`: https://github.com/cpp-netlib/cpp-netlib/issues/287
.. _`#313`: https://github.com/cpp-netlib/cpp-netlib/issues/313
.. _`#316`: https://github.com/cpp-netlib/cpp-netlib/issues/316
.. _`#349`: https://github.com/cpp-netlib/cpp-netlib/issues/349
.. _`#69`: https://github.com/cpp-netlib/cpp-netlib/issues/69
.. _`#86`: https://github.com/cpp-netlib/cpp-netlib/issues/86
.. _`#361`: https://github.com/cpp-netlib/cpp-netlib/pull/361

:mod:`cpp-netlib` 0.10
----------------------

v0.10.1
~~~~~~~
* Documentation updates (`#182`_, `#265`_, `#194`_, `#233`_, `#255`_)
* Fix issue with async server inadvertently stopping from listening when
  accepting a connection fails. (`#172`_)
* Allow overriding and ultimately removing defaulted headers from HTTP
  requests. (`#263`_)
* Add `-Wall` to the base rule for GCC builds. (`#264`_)
* Make the server implementation throw on startup errors. (`#166`_)

.. _`#182`: https://github.com/cpp-netlib/cpp-netlib/issues/182
.. _`#265`: https://github.com/cpp-netlib/cpp-netlib/issues/265
.. _`#194`: https://github.com/cpp-netlib/cpp-netlib/issues/194
.. _`#172`: https://github.com/cpp-netlib/cpp-netlib/issues/172
.. _`#263`: https://github.com/cpp-netlib/cpp-netlib/issues/263
.. _`#233`: https://github.com/cpp-netlib/cpp-netlib/issues/233
.. _`#264`: https://github.com/cpp-netlib/cpp-netlib/issues/264
.. _`#255`: https://github.com/cpp-netlib/cpp-netlib/issues/255
.. _`#166`: https://github.com/cpp-netlib/cpp-netlib/issues/166

v0.10.0
~~~~~~~
* Added support for more HTTP status codes (206, 408, 412, 416, 507).
* Refactored the parser for chunked encoding.
* Fixed parsing chunked encoding if the response body has ``<chunk>CLRF<hex>CLRF<data>``.
* Added librt dependency on Linux.
* Check the callback in the asynchronous client before calling it.
* Fixed issues `#110`_, `#168`_, `#213`_.

.. _`#110`: https://github.com/cpp-netlib/cpp-netlib/issues/110
.. _`#168`: https://github.com/cpp-netlib/cpp-netlib/issues/168
.. _`#213`: https://github.com/cpp-netlib/cpp-netlib/issues/213

:mod:`cpp-netlib` 0.9
---------------------

v0.9.5
~~~~~~
* Removed dependency on Boost.Parameter from HTTP client and server.
* Fixed for Clang error on Twitter example.
* Added source port to the request (HTTP server).
* Updated CMake config for MSVC 2010/2012.
* Now support chunked content encoding in client response parsing.
* Fixed bug with client not invoking callback when a request fails.

v0.9.4
~~~~~~
* Lots of URI fixes.
* Fixed async_server's request handler so it doesn't make copies of the supplied handler.
* Fix for issue `#73`_ regarding SSL connections ending in short read errors.
* Final C++03-only release.

.. _`#73`: https://github.com/cpp-netlib/cpp-netlib/issues/73

v0.9.3
~~~~~~
* URI, HTTP client and HTTP server are now built as static libraries (``libcppnetlib-uri.a``, ``libcppnetlib-client-connections.a`` and ``libcppnetlib-server-parsers.a`` on Linux and ``cppnetlib-uri.lib``, ``cppnetlib-client-connections.lib`` and ``cppnetlib-server-parsers.lib`` on Windows).
* Updated URI parser.
* A new URI builder.
* URI support for IPv6 RFC 2732.
* Fixed issues `#67`_, `#72`_, `#78`_, `#79`_, `#80`_, `#81`_, `#82`_, `#83`_.
* New examples for the HTTP client, including an Atom feed, an RSS feed and a
  very simple client that uses the Twitter Search API.

.. _`#67`: https://github.com/cpp-netlib/cpp-netlib/issues/67
.. _`#72`: https://github.com/cpp-netlib/cpp-netlib/issues/72
.. _`#78`: https://github.com/cpp-netlib/cpp-netlib/issues/78
.. _`#79`: https://github.com/cpp-netlib/cpp-netlib/issues/79
.. _`#80`: https://github.com/cpp-netlib/cpp-netlib/issues/80
.. _`#81`: https://github.com/cpp-netlib/cpp-netlib/issues/81
.. _`#82`: https://github.com/cpp-netlib/cpp-netlib/issues/82
.. _`#83`: https://github.com/cpp-netlib/cpp-netlib/issues/83

v0.9.2
~~~~~~
* Critial bug fixes to v0.9.1.

v0.9.1
~~~~~~
* Introduced macro ``BOOST_NETWORK_DEFAULT_TAG`` to allow for programmatically
  defining the default flag to use throughout the compilation unit.
* Support for streaming body handlers when performing HTTP client operations.
  See documentation for HTTP client interface for more information.
* Numerous bug fixes from v0.9.0.
* Google, Inc. contributions.

v0.9.0
~~~~~~
* **IMPORTANT BREAKING CHANGE**: By default all compile-time heavy parser
  implementations are now compiled to external static libraries. In order to use
  :mod:`cpp-netlib` in header-only mode, users must define the preprocessor
  macro ``BOOST_NETWORK_NO_LIB`` before including any :mod:`cpp-netlib` header.
  This breaks code that relied on the version 0.8.x line where the library is
  strictly header-only.
* Fix issue #41: Introduce a macro ``BOOST_NETWORK_HTTP_CLIENT_DEFAULT_TAG``
  which makes the default HTTP client use ``tags::http_async_8bit_udp_resolve``
  as the tag.
* Fix issue #40: Write the status line and headers in a single buffer write
  instead of two writes.
* More consistent message API for client and server messages (request and
  response objects).
* Refactoring of internal implementations to allow better separation of concerns
  and more manageable coding/documentation.
* Client and server constructors that support Boost.Parameter named parameters.
* Client and server constructors now take in an optional reference to a Boost.Asio
  ``io_service`` to use internally.
* Documentation updates to reflect new APIs.

:mod:`cpp-netlib` 0.8
---------------------

* Updates to URI unit tests and documentation.
* More documentation, covering the HTTP Client and HTTP Server APIs
* Asynchronous HTTP Server that now supports running request handlers on a
  different thread pool.
* An initial thread pool implementation, using Boost.Asio underneath.
* Adding a ready(...) wrapper to check whether a response object returned by
  the asynchronous client in 0.7 already has all the parts available.
* Some attempts at lowering compile time costs.

:mod:`cpp-netlib` 0.7
---------------------

* Radical documentation overhaul
* Asynchronous HTTP client
* Tag dispatch overhaul, using Boost.MPL
* HTTP Client Facade refactoring
* Bug fixes for HTTP 1.1 response parsing
* Minimized code repetition with some header macro's
* Configurable HTTPS support in the library with ``BOOST_NETWORK_ENABLE_HTTPS``


:mod:`cpp-netlib` 0.6
---------------------

* Many fixes for MSVC compiler

:mod:`cpp-netlib` 0.5
---------------------

* An embeddable HTTP 1.1 server
* An HTTP 1.1 client upgraded to support HTTPS
* An updated URI parser implementation
* An asynchronous HTTP 1.1 client
* An HTTP 1.1 client that supports streaming function handlers
