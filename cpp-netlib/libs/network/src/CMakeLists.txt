# Copyright (c) Glyn Matthews 2011.
# Copyright 2011 Dean Michael Berris (dberris@google.com)
# Copyright 2011 Google, Inc.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)


include_directories(${CPP-NETLIB_SOURCE_DIR})

file(GLOB_RECURSE CPP-NETLIB_HEADERS
    "${CPP-NETLIB_SOURCE_DIR}/boost/" "*.hpp")

set(CPP-NETLIB_URI_SRCS uri/uri.cpp uri/schemes.cpp)
add_library(cppnetlib-uri ${CPP-NETLIB_URI_SRCS})
set_target_properties(cppnetlib-uri
    PROPERTIES VERSION ${CPPNETLIB_VERSION_STRING}
    SOVERSION ${CPPNETLIB_VERSION_MAJOR}
    PUBLIC_HEADER "${CPP-NETLIB_HEADERS}")
install(TARGETS cppnetlib-uri
    EXPORT cppnetlibTargets
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

set(CPP-NETLIB_HTTP_SERVER_SRCS server_request_parsers_impl.cpp)
add_library(cppnetlib-server-parsers ${CPP-NETLIB_HTTP_SERVER_SRCS})
set_target_properties(cppnetlib-server-parsers
    PROPERTIES VERSION ${CPPNETLIB_VERSION_STRING}
    SOVERSION ${CPPNETLIB_VERSION_MAJOR}
    PUBLIC_HEADER "${CPP-NETLIB_HEADERS}")
install(TARGETS cppnetlib-server-parsers
    EXPORT cppnetlibTargets
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

set(CPP-NETLIB_HTTP_CLIENT_SRCS client.cpp)
add_library(cppnetlib-client-connections ${CPP-NETLIB_HTTP_CLIENT_SRCS})
set_target_properties(cppnetlib-client-connections
    PROPERTIES VERSION ${CPPNETLIB_VERSION_STRING}
    SOVERSION ${CPPNETLIB_VERSION_MAJOR}
    PUBLIC_HEADER "${CPP-NETLIB_HEADERS}")
if (OPENSSL_FOUND)
    target_link_libraries(cppnetlib-client-connections ${OPENSSL_LIBRARIES})
endif ()
if (Boost_FOUND)
    target_link_libraries(cppnetlib-client-connections ${Boost_LIBRARIES})
endif ()
install(TARGETS cppnetlib-client-connections
    EXPORT cppnetlibTargets
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
