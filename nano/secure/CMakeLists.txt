if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(PLATFORM_SECURE_SOURCE plat/osx/working.mm)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(PLATFORM_SECURE_SOURCE plat/windows/working.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(PLATFORM_SECURE_SOURCE plat/posix/working.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  set(PLATFORM_SECURE_SOURCE plat/posix/working.cpp)
else()
  error("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif()

# Embed bootstrap representative weights in executable. Both live and beta
# weights are added to accommodate switching networks from the command line.
file(READ ${CMAKE_SOURCE_DIR}/rep_weights_live.bin liveweights HEX)
string(REGEX REPLACE "(..)" "0x\\1," liveweights ${liveweights})
file(
  WRITE ${CMAKE_BINARY_DIR}/bootstrap_weights_live.cpp
  "#include <cstddef>\n"
  "namespace nano {\n"
  "	unsigned char nano_bootstrap_weights_live[] = {${liveweights} 0x00};\n"
  "	size_t nano_bootstrap_weights_live_size = sizeof(nano_bootstrap_weights_live) - 1;\n"
  "}\n")

if(EXISTS ${CMAKE_SOURCE_DIR}/rep_weights_beta.bin)
  file(READ ${CMAKE_SOURCE_DIR}/rep_weights_beta.bin betaweights HEX)
  string(REGEX REPLACE "(..)" "0x\\1," betaweights ${betaweights})
endif()
file(
  WRITE ${CMAKE_BINARY_DIR}/bootstrap_weights_beta.cpp
  "#include <cstddef>\n"
  "namespace nano {\n"
  "	unsigned char nano_bootstrap_weights_beta[] = {${betaweights} 0x00};\n"
  "	size_t nano_bootstrap_weights_beta_size = sizeof(nano_bootstrap_weights_beta) - 1;\n"
  "}\n")

add_library(
  secure
  ${PLATFORM_SECURE_SOURCE}
  ${CMAKE_BINARY_DIR}/bootstrap_weights_live.cpp
  ${CMAKE_BINARY_DIR}/bootstrap_weights_beta.cpp
  account_info.hpp
  account_info.cpp
  common.hpp
  common.cpp
  generate_cache_flags.hpp
  generate_cache_flags.cpp
  ledger.hpp
  ledger.cpp
  ledger_cache.hpp
  ledger_cache.cpp
  network_filter.hpp
  network_filter.cpp
  pending_info.hpp
  pending_info.cpp
  rep_weights.hpp
  rep_weights.cpp
  utility.hpp
  utility.cpp
  vote.hpp
  vote.cpp
  working.hpp)

target_link_libraries(secure nano_lib ed25519 crypto_lib Boost::system)

target_compile_definitions(secure PUBLIC -DQT_NO_KEYWORDS
                                         -DBOOST_ASIO_HAS_STD_ARRAY=1)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
