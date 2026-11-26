# Secure Egress Gateway

A small but real **secure web gateway**: a C++ forward proxy that sits between users and the internet,
enforces a domain policy on live traffic, and reports every decision to a control plane with a dashboard.

Point a browser or `curl` at it and it genuinely allows or blocks traffic. Nothing is simulated.

## The problem it solves

Organisations need to control and audit where their machines send traffic: block malicious or
non-business sites, make exceptions for specific teams, and answer "who tried to reach what, and
why was it blocked?". This project implements the core of that: **policy enforcement on the data path**,
**central policy management**, and **audit logging**.

## Architecture

```
                     DATA PLANE (hot path)                    CONTROL PLANE (cold path)

 Browser / curl ──►  C++ proxy  ─────────────────────────►  Internet
  (HTTP proxy       │  accept loop + thread pool
   localhost:8080)  │  policy engine: trie + LRU cache
                    │  SSRF guard, port restrictions
                    │
        policy poll │ ▲ every 5s: "newer than vN?"          ┌────────────────────┐
        (304 / 200) │ │                                     │ Spring Boot API    │
                    ▼ │                                     │  rules, versions,  │──► PostgreSQL
                    └─┴──── async batched logs ───────────► │  log ingestion,    │     rules
                                                            │  analytics         │     policy_meta
                                                            └─────────▲──────────┘     request_logs
                                                                      │ /api (same origin via nginx)
                                                            ┌─────────┴──────────┐
                                                            │ React + TS         │
                                                            │ dashboard          │
                                                            └────────────────────┘
```

**The key idea is the split between data plane and control plane.** The proxy never calls the
database or the API while handling a request. It decides locally, in microseconds, from an
in-memory compiled policy. The control plane is only reached in the background. If it goes down, traffic keeps
flowing and the last policy keeps being enforced.

| Part | Tech | Responsibility |
|---|---|---|
| `proxy/` | C++17, POSIX sockets, STL | Forward proxy, policy engine, decision cache, log shipping |
| `control-plane/` | Java 17, Spring Boot 3, JDBC | Policy CRUD + versioning, log ingestion, stats APIs, auth |
| `db/init.sql` | PostgreSQL 16 | Rules, policy version, append-only request log |
| `dashboard/` | React 18, TypeScript, Vite, nginx | Live traffic, log search, policy editor |
| `proxy/static/blockpage.html` | HTML, CSS, JS | Page shown to the user when a request is blocked |

## Quick start (Windows + Docker Desktop)

1. Start Docker Desktop.
2. In this folder, run:
   ```
   docker compose up --build
   ```
   The first build takes several minutes (Maven and apt downloads). Later starts take seconds.
3. Open the dashboard at **http://localhost:3000** and sign in with `admin` / `admin123`.
4. In a second terminal run the demo:
   ```
   powershell -ExecutionPolicy Bypass -File scripts\demo.ps1
   ```
   (Linux/macOS/Git Bash: `bash scripts/demo.sh`)
5. Watch the requests appear on the dashboard's **Traffic log** tab.
6. Use the proxy from your browser: set an HTTP proxy of `localhost` port `8080` (Windows: Settings, Network,
   Proxy, Manual proxy setup). Visit `http://example.org` to see the block page.

Stop with `Ctrl+C`; reset everything including data with `docker compose down -v`.

> **PowerShell tip:** `curl` is an alias for `Invoke-WebRequest` there. Always type `curl.exe`.

| URL | What |
|---|---|
| http://localhost:3000 | Dashboard |
| localhost:8080 | The proxy itself (use it as an HTTP proxy) |
| http://localhost:8081 | Control plane API (needs credentials) |

Look at the database: `docker compose exec postgres psql -U sgw -d sgw -c "select * from rules;"`

## What happens to one request

1. The client sends `CONNECT example.com:443` (HTTPS) or `GET http://example.org/ ...` (plain HTTP).
2. A worker thread from the pool reads and parses the request line and headers (10 s timeout, 16 KB cap).
3. **Policy decision.** The worker takes a `shared_ptr` to the current immutable policy snapshot, checks that
   snapshot's LRU cache, and on a miss walks the domain trie from the TLD downwards. The **deepest matching
   rule wins**, so `developers.facebook.com` (allow) beats `facebook.com` (block).
4. Extra gateway guards: `CONNECT` is only allowed to ports 443/8443, and after DNS resolution the destination
   IP must not be private or loopback (SSRF protection).
