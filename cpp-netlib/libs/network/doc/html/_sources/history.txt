Project history
===============

The :mod:`cpp-netlib` was founded by Dean Michael Berris in 2007.
Initially it consisted of a message template and an HTTP client.  It
found a home on Sourceforge_ but was migrated at the end of 2009 to
Github_ where development is actively continued by a committed
community.

Motivation
~~~~~~~~~~

We're a group of C++ developers and we kept becoming annoyed that we
had to repeatedly write the same code when building applications that
needed to be network-aware.

We found that there was a lack of accessible networking libraries,
either standard or open source, that fulfilled our needs.  Such
libraries exist for every other major language.  So, building on top
of `Boost.Asio`_, we decided to get together and build our own.

Objectives
~~~~~~~~~~

The objectives of the :mod:`cpp-netlib` are to:

* develop a high quality, portable, easy to use C++ networking library
* enable developers to easily extend the library
* lower the barrier to entry for cross-platform network-aware C++
  applications

The goal the of :mod:`cpp-netlib` has never been to build a
fully-featured web server - there are plenty of excellent options
already available.  The niche that this library targets is for
light-weight networking functionality for C++ applications that have
demanding performance requirements or memory constraints, but that
also need to be portable.  This type of application is becoming
increasingly common as software becomes more distributed, and
applications need to communicate with services.

While many languages provide direct library support for high level
network programming, this feature is missing in C++.  Therefore, this
library has been developed with the intention of eventually being
submitted to Boost_, a collection of general, high quality
libraries for C++ developers.

Eventually, the :mod:`cpp-netlib` will be extended to support many of
the application layer protocols such as SMTP, FTP, SOAP, XMPP etc.


.. _Sourceforge: http://sourceforge.net/projects/cpp-netlib/
.. _Github: http://github.com/cpp-netlib/cpp-netlib
.. _Boost: http://www.boost.org/
.. _`Boost.Asio`: http://www.boost.org/libs/asio/
