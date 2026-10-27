#!/bin/sh
# Usage: pgtest.sh command [args...]. Runs the command against a temporary
# Postgres server: initdb into a temp dir (as the current, non-root user),
# password auth (scram), a private socket directory and a TCP listener on
# 127.0.0.1 at the same port (for the integration test's TCP cases), PG*
# exported for libpq, and the server stopped and removed afterwards. initdb and
# pg_ctl come from `pg_config --bindir`, since neither Debian nor Ubuntu puts
# them on PATH.
set -eu
bindir=$(pg_config --bindir)
[ -x "$bindir/initdb" ] || { echo "pgtest: no initdb in $bindir; install the Postgres server" >&2; exit 1; }
dir=$(mktemp -d "${TMPDIR:-/tmp}/pgtest.XXXXXX")
port=${PGTEST_PORT:-55432}
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
if ! "$bindir/pg_ctl" -D "$dir/data" -l "$dir/server.log" -w \
    -o "-k $dir -p $port -c listen_addresses=127.0.0.1" start >/dev/null; then
    cat "$dir/server.log" >&2
    exit 1
fi
export PGHOST="$dir" PGPORT="$port" PGUSER=explorer PGPASSWORD=explorer PGDATABASE=postgres
"$@"
