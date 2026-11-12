"""A local HTTP server for the composite example of eval/timeline/run_all.sh: serves the files
under a directory on 127.0.0.1, on a port the system picks, and holds the response head of each
path given with --delay for its time after the request was read, as a slow server's first byte
(eval/phase3-survey/design.md §9.3 and §9.4; the design's P-O and P-S). It answers as
tests/HttpFileServer.h does: one response per connection, "Connection: close", the whole file with
"200 OK" and its Content-Length, and "404 Not Found" for anything that isn't a regular file under
the directory; it lists no directory and follows no link out of it.

usage: python3 -I -B eval/timeline/slow_server.py <root> <port file> <log file>
                                                  [--delay <path>=<seconds>]...

<path> is a request's path without its leading '/', as tests/HttpFileServer.h's rules name it
("v180/ad_000.ts"). Once it listens, the server writes its port to <port file>, through a
temporary file renamed over it, so that a reader never sees part of it. <log file> gets one line
of JSON per request once it is answered: n, the request's number from 0 in the order the server
read them; path; status; bytes, the body bytes sent; delay, the seconds its head was held; and
read, head and end, the seconds from the server's start at which the request was read, the head
sent and the response done, on the monotonic clock. The paths, statuses, bytes and delays are
what the requests asked and the server did; the three times are measured, and
eval/timeline/check.py compares none of them. SIGTERM or SIGINT stops it, exit status 0, once
the requests it is answering are answered, so that its log has every request it read. Python 3
standard library only.

Exit status: 0 stopped by a signal; 2 bad arguments, or it couldn't listen.
"""

import argparse
import http.server
import json
import math
import os
import signal
import sys
import threading
import time
import urllib.parse


def parse_delay(text):
    """('<path>', seconds) of a --delay argument, '<path>=<seconds>'; raises ValueError."""
    path, sep, seconds = text.rpartition("=")
    if not sep or not path or path.startswith("/"):
        raise ValueError("--delay wants <path>=<seconds>, the path without a leading '/': "
                         f"{text!r}")
    try:
        value = float(seconds)
    except ValueError:
        raise ValueError(f"--delay {text!r}: {seconds!r} isn't a number of seconds") from None
    if not math.isfinite(value) or value < 0:
        raise ValueError(f"--delay {text!r}: the seconds must be a finite number from 0")
    return path, value


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="slow_server.py",
        description="Serve a directory on 127.0.0.1, holding the response head of some paths.")
    p.add_argument("root", help="the directory whose files are served")
    p.add_argument("port_file", help="where the port is written once the server listens")
    p.add_argument("log_file", help="one line of JSON per request")
    p.add_argument("--delay", action="append", default=[], metavar="PATH=SECONDS",
                   help="hold the response head of PATH this long after its request was read")
    a = p.parse_args(argv)
    if not os.path.isdir(a.root):
        p.error(f"{a.root} is not a directory")
    delays = {}
    for text in a.delay:
        try:
            path, value = parse_delay(text)
        except ValueError as e:
            p.error(str(e))
        if path in delays:
            p.error(f"--delay names {path} twice")
        delays[path] = value
    a.delays = delays
    return a


def content_type(path):
    if path.endswith(".m3u8"):
        return "application/vnd.apple.mpegurl"
    if path.endswith(".ts"):
        return "video/mp2t"
    return "application/octet-stream"


class Server(http.server.ThreadingHTTPServer):
    # Not daemon threads: server_close() then waits for each request's thread
    # (socketserver.ThreadingMixIn's block_on_close), so that a stop never cuts a request's
    # line out of the log.
    daemon_threads = False

    def __init__(self, root, delays, log):
        self.root = os.path.realpath(root)
        self.delays = delays
        self.log = log
        self.lock = threading.Lock()
        self.count = 0
        self.t0 = time.monotonic()
        super().__init__(("127.0.0.1", 0), Handler)

    def clock(self):
        return round(time.monotonic() - self.t0, 6)

    def resolve(self, path):
        """The real path of the regular file `path` names under the root, or None."""
        if "\0" in path:
            return None
        full = os.path.realpath(os.path.join(self.root, path))
        if os.path.commonpath([self.root, full]) != self.root or not os.path.isfile(full):
            return None
        return full

    def number(self):
        """The next request's number, in the order the requests are read."""
        with self.lock:
            n = self.count
            self.count += 1
            return n

    def record(self, row):
        with self.lock:
            self.log.write(json.dumps(row) + "\n")
            self.log.flush()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "slow_server"
    sys_version = ""

    def do_GET(self):
        self.answer(body=True)

    def do_HEAD(self):
        self.answer(body=False)

    def answer(self, body):
        read = self.server.clock()
        n = self.server.number()
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path).lstrip("/")
        delay = self.server.delays.get(path, 0.0)
        full = self.server.resolve(path)
        data = None
        if full is not None:
            try:
                with open(full, "rb") as f:
                    data = f.read()
            except OSError:
                data = None
        if delay:
            time.sleep(delay)
        head = self.server.clock()
        self.close_connection = True
        sent = 0
        try:
            if data is None:
                status = 404
                self.send_response(status)
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()
            else:
                status = 200
                self.send_response(status)
                self.send_header("Content-Type", content_type(path))
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Connection", "close")
                self.end_headers()
                if body:
                    self.wfile.write(data)
                    sent = len(data)
            self.wfile.flush()
            error = None
        except OSError as e:   # the client went away
            error = str(e)
        row = {"n": n, "path": path, "status": status, "bytes": sent, "delay": delay,
               "read": read, "head": head, "end": self.server.clock()}
        if error:
            row["error"] = error
        self.server.record(row)

    def log_message(self, fmt, *args):   # the log file has every request
        pass


def main(argv=None):
    a = parse_args(sys.argv[1:] if argv is None else argv)

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        log = open(a.log_file, "w", encoding="utf-8")
    except OSError as e:
        print(f"slow_server: {a.log_file}: {e}", file=sys.stderr)
        return 2
    with log:
        try:
            server = Server(a.root, a.delays, log)
        except OSError as e:
            print(f"slow_server: can't listen on 127.0.0.1: {e}", file=sys.stderr)
            return 2
        with server:
            tmp = a.port_file + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                f.write(f"{server.server_address[1]}\n")
            os.replace(tmp, a.port_file)
            try:
                server.serve_forever(poll_interval=0.1)
            except KeyboardInterrupt:
                pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
