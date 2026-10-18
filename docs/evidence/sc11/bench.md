# SC-11 HTTP demo: performance record

Recorded, not gated. Command: `make -C examples/http && python3 examples/http/bench.py`.
Build: `clang -O2`, no sanitizers. Machine: 4-core Linux 6.18 container, client and
server on one host. The client is four Python threads on keep-alive connections, so it
is the bottleneck; read requests per second as a floor, not as the server's limit.

```
requests        100000
errors          0
seconds         28.80
requests/s      3472
RSS before KiB  4708
RSS after KiB   4708
RSS growth KiB  0
```

The server runs one polling thread, so requests are served one at a time. Each request
takes one slot from a pool of 64 arenas of 16 KiB; the adapter resets the arena when
libmicrohttpd reports the request complete. RSS is read after a 1000-request warm-up, so
the flat line is the steady state: no per-request growth over 100,000 requests.
