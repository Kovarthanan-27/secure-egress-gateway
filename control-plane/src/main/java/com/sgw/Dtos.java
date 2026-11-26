package com.sgw;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Pattern;
import jakarta.validation.constraints.Size;

import java.time.Instant;
import java.util.List;

/** All request/response shapes in one place (Java records = immutable data carriers). */
public final class Dtos {
    private Dtos() {}

    // ---- policy management (dashboard <-> control plane) -------------------------------
    public record Rule(long id, String pattern, String action, String category, String reason,
                       boolean enabled, Instant createdAt, Instant updatedAt) {}

    public record RuleRequest(
            @NotBlank @Size(max = 253) String pattern,
            @NotBlank @Pattern(regexp = "ALLOW|BLOCK", message = "must be ALLOW or BLOCK") String action,
            @Size(max = 64) String category,
            @Size(max = 255) String reason,
            Boolean enabled) {}

    public record DefaultActionRequest(
            @NotBlank @Pattern(regexp = "ALLOW|BLOCK", message = "must be ALLOW or BLOCK") String action) {}

    public record Settings(String defaultAction, long policyVersion) {}

    // ---- policy distribution (control plane -> proxies) --------------------------------
    public record ProxyRule(long id, String pattern, String action, String category, String reason) {}

    public record PolicySnapshot(long version, String defaultAction, List<ProxyRule> rules) {}

    // ---- log ingestion (proxies -> control plane) --------------------------------------
    public record LogEntry(long ts, String clientIp, String host, int port, String method, String decision,
                           Long ruleId, String rulePattern, String category, String reason,
                           long policyVersion, long decisionUs, long bytesUp, long bytesDown, long durationMs) {}

    // ---- analytics (control plane -> dashboard) ----------------------------------------
    public record LogRow(long id, Instant ts, String clientIp, String host, int port, String method,
                         String decision, Long ruleId, String rulePattern, String category, String reason,
                         long policyVersion, long decisionUs, long bytesUp, long bytesDown, long durationMs) {}

    public record Summary(long total, long allowed, long blocked, double avgDecisionUs,
                          long policyVersion, String defaultAction, long activeRules) {}

    public record Bucket(Instant minute, long allowed, long blocked) {}

    public record HostCount(String host, long count) {}
}
