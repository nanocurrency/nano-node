add_library(qt qt.cpp qt.hpp)

target_link_libraries(
  qt
  node
  secure
  nano_lib
  libminiupnpc-static
  Qt5::Gui
  Qt5::Widgets)

set_target_properties(
  qt PROPERTIES COMPILE_FLAGS "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")
