#!/usr/bin/python
#
#	A python program to compare against the output from "mime-structure" tool.
#
#	Usage: python mimeParse.py <file list>
#

import sys
import email

def parseOne ( msg, title, prefix ):
#	if prefix != "":
#		print msg
	if title != None:
		print "%sData from: %s"				% ( prefix, title )
	print "%sContent-Type: %s" 				% ( prefix, msg.get_content_type ())
	print "%sThere are %d headers" 			% ( prefix, msg.__len__ ())
	payload = msg.get_payload ();
	if msg.is_multipart ():
		print "%sThere are %s sub parts"	% ( prefix, len ( payload ))
		for p in payload:
			parseOne ( p, None, prefix + "  " )
	else:
		bodyLen = 0
		aBody = ""
		for p in payload:
			bodyLen += len (p)
			aBody += p
			
		print "%sThe body is %d bytes long"	% ( prefix, bodyLen )
		print prefix,
		if bodyLen < 10:
			for c in aBody:
				print ord(c),
		else:
			for i in range(0,5):
				print ord (aBody[i]),
			
			print "... ",
			for i in range(1,6):
				print ord (aBody[-i]),
		print ''
	
#	_structure ( msg )


def main():
	for a in sys.argv[1:]:
		print "**********************************"
		parseOne ( email.message_from_file ( open ( a )), a, "" )

main ()