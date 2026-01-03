if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(PLATFORM_SOURCES
      plat/default/priority.cpp plat/posix/perms.cpp
      plat/darwin/thread_role.cpp plat/default/debugging.cpp
      plat/darwin/working.mm)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(PLATFORM_SOURCES
      plat/windows/priority.cpp plat/windows/perms.cpp
      plat/windows/registry.cpp plat/windows/thread_role.cpp
      plat/default/debugging.cpp plat/windows/working.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(PLATFORM_SOURCES
      plat/linux/priority.cpp plat/posix/perms.cpp plat/linux/thread_role.cpp
      plat/linux/debugging.cpp plat/posix/working.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  set(PLATFORM_SOURCES
      plat/default/priority.cpp plat/posix/perms.cpp
      plat/freebsd/thread_role.cpp plat/default/debugging.cpp
      plat/posix/working.cpp)
else()
  error("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif()

add_library(
  nano_lib
  ${PLATFORM_SOURCES}
  asio.hpp
  asio.cpp
  assert.hpp
  assert.cpp
  block_sideband.hpp
  block_sideband.cpp
  block_type.hpp
  block_type.cpp
  block_uniquer.hpp
  blockbuilders.hpp
  blockbuilders.cpp
  blocks.hpp
  blocks.cpp
  char_traits.hpp
  cli.hpp
  cli.cpp
  common.hpp
  common.cpp
  config.hpp
  config.cpp
  configbase.hpp
  version.hpp
  version.cpp
  constants.hpp
  constants.cpp
  container_info.hpp
  container_info.cpp
  env.hpp
  env.cpp
  epoch.hpp
  epoch.cpp
  epochs.cpp
  epochs.hpp
  errors.hpp
  errors.cpp
  files.hpp
  files.cpp
  formatting.hpp
  formatting.cpp
  fwd.hpp
  id_dispenser.hpp
  interval.hpp
  ipc.hpp
  ipc.cpp
  ipc_client.hpp
  ipc_client.cpp
  json_error_response.hpp
  jsonconfig.hpp
  jsonconfig.cpp
  keypair.hpp
  keypair.cpp
  lmdbconfig.hpp
  lmdbconfig.cpp
  locks.hpp
  locks.cpp
  logging.hpp
  logging.cpp
  logging_enums.hpp
  logging_enums.cpp
  memory.hpp
  memory.cpp
  network_filter.hpp
  network_filter.cpp
  networks.hpp
  networks.cpp
  numbers.hpp
  numbers.cpp
  numbers_templ.hpp
  object_stream.hpp
  object_stream.cpp
  object_stream_adapters.hpp
  observer_set.hpp
  optional_ptr.hpp
  processing_queue.hpp
  rate_limiting.hpp
  rate_limiting.cpp
  relaxed_atomic.hpp
  rocksdbconfig.hpp
  rocksdbconfig.cpp
  rpc_handler_interface.hpp
  rpcconfig.hpp
  rpcconfig.cpp
  signal_manager.hpp
  signal_manager.cpp
  stacktrace.hpp
  stacktrace.cpp
  stats.hpp
  stats.cpp
  stats_enums.hpp
  stats_enums.cpp
  stats_sinks.hpp
  stream.hpp
  thread_pool.hpp
  thread_roles.hpp
  thread_roles.cpp
  thread_runner.hpp
  thread_runner.cpp
  threading.hpp
  threading.cpp
  timer.hpp
  timer.cpp
  tomlconfig.hpp
  tomlconfig.cpp
  uniquer.hpp
  utility.hpp
  utility.cpp
  vote.hpp
  vote.cpp
  walletconfig.hpp
  walletconfig.cpp
  work.hpp
  work.cpp
  work_version.hpp)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
include_directories(
  ${CMAKE_SOURCE_DIR}/submodules/nano-pow-server/deps/cpptoml/include)

target_link_libraries(
  nano_lib
  ipc_flatbuffers_lib
  crypto_lib
  blake2
  ${CRYPTOPP_LIBRARY}
  ${CMAKE_DL_LIBS}
  fmt::fmt
  spdlog::spdlog
  Boost::iostreams
  Boost::asio
  Boost::circular_buffer
  Boost::dll
  Boost::multiprecision
  Boost::program_options
  Boost::property_tree
  Boost::stacktrace)

target_compile_definitions(
  nano_lib
  PRIVATE -DMAJOR_VERSION_STRING=${CPACK_PACKAGE_VERSION_MAJOR}
          -DMINOR_VERSION_STRING=${CPACK_PACKAGE_VERSION_MINOR}
          -DPATCH_VERSION_STRING=${CPACK_PACKAGE_VERSION_PATCH}
          -DPRE_RELEASE_VERSION_STRING=${CPACK_PACKAGE_VERSION_PRE_RELEASE}
  PUBLIC -DACTIVE_NETWORK=${ACTIVE_NETWORK})
