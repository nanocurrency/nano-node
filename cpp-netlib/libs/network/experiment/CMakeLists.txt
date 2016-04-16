# Copyright 2013 Google, Inc.
# Copyright 2010 Dean Michael Berris <dberris@google.com>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)

# Commenting this out until it's ready for prime-time later on.
# include_directories(${CPP-NETLIB_SOURCE_DIR})
# set(CMAKE_BUILD_TYPE Release)
# if (Boost_FOUND)
#   add_executable(cpp-netlib-utils_base64_experiment utils_base64_experiment.cpp)
#   target_link_libraries(cpp-netlib-utils_base64_experiment ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
#   if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
#     target_link_libraries(cpp-netlib-utils_base64_experiment rt)
#   endif()
#   set_target_properties(cpp-netlib-utils_base64_experiment
#                         PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CPP-NETLIB_BINARY_DIR}/tests)
# endif()
