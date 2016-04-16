
HTTP Response
=============

This part of the documentation talks about the publicly accessible API of the
HTTP Response objects. This section details the `Response Concept`_ requirements,
the implemented and required Directives_, Modifiers_, and Wrappers_ that work
with the HTTP Response objects.

.. note:: The HTTP server response object is a POD type, which doesn't support
   any of the following details. There are only a few fields available in the
   HTTP server response type, which can be seen in
   ``boost/network/protocol/http/impl/response.ipp``.

Response Concept
----------------

A type models the Response Concept if it models the `Message Concept`_ and also
supports the following constructs.

**Legend**

:R: The response type.
:r: An instance of R.
:S: The string type.
:s,e,g: Instances of S.
:P: The port type.
:p: An instance of P.
:V: The version type.
:v: An instance of v.
:T: The status type.
:t: An instance of T.
:M: The status message type.
:m: An instance of M.
:U: An unsigned 16-bit int.
:u: An instance of U.

.. note:: In the table below, the namespace ``traits`` is an alias for
   ``boost::network::http::traits``.

+-------------------------------------+----------+-----------------------------+
| Construct                           | Result   | Description                 |
+=====================================+==========+=============================+
| ``R::string_type``                  | ``S``    | The nested ``string_type``  |
|                                     |          | type.                       |
+-------------------------------------+----------+-----------------------------+
| ``traits::version<R>::type``        | ``V``    | The version type associated |
|                                     |          | with R.                     |
+-------------------------------------+----------+-----------------------------+
| ``traits::status<R>::type``         | ``T``    | The status type associated  |
|                                     |          | with R.                     |
+-------------------------------------+----------+-----------------------------+
| ``traits::status_message<R>::type`` | ``M``    | The status message type     |
|                                     |          | associated with R.          |
+-------------------------------------+----------+-----------------------------+
| ``r << version(v)``                 | ``R&``   | Sets the version of ``r``.  |
+-------------------------------------+----------+-----------------------------+
| ``r << status(t)``                  | ``R&``   | Sets the status of ``r``.   |
+-------------------------------------+----------+-----------------------------+
| ``r << status_message(m)``          | ``R&``   | Sets the status message of  |
|                                     |          | ``r``.                      |
+-------------------------------------+----------+-----------------------------+
| ``version(r, v)``                   | ``void`` | Sets the version of ``r``.  |
+-------------------------------------+----------+-----------------------------+
| ``status(r, t)``                    | ``void`` | Sets the status of ``r``.   |
+-------------------------------------+----------+-----------------------------+
| ``status_message(r, m)``            | ``void`` | Sets the status message of  |
|                                     |          | ``r``.                      |
+-------------------------------------+----------+-----------------------------+
| ``S e = version(r)``                | **NA**   | Get the version of ``r``.   |
+-------------------------------------+----------+-----------------------------+
| ``U u = status(r)``                 | **NA**   | Get the status of ``r``.    |
+-------------------------------------+----------+-----------------------------+
| ``S g = status_message(r)``         | **NA**   | Get the status message of   |
|                                     |          | ``r``.                      |
+-------------------------------------+----------+-----------------------------+

.. _Message Concept: ../in_depth/message.html#message-concept

Directives
----------

This section details the provided directives that are provided by
:mod:`cpp-netlib`. The section was written to assume that an appropriately
constructed response instance is either of the following:

.. code-block:: c++

    boost::network::http::basic_response<
      boost::network::http::tags::http_default_8bit_udp_resolve
    > response;

    // or

    boost::network::http::basic_response<
      boost::network::http::tags::http_server
    > response;

The section also assumes that there following using namespace declaration is in
effect:

.. code-block:: c++

    using namespace boost::network;

Directives are meant to be used in the following manner:

.. code-block:: c++

    response << directive(...);

.. warning:: There are four versions of directives, those that are applicable
   to messages that support narrow strings (``std::string``), those that are
   applicable to messages that support wide strings (``std::wstring``), those
   that are applicable to messages that support future-wrapped narrow and wide
   strings (``boost::shared_future<std::string>`` and
   ``boost::shared_future<std::wstring>``).

   The :mod:`cpp-netlib` implementation still does not convert wide strings into
   UTF-8 encoded narrow strings. This will be implemented in subsequent
   library releases.

   For now all the implemented directives are listed, even if some of them still
   do not implement things correctly.

*unspecified* ``source(std::string const & source_)``
    Create a source directive with a ``std::string`` as a parameter, to be set
    as the source of the response.
*unspecified* ``source(std::wstring const & source_)``
    Create a source directive with a ``std::wstring`` as a parameter, to be set
    as the source of the response.
