package com.sgw;

import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;

/** SQL for rules and the policy version row. Plain JDBC: the SQL is the point, no ORM magic to explain. */
@Repository
public class PolicyRepository {

    private static final String RULE_COLUMNS =
            "id, pattern, action, category, reason, enabled, created_at, updated_at";

    private static final RowMapper<Dtos.Rule> RULE_MAPPER = (rs, i) -> new Dtos.Rule(
            rs.getLong("id"), rs.getString("pattern"), rs.getString("action"),
            rs.getString("category"), rs.getString("reason"), rs.getBoolean("enabled"),
            rs.getTimestamp("created_at").toInstant(), rs.getTimestamp("updated_at").toInstant());

    private final JdbcTemplate jdbc;

    public PolicyRepository(JdbcTemplate jdbc) {
        this.jdbc = jdbc;
    }

    public List<Dtos.Rule> findAll() {
        return jdbc.query("SELECT " + RULE_COLUMNS + " FROM rules ORDER BY pattern", RULE_MAPPER);
    }

    public Optional<Dtos.Rule> findById(long id) {
        List<Dtos.Rule> rows = jdbc.query("SELECT " + RULE_COLUMNS + " FROM rules WHERE id = ?", RULE_MAPPER, id);
        return rows.stream().findFirst();
    }

    public Dtos.Rule insert(String pattern, String action, String category, String reason, boolean enabled) {
        return jdbc.queryForObject(
                "INSERT INTO rules (pattern, action, category, reason, enabled) VALUES (?, ?, ?, ?, ?) "
                        + "RETURNING " + RULE_COLUMNS,
                RULE_MAPPER, pattern, action, category, reason, enabled);
    }

    public int update(long id, String pattern, String action, String category, String reason, boolean enabled) {
        return jdbc.update(
                "UPDATE rules SET pattern = ?, action = ?, category = ?, reason = ?, enabled = ?, "
                        + "updated_at = now() WHERE id = ?",
                pattern, action, category, reason, enabled, id);
    }

    public int delete(long id) {
        return jdbc.update("DELETE FROM rules WHERE id = ?", id);
    }

    public List<Dtos.ProxyRule> findEnabledForProxy() {
        return jdbc.query("SELECT id, pattern, action, category, reason FROM rules WHERE enabled ORDER BY id",
                (rs, i) -> new Dtos.ProxyRule(rs.getLong("id"), rs.getString("pattern"), rs.getString("action"),
                        rs.getString("category"), rs.getString("reason")));
    }

    public long countEnabled() {
        Long n = jdbc.queryForObject("SELECT count(*) FROM rules WHERE enabled", Long.class);
        return n == null ? 0 : n;
    }

    // ---- policy_meta (single row) ------------------------------------------------------
    public long version() {
        Long v = jdbc.queryForObject("SELECT version FROM policy_meta WHERE id = 1", Long.class);
        return v == null ? 0 : v;
    }

    public String defaultAction() {
        return jdbc.queryForObject("SELECT default_action FROM policy_meta WHERE id = 1", String.class);
    }

    public void bumpVersion() {
        jdbc.update("UPDATE policy_meta SET version = version + 1, updated_at = now() WHERE id = 1");
    }

    public void setDefaultAction(String action) {
        jdbc.update("UPDATE policy_meta SET default_action = ? WHERE id = 1", action);
    }
}
