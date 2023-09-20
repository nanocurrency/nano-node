# Build flatc from the Flatbuffers submodule
set(FLATBUFFERS_BUILD_TESTS
    OFF
    CACHE BOOL "")
set(FLATBUFFERS_BUILD_FLATHASH
    OFF
    CACHE BOOL "")
mark_as_advanced(
  FLATBUFFERS_BUILD_CPP17
  FLATBUFFERS_BUILD_FLATC
  FLATBUFFERS_BUILD_FLATHASH
  FLATBUFFERS_BUILD_FLATLIB
  FLATBUFFERS_BUILD_GRPCTEST
  FLATBUFFERS_BUILD_LEGACY
  FLATBUFFERS_BUILD_SHAREDLIB
  FLATBUFFERS_BUILD_TESTS
  FLATBUFFERS_CODE_COVERAGE
  FLATBUFFERS_CODE_SANITIZE
  FLATBUFFERS_INSTALL
  FLATBUFFERS_LIBCXX_WITH_CLANG
  FLATBUFFERS_PACKAGE_DEBIAN
  FLATBUFFERS_PACKAGE_REDHAT
  FLATBUFFERS_STATIC_FLATC)
add_subdirectory(../../submodules/flatbuffers
                 ${CMAKE_CURRENT_BINARY_DIR}/flatbuffers-build EXCLUDE_FROM_ALL)

# Generate Flatbuffers files into the ipc_flatbuffers_lib library, which will be
# rebuilt whenever any of the fbs files change. Note that while this supports
# multiple fbs files, we currently only use one, to avoid include-file issues
# with certain language bindings.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/generated/flatbuffers)
if(APPLE)
  install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../api/flatbuffers/
    DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/MacOS/api/flatbuffers)
elseif(LINUX)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../api/flatbuffers/
          DESTINATION ./bin/api/flatbuffers)
else()
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../api/flatbuffers/
          DESTINATION ./api/flatbuffers)
endif()

file(GLOB files
     "${CMAKE_CURRENT_SOURCE_DIR}/../../api/flatbuffers/nanoapi*.fbs")
foreach(file ${files})
  get_filename_component(flatbuffers_filename ${file} NAME_WE)
  message(
    STATUS
      "Generating flatbuffers code for: ${flatbuffers_filename} into ${CMAKE_CURRENT_SOURCE_DIR}/generated/flatbuffers"
  )

  configure_file(
    ${file}
    ${PROJECT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/api/flatbuffers/${flatbuffers_filename}.fbs
    COPYONLY)

  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/flatbuffers/${flatbuffers_filename}_generated.h
    COMMAND
      "$<TARGET_FILE:flatc>" --force-empty-vectors --reflect-names --gen-mutable
      --gen-name-strings --gen-object-api --strict-json --cpp -o
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/flatbuffers
      ${CMAKE_CURRENT_SOURCE_DIR}/../../api/flatbuffers/${flatbuffers_filename}.fbs
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/../../api/flatbuffers/${flatbuffers_filename}.fbs
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

add_library(ipc_flatbuffers_lib generated/flatbuffers/nanoapi_generated.h
                                flatbuffer_producer.hpp flatbuffer_producer.cpp)

target_link_libraries(ipc_flatbuffers_lib flatbuffers)
