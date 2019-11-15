#pragma once

#ifndef BOOST_PROCESS_SUPPORTED
#error BOOST_PROCESS_SUPPORTED must be set, check configuration
#endif

#if BOOST_PROCESS_SUPPORTED

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4191)
#pragma warning(disable : 4242)
#pragma warning(disable : 4244)
#endif

#include <boost/process.hpp>

#ifdef _WIN32
#pragma warning(pop)
#endif

#endif
