message("-- Found Packaging")

set(CPACK_PACKAGE_VERSION_MAJOR "${NANO_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${NANO_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${NANO_VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION_PRE_RELEASE "${NANO_VERSION_PRE_RELEASE}")
set(CPACK_PACKAGE_VENDOR "${NANO_VENDOR}")

set(CPACK_PACKAGE_NAME
    "nano-node"
    CACHE STRING "" FORCE)
set(CPACK_NSIS_PACKAGE_NAME
    "Nano"
    CACHE STRING "" FORCE)
set(CPACK_PACKAGE_INSTALL_DIRECTORY
    "nanocurrency"
    CACHE STRING "" FORCE)

if("${ACTIVE_NETWORK}" MATCHES "nano_beta_network")
  set(CPACK_PACKAGE_NAME
      "nano-node-beta"
      CACHE STRING "" FORCE)
  set(CPACK_NSIS_PACKAGE_NAME
      "Nano-Beta"
      CACHE STRING "" FORCE)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY
      "nanocurrency-beta"
      CACHE STRING "" FORCE)
elseif("${ACTIVE_NETWORK}" MATCHES "nano_test_network")
  set(CPACK_PACKAGE_NAME
      "nano-node-test"
      CACHE STRING "" FORCE)
  set(CPACK_NSIS_PACKAGE_NAME
      "Nano-Test"
      CACHE STRING "" FORCE)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY
      "nanocurrency-test"
      CACHE STRING "" FORCE)
endif()
set(NANO_OSX_PACKAGE_NAME
    ${CPACK_NSIS_PACKAGE_NAME}
    CACHE STRING "" FORCE)

install(FILES ${PROJECT_BINARY_DIR}/config-node.toml.sample DESTINATION .)
install(FILES ${PROJECT_BINARY_DIR}/config-rpc.toml.sample DESTINATION .)

# From node/CMakeLists.txt
if((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS nano_node RUNTIME DESTINATION .)
  else()
    install(TARGETS nano_node RUNTIME DESTINATION ./bin)
  endif()
endif()

# From nano/ipc_flatbuffers_lib/CMakeLists.txt
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

# From nano/nano_rpc/CMakeLists.txt
if((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS nano_rpc RUNTIME DESTINATION .)
  else()
    install(TARGETS nano_rpc RUNTIME DESTINATION ./bin)
  endif()
endif()

if(APPLE)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules/plat/osx")
  include(Packaging-dragndrop)
elseif(WIN32)
  list(APPEND CMAKE_MODULE_PATH
       "${CMAKE_SOURCE_DIR}/cmake/Modules/plat/windows")
  include(Packaging-nsis)
else()
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules/plat/linux")
  include(Packaging-debian-tbz2)
endif()

set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
include(CPack)
