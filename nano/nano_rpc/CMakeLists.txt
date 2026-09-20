add_executable(nano_rpc entry.cpp)

target_link_libraries(nano_rpc node)

target_compile_definitions(nano_rpc PUBLIC -DACTIVE_NETWORK=${ACTIVE_NETWORK})

if((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS nano_rpc RUNTIME DESTINATION .)
  else()
    install(TARGETS nano_rpc RUNTIME DESTINATION ./bin)
  endif()
endif()
