#!/bin/bash
response=$'HTTP/1.1 200 OK\r\n'
response+=$'Content-Type: text/html; charset=utf-8\r\n'
response+=$'Content-Length: 20\r\n'
response+=$'Connection: close\r\n'
response+=$'\r\n'
response+=$'<p>Hello, world!</p>'

while true ; do
    echo -n "$response" | nc -l 8080 > /dev/null
done

