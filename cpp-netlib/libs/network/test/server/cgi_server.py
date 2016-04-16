#!/usr/bin/python
#
#          Copyright Divye Kapoor 2008.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http:#www.boost.org/LICENSE_1_0.txt)
#
# This program sets up a CGI HTTP Server at port 8000 on localhost
# It will be used to test the http::client interface of the library

import CGIHTTPServer
import BaseHTTPServer
import threading

from threading import Thread, Event

stop_serving = Event()
server_thread = None

def run(server_class=BaseHTTPServer.HTTPServer, handler_class=CGIHTTPServer.CGIHTTPRequestHandler):
    server_address = ('',8000)
    httpd = server_class(server_address, handler_class)
    while not stop_serving.isSet():
        httpd.handle_request()


def start_server():
    server_thread = Thread(None, run, "HTTP Server",(), None, None)
    server_thread.start()
    server_thread.join()


if __name__ == '__main__':
    start_server()
