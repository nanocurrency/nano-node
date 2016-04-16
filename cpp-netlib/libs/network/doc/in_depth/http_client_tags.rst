+---------------------------------+---------------------------------------------+
| Tag                             | Description                                 |
+=================================+=============================================+
| http_default_8bit_tcp_resolve   | This is the default HTTP implementation tag |
|                                 | that resolves addresses with a TCP resolver |
|                                 | and provides a synchronous/blocking HTTP    |
|                                 | client interface.                           |
+---------------------------------+---------------------------------------------+
| http_default_8bit_udp_resolve   | This is similar to the above tag except that|
|                                 | it specifies the HTTP client to use a UDP   |
|                                 | resolver. It also provides a synchronous/   |
|                                 | blocking HTTP client interface.             |
+---------------------------------+---------------------------------------------+
| http_keepalive_8bit_tcp_resolve | This tag specifies that the HTTP client by  |
|                                 | default will keep connections to the server |
|                                 | alive. It only makes sense if the           |
|                                 | ``version_major`` and ``version_minor`` are |
|                                 | both ``1``, to indicate HTTP 1.1. This tag  |
|                                 | causes the HTTP client to resolve using a   |
|                                 | TCP resolver and provides a synchronous/    |
|                                 | blocking HTTP client interface.             |
+---------------------------------+---------------------------------------------+
| http_keepalive_8bit_udp_resolve | This is similar to the above tag except that|
|                                 | it specifies the HTTP client to use a UDP   |
|                                 | resolver. It also provides a synchronous/   |
|                                 | blocking HTTP client interface.             |
+---------------------------------+---------------------------------------------+
| http_async_8bit_tcp_resolve     | This tag provides an active HTTP client     |
|                                 | object implementation that uses a TCP       |
|                                 | resolver. Response objects returned will    |
|                                 | encapsulate a number of Boost.Thread_       |
|                                 | shared futures to hold values. Users don't  |
|                                 | have to see this as they are implementation |
|                                 | details.                                    |
+---------------------------------+---------------------------------------------+
| http_async_8bit_udp_resolve     | This is similar to the above tag except that|
|                                 | specifies the HTTP client to use a UDP      |
|                                 | resolver.                                   |
+---------------------------------+---------------------------------------------+

.. _Boost.Thread: http://www.boost.org/libs/thread

