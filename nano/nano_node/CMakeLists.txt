add_executable(nano_node daemon.cpp daemon.hpp entry.cpp)

target_link_libraries(
  nano_node
  node
  rpc
  secure
  argon2
  Boost::boost
  ${PLATFORM_LIBS})

include_directories(${CMAKE_SOURCE_DIR}/submodules)

if(NANO_STACKTRACE_BACKTRACE)
  target_link_libraries(nano_node backtrace)
endif()

target_compile_definitions(
  nano_node PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
                    -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

set_target_properties(
  nano_node PROPERTIES COMPILE_FLAGS
                       "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

add_custom_command(
  TARGET nano_node
  POST_BUILD
  COMMAND nano_node --generate_config node >
          ${PROJECT_BINARY_DIR}/config-node.toml.sample
  COMMAND nano_node --generate_config rpc >
          ${PROJECT_BINARY_DIR}/config-rpc.toml.sample)

if((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS nano_node RUNTIME DESTINATION .)
  else()
    install(TARGETS nano_node RUNTIME DESTINATION ./bin)
  endif()
endif()
