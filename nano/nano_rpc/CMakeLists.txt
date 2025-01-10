add_executable(celerix_rpc entry.cpp)

target_link_libraries(celerix_rpc node)

target_compile_definitions(
  celerix_rpc
  PUBLIC -DACTIVE_NETWORK=${ACTIVE_NETWORK}
  PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
          -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

if((CELERIX_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS celerix_rpc RUNTIME DESTINATION .)
  else()
    install(TARGETS celerix_rpc RUNTIME DESTINATION ./bin)
  endif()
endif()
