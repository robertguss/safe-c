#!/bin/sh
# Usage: smoke.sh path/to/server. Fails unless GET / answers "hello from safe c",
# an unknown path gets 404, an oversized header 431, and an encoded CRLF or NUL in
# the path cannot forge a header line or hide behind a NUL (both stay 404).
set -eu
server=$1
port=${SC_HTTP_PORT:-18080}
SC_HTTP_PORT=$port "$server" 2>/dev/null &
pid=$!
trap 'kill $pid 2>/dev/null || true' EXIT
for _ in 1 2 3 4 5 6 7 8 9 10; do
    curl -s -o /dev/null "localhost:$port/" && break
    sleep 0.2
done
body=$(curl -s "localhost:$port/")
[ "$body" = "hello from safe c" ] || { echo "smoke: GET / returned '$body'" >&2; exit 1; }
code=$(curl -s -o /dev/null -w '%{http_code}' "localhost:$port/missing")
[ "$code" = 404 ] || { echo "smoke: /missing returned $code" >&2; exit 1; }
for path in '/%20HTTP/1.1%0d%0aX-Evil:%201%0d%0aFoo:%20x' '/%00x'; do
    code=$(curl -s --path-as-is -o /dev/null -w '%{http_code}' "localhost:$port$path")
    [ "$code" = 404 ] || { echo "smoke: $path returned $code" >&2; exit 1; }
done
big=$(head -c 9000 /dev/zero | tr '\0' a)
code=$(curl -s -o /dev/null -w '%{http_code}' -H "X-Big: $big" "localhost:$port/")
[ "$code" = 431 ] || { echo "smoke: oversized header returned $code" >&2; exit 1; }
echo "smoke ok"
