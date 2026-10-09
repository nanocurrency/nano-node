if(WIN32)
  set(PLATFORM_GUI_TARGET_PROPERTIES WIN32)
else()
  set(PLATFORM_GUI_TARGET_PROPERTIES "")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(PLATFORM_WALLET_SOURCE plat/default/icon.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(PLATFORM_WALLET_SOURCE plat/windows/icon.cpp ${CMAKE_SOURCE_DIR}/Nano.rc)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(PLATFORM_WALLET_SOURCE plat/default/icon.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  set(PLATFORM_WALLET_SOURCE plat/default/icon.cpp)
else()
  error("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif()

qt5_add_resources(RES ${CMAKE_SOURCE_DIR}/resources.qrc)

add_executable(nano_wallet ${PLATFORM_GUI_TARGET_PROPERTIES}
                           ${PLATFORM_WALLET_SOURCE} entry.cpp icon.hpp ${RES})

target_link_libraries(nano_wallet Boost::process rpc node qt)

if(WIN32)
  target_link_libraries(nano_wallet Qt5::WinExtras)
endif()

set_target_properties(
  nano_wallet PROPERTIES COMPILE_FLAGS
                         "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")