5. **Blocked:** the proxy answers `403` with the HTML block page (rule, category, reason, request ID). **Allowed:**
   it connects upstream with a timeout and relays bytes in both directions using `poll()`.
6. A log entry (decision, rule, reason, policy version, decision time in microseconds, bytes) goes onto an in-memory
   queue. A background thread ships batches to the control plane.

For HTTPS the proxy only sees the hostname in the `CONNECT` line, never the encrypted content. That is why
decisions are domain-based (see limitations).

## Design decisions and trade-offs

| Decision | Why | Trade-off |
|---|---|---|
| Separate data plane and control plane | Proxy latency and availability do not depend on the DB or API | Policy changes take up to `POLL_SECONDS` to apply |
| Proxies **pull** policy | No inbound port on proxies, works behind NAT, scales as N independent pollers, self-heals after outages | Not instant; a push channel could be added for urgent changes |
| Immutable policy snapshot swapped via `shared_ptr` | Request threads never take a write lock; in-flight requests finish on the policy they started with | Rebuilds the whole trie per change (fine for thousands of rules) |
| Per-snapshot LRU cache | New snapshot means fresh cache, so there is no invalidation logic to get wrong | Cache is cold after each policy change |
| Trie of reversed labels | Lookup is O(labels), independent of rule count; longest-suffix match is natural | Domain-only matching (no URL paths or regex) |
| Policy version bumped in the same DB transaction as the change | A proxy that sees version N sees exactly the rules of version N | One hot row (`policy_meta`) serialises writers; fine for admin-rate changes |
| `REPEATABLE_READ` when building the snapshot | Rules and default action are read from one consistent view | Slightly more overhead than read committed |
| Batched, async log shipping with a bounded queue | Request path never waits on I/O; memory is bounded during outages | Logs can be dropped after a very long outage (counted and printed at shutdown) |
| Allowed traffic is logged when the connection closes | Byte counts and duration are only known then | A long-lived tunnel appears late. A production system would log start and end events |
| Thread-per-connection with a bounded pool | Simple, easy to reason about and explain | Does not scale to 100k connections. See "What I would do next" |
| `poll()` for relaying | Portable and simple for two file descriptors per thread | `epoll`/`io_uring` event loops are better at scale |
| Plain JDBC instead of JPA | The SQL is the interesting part; fewer moving parts | More boilerplate than an ORM |
| No FK from `request_logs.rule_id` to `rules` | Deleting a rule must not rewrite history; faster inserts | Possible dangling ids (intended) |

## Failure behaviour

| Failure | What happens |
|---|---|
| Control plane down | Proxy keeps enforcing the last policy; logs queue up (bounded) and are retried |
| Proxy restarts while control plane is down | Last policy is reloaded from `/data/policy-cache.json` (written atomically) |
| Proxy has never had any policy | `FAIL_MODE`: `closed` (default) blocks, `open` allows |
| Database down | Control plane returns errors; proxies are unaffected |
| Upstream unreachable | Client gets `502`; the decision is still logged |
| Too many connections | Queue is bounded, extra clients get `503` immediately (back-pressure) |
| Slow or malicious client | Header read has a 10 s timeout and 16 KB cap; idle tunnels are closed after 60 s |

## Security considerations

* **SSRF guard:** the destination IP is checked *after* DNS resolution, so a public name that resolves to
  `10.x.x.x` (DNS rebinding style) is still refused. It blocks loopback, RFC 1918, link-local (cloud metadata
  `169.254.x.x`) and carrier-grade NAT ranges.
* **Port restriction on CONNECT** stops the proxy being used to tunnel to SSH, SMTP, databases, etc.
* **Request parsing is strict:** hostnames are character-whitelisted, `user@host` tricks use the real host, and
  the host is HTML-escaped in the block page (otherwise a crafted hostname would be an XSS vector).
* **Hop-by-hop headers** (`Proxy-Authorization`, `Connection`, ...) are stripped before forwarding.
* **Two credentials for two trust domains:** `X-API-Key` for machines, HTTP Basic for the admin, both compared
  in constant time. Log ingestion validates and truncates every field. Search input is a bound parameter.
* **Non-root container** for the proxy; secrets come from environment variables.

## Limitations (deliberate, so the scope fits a one-day build)

