#!/usr/bin/env bash
# Smoke-test the Linamp HTTP API. Usage: ./api-examples.sh [host:port]
set -u
BASE="http://${1:-10.10.0.204:8080}"

check() {
  local path="$1" expect="$2"
  local code
  code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE$path")
  printf "%-40s -> %s (want %s)\n" "$path" "$code" "$expect"
}

check "/api/health"               200
check "/api/clock/list"           200
check "/api/play"                 200
check "/api/pause"                200
check "/api/volume?level=50"      200
check "/api/balance?value=0"      200
check "/api/clock?face=Nixie"     200
check "/api/screensaver/off"      200
check "/api/volume?level=999"     400
check "/api/clock?face=Nope"      400
check "/api/nope"                 404
