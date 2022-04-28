set(CPACK_GENERATOR "DragNDrop")
configure_file(${CMAKE_SOURCE_DIR}/Info.plist.in ${CMAKE_SOURCE_DIR}/Info.plist
               @ONLY)
install(TARGETS nano_wallet
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/MacOS)
install(TARGETS nano_node
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/MacOS)
install(TARGETS nano_rpc
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/MacOS)
install(FILES Info.plist DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents)
install(FILES qt.conf
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Resources)
install(DIRECTORY ${Qt5_DIR}/../../QtCore.framework
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
install(DIRECTORY ${Qt5_DIR}/../../QtDBus.framework
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
install(DIRECTORY ${Qt5_DIR}/../../QtGui.framework
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
install(DIRECTORY ${Qt5_DIR}/../../QtPrintSupport.framework
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
install(DIRECTORY ${Qt5_DIR}/../../QtTest.framework
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
install(DIRECTORY ${Qt5_DIR}/../../QtWidgets.framework
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
install(FILES "${Qt5_DIR}/../../../plugins/platforms/libqcocoa.dylib"
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/PlugIns/platforms)
if(NANO_SHARED_BOOST)
  foreach(boost_lib IN LISTS Boost_LIBRARIES)
    string(REGEX MATCH "(.+/.*boost_[^-]+)" boost_lib_name ${boost_lib})
    set(boost_dll "${CMAKE_MATCH_1}")
    if(${boost_dll} MATCHES "boost")
      install(FILES ${boost_dll}
              DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/boost/lib)
    endif()
  endforeach(boost_lib)
endif()
if(NANO_POW_SERVER)
  install(TARGETS nano_pow_server
          DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/MacOS)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/nano-pow-server/public
          DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/MacOS)
endif()
install(FILES Nano.icns
        DESTINATION ${NANO_OSX_PACKAGE_NAME}.app/Contents/Resources)
