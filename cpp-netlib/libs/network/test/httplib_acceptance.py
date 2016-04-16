#            Copyright (c) 2010.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)

#!/bin/env python

from sys import argv
from time import sleep
import httplib2 as httplib
from subprocess import Popen,PIPE

if len(argv) < 4:
    print('I need the executable to run, the port to run it on, and the final touched file indicator.')
    exit(1)

print('Running {0} on port {1}...'.format(argv[1], argv[2]))

pipe = None
try:
    pipe = Popen(args=[argv[1], argv[2]], executable=argv[1], stdin=PIPE, stdout=PIPE, close_fds=True)
    print('Done with spawning {0}.'.format(argv[1]))
    print('Sleeping to give the server a chance to run...')
    sleep(1)
except:
    print('I cannot spawn \'{0}\' properly.'.format(argv[1]))
    exit(1)

status = 0
client = httplib.Http(timeout=5)
expected = b'Hello, World!'

def test(url, method, expected, headers={}, body=''):
    global status 
    try:
        print('Request: {method} {url} body=\'{body}\''.format(method=method, url=url, body=body)),
        resp, content = client.request(url, method, headers=headers, body=body)
        if content != expected:
            print('ERROR: \'{0}\' != \'{1}\'; sizes: {2} != {3}'.format(content, expected, len(content), len(expected)))
            status = 1
        else:
            print('... passed.')
    except Exception as e:
        print('Caught Exception: {0}'.format(e))
        status = 1

def test_status(url, method, expected, headers={}, body=''):
    global status
    try:
        print('Request: {method} {url} body=\'{body}\''.format(method=method, url=url, body=body)),
        resp, content = client.request('http://localhost:8000/', 'PUT', body='')
        if resp['status'] != expected:
            print('ERROR: response status (got {0}) != expecting {1}'.format(resp['status'], expected))
            status = 1
        else:
            print('... passed.')
    except Exception as e:
        print('Caught Exception: {0}'.format(e))
        status = 1

url = 'http://127.0.0.1:{0}/'.format(argv[2])
test(url, 'GET', expected)
test(url, 'DELETE', expected)
# Good request case, there's a content-length header for POST
test(url, 'POST', expected, {'Content-Length': '0'})
# Good request case, there's a content-length header for PUT
test(url, 'PUT', expected, {'Content-Length': '0'})
# Bad request case, no content-length for POST
test_status(url, 'POST', '400')
# Bad request case, no content-length for PUT
test_status(url, 'PUT', '400')

if status != 0:
    print('Failures encountered.')
    pipe.terminate()
    exit(status)

open(argv[len(argv) - 1], 'w').close()
print('All tests pass.')
pipe.terminate()

