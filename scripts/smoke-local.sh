#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

if [ ! -x "$ROOT/bin/seeker" ]; then
  echo "smoke-local: build seeker first (e.g. make all)" >&2
  exit 1
fi

PORT="${SMOKE_LOCAL_PORT:-}"
if [ -z "$PORT" ]; then
  PORT=$((30000 + ($$ % 10000)))
fi

TMP=$(mktemp -d)
cleanup() {
  rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

cp -r "$ROOT/templates" "$ROOT/static" "$TMP/"
cat > "$TMP/config.ini" <<EOF
[server]
host = 127.0.0.1
port = ${PORT}
domain = http://127.0.0.1:${PORT}

[engines]
engines = ddg
EOF

(
  cd "$TMP" || exit 1
  exec "$ROOT/bin/seeker"
) &
PID=$!

sleep 2

ok=0
for path in / /robots.txt /opensearch.xml; do
  code=$(curl -g -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:${PORT}${path}" || echo "000")
  if [ "$code" = "200" ]; then
    ok=$((ok + 1))
  else
    echo "smoke-local: ${path} expected HTTP 200, got ${code}" >&2
  fi
done

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

if [ "$ok" -eq 3 ]; then
  echo "smoke-local: ok (port ${PORT})"
  exit 0
fi
exit 1
