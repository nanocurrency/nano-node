#!/usr/bin/python 
#
#          Copyright Divye Kapoor 2008.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http:#www.boost.org/LICENSE_1_0.txt)
#
# This program sets up a CGI application on localhost
# It can be accessed by http://localhost:8000/cgi-bin/echo_form.py
# It returns the query parameters passed to the CGI Script as plain text.
#
import cgitb; cgitb.enable() # for debugging only
import cgi
import os, sys

sys.stdout.write( "Content-type: text/plain; charset=us-ascii\r\n\r\n" )
sys.stdout.write( "\r\n" )

# POST data/form data is available in the .value property
sys.stdout.write( cgi.FieldStorage().value )
