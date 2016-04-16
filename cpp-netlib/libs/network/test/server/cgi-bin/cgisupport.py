#!/usr/bin/python 
#
#          Copyright Kim Grasman 2008.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http:#www.boost.org/LICENSE_1_0.txt)
#

class http_headers:
    def __init__(self, header_str):
        self.parse(header_str)

    def raw_headers(self):
        return self.raw

    def iteritems(self):
        return self.headers.iteritems()

    def value(self, name):
        return self.headers[name]
    
    def parse(self, header_str):
        self.raw = header_str
        self.headers = self.__parse_headers(header_str)
        
    def __parse_headers(self, str):
        dict = {}
        
        # I'm sure there are better ways to do this in Python,
        # but I don't know my way around so well
        lines = str.split('\n')
        for line in lines:
            header = line.split(': ')
            if len(header) > 0:
                name = header[0]
            if len(header) > 1:
                value = ': '.join(header[1:]) # re-join the rest of the items as value
            
            if len(name) > 0:
                dict[name] = value

        return dict
