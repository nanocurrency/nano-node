# - Try to find the Botan library
#
# Once done this will define
#
#  BOTAN_FOUND - System has Botan
#  BOTAN_INCLUDE_DIR - The Botan include directory
#  BOTAN_LIBRARIES - The libraries needed to use Botan
#  BOTAN_DEFINITIONS - Compiler switches required for using Botan

IF (BOTAN_INCLUDE_DIR AND BOTAN_LIBRARY)
   # in cache already
   SET(Botan_FIND_QUIETLY TRUE)
ENDIF (BOTAN_INCLUDE_DIR AND BOTAN_LIBRARY)

IF (NOT WIN32)
   # try using pkg-config to get the directories and then use these values
   # in the FIND_PATH() and FIND_LIBRARY() calls
   # also fills in BOTAN_DEFINITIONS, although that isn't normally useful
   FIND_PACKAGE(PkgConfig)
   PKG_SEARCH_MODULE(PC_BOTAN botan-1.10 botan-1.9 botan-1.8 botan)
   SET(BOTAN_DEFINITIONS ${PC_BOTAN_CFLAGS})
ENDIF (NOT WIN32)

FIND_PATH(BOTAN_INCLUDE_DIR botan/botan.h
   HINTS
   ${PC_BOTAN_INCLUDEDIR}
   ${PC_BOTAN_INCLUDE_DIRS}
   )

FIND_LIBRARY(BOTAN_LIBRARY NAMES ${PC_BOTAN_LIBRARIES}
   HINTS
   ${PC_BOTAN_LIBDIR}
   ${PC_BOTAN_LIBRARY_DIRS}
   )

MARK_AS_ADVANCED(BOTAN_INCLUDE_DIR BOTAN_LIBRARY)

# handle the QUIETLY and REQUIRED arguments and set BOTAN_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Botan DEFAULT_MSG BOTAN_LIBRARY BOTAN_INCLUDE_DIR)

IF(BOTAN_FOUND)
    SET(BOTAN_LIBRARIES    ${BOTAN_LIBRARY})
    SET(BOTAN_INCLUDE_DIRS ${BOTAN_INCLUDE_DIR})
ENDIF()
