#!/usr/bin/python
import os, sys, time

if os.environ.has_key("QUERY_STRING") and os.environ["QUERY_STRING"].isdigit():
    time.sleep(int(os.environ["QUERY_STRING"]))

sys.stdout.write( "HTTP/1.0 200\r\n" )
sys.stdout.write( "\r\n" )
