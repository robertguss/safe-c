# HTTP demo

A small HTTP server written in the Safe C subset (`docs/subset-v1.md`). It answers
`GET /` with `hello from safe c`, `404` for other paths, and `405` for other methods.

```
make -C examples/http run          # listens on :8080, or $SC_HTTP_PORT
curl -s localhost:8080/            # hello from safe c
make -C examples/http smoke        # starts it, checks GET /, 404 and 431, stops it
python3 examples/http/bench.py     # RSS growth and requests/s over 100k requests
```

Needs libmicrohttpd (`libmicrohttpd-dev` on Debian and Ubuntu).

## Shape

- `adapters/microhttpd.c` is the only file that touches libmicrohttpd (rule R0). For
  each request it takes one 16 KiB `sc_arena` from a pool, rebuilds the request head
  as bytes in it, calls the handler, copies the reply out, and resets the arena when
  libmicrohttpd reports the request done.
- `parse.c` parses that head into `sc_str` fields and answers 200, 400, 414 or 431.
  libmicrohttpd has parsed the request once already; the demo parses it again from
  bytes so the subset's own parser is what the tests, the fuzzer and the server share.
- `server.c` is the handler and `main`.

The demo uses no channel. If a later version adds one, create it in a long-lived arena,
never a request arena: the channel does not check the arena epoch (SC-10).

`make lint` covers `parse.c`, `parse.h` and `server.c`. `tests/http_parse_test.c` and
`fuzz/http_parse_fuzz.c` include `parse.c` directly. `make gates` does not build the
server; CI does, and runs `make -C examples/http smoke`.
