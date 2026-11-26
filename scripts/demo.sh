#!/usr/bin/env bash
# Demo script for Linux/macOS/Git Bash. Run:  bash scripts/demo.sh
proxy="http://localhost:8080"
step() { printf '\n=== %s\n' "$1"; }

step "1. Allowed site over plain HTTP (expect the example.com HTML)"
curl -s -x "$proxy" http://example.com | head -5

step "2. Blocked site over plain HTTP (expect HTTP 403 and the block page)"
curl -s -i -x "$proxy" http://example.org | head -12

step "3. Allowed HTTPS site through a CONNECT tunnel (expect 200)"
curl -s -o /dev/null -w 'HTTP %{http_code}\n' -x "$proxy" https://example.com

step "4. Blocked HTTPS site (expect: CONNECT tunnel failed, response 403)"
curl -sS -o /dev/null -x "$proxy" https://facebook.com

step "5. Longest-match exception: developers.facebook.com allowed although facebook.com is blocked"
curl -s -o /dev/null -w 'HTTP %{http_code}\n' -x "$proxy" https://developers.facebook.com

step "6. CONNECT to a non-TLS port is refused (port restriction)"
curl -sS -o /dev/null -x "$proxy" https://example.com:22

step "7. SSRF guard: the proxy refuses to reach internal addresses"
curl -s -x "$proxy" http://localhost:8081/ | grep -o "private/internal[^<]*"

printf '\nNow open http://localhost:3000 (admin / admin123) and look at the Traffic log.\n'
