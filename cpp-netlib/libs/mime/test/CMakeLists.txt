include_directories(${CPP-NETLIB_SOURCE_DIR})
file ( COPY TestMessages DESTINATION ${CMAKE_CURRENT_BINARY_DIR} )

#This test causes a "too many sections" error on Windows MinGW64
#(MSVC has /bigobj, MinGW does not)
if (NOT(${CMAKE_CXX_COMPILER_ID} MATCHES GNU AND ${CMAKE_SYSTEM_NAME} MATCHES "Windows"))
  if ( Boost_FOUND )
    add_executable ( mime-roundtrip mime-roundtrip.cpp )
    target_link_libraries ( mime-roundtrip ${Boost_LIBRARIES}
      ${CMAKE_THREAD_LIBS_INIT})
    set_target_properties( mime-roundtrip
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CPP-NETLIB_BINARY_DIR}/tests)
    add_test ( mime-roundtrip ${CPP-NETLIB_BINARY_DIR}/tests/mime-roundtrip )
  endif ()
endif()

