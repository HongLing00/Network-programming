#! /usr/bin/env python3
import os

check_env = [
    'REQUEST_METHOD',
    'REQUEST_URI',
    'QUERY_STRING',
    'SERVER_PROTOCOL',
    'HTTP_HOST',
    'SERVER_ADDR',
    'SERVER_PORT',
    'REMOTE_ADDR',
    'REMOTE_PORT'
]

print('Content-type: text/plain', end='\r\n\r\n')

for env in check_env:
    print(env, '=', os.getenv(env, ''))
