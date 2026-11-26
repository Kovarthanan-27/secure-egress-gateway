package com.sgw;

import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;

import java.util.List;
import java.util.Map;
import java.util.Optional;

/** Machine-to-machine API used by the C++ proxies (authenticated with X-API-Key). */
@RestController
@RequestMapping("/api/proxy")
public class ProxyController {

    private static final int MAX_BATCH = 1000;

    private final PolicyService policy;
    private final LogRepository logs;

    public ProxyController(PolicyService policy, LogRepository logs) {
        this.policy = policy;
        this.logs = logs;
    }

    /** Conditional fetch: 304 when the proxy is current, 200 + full snapshot when it is not. */
    @GetMapping("/policy")
    public ResponseEntity<Dtos.PolicySnapshot> policy(@RequestParam(defaultValue = "-1") long version) {
        Optional<Dtos.PolicySnapshot> snapshot = policy.snapshotIfNewer(version);
        return snapshot.map(ResponseEntity::ok)
                .orElseGet(() -> ResponseEntity.status(HttpStatus.NOT_MODIFIED).build());
    }

    /** Batched ingestion. Never trust the sender: validate the batch before it reaches the database. */
    @PostMapping("/logs")
    public ResponseEntity<Map<String, Object>> ingest(@RequestBody List<Dtos.LogEntry> batch) {
        if (batch.size() > MAX_BATCH) {
            throw new ResponseStatusException(HttpStatus.PAYLOAD_TOO_LARGE, "Batch too large (max " + MAX_BATCH + ")");
        }
        List<Dtos.LogEntry> valid = batch.stream()
                .filter(e -> e != null && e.host() != null && !e.host().isBlank() && e.method() != null
                        && ("ALLOW".equals(e.decision()) || "BLOCK".equals(e.decision()))
                        && e.port() >= 0 && e.port() <= 65535)
                .toList();
        if (!valid.isEmpty()) logs.insertBatch(valid);
        return ResponseEntity.accepted().body(Map.of("accepted", valid.size(), "rejected", batch.size() - valid.size()));
    }
}
