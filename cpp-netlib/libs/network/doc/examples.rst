.. _examples:

Examples
========

The :mod:`cpp-netlib` is a practical library that is designed to aid
the development of applications for that need to communicate using
common networking protocols.  The following set of examples describe a
series of realistic examples that use the :mod:`cpp-netlib` for these
kinds of application.  All examples are built using CMake.

HTTP examples
`````````````

The HTTP component of the :mod:`cpp-netlib` contains a client and server.
The examples that follow show how to use both for programs that can be
embedded into larger applications.

.. toctree::
   :maxdepth: 1

   examples/http/http_client
   examples/http/simple_wget
   examples/http/hello_world_server
   examples/http/hello_world_client
   examples/http/atom_reader
   examples/http/twitter_search
