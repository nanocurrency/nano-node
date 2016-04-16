
HTTP Request
============

This part of the documentation talks about the publicly accessible API of the
HTTP Request objects. This section details the `Request Concepts`_ requirements,
the implemented and required Directives_, Modifiers_, and Wrappers_ that work
with the HTTP Request objects.

Request Concepts
----------------

There are two generally supported Request Concepts implemented in the library.
The first of two is the `Normal Client Request Concept`_ and the second is the
`Pod Server Request Concept`_.

The `Normal Client Request Concept`_ is what the HTTP Client interface requires.
All operations performed internally by the HTTP Client abide by the interface
required by this concept definition.

The `Pod Server Request Concept`_ is as the name suggests what the HTTP Server
implementation requires from Request Objects.

Switching on whether the `Request` concept chooses either of the `Normal Client
Request Concept`_ or the `Pod Server Request Concept`_ is done through the
nested ``tag`` type and whether that tag derives from the root tag ``pod``.
Simply, if the Request type's nested ``tag`` type derives from
``boost::network::tags::pod`` then it chooses to enforce the `Pod Server Request
Concept`_, otherwise it chooses the `Normal Client Request Concept`_.

Normal Client Request Concept
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A type models the Normal Client Request Concept if it models the `Message
Concept`_ and also supports the following constructs.

**Legend**

:R: The request type.
:r: An instance of R.
:S: The string type.
:s: An instance of S.
:P: The port type.
:p: An instance of P.

+-----------------------+-------------+----------------------------------------+
| Construct             | Result      | Description                            |
+=======================+=============+========================================+
| ``R::string_type``    | ``S``       | The nested ``string_type`` type.       |
+-----------------------+-------------+----------------------------------------+
| ``R::port_type``      | ``P``       | The nested ``port_type`` type.         |
+-----------------------+-------------+----------------------------------------+
| ``R r(s)``            | **NA**      | Construct a Request with an ``s``      |
|                       |             | provided. This treats ``s`` as the URI |
|                       |             | to where the request is destined for.  |
+-----------------------+-------------+----------------------------------------+
| ``host(request)``     | Convertible | Return the host to where the request   |
|                       | to ``S``    | is destined for.                       |
+-----------------------+-------------+----------------------------------------+
| ``port(request)``     | Convertible | Return the port to where the request   |
|                       | to ``P``    | is destined for.                       |
+-----------------------+-------------+----------------------------------------+
| ``path(request)``     | Convertible | Return the path included in the URI.   |
|                       | to ``S``    |                                        |
+-----------------------+-------------+----------------------------------------+
| ``query(request)``    | Convertible | Return the query part of the URI.      |
|                       | to ``S``    |                                        |
+-----------------------+-------------+----------------------------------------+
| ``anchor(request)``   | Convertible | Return the anchor part of the URI.     |
|                       | to ``S``    |                                        |
+-----------------------+-------------+----------------------------------------+
| ``protocol(request)`` | Convertible | Return the protocol/scheme part of the |
|                       | to ``S``    | URI.                                   |
+-----------------------+-------------+----------------------------------------+
| ``r << uri(s)``       | ``R&``      | Set the URI of the request.            |
+-----------------------+-------------+----------------------------------------+
| ``uri(r, s)``         | ``void``    | Set the URI of the request.            |
+-----------------------+-------------+----------------------------------------+

Pod Server Request Concept
~~~~~~~~~~~~~~~~~~~~~~~~~~

A type models the Pod Server Request Concept if it models the `Message Concept`_
and also supports the following constructs.

**Legend**

:R: The request type.
:r: An instance of R.
:S: The string type.
:I: An unsigned 8 bit integer.
:V: The vector type for headers.

+-------------------------------+--------+-------------------------------------+
| Construct                     | Result | Description                         |
+===============================+========+=====================================+
| ``R::string_type``            | ``S``  | The nested ``string_type`` type.    |
+-------------------------------+--------+-------------------------------------+
| ``R::headers_container_type`` | ``V``  | The nested                          |
|                               |        | ``headers_container_type`` type.    |
+-------------------------------+--------+-------------------------------------+
| ``r.source``                  | ``S``  | The nested source of the request.   |
+-------------------------------+--------+-------------------------------------+
| ``r.method``                  | ``S``  | The method of the request.          |
+-------------------------------+--------+-------------------------------------+
| ``r.destination``             | ``S``  | The destination of the request.     |
|                               |        | This is normally the URI of the     |
|                               |        | request.                            |
+-------------------------------+--------+-------------------------------------+
| ``r.version_major``           | ``I``  | The major version number part of    |
|                               |        | the request.                        |
+-------------------------------+--------+-------------------------------------+
| ``r.version_minor``           | ``I``  | The minor version number part of    |
|                               |        | the request.                        |
+-------------------------------+--------+-------------------------------------+
| ``r.headers``                 | ``V``  | The vector of headers.              |
+-------------------------------+--------+-------------------------------------+
| ``r.body``                    | ``S``  | The body of the request.            |
+-------------------------------+--------+-------------------------------------+

.. _Message Concept: ../in_depth/message.html#message-concept

