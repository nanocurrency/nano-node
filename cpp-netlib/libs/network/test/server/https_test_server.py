#!/usr/bin/env python
# Copyright 2009 Jeroen Habraken
# Copyright 2009 Dean Michael Berris
# Distributed under  the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)

import socket

from BaseHTTPServer import HTTPServer
from SimpleHTTPServer import SimpleHTTPRequestHandler
from CGIHTTPServer import CGIHTTPRequestHandler

from OpenSSL import SSL

import os

class SecureHTTPServer(HTTPServer):
    def __init__(self, server_address, HandlerClass):
        HTTPServer.__init__(self, server_address, HandlerClass)
    
        ctx = SSL.Context(SSL.SSLv23_METHOD)
        ctx.use_privatekey_file ("key.pem")
        ctx.use_certificate_file("certificate.pem")
        self.socket = SSL.Connection(ctx, socket.socket(self.address_family,
                                                        self.socket_type))
        self.server_bind()
        self.server_activate()

class SecureHTTPRequestHandler(CGIHTTPRequestHandler):
    def setup(self):
        self.connection = self.request
        self.rfile = socket._fileobject(self.request, "rb", self.rbufsize)
        self.wfile = socket._fileobject(self.request, "wb", self.wbufsize)

    def run_cgi(self):
        """Version of run_cgi that provides more HTTP headers."""
        headers_str = ''
        for a in self.headers.items():
            headers_str += "%s: %s\n" % a

        os.environ['HTTP_ALL_HEADERS'] = headers_str

        # run the rest of run_cgi
        CGIHTTPRequestHandler.run_cgi(self)

if __name__ == '__main__':
    SecureHTTPServer(('127.0.0.1', 8443), SecureHTTPRequestHandler).serve_forever()

