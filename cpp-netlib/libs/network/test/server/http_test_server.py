#!/usr/bin/env python
#
#         Copyright Allister Levi Sanchez 2008.
# Distributed under  the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)
#
# This program sets up a CGI HTTP Server at port 8000 on localhost
# It will be used to test the http::client interface of the library

import BaseHTTPServer
import CGIHTTPServer
import os

class HttpTestHandler(CGIHTTPServer.CGIHTTPRequestHandler):

    def run_cgi(self):
        """Version of run_cgi that provides more HTTP headers."""
        headers_str = ''
        for a in self.headers.items():
            headers_str += "%s: %s\n" % a

        os.environ['HTTP_ALL_HEADERS'] = headers_str

        # run the rest of run_cgi
        CGIHTTPServer.CGIHTTPRequestHandler.run_cgi(self)

def run_server(server_class=BaseHTTPServer.HTTPServer, handler_class=HttpTestHandler):
    server_address = ('',8000)
    httpd = server_class(server_address, handler_class)
    httpd.serve_forever()

if __name__ == '__main__':
    run_server()