*unspecified* ``source(boost::shared_future<std::string> const & source_)``
    Create a source directive with a ``boost::shared_future<std::string>`` as a parameter, to be set
    as the source of the response.
*unspecified* ``source(boost::shared_future<std::wstring> const & source_)``
    Create a source directive with a ``boost::shared_future<std::wstring>`` as a parameter, to be set
    as the source of the response.
*unspecified* ``destination(std::string const & source_)``
    Create a destination directive with a ``std::string`` as a parameter, to be
    set as the destination of the response.
*unspecified* ``destination(std::wstring const & source_)``
    Create a destination directive with a ``std::wstring`` as a parameter, to be
    set as the destination of the response.
*unspecified* ``destination(boost::shared_future<std::string> const & destination_)``
    Create a destination directive with a ``boost::shared_future<std::string>`` as a parameter, to be set
    as the destination of the response.
*unspecified* ``destination(boost::shared_future<std::wstring> const & destination_)``
    Create a destination directive with a ``boost::shared_future<std::wstring>`` as a parameter, to be set
    as the destination of the response.
*unspecified* ``header(std::string const & name, std::string const & value)``
    Create a header directive that will add the given name and value pair to the
    headers already associated with the response. In this case the name and
    values are both ``std::string``.
*unspecified* ``header(std::wstring const & name, std::wstring const & value)``
    Create a header directive that will add the given name and value pair to the
    headers already associated with the response. In this case the name and
    values are both ``std::wstring``.
*unspecified* ``remove_header(std::string const & name)``
    Create a remove_header directive that will remove all the occurences of the
    given name from the headers already associated with the response. In this
    case the name of the header is of type ``std::string``.
*unspecified* ``remove_header(std::wstring const & name)``
    Create a remove_header directive that will remove all the occurences of the
    given name from the headers already associated with the response. In this
    case the name of the header is of type ``std::wstring``.
*unspecified* ``body(std::string const & body_)``
    Create a body directive that will set the response's body to the given
    parameter. In this case the type of the body is an ``std::string``.
*unspecified* ``body(std::wstring const & body_)``
    Create a body directive that will set the response's body to the given
    parameter. In this case the type of the body is an ``std::wstring``.
*unspecified* ``body(boost::shared_future<std::string> const & body_)``
    Create a body directive that will set the response's body to the given
    parameter. In this case the type of the body is an ``boost::shared_future<std::string>``.
*unspecified* ``body(boost::shared_future<std::wstring> const & body_)``
    Create a body directive that will set the response's body to the given
    parameter. In this case the type of the body is an ``boost::shared_future<std::wstring>``.
*unspecified* ``version(std::string const & version_)``
    Create a version directive that will set the response's version to the given
    parameter. In this case the type of the version is an ``std::string``.

    Note that this version includes the full ``"HTTP/"`` string.
*unspecified* ``version(std::wstring const & version_)``
    Create a version directive that will set the response's version to the given
    parameter. In this case the type of the version is an ``std::wstring``.

    Note that this version includes the full ``"HTTP/"`` string.
*unspecified* ``version(boost::shared_future<std::string> const & version_)``
    Create a version directive that will set the response's version to the given
    parameter. In this case the type of the version is an ``boost::shared_future<std::string>``.

    Note that this version includes the full ``"HTTP/"`` string.
*unspecified* ``version(boost::shared_future<std::wstring> const & version_)``
    Create a version directive that will set the response's version to the given
    parameter. In this case the type of the version is an ``boost::shared_future<std::wstring>``.

    Note that this version includes the full ``"HTTP/"`` string.
*unspecified* ``status_message(std::string const & status_message_)``
    Create a status_message directive that will set the response's status_message to the given
    parameter. In this case the type of the status_message is an ``std::string``.

    Note that this status_message includes the full ``"HTTP/"`` string.
*unspecified* ``status_message(std::wstring const & status_message_)``
    Create a status_message directive that will set the response's status_message to the given
    parameter. In this case the type of the status_message is an ``std::wstring``.

    Note that this status_message includes the full ``"HTTP/"`` string.
*unspecified* ``status_message(boost::shared_future<std::string> const & status_message_)``
    Create a status_message directive that will set the response's status_message to the given
    parameter. In this case the type of the status_message is an ``boost::shared_future<std::string>``.

    Note that this status_message includes the full ``"HTTP/"`` string.
*unspecified* ``status_message(boost::shared_future<std::wstring> const & status_message_)``
    Create a status_message directive that will set the response's status_message to the given
    parameter. In this case the type of the status_message is an ``boost::shared_future<std::wstring>``.

    Note that this status_message includes the full ``"HTTP/"`` string.