Directives
----------

This section details the provided directives that are provided by
:mod:`cpp-netlib`. The section was written to assume that an appropriately
constructed request instance is either of the following:

.. code-block:: c++

    boost::network::http::basic_request<
      boost::network::http::tags::http_default_8bit_udp_resolve
    > request;

    // or

    boost::network::http::basic_request<
      boost::network::http::tags::http_server
    > request;

The section also assumes that there following using namespace declaration is in
effect:

.. code-block:: c++

    using namespace boost::network;

Directives are meant to be used in the following manner:

.. code-block:: c++

    request << directive(...);

.. warning::

    There are two versions of directives, those that are applicable to
    messages that support narrow strings (``std::string``) and those that are
    applicable to messages that support wide strings (``std::wstring``). The
    :mod:`cpp-netlib` implementation still does not convert wide strings into
    UTF-8 encoded narrow strings. This will be implemented in subsequent
    library releases.

    For now all the implemented directives are listed, even if some of them still
    do not implement things correctly.

*unspecified* ``source(std::string const & source_)``
    Create a source directive with a ``std::string`` as a parameter, to be set
    as the source of the request.
*unspecified* ``source(std::wstring const & source_)``
    Create a source directive with a ``std::wstring`` as a parameter, to be set
    as the source of the request.
*unspecified* ``destination(std::string const & source_)``
    Create a destination directive with a ``std::string`` as a parameter, to be
    set as the destination of the request.
*unspecified* ``destination(std::wstring const & source_)``
    Create a destination directive with a ``std::wstring`` as a parameter, to be
    set as the destination of the request.
*unspecified* ``header(std::string const & name, std::string const & value)``
    Create a header directive that will add the given name and value pair to the
    headers already associated with the request. In this case the name and
    values are both ``std::string``.
*unspecified* ``header(std::wstring const & name, std::wstring const & value)``
    Create a header directive that will add the given name and value pair to the
    headers already associated with the request. In this case the name and
    values are both ``std::wstring``.
*unspecified* ``remove_header(std::string const & name)``
    Create a remove_header directive that will remove all the occurences of the
    given name from the headers already associated with the request. In this
    case the name of the header is of type ``std::string``.
*unspecified* ``remove_header(std::wstring const & name)``
    Create a remove_header directive that will remove all the occurences of the
    given name from the headers already associated with the request. In this
    case the name of the header is of type ``std::wstring``.
*unspecified* ``body(std::string const & body_)``
    Create a body directive that will set the request's body to the given
    parameter. In this case the type of the body is an ``std::string``.
*unspecified* ``body(std::wstring const & body_)``
    Create a body directive that will set the request's body to the given
    parameter. In this case the type of the body is an ``std::wstring``.

Modifiers
---------

This section details the provided modifiers that are provided by
:mod:`cpp-netlib`.

``template <class Tag> inline void source(basic_request<Tag> & request, typename string<Tag>::type const & source_)``
    Modifies the source of the given ``request``. The type of ``source_`` is
    dependent on the ``Tag`` specialization of ``basic_request``.
``template <class Tag> inline void destination(basic_request<Tag> & request, typename string<Tag>::type const & destination_)``
    Modifies the destination of the given ``request``. The type of ``destination_`` is
    dependent on the ``Tag`` specialization of ``basic_request``.
``template <class Tag> inline void add_header(basic_request<Tag> & request, typename string<Tag>::type const & name, typename string<Tag>::type const & value)``
    Adds a header to the given ``request``. The type of the ``name`` and
    ``value`` parameters are dependent on the ``Tag`` specialization of
    ``basic_request``.
``template <class Tag> inline void remove_header(basic_request<Tag> & request, typename string<Tag>::type const & name)``
    Removes a header from the given ``request``. The type of the ``name``
    parameter is dependent on the ``Tag`` specialization of ``basic_request``.
``template <class Tag> inline void clear_headers(basic_request<Tag> & request)``
    Removes all headers from the given ``request``.
``template <class Tag> inline void body(basic_request<Tag> & request, typename string<Tag>::type const & body_)``
    Modifies the body of the given ``request``. The type of ``body_`` is
    dependent on the ``Tag`` specialization of ``basic_request``.

Wrappers
--------

This section details the provided request wrappers that come with
:mod:`cpp-netlib`. Wrappers are used to convert a message into a different type,
usually providing accessor operations to retrieve just part of the message. This
section assumes that the following using namespace directives are in
effect:

.. code-block:: c++

    using namespace boost::network;
    using namespace boost::network::http;

``template <class Tag>`` *unspecified* ``source(basic_request<Tag> const & request)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the source of a given request.
``template <class Tag>`` *unspecified* ``destination(basic_request<Tag> const & request)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the destination of a given request.
``template <class Tag>`` *unspecified* ``headers(basic_request<Tag> const & request)``
    Returns a wrapper convertible to ``typename headers_range<basic_request<Tag>
    >::type`` or ``typename basic_request<Tag>::headers_container_type`` that
    provides the headers of a given request.
``template <class Tag>`` *unspecified* ``body(basic_request<Tag> const & request)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the body of a given request.

