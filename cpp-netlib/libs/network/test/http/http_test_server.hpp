//
//          Copyright Kim Grasman 2008.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//
// Changes by Allister Levi Sanchez 2008
// Changes by Dean Michael Berris 2010
#ifndef __NETWORK_TEST_HTTP_TEST_SERVER_HPP__
#define __NETWORK_TEST_HTTP_TEST_SERVER_HPP__

#define BOOST_FILESYSTEM_VERSION 3
#include <boost/filesystem.hpp>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32) || defined(MIN)
#include <windows.h>

// ShellExecuteEx
#include <shellapi.h>
#pragma comment(lib, "shell32")
#else
#include <unistd.h>  // fork, execlp etc.
#include <sys/types.h>
#include <sys/wait.h>  // for waitpid
#include <sys/stat.h>  // for chmod
#include <signal.h>    // for kill
#endif

struct http_test_server {
  bool start() {
    using namespace boost::filesystem;

    path script_path = get_server_path(current_path());
    if (script_path.empty()) return false;

    path cgibin_path = script_path.parent_path() / "cgi-bin";
    if (!set_cgibin_permissions(cgibin_path)) return false;

    server_child = launch_python_script(script_path);
    if (server_child == 0) return false;

    return true;
  }

  bool stop() {
    kill_script(server_child);
    return true;
  }

 private:
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
  typedef HANDLE script_handle_t;
#else
  typedef pid_t script_handle_t;
#endif

  boost::filesystem::path get_server_path(
      const boost::filesystem::path& base_path) {
    using namespace boost::filesystem;

    const path script_name =
#if defined(HTTPS_SERVER_TEST)
        "https_test_server.py"
#else
        "http_test_server.py"
#endif
        ;

    // if executed from $CPP_NETLIB_HOME
    path server_path = base_path / "libs/network/test/server" / script_name;
    if (exists(server_path))
      return server_path;

    // if executed from $CPP_NETLIB_HOME/libs/network/test
    server_path = base_path / "server" / script_name;
    if (exists(server_path))
      return server_path;

    // if executed from $CPP_NETLIB_HOME/libs/network/test/*
    server_path = base_path / "../server" / script_name;
    if (exists(server_path))
      return server_path;

    return path();
  }

  script_handle_t launch_python_script(
      const boost::filesystem::path& python_script_path) {
    using namespace boost::filesystem;

    path::string_type script_name = python_script_path.filename().native();
    path::string_type script_dir = python_script_path.parent_path().native();

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = "open";
    sei.lpFile = "python.exe";
    sei.lpParameters = reinterpret_cast<LPCSTR>(script_name.c_str());
    sei.lpDirectory = reinterpret_cast<LPCSTR>(script_dir.c_str());
    sei.nShow = SW_SHOWNOACTIVATE;

    if (!ShellExecuteExA(&sei)) return 0;

    return sei.hProcess;
#else
    // Try general Unix code
    pid_t child_process = fork();
    if (child_process < 0) return false;

    if (child_process == 0) {
      // child process

      // cd into script dir and launch python script
      current_path(script_dir);

      if (execlp("python", "python", script_name.c_str(), (char*)NULL) == -1)
        return 0;
    } else {
      // parent
      sleep(1);
    }

    return child_process;
#endif
  }

  void kill_script(script_handle_t h) {
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    TerminateProcess(h, 0U);
    CloseHandle(h);
#else
    kill(h, SIGTERM);
#endif
  }

  bool set_cgibin_permissions(const boost::filesystem::path& cgibin_path) {
    using namespace boost::filesystem;

#if !defined(_WIN32) && !defined(__WIN32__) && !defined(WIN32)
    // set the CGI script execute permission
    for (directory_iterator i(cgibin_path); i != directory_iterator(); ++i) {
      if (is_regular_file(i->status())) {
        path::string_type file_path = i->path().string();
        if (chmod(file_path.c_str(), S_IWUSR | S_IXUSR | S_IXGRP | S_IXOTH |
                                         S_IRUSR | S_IRGRP | S_IROTH) != 0)
          return false;
      }
    }
#endif
    return true;
  }

  script_handle_t server_child;
};

#endif  // __NETWORK_TEST_HTTP_TEST_SERVER_HPP__
