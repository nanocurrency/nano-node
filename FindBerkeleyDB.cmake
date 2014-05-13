#---------------------------------------------------------------------------------------------------
#
#  Copyright (C) 2009  Artem Rodygin
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#---------------------------------------------------------------------------------------------------
#
#  This module finds if C API of Berkeley DB is installed and determines where required
#  include files and libraries are. The module sets the following variables:
#
#    BerkeleyDB_FOUND         - system has Berkeley DB
#    BerkeleyDB_INCLUDE_DIR   - the Berkeley DB include directory
#    BerkeleyDB_LIBRARIES     - the libraries needed to use Berkeley DB
#    BerkeleyDB_VERSION       - Berkeley DB full version information string
#    BerkeleyDB_VERSION_MAJOR - the major version of the Berkeley DB release
#    BerkeleyDB_VERSION_MINOR - the minor version of the Berkeley DB release
#    BerkeleyDB_VERSION_PATCH - the patch version of the Berkeley DB release
#
#  You can help the module to find Berkeley DB by specifying its root path
#  in environment variable named "DBROOTDIR". If this variable is not set
#  then module will search for files in default path as following:
#
#    CMAKE_HOST_WIN32 - "C:\Program Files\Oracle\Berkeley DB.X.Y"
#    CMAKE_HOST_UNIX  - "/usr/local/BerkeleyDB.X.Y", "/usr/local", "/usr"
#
#---------------------------------------------------------------------------------------------------

set(BerkeleyDB_FOUND TRUE)

# set the search path

if (WIN32)
    file(GLOB BerkeleyDB_SEARCH_PATH "C:/Program Files/Oracle/Berkeley DB*")
    if (NOT BerkeleyDB_SEARCH_PATH)
    file(GLOB BerkeleyDB_SEARCH_PATH "C:/Program Files (x86)/Oracle/Berkeley DB*")
    endif (NOT BerkeleyDB_SEARCH_PATH)
else (WIN32)
    file(GLOB BerkeleyDB_SEARCH_PATH "/usr/local/BerkeleyDB*")
endif (WIN32)

file(TO_CMAKE_PATH "$ENV{DBROOTDIR}" DBROOTDIR)

# search for header

find_path(BerkeleyDB_INCLUDE_DIR
          NAMES "db.h"
          PATHS "${BerkeleyDB_SEARCH_PATH}"
                "/usr/local"
                "/usr"
		“/opt/local”
          ENV DBROOTDIR
          PATH_SUFFIXES "include")

# header is found

if (BerkeleyDB_INCLUDE_DIR)

    # retrieve version information from the header
    file(READ "${BerkeleyDB_INCLUDE_DIR}/db.h" DB_H_FILE)

    string(REGEX REPLACE ".*#define[ \t]+DB_VERSION_STRING[ \t]+\"([^\"]+)\".*" "\\1" BerkeleyDB_VERSION       "${DB_H_FILE}")
    string(REGEX REPLACE ".*#define[ \t]+DB_VERSION_MAJOR[ \t]+([0-9]+).*"      "\\1" BerkeleyDB_VERSION_MAJOR "${DB_H_FILE}")
    string(REGEX REPLACE ".*#define[ \t]+DB_VERSION_MINOR[ \t]+([0-9]+).*"      "\\1" BerkeleyDB_VERSION_MINOR "${DB_H_FILE}")
    string(REGEX REPLACE ".*#define[ \t]+DB_VERSION_PATCH[ \t]+([0-9]+).*"      "\\1" BerkeleyDB_VERSION_PATCH "${DB_H_FILE}")

    # search for library
    if (WIN32)

        file(GLOB BerkeleyDB_LIBRARIES
             "${DBROOTDIR}/lib/libdb${BerkeleyDB_VERSION_MAJOR}${BerkeleyDB_VERSION_MINOR}.lib"
             "${BerkeleyDB_SEARCH_PATH}/lib/libdb${BerkeleyDB_VERSION_MAJOR}${BerkeleyDB_VERSION_MINOR}.lib")

    else (WIN32)

        find_library(BerkeleyDB_LIBRARIES
                     NAMES "libdb-${BerkeleyDB_VERSION_MAJOR}.${BerkeleyDB_VERSION_MINOR}.so"
                     PATHS "${BerkeleyDB_SEARCH_PATH}"
                     ENV DBROOTDIR
                     PATH_SUFFIXES "lib")

    endif (WIN32)

endif (BerkeleyDB_INCLUDE_DIR)

# header is not found

if (NOT BerkeleyDB_INCLUDE_DIR)
    set(BerkeleyDB_FOUND FALSE)
endif (NOT BerkeleyDB_INCLUDE_DIR)

# library is not found

if (NOT BerkeleyDB_LIBRARIES)
    set(BerkeleyDB_FOUND FALSE)
endif (NOT BerkeleyDB_LIBRARIES)

# set default error message

if (BerkeleyDB_FIND_VERSION)
    set(BerkeleyDB_ERROR_MESSAGE "Unable to find Berkeley DB library v${BerkeleyDB_FIND_VERSION}")
else (BerkeleyDB_FIND_VERSION)
    set(BerkeleyDB_ERROR_MESSAGE "Unable to find Berkeley DB library")
endif (BerkeleyDB_FIND_VERSION)

# check found version

if (BerkeleyDB_FIND_VERSION AND BerkeleyDB_FOUND)

    set(BerkeleyDB_FOUND_VERSION "${BerkeleyDB_VERSION_MAJOR}.${BerkeleyDB_VERSION_MINOR}.${BerkeleyDB_VERSION_PATCH}")

    if (BerkeleyDB_FIND_VERSION_EXACT)
        if (NOT ${BerkeleyDB_FOUND_VERSION} VERSION_EQUAL ${BerkeleyDB_FIND_VERSION})
            set(BerkeleyDB_FOUND FALSE)
        endif (NOT ${BerkeleyDB_FOUND_VERSION} VERSION_EQUAL ${BerkeleyDB_FIND_VERSION})
    else (BerkeleyDB_FIND_VERSION_EXACT)
        if (${BerkeleyDB_FOUND_VERSION} VERSION_LESS ${BerkeleyDB_FIND_VERSION})
            set(BerkeleyDB_FOUND FALSE)
        endif (${BerkeleyDB_FOUND_VERSION} VERSION_LESS ${BerkeleyDB_FIND_VERSION})
    endif (BerkeleyDB_FIND_VERSION_EXACT)

    if (NOT BerkeleyDB_FOUND)
        set(BerkeleyDB_ERROR_MESSAGE "Unable to find Berkeley DB library v${BerkeleyDB_FIND_VERSION} (${BerkeleyDB_FOUND_VERSION} was found)")
    endif (NOT BerkeleyDB_FOUND)

endif (BerkeleyDB_FIND_VERSION AND BerkeleyDB_FOUND)

# final status messages

if (BerkeleyDB_FOUND)

    if (NOT BerkeleyDB_FIND_QUIETLY)
        message(STATUS ${BerkeleyDB_VERSION})
    endif (NOT BerkeleyDB_FIND_QUIETLY)

    mark_as_advanced(BerkeleyDB_INCLUDE_DIR
                     BerkeleyDB_LIBRARIES)

else (BerkeleyDB_FOUND)

    if (BerkeleyDB_FIND_REQUIRED)
        message(SEND_ERROR "${BerkeleyDB_ERROR_MESSAGE}")
    endif (BerkeleyDB_FIND_REQUIRED)

endif (BerkeleyDB_FOUND)
