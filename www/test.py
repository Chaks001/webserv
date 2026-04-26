#!/usr/bin/env python3
import os
import sys

print("Content-Type: text/html\r\n\r\n")
print("<html><body>")
print("<h1>Hello from Python CGI!</h1>")
print("<p>Request Method: " + os.environ.get("REQUEST_METHOD", "Unknown") + "</p>")
print("</body></html>")
