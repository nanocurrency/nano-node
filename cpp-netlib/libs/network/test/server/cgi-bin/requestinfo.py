#!/usr/bin/python 
#
#          Copyright Divye Kapoor 2008.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http:#www.boost.org/LICENSE_1_0.txt)
#
# This program sets up a CGI application on localhost
# It can be accessed by http://localhost:8000/cgi-bin/requestinfo.py
# It returns the query parameters passed to the CGI Script as plain text.
#
import cgitb; cgitb.enable() # for debugging only
import cgi
import os, sys

sys.stdout.write( "HTTP/1.0 200 Requestinfo.py Output follows\r\n" )
sys.stdout.write( "Content-type: text/plain; charset=us-ascii\r\n\r\n" )
sys.stdout.write( "\r\n" )

form = cgi.FieldStorage()
qstring = ""
qstring_dict = {}

print "Headers: ",form.headers

if os.environ.has_key("QUERY_STRING"):
    qstring = os.environ["QUERY_STRING"]
    try:
        qstring_dict = cgi.parse_qs(qstring,1,1) # parse_qs(query_string,keep_blanks,strict_parsing)
    except ValueError:
        print "Error parsing query string."

    
print "Query string:", qstring

print "GET parameters:",
for i in qstring_dict.keys():
    print i,"-",qstring_dict[i],";",
print

# Remove GET params and print only the POST ones
print "POST parameters:",
try:
    for i in form.keys():
        if i not in qstring_dict.keys():
            print i,"-",form.getfirst(i, ""),";",
    print
except TypeError: # In case of empty POST bodies.
    pass

print 'HTTP HEADERS-------------------'
print 'Content-Type:', os.environ.get('CONTENT_TYPE')
print 'Content-Length:', os.environ.get('CONTENT_LENGTH')
for k in os.environ.keys():
        if k.startswith('HTTP_'):
                print k, ':', os.environ[k]