*unspecified* ``status(boost::uint16_t status_)``
    Create a status directive that will set the response's status to the given
    parameter. In this case the type of ``status_`` is ``boost::uint16_t``.
*unspecified* ``status(boost::shared_future<boost::uint16_t> const & status_)``
    Create a status directive that will set the response's status to the given
    parameter. In this case the type of ``status_`` is ``boost::shared_future<boost::uint16_t>``.

Modifiers
---------

This section details the provided modifiers that are provided by
:mod:`cpp-netlib`.

``template <class Tag> inline void source(basic_response<Tag> & response, typename string<Tag>::type const & source_)``
    Modifies the source of the given ``response``. The type of ``source_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void source(basic_response<Tag> & response, boost::shared_future<typename string<Tag>::type> const & source_)``
    Modifies the source of the given ``response``. The type of ``source_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void destination(basic_response<Tag> & response, typename string<Tag>::type const & destination_)``
    Modifies the destination of the given ``response``. The type of ``destination_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void destination(basic_response<Tag> & response, boost::shared_future<typename string<Tag>::type> const & destination_)``
    Modifies the destination of the given ``response``. The type of ``destination_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void add_header(basic_response<Tag> & response, typename string<Tag>::type const & name, typename string<Tag>::type const & value)``
    Adds a header to the given ``response``. The type of the ``name`` and
    ``value`` parameters are dependent on the ``Tag`` specialization of
    ``basic_response``.
``template <class Tag> inline void remove_header(basic_response<Tag> & response, typename string<Tag>::type const & name)``
    Removes a header from the given ``response``. The type of the ``name``
    parameter is dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void headers(basic_response<Tag> & response, typename headers_container<basic_response<Tag> >::type const & headers_)``
    Sets the whole headers contained in ``response`` as the given parameter
    ``headers_``.
``template <class Tag> inline void headers(basic_response<Tag> & response, boost::shared_future<typename headers_container<basic_response<Tag> >::type> const & headers_)``
    Sets the whole headers contained in ``response`` as the given parameter
    ``headers_``.
``template <class Tag> inline void clear_headers(basic_response<Tag> & response)``
    Removes all headers from the given ``response``.
``template <class Tag> inline void body(basic_response<Tag> & response, typename string<Tag>::type const & body_)``
    Modifies the body of the given ``response``. The type of ``body_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void body(basic_response<Tag> & response, boost::shared_future<typename string<Tag>::type> const & body_)``
    Modifies the body of the given ``response``. The type of ``body_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void version(basic_response<Tag> & response, typename traits::version<basic_response<Tag> >::type const & version_)``
    Modifies the version of the given ``response``. The type of ``version_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void status(basic_response<Tag> & response, typename traits::status<basic_response<Tag> >::type const & status_)``
    Modifies the status of the given ``response``. The type of ``status_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.
``template <class Tag> inline void status_message(basic_response<Tag> & response, typename traits::status_message<basic_response<Tag> >::type const & status_message_)``
    Modifies the status message of the given ``response``. The type of ``status_message_`` is
    dependent on the ``Tag`` specialization of ``basic_response``.

Wrappers
--------

This section details the provided response wrappers that come with
:mod:`cpp-netlib`. Wrappers are used to convert a message into a different type,
usually providing accessor operations to retrieve just part of the message. This
section assumes that the following using namespace directives are in
effect:

.. code-block:: c++

    using namespace boost::network;
    using namespace boost::network::http;

``template <class Tag>`` *unspecified* ``source(basic_response<Tag> const & response)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the source of a given response.
``template <class Tag>`` *unspecified* ``destination(basic_response<Tag> const & response)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the destination of a given response.
``template <class Tag>`` *unspecified* ``headers(basic_response<Tag> const & response)``
    Returns a wrapper convertible to ``typename headers_range<basic_response<Tag>
    >::type`` or ``typename basic_response<Tag>::headers_container_type`` that
    provides the headers of a given response.
``template <class Tag>`` *unspecified* ``body(basic_response<Tag> const & response)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the body of a given response.
``template <class Tag>`` *unspecified* ``version(basic_response<Tag> const & response)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the version of the given response.
``template <class Tag>`` *unspecified* ``status(basic_response<Tag> const & response)``
    Returns a wrapper convertible to ``typename boost::uint16_t`` that
    provides the status of the given response.
``template <class Tag>`` *unspecified* ``status_message(basic_response<Tag> const & response)``
    Returns a wrapper convertible to ``typename string<Tag>::type`` that
    provides the status message of the given response.
