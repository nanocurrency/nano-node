.. _getting_started:

*****************
 Getting Started
*****************

Downloading an official release
===============================

You can find links to the latest official release from the project's official
website:

    http://cpp-netlib.org/

All previous stable versions of :mod:`cpp-netlib` can be downloaded from
Github_ from this url:

    http://github.com/cpp-netlib/cpp-netlib/downloads

Each release is available as gzipped (Using the command
``tar xzf cpp-netlib.tar.gz``) or bzipped (Using ``tar xjf
cpp-netlib.tar.bz2``) tarball, or as a zipfile (``unzip
cpp-netlib.zip``, or on Windows using a tool such as 7zip_).

.. _Github: http://github.com/cpp-netlib/cpp-netlib/downloads
.. _7zip: http://www.7-zip.org/

Downloading a development version
=================================

The :mod:`cpp-netlib` uses Git_ for source control, so to use any
development versions Git must be installed on your system.

Using the command line, the command to get the latest code is:

::

    shell$ git clone git://github.com/cpp-netlib/cpp-netlib.git

This should be enough information get to started.  To do more complex
things with Git, such as pulling changes or checking out a new branch,
refer to the `Git documentation`_.

.. note:: Previous versions of :mod:`cpp-netlib` referred to the
   *mikhailberis* repository as the main development repository. This
   account is still valid, but not always up-to-date. In the interest of
   consistency, the main repository has been changed to *cpp-netlib*.

Windows users need to use msysGit_, and to invoke the command above
from a shell.

For fans of Subversion_, the same code can be checked out from
http://svn.github.com/cpp-netlib/cpp-netlib.git.

.. _Git: http://git-scm.com/
.. _`Git documentation`: http://git-scm.com/documentation
.. _msysGit: http://code.google.com/p/msysgit/downloads/list
.. _Subversion: http://subversion.tigris.org/

.. note:: The :mod:`cpp-netlib` project is hosted on GitHub_ and follows the
   prescribed development model for GitHub_ based projects. This means in case
   you want to submit patches, you will have to create a fork of the project
   (read up on forking_) and then submit a pull request (read up on submitting
   `pull requests`_).

.. _forking: http://help.github.com/forking/
.. _`pull requests`: http://help.github.com/pull-requests/

Getting Boost
=============

:mod:`cpp-netlib` depends on Boost_.  It should work for any version
of Boost above 1.50.0.  If Boost is not installed on your system, the
latest package can be found on the `Boost web-site`_.  The environment
variable ``BOOST_ROOT`` must be defined, which must be the full path
name of the top directory of the Boost distribution.  Although Boost
is mostly header only, applications built using :mod:`cpp-netlib`
still requires linking with `Boost.System`_, `Boost.Date_time`_, and
`Boost.Regex`_.

.. _Boost: http://www.boost.org/doc/libs/release/more/getting_started/index.html
.. _`Boost web-site`: http://www.boost.org/users/download/
.. _`Boost.System`: http://www.boost.org/libs/system/index.html
.. _`Boost.Date_time`: http://www.boost.org/libs/date_time/index.html
.. _`Boost.Regex`: http://www.boost.org/libs/regex/index.html

.. note:: You can follow the steps in the `Boost Getting Started`_ guide to
   install Boost into your development system.

.. _`Boost Getting Started`:
   http://www.boost.org/doc/libs/release/more/getting_started/index.html

.. warning:: There is a known incompatibility between :mod:`cpp-netlib` and
   Boost 1.46.1 on some compilers. It is not recommended to use :mod:`cpp-netlib`
   with Boost 1.46.1. Some have reported though that Boost 1.47.0
   and :mod:`cpp-netlib` work together better.

Getting CMake
=============

The :mod:`cpp-netlib` uses CMake_ to generate platform-specific build files. If
you intend to run the test suite, you can follow the instructions below.
Otherwise, you don't need CMake to use :mod:`cpp-netlib` in your project. The
:mod:`cpp-netlib` requires CMake version 2.8 or higher.

.. _CMake: http://www.cmake.org/

Let's assume that you have unpacked the :mod:`cpp-netlib` at the top of your
HOME directory. On Unix-like systems you will typically be able to change into
your HOME directory using the command ``cd ~``. This sample below assumes that
the ``~/cpp-netlib`` directory exists, and is the top-level directory of the
:mod:`cpp-netlib` release.

Building with CMake
===================

To build the tests that come with :mod:`cpp-netlib`, we first need to configure the
build system to use our compiler of choice. This is done by running the
``cmake`` command at the top-level directory of :mod:`cpp-netlib` with
additional parameters::

    $ mkdir ~/cpp-netlib-build
    $ cd ~/cpp-netlib-build
    $ cmake -DCMAKE_BUILD_TYPE=Debug \
    >       -DCMAKE_C_COMPILER=gcc   \
    >       -DCMAKE_CXX_COMPILER=g++ \
    >       ../cpp-netlib

.. note::

    While it's not compulsory, it's recommended that
    :mod:`cpp-netlib` is built outside the source directory.
    For the purposes of documentation, we'll assume that all
    builds are done in ``~/cpp-netlib-build``.

