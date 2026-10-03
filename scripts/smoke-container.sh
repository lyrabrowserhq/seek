#!/bin/sh
# Build the Dockerfile, run a one-off container, GET / (catches musl stack / HTTP crashes).
# Usage: ./scripts/smoke-container.sh
# Env: CONTAINER_RUNTIME=podman|docker (default podman), SMOKE_IMAGE, SMOKE_PORT

set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

RT="${CONTAINER_RUNTIME:-podman}"
IMAGE="${SMOKE_IMAGE:-localhost/seek-search:smoke}"
PORT="${SMOKE_PORT:-18080}"

"$RT" build -t "$IMAGE" .
CID=$("$RT" run -d -p "127.0.0.1:${PORT}:5000" "$IMAGE")

cleanup() {
  "$RT" rm -f "$CID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 2
code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:${PORT}/" || echo "000")

if [ "$code" != "200" ]; then
  echo "smoke-container: expected HTTP 200, got $code" >&2
  "$RT" logs "$CID" 2>&1 || true
  exit 1
fi

echo "smoke-container: ok (HTTP $code)"
