#!/bin/sh
set -e
base="${SEEK_URL:-http://127.0.0.1:8087}"
fail=0

home=$(wget -q -O - "$base/")
if echo "$home" | grep -q 'today-card'; then
  echo "FAIL home still has today card"
  fail=1
else
  echo "ok home"
fi

check() {
  q=$1
  extra=$2
  enc=$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1]))" "$q")
  url="$base/search?q=$enc"
  if [ -n "$extra" ]; then
    url="$url&$extra"
  fi
  code=$(wget -q -S -O /tmp/seek-smoke.html "$url" 2>&1 | awk '/HTTP/{print $2}' | tail -n1)
  if [ "$code" != "200" ]; then
    echo "FAIL $q status=$code"
    fail=1
    return
  fi
  if ! grep -q 'class="result"' /tmp/seek-smoke.html; then
    echo "FAIL $q no results"
    fail=1
    return
  fi
  echo "ok $q"
}

check "linux kernel" ""
sleep 1
check "arch linux pacman" ""
sleep 1
check "privacy" ""
sleep 1
check "rust borrow checker" ""
sleep 1
check "what is a mux" ""
sleep 1
check "debian sid" ""
sleep 1
check "python list comprehension" ""
sleep 1
check "segfault in c" ""
sleep 1
check "stackoverflow golang context cancel" ""

redir=$(wget -q --max-redirect=0 -S -O /dev/null "$base/search" 2>&1 | awk '/HTTP/{print $2}' | tail -n1)
if [ "$redir" != "302" ] && [ "$redir" != "301" ]; then
  echo "FAIL empty search status=$redir"
  fail=1
else
  echo "ok empty search redirect"
fi

img=$(wget -q -S -O /tmp/seek-images-home.html "$base/images" 2>&1 | awk '/HTTP/{print $2}' | tail -n1)
if [ "$img" != "200" ]; then
  echo "FAIL images home status=$img"
  fail=1
elif ! grep -q 'action="/images"' /tmp/seek-images-home.html; then
  echo "FAIL images home missing form"
  fail=1
else
  echo "ok images home"
fi

save=$(wget -q -O - "$base/save_settings?format=json&locale=en_us&engines_present=1&engine_ddg=1&engine_brave=1&default_engine=all&forums_present=1&slop_present=1&slop=off")
if echo "$save" | grep -q '"ok":true'; then
  echo "ok save_settings json"
else
  echo "FAIL save_settings json: $save"
  fail=1
fi

linux=$(wget -q -O - "$base/search?q=linux")
if echo "$linux" | grep -q 'today-card'; then
  echo "FAIL linux search still has today card"
  fail=1
else
  echo "ok linux search has no today card"
fi

news=$(wget -q -O - "$base/news")
if echo "$news" | grep -q 'source-overflow-wrap'; then
  echo "ok news overflow menu"
else
  echo "FAIL news overflow menu missing"
  fail=1
fi

exit $fail
