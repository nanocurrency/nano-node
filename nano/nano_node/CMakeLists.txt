add_executable(celerix_node daemon.cpp daemon.hpp entry.cpp)

target_link_libraries(celerix_node node Boost::process ${PLATFORM_LIBS})

include_directories(${CMAKE_SOURCE_DIR}/submodules)

target_compile_definitions(
  celerix_node PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
                    -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

set_target_properties(
  celerix_node PROPERTIES COMPILE_FLAGS
                       "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

add_custom_command(
  TARGET celerix_node
  POST_BUILD
  COMMAND celerix_node --generate_config node >
          ${PROJECT_BINARY_DIR}/config-node.toml.sample
  COMMAND celerix_node --generate_config rpc >
          ${PROJECT_BINARY_DIR}/config-rpc.toml.sample
  COMMAND celerix_node --generate_config log >
          ${PROJECT_BINARY_DIR}/config-log.toml.sample)

if((CELERIX_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS celerix_node RUNTIME DESTINATION .)
  else()
    install(TARGETS celerix_node RUNTIME DESTINATION ./bin)
  endif()
endif()
