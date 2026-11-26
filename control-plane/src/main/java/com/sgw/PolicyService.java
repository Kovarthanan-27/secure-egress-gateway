package com.sgw;

import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Isolation;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.server.ResponseStatusException;

import java.util.List;
import java.util.Locale;
import java.util.Optional;
import java.util.regex.Pattern;

/**
 * Business rules for policy changes.
 *
 * KEY INVARIANT: every change to rules or default action bumps policy_meta.version in the
 * SAME transaction. A proxy that sees version N therefore sees exactly the rules of version N,
 * never a half-applied change.
 */
@Service
public class PolicyService {

    // labels of a-z, 0-9 and '-', separated by dots (no leading/trailing hyphen per label)
    private static final Pattern DOMAIN =
            Pattern.compile("^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)*$");

    private final PolicyRepository repo;

    public PolicyService(PolicyRepository repo) {
        this.repo = repo;
    }

    public List<Dtos.Rule> listRules() {
        return repo.findAll();
    }

    @Transactional
    public Dtos.Rule create(Dtos.RuleRequest req) {
        Dtos.Rule created = repo.insert(
                normalize(req.pattern()), req.action(), categoryOrDefault(req.category()),
                req.reason() == null ? "" : req.reason().trim(), req.enabled() == null || req.enabled());
        repo.bumpVersion();
        return created;
    }

    @Transactional
    public Dtos.Rule update(long id, Dtos.RuleRequest req) {
        int changed = repo.update(
                id, normalize(req.pattern()), req.action(), categoryOrDefault(req.category()),
                req.reason() == null ? "" : req.reason().trim(), req.enabled() == null || req.enabled());
        if (changed == 0) throw new ResponseStatusException(HttpStatus.NOT_FOUND, "Rule not found");
        repo.bumpVersion();
        return repo.findById(id).orElseThrow();
    }

    @Transactional
    public void delete(long id) {
        if (repo.delete(id) == 0) throw new ResponseStatusException(HttpStatus.NOT_FOUND, "Rule not found");
        repo.bumpVersion();
    }

    @Transactional
    public Dtos.Settings setDefaultAction(String action) {
        repo.setDefaultAction(action);
        repo.bumpVersion();
        return settings();
    }

    @Transactional(readOnly = true)
    public Dtos.Settings settings() {
        return new Dtos.Settings(repo.defaultAction(), repo.version());
    }

    /**
     * What proxies poll. Returns empty when the proxy is already up to date (-> HTTP 304).
     * REPEATABLE_READ gives both queries below one consistent snapshot of the database,
     * even if an admin edits a rule between them.
     */
    @Transactional(readOnly = true, isolation = Isolation.REPEATABLE_READ)
    public Optional<Dtos.PolicySnapshot> snapshotIfNewer(long clientVersion) {
        long current = repo.version();
        if (current == clientVersion) return Optional.empty();
        return Optional.of(new Dtos.PolicySnapshot(current, repo.defaultAction(), repo.findEnabledForProxy()));
    }

    public long activeRuleCount() {
        return repo.countEnabled();
    }

    /** "*.Example.COM." -> "example.com". Rejects anything that is not a plausible domain. */
    static String normalize(String raw) {
        String p = raw.trim().toLowerCase(Locale.ROOT);
        if (p.startsWith("*.")) p = p.substring(2);
        while (p.startsWith(".")) p = p.substring(1);
        while (p.endsWith(".")) p = p.substring(0, p.length() - 1);
        if (p.isEmpty() || p.length() > 253 || !DOMAIN.matcher(p).matches()) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST,
                    "Invalid domain. Use a name like example.com or *.example.com");
        }
        return p;
    }

    private static String categoryOrDefault(String c) {
        return (c == null || c.isBlank()) ? "Uncategorized" : c.trim();
    }
}