* **No TLS interception.** HTTPS is filtered by hostname only. Real gateways inspect traffic by deploying a
  trusted CA, which brings large privacy, legal and operational complexity.
* Domain rules only (no URL paths, categories feeds, malware sandboxing, DLP).
* Single proxy instance in the compose file (the design supports many; see below).
* Basic auth and demo credentials; one admin account; no audit trail of who changed which rule.
* No client authentication on the proxy itself (anyone who can reach port 8080 can use it).
* Plain HTTP connections are closed after each response (`Connection: close`), so no keep-alive.
* No IPv6 listener; IPv6 destinations are supported.

## What I would do next

1. Replace thread-per-connection with an `epoll` event loop (or worker threads each running an event loop).
2. Proxy authentication and per-user or per-group policy (tie requests to identity).
3. Partition `request_logs` by day; ship logs through a queue (Kafka) into a column store for analytics.
4. Push-based policy invalidation (long polling or gRPC stream) alongside polling as a fallback.
5. Metrics endpoint (Prometheus): connections, decision latency histogram, cache hit rate, dropped logs.
6. Signed policy snapshots so a proxy can verify policy integrity.
7. Integration tests that start the proxy and a fake upstream; fuzz the request parser.

## How it scales

* **Proxies are stateless** apart from a disk cache of policy. Run N behind an L4 load balancer or anycast.
* **Policy distribution cost** is one tiny conditional GET per proxy every few seconds (HTTP 304 most of the time).
  The control plane can sit behind a cache or CDN for large fleets.
* **Writes:** log ingestion is batched (up to 500 per request, one JDBC batch). For higher volume, put a queue in front
  and partition the table.
* **Reads:** dashboard queries are time-bounded and index-backed (`ts`, `(decision, ts)`, `host`).

## Project layout (suggested reading order)

```
proxy/src/
  domain_trie.hpp      1  longest-suffix domain matching
  lru_cache.hpp        2  O(1) thread-safe cache
  policy.hpp           3  immutable snapshot + atomic swap
  net.cpp              4  connect with timeout, SSRF guard, poll() relay
  proxy_server.cpp     5  request parsing, decisions, tunnelling, block page
  log_shipper.hpp      6  bounded async batching
  policy_syncer.hpp    7  polling + disk cache
  http_client.cpp      8  tiny HTTP client (chunked decoding)
  thread_pool.hpp      9  bounded worker pool
  main.cpp            10  wiring and graceful shutdown
control-plane/src/main/java/com/sgw/
  PolicyService, PolicyRepository   policy + versioning (transactional)
  ProxyController                   what the proxies call
  LogRepository                     batch insert + analytics SQL
  AuthFilter                        the two credentials
db/init.sql                         schema and indexes (with comments)
dashboard/src/                      App, api.ts, components/
```

## Configuration

Proxy (environment variables): `PROXY_PORT` (8080), `CONTROL_PLANE_HOST`, `CONTROL_PLANE_PORT`, `PROXY_API_KEY`,
`WORKERS` (256), `MAX_QUEUED` (256), `POLL_SECONDS` (5), `FAIL_MODE` (`closed`|`open`), `BLOCK_PRIVATE_RANGES`
(`true`), `CONNECT_PORTS` (`443,8443`).
Control plane: `DB_URL`, `DB_USER`, `DB_PASSWORD`, `PROXY_API_KEY`, `ADMIN_USER`, `ADMIN_PASSWORD`, `LOG_RETENTION_DAYS` (7).

## Troubleshooting

* **Ports already in use:** change the left side of the port mappings in `docker-compose.yml`
  (`8080:8080`, `3000:80`, `8081:8081`).
* **Dashboard says "Wrong username or password":** credentials are `admin` / `admin123` unless you changed the compose file.
* **`docker compose up` is slow the first time:** Maven and apt download dependencies once, then they are cached.
* **Proxy log shows "control plane not reachable yet":** normal for a few seconds while Spring Boot starts. It retries.
* **Browser says the proxy refuses connections:** blocked HTTPS sites show as a proxy error (the browser cannot render a body
  returned to a `CONNECT`). Plain HTTP blocks show the block page. Check the Traffic log for the reason.
* **A site works in the browser but is not in the log:** browsers may bypass the proxy for `localhost` or use a system proxy
  exception list.
* **Logs for long-lived HTTPS tunnels appear late:** by design, allowed connections are logged when they close.
