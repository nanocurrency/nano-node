message("-- Found Packaging-debian-tbz2")

set(CPACK_GENERATOR "TBZ2;DEB")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qt5-default | qtbase5-dev, qtchooser, qt5-qmake, qtbase5-dev-tools")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "russel@nano.org")
install(TARGETS nano_wallet RUNTIME DESTINATION ./bin)
if(NANO_SHARED_BOOST)
  foreach(boost_lib IN LISTS Boost_LIBRARIES)
    string(REGEX MATCH "(.+/.*boost_[^-]+)" boost_lib_name ${boost_lib})
    set(boost_dll "${CMAKE_MATCH_1}.${Boost_VERSION_STRING}")
    if(${boost_dll} MATCHES "boost")
      install(FILES ${boost_dll} DESTINATION ./lib)
    endif()
  endforeach(boost_lib)
endif()
if(NANO_POW_SERVER)
  install(TARGETS nano_pow_server DESTINATION ./bin)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/nano-pow-server/public
          DESTINATION ./bin)
endif()
set(DEBIAN_POSTINST postinst.in)
set(DEBIAN_POSTRM postrm.in)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/debian-control/${DEBIAN_POSTINST}
               ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postinst)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/debian-control/${DEBIAN_POSTRM}
               ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postrm)
file(
  COPY ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postinst
       ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postrm
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/debian-control/.
  FILE_PERMISSIONS
    OWNER_READ
    OWNER_WRITE
    OWNER_EXECUTE
    GROUP_READ
    GROUP_EXECUTE
    WORLD_READ
    WORLD_EXECUTE)
install(FILES etc/systemd/${NANO_SERVICE} DESTINATION ./extras/systemd/.)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_BINARY_DIR}/debian-control/postinst;${CMAKE_CURRENT_BINARY_DIR}/debian-control/postrm"
)
