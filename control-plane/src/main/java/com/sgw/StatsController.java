package com.sgw;

import org.springframework.http.HttpStatus;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;

import java.util.List;
import java.util.Map;

/** Read-only analytics for the dashboard. */
@RestController
@RequestMapping("/api")
public class StatsController {

    private final LogRepository logs;
    private final PolicyService policy;

    public StatsController(LogRepository logs, PolicyService policy) {
        this.logs = logs;
        this.policy = policy;
    }

    /** The dashboard calls this to validate the login (the AuthFilter already did the real check). */
    @GetMapping("/auth/check")
    public Map<String, Boolean> authCheck() {
        return Map.of("ok", true);
    }

    @GetMapping("/stats/summary")
    public Dtos.Summary summary(@RequestParam(defaultValue = "60") int minutes) {
        Dtos.Settings s = policy.settings();
        return logs.summary(clamp(minutes, 1, 1440), s.policyVersion(), s.defaultAction(), policy.activeRuleCount());
    }

    @GetMapping("/stats/timeseries")
    public List<Dtos.Bucket> timeseries(@RequestParam(defaultValue = "60") int minutes) {
        return logs.timeseries(clamp(minutes, 1, 240));
    }

    @GetMapping("/stats/top-blocked")
    public List<Dtos.HostCount> topBlocked(@RequestParam(defaultValue = "60") int minutes,
                                           @RequestParam(defaultValue = "8") int limit) {
        return logs.topBlocked(clamp(minutes, 1, 1440), clamp(limit, 1, 50));
    }

    @GetMapping("/logs")
    public List<Dtos.LogRow> logs(@RequestParam(defaultValue = "100") int limit,
                                  @RequestParam(required = false) String decision,
                                  @RequestParam(required = false) String q) {
        String d = null;
        if (decision != null && !decision.isBlank()) {
            d = decision.toUpperCase();
            if (!d.equals("ALLOW") && !d.equals("BLOCK")) {
                throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "decision must be ALLOW or BLOCK");
            }
        }
        return logs.search(clamp(limit, 1, 500), d, q);
    }

    private static int clamp(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }
}
