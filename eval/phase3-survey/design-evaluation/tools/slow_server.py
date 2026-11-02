"""A local HTTP server for the design's session-fetch run: serves the files under a directory on
127.0.0.1, on a port the system picks, and answers one path late: it waits a given number of
seconds before sending that response's status line and headers. Writes the port to a file once it
listens. Python 3 standard library only; for the Phase 3 design's runs (design-evaluation.md),
not repository code. The repository's tests would add such a rule to tests/HttpFileServer.h
instead (the design's work package for sessions).

usage: python3 -I -B slow_server.py <root> <port file> <slow path> <delay seconds>
"""

import functools
import http.server
import os
import sys
import time


class Handler(http.server.SimpleHTTPRequestHandler):
    slow_path = ""
    delay = 0.0

    def send_head(self):
        if self.path.lstrip("/") == self.slow_path:
            time.sleep(self.delay)
        return super().send_head()

    def log_message(self, fmt, *args):
        sys.stderr.write("server: %s\n" % (fmt % args))


def main(argv):
    root, port_file, slow_path, delay = argv[0], argv[1], argv[2], float(argv[3])
    Handler.slow_path = slow_path
    Handler.delay = delay
    handler = functools.partial(Handler, directory=root)
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as httpd:
        tmp = port_file + ".tmp"
        with open(tmp, "w") as f:
            f.write(str(httpd.server_address[1]))
        os.replace(tmp, port_file)
        httpd.serve_forever()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
