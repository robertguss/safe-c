#!/bin/sh
# Usage: pgtest.sh command [args...]. Runs the command against a temporary
# Postgres server: initdb into a temp dir (as the current, non-root user),
# password auth (scram), a private socket directory and a TCP listener on
# 127.0.0.1 at the same port (for the integration test's TCP cases), PG*
# exported for libpq, and the server stopped and removed afterwards. initdb and
# pg_ctl come from `pg_config --bindir`, since neither Debian nor Ubuntu puts
# them on PATH. With no PGTEST_PORT, each run picks an exclusive free loopback
# port so concurrent screenshot and pq-test runs do not share 55432.
set -eu
bindir=$(pg_config --bindir)
[ -x "$bindir/initdb" ] || { echo "pgtest: no initdb in $bindir; install the Postgres server" >&2; exit 1; }
dir=$(mktemp -d "${TMPDIR:-/tmp}/pgtest.XXXXXX")
# Bind 127.0.0.1:0, read the port, then close so pg_ctl can listen there.
# Retry a few times if something else takes the port between close and start.
pick_port() {
    python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()'
}
cleanup() {
    "$bindir/pg_ctl" -D "$dir/data" -m immediate stop >/dev/null 2>&1 || true
    rm -rf "$dir"
}
trap cleanup EXIT INT TERM
printf 'explorer\n' > "$dir/pw"
if ! "$bindir/initdb" -D "$dir/data" -U explorer --pwfile="$dir/pw" --auth=scram-sha-256 \
    >"$dir/initdb.log" 2>&1; then
    cat "$dir/initdb.log" >&2
    exit 1
fi
started=
if [ -n "${PGTEST_PORT:-}" ]; then
    port=$PGTEST_PORT
    if "$bindir/pg_ctl" -D "$dir/data" -l "$dir/server.log" -w \
        -o "-k $dir -p $port -c listen_addresses=127.0.0.1" start >/dev/null; then
        started=1
    fi
else
    tries=0
    while [ "$tries" -lt 20 ]; do
        port=$(pick_port)
        if "$bindir/pg_ctl" -D "$dir/data" -l "$dir/server.log" -w \
            -o "-k $dir -p $port -c listen_addresses=127.0.0.1" start >/dev/null 2>"$dir/start.err"; then
            started=1
            break
        fi
        # Address already in use: pick another port. Any other failure stops.
        if ! grep -qiE 'address already in use|could not bind' "$dir/server.log" "$dir/start.err" 2>/dev/null; then
            break
        fi
        tries=$((tries + 1))
    done
fi
if [ -z "$started" ]; then
    cat "$dir/server.log" >&2
    [ -f "$dir/start.err" ] && cat "$dir/start.err" >&2
    exit 1
fi
echo "pgtest: port $port" >&2
export PGHOST="$dir" PGPORT="$port" PGUSER=explorer PGPASSWORD=explorer PGDATABASE=postgres
"$bindir/psql" -X -q -c "CREATE ROLE sc_prompt LOGIN PASSWORD 'prompt-only'" >/dev/null
"$@"
