add_executable(bananode daemon.cpp daemon.hpp entry.cpp)

target_link_libraries(
  bananode
  node
  rpc
  secure
  argon2
  Boost::boost
  ${PLATFORM_LIBS})

if(NANO_STACKTRACE_BACKTRACE)
  target_link_libraries(bananode backtrace)
endif()

target_compile_definitions(
  bananode PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
                   -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

set_target_properties(
  bananode PROPERTIES COMPILE_FLAGS
                      "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

add_custom_command(
  TARGET bananode
  POST_BUILD
  COMMAND bananode --generate_config node >
          ${PROJECT_BINARY_DIR}/config-node.toml.sample
  COMMAND bananode --generate_config rpc >
          ${PROJECT_BINARY_DIR}/config-rpc.toml.sample)

if((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS bananode RUNTIME DESTINATION .)
  else()
    install(TARGETS bananode RUNTIME DESTINATION ./bin)
  endif()
endif()
