#!/usr/bin/env python3
"""Records RSS growth and requests per second for the HTTP demo.

Usage: bench.py [server] [requests]. Defaults: build/http/server, 100000.
Starts the server on a spare port, warms it with 1000 requests, reads VmRSS,
sends the timed requests over keep-alive connections from four client threads,
then reads VmRSS again. Numbers are recorded, not gated; the Python client is
slower than the server, so requests per second is a floor on the server, not a
measure of it."""
import http.client
import os
import subprocess
import sys
import threading
import time

PORT = 18081
THREADS = 4


def rss_kib(pid):
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    raise RuntimeError("no VmRSS")


def hammer(count, errors):
    conn = http.client.HTTPConnection("127.0.0.1", PORT)
    for _ in range(count):
        conn.request("GET", "/")
        resp = conn.getresponse()
        if resp.status != 200 or resp.read() != b"hello from safe c":
            errors.append(resp.status)
            conn = http.client.HTTPConnection("127.0.0.1", PORT)
    conn.close()


def run(total):
    errors = []
    per = total // THREADS
    threads = [threading.Thread(target=hammer, args=(per, errors)) for _ in range(THREADS)]
    start = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return per * THREADS, time.perf_counter() - start, errors


def main():
    server = sys.argv[1] if len(sys.argv) > 1 else "build/http/server"
    total = int(sys.argv[2]) if len(sys.argv) > 2 else 100000
    proc = subprocess.Popen([server], env={**os.environ, "SC_HTTP_PORT": str(PORT)},
                            stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                http.client.HTTPConnection("127.0.0.1", PORT).request("GET", "/")
                break
            except OSError:
                time.sleep(0.1)
        _, _, errors = run(1000)
        before = rss_kib(proc.pid)
        sent, seconds, more = run(total)
        errors += more
        after = rss_kib(proc.pid)
        print(f"requests        {sent}")
        print(f"errors          {len(errors)}")
        print(f"seconds         {seconds:.2f}")
        print(f"requests/s      {sent / seconds:.0f}")
        print(f"RSS before KiB  {before}")
        print(f"RSS after KiB   {after}")
        print(f"RSS growth KiB  {after - before}")
        return 1 if errors else 0
    finally:
        proc.terminate()
        proc.wait()


if __name__ == "__main__":
    sys.exit(main())