If you intend to use the SSL support when using the HTTP client libraries in
:mod:`cpp-netlib`, you may need to build it with OpenSSL_ installed or at least
available to CMake. If you have the development headers for OpenSSL_ installed
on your system when you build :mod:`cpp-netlib`, CMake will be able to detect it
and set the ``BOOST_NETWORK_ENABLE_HTTPS`` macro when building the library to
support HTTPS URIs.

One example for building the library with OpenSSL_ support with a custom
(non-installed) version of OpenSSL_ is by doing the following::

    $ cmake -DCMAKE_BUILD_TYPE=Debug \
    >       -DCMAKE_C_COMPILER=clang \
    >       -DCMAKE_CXX_COMPILER=clang++ \
    >       -DOPENSSL_ROOT_DIR=/Users/dberris/homebrew/Cellar/openssl/1.0.1f
    >       ../cpp-netlib

.. _OpenSSL: http://www.openssl.org/

You can also use a different root directory for the Boost_ project by using the
``-DBOOST_ROOT`` configuration option to CMake. This is useful if you intend to
build the library with a specific version of Boost that you've built in a
separate directory::

    $ cmake -DCMAKE_BUILD_TYPE=Debug \
    >       -DCMAKE_C_COMPILER=clang \
    >       -DCMAKE_CXX_COMPILER=clang++ \
    >       -DOPENSSL_ROOT_DIR=/Users/dberris/homebrew/Cellar/openssl/1.0.1f \
    >       -DBOOST_ROOT=/Users/dberris/Source/boost_1_55_0
    >       ../cpp-netlib

Building on Linux
~~~~~~~~~~~~~~~~~

On Linux, this will generate the appropriate Makefiles that will enable you to
build and run the tests and examples that come with :mod:`cpp-netlib`. To build
the tests, you can run ``make`` in the same top-level directory of
``~/cpp-netlib-build``::

    $ make

.. note:: Just like with traditional GNU Make, you can add the ``-j`` parameter
   to specify how many parallel builds to run. In case you're in a sufficiently
   powerful system and would like to parallelize the build into 4 jobs, you can
   do this with::

       make -j4

   As a caveat, :mod:`cpp-netlib` is heavy on template metaprogramming and will
   require a lot of computing and memory resources to build the individual
   tests. Do this at the risk of thrashing_ your system.  However, this
   compile-time burden is much reduced in recent versions.

.. _thrashing: http://en.wikipedia.org/wiki/Thrashing_(computer_science)

Once the build has completed, you can now run the test suite by issuing::

    $ make test

You can install :mod:`cpp-netlib` by issuing::

    $ sudo make install

By default this installs :mod:`cpp-netlib` into ``/usr/local``.

.. note:: As of version 0.9.3, :mod:`cpp-netlib` produces three static
   libraries.  Using GCC on Linux these are::

      libcppnetlib-client-connections.a
      libcppnetlib-server-parsers.a
      libcppnetlib-uri.a

   Users can find them in ``~/cpp-netlib-build/libs/network/src``.

Building On Windows
~~~~~~~~~~~~~~~~~~~

If you're using the Microsoft Visual C++ compiler or the Microsoft Visual Studio
IDE and you would like to build :mod:`cpp-netlib` from within Visual Studio, you
can look for the solution and project files as the artifacts of the call to
``cmake`` -- the file should be named ``CPP-NETLIB.sln`` (the solution) along
with a number of project files for Visual Studio.

.. note:: As of version 0.9.3, :mod:`cpp-netlib` produces three static
   libraries.  Using Visual C++ on Windows they are::

      cppnetlib-client-connections.lib
      cppnetlib-server-parsers.lib
      cppnetlib-uri.lib

   Users can find them in ``~/cpp-netlib-build/libs/network/src``.

Using :mod:`cpp-netlib`
=======================

CMake projects
~~~~~~~~~~~~~~

Projects using CMake can add the following lines in their ``CMakeLists.txt`` to
be able to use :mod:`cpp-netlib`::

   set ( CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ~/cpp-netlib-build )
   find_package ( cppnetlib 0.11.0 REQUIRED )
   include_directories ( ${CPPNETLIB_INCLUDE_DIRS} )
   target_link_libraries ( MyApplication ${CPPNETLIB_LIBRARIES} )

.. note:: Setting ``CMAKE_PREFIX_PATH`` is only required when :mod:`cpp-netlib`
   is not installed to a location that CMake searches.  When :mod:`cpp-netlib`
   is installed to the default location (``/usr/local``), ``CMake`` can find it.

.. note:: We assume that ``MyApplication`` is the application that you are
   building and which depends on :mod:`cpp-netlib`.


Reporting Issues, Getting Support
=================================

In case you find yourself stuck or if you've found a bug (or you want to just
join the discussion) you have a few options to choose from.

For reporting bugs, feature requests, and asking questions about the
implementation and/or the documentation, you can go to the GitHub issues page
for the project at http://github.com/cpp-netlib/cpp-netlib/issues.

You can also opt to join the developers mailing list for a more personal
interaction with the developers of the project. You can join the mailing list
through http://groups.google.com/forum/#!forum/cpp-netlib.

