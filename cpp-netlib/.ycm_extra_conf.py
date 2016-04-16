# Copyright 2013 Google, Inc.
# Copyright 2013 Dean Michael Berris <dberris@google.com>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
# Project-wide configuration for YouCompleteMe Vim plugin.
#
# Based off of Valloric's .ycm_conf_extra.py for YouCompleteMe:
#  https://github.com/Valloric/YouCompleteMe/blob/master/cpp/ycm/.ycm_extra_conf.py
#

import os
import ycm_core

flags = [
    '-Wall',
    '-Wextra',
    '-Werror',
    '-std=c++03',
    '-isystem',
    '.',
    '-isystem',
    '/usr/include',
    '-isystem',
    '/usr/include/c++/4.6',
    '-isystem',
    '/usr/include/clang/3.0/include',
    '-isystem',
    '/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/c++/v1',
    '-I',
    os.environ['BOOST_ROOT'],
    # Always enable debugging for the project when building for semantic
    # completion.
    '-DBOOST_NETWORK_DEBUG',
    ]

def DirectoryOfThisScript():
  return os.path.dirname(os.path.abspath(__file__))


def MakeRelativePathsInFlagsAbsolute(flags, working_directory):
  if not working_directory:
    return list(flags)
  new_flags = []
  make_next_absolute = False
  path_flags = ['-isystem', '-I', '-iquote', '--sysroot=']
  for flag in flags:
    new_flag = flag
    if make_next_absolute:
      make_next_absolute = False
      if not flag.startswith('/'):
        new_flag = os.path.join(working_directory, flag)

    for path_flag in path_flags:
      if flag == path_flag:
        make_next_absolute = True
        break
      if flag.startswith(path_flag):
        path = flag[len(path_flag):]
        new_flag = path_flag + os.path.join(working_directory, path)
        break

    if new_flag:
      new_flags.append(new_flag)
  return new_flags


def FlagsForFile(filename):
  relative_to = DirectoryOfThisScript()
  final_flags = MakeRelativePathsInFlagsAbsolute(flags, relative_to)
  return {'flags': final_flags, 'do_cache': True }
