# Demo script for Windows PowerShell. Run:  .\scripts\demo.ps1
# NOTE: in PowerShell "curl" is an alias for Invoke-WebRequest, so we call curl.exe explicitly.
$proxy = "http://localhost:8080"

function Step($title) { Write-Host "`n=== $title" -ForegroundColor Cyan }

Step "1. Allowed site over plain HTTP (expect the example.com HTML)"
curl.exe -s -x $proxy http://example.com | Select-Object -First 5

Step "2. Blocked site over plain HTTP (expect HTTP 403 and the block page)"
curl.exe -s -i -x $proxy http://example.org | Select-Object -First 12

Step "3. Allowed HTTPS site through a CONNECT tunnel (expect 200)"
curl.exe -s -o NUL -w 'HTTP %{http_code}\n' -x $proxy https://example.com

Step "4. Blocked HTTPS site (expect: CONNECT tunnel failed, response 403)"
curl.exe -sS -o NUL -x $proxy https://facebook.com

Step "5. Longest-match exception: developers.facebook.com is allowed although facebook.com is blocked"
curl.exe -s -o NUL -w 'HTTP %{http_code}\n' -x $proxy https://developers.facebook.com

Step "6. CONNECT to a non-TLS port is refused (port restriction)"
curl.exe -sS -o NUL -x $proxy https://example.com:22

Step "7. SSRF guard: the proxy refuses to reach internal addresses"
curl.exe -s -x $proxy http://localhost:8081/ | Select-String "private/internal"

Write-Host "`nNow open http://localhost:3000 (admin / admin123) and look at the Traffic log." -ForegroundColor Green
