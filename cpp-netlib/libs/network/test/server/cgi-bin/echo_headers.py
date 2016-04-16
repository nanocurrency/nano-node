#!/usr/bin/python 
#
#          Copyright Kim Grasman 2008.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http:#www.boost.org/LICENSE_1_0.txt)
#
# This program sets up a CGI application on localhost
# It can be accessed by http://localhost:8000/cgi-bin/echo_headers.py
#
import cgi
import os, sys
import cgisupport

sys.stdout.write( "Content-type: text/plain; charset=us-ascii\r\n\r\n" )
sys.stdout.write( "\r\n" )

hdrs = cgisupport.http_headers(os.environ.get('HTTP_ALL_HEADERS'))

for h,v in hdrs.iteritems():
    print h + ": " + v
