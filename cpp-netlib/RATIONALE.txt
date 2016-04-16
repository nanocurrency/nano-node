C++ Networking Library
Goals and Scope


Objectives
----------

 o  Develop a high quality, portable, easy to use C++ networking library

 o  Enable users to easily extend the library

 o  Lower the barrier to entry for cross-platform network-aware C++ 
    applications


Goals
-----

 *  Implement a simple message implementation which can be used in
    network protocol-specific routines for inter-operability and
    to provide a generic interface to manipulating network-oriented
    messages.

 *  Implement easy to use protocol client libraries such as (but not
    limited to):
    
        -  HTTP 1.0/1.1
        -  (E)SMTP
        -  SNMP
        -  ICMP

 *  Implement an easy to embed HTTP server container type that supports
    most modern HTTP 1.1 features.
    
 *  Implement an efficient easy to use URI class/parser.

 *  Implement a fully compliant cross-platform asynchronous DNS resolver
    either as a wrapper to external (C) libraries, or as hand-rolled
    implementation.

 *  Implement a MIME handler which builds message objects from either
    data retrieved from the network or other sources and create
    text/binary representations from existing message objects intended
    for transport over the network.


Scope
-----

 *  The library will provide a generic message class which is intended 
    to be the common message type used by the protocol libraries.

 *  The library will only contain client implementations for the various 
    supported protocols.

 *  The library will use only STL and Boost C++ library components, 
    utilities, and libraries throughout the implementation.

 *  The library will strive to use C++ templates and template 
    metaprogramming techniques in order to not require the building of
    external shared/static libraries. In other words, the library will be
    header-only and compliant with the C++ standard.


