package com.sgw;

import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.stereotype.Repository;

import java.sql.Timestamp;
import java.sql.Types;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;

/** Write path (batched inserts from proxies) and read path (dashboard analytics). */
@Repository
public class LogRepository {

    private static final RowMapper<Dtos.LogRow> ROW_MAPPER = (rs, i) -> new Dtos.LogRow(
            rs.getLong("id"), rs.getTimestamp("ts").toInstant(), rs.getString("client_ip"),
            rs.getString("host"), rs.getInt("port"), rs.getString("method"), rs.getString("decision"),
            (Long) rs.getObject("rule_id"), rs.getString("rule_pattern"), rs.getString("category"),
            rs.getString("reason"), rs.getLong("policy_version"), rs.getLong("decision_us"),
            rs.getLong("bytes_up"), rs.getLong("bytes_down"), rs.getLong("duration_ms"));

    private final JdbcTemplate jdbc;

    public LogRepository(JdbcTemplate jdbc) {
        this.jdbc = jdbc;
    }

    /** One JDBC batch per call: far fewer round trips than inserting row by row. */
    public void insertBatch(List<Dtos.LogEntry> entries) {
        jdbc.batchUpdate(
                "INSERT INTO request_logs (ts, client_ip, host, port, method, decision, rule_id, rule_pattern, "
                        + "category, reason, policy_version, decision_us, bytes_up, bytes_down, duration_ms) "
                        + "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                entries, 500, (ps, e) -> {
                    long ts = e.ts() > 0 ? e.ts() : System.currentTimeMillis();
                    ps.setTimestamp(1, Timestamp.from(Instant.ofEpochMilli(ts)));
                    ps.setString(2, cut(e.clientIp(), 64));
                    ps.setString(3, cut(e.host(), 255));
                    ps.setInt(4, e.port());
                    ps.setString(5, cut(e.method(), 16));
                    ps.setString(6, e.decision());
                    if (e.ruleId() == null) ps.setNull(7, Types.BIGINT); else ps.setLong(7, e.ruleId());
                    ps.setString(8, cut(e.rulePattern(), 253));
                    ps.setString(9, cut(e.category(), 64));
                    ps.setString(10, cut(e.reason(), 512));
                    ps.setLong(11, e.policyVersion());
                    ps.setLong(12, e.decisionUs());
                    ps.setLong(13, e.bytesUp());
                    ps.setLong(14, e.bytesDown());
                    ps.setLong(15, e.durationMs());
                });
    }

    public List<Dtos.LogRow> search(int limit, String decision, String hostContains) {
        StringBuilder sql = new StringBuilder(
                "SELECT id, ts, client_ip, host, port, method, decision, rule_id, rule_pattern, category, reason, "
                        + "policy_version, decision_us, bytes_up, bytes_down, duration_ms FROM request_logs WHERE 1=1");
        List<Object> args = new ArrayList<>();
        if (decision != null) {
            sql.append(" AND decision = ?");
            args.add(decision);
        }
        if (hostContains != null && !hostContains.isBlank()) {
            sql.append(" AND host ILIKE ?");  // user input is bound as a parameter, never concatenated
            args.add("%" + escapeLike(hostContains.trim()) + "%");
        }
        sql.append(" ORDER BY ts DESC, id DESC LIMIT ?");
        args.add(limit);
        return jdbc.query(sql.toString(), ROW_MAPPER, args.toArray());
    }

    public Dtos.Summary summary(int minutes, long policyVersion, String defaultAction, long activeRules) {
        return jdbc.queryForObject(
                "SELECT count(*) AS total, "
                        + "count(*) FILTER (WHERE decision = 'ALLOW') AS allowed, "
                        + "count(*) FILTER (WHERE decision = 'BLOCK') AS blocked, "
                        + "coalesce(avg(decision_us), 0) AS avg_us "
                        + "FROM request_logs WHERE ts >= now() - (? * interval '1 minute')",
                (rs, i) -> new Dtos.Summary(rs.getLong("total"), rs.getLong("allowed"), rs.getLong("blocked"),
                        rs.getDouble("avg_us"), policyVersion, defaultAction, activeRules),
                minutes);
    }

    /** Per-minute counts with empty minutes filled by generate_series, so the chart has no gaps. */
    public List<Dtos.Bucket> timeseries(int minutes) {
        return jdbc.query(
                "SELECT g.minute, coalesce(c.allowed, 0) AS allowed, coalesce(c.blocked, 0) AS blocked "
                        + "FROM generate_series(date_trunc('minute', now()) - (? * interval '1 minute'), "
                        + "                     date_trunc('minute', now()), interval '1 minute') AS g(minute) "
                        + "LEFT JOIN ( "
                        + "  SELECT date_trunc('minute', ts) AS minute, "
                        + "         count(*) FILTER (WHERE decision = 'ALLOW') AS allowed, "
                        + "         count(*) FILTER (WHERE decision = 'BLOCK') AS blocked "
                        + "  FROM request_logs "
                        + "  WHERE ts >= date_trunc('minute', now()) - (? * interval '1 minute') "
                        + "  GROUP BY 1) c ON c.minute = g.minute "
                        + "ORDER BY g.minute",
                (rs, i) -> new Dtos.Bucket(rs.getTimestamp("minute").toInstant(),
                        rs.getLong("allowed"), rs.getLong("blocked")),
                minutes, minutes);
    }

    public List<Dtos.HostCount> topBlocked(int minutes, int limit) {
        return jdbc.query(
                "SELECT host, count(*) AS c FROM request_logs "
                        + "WHERE decision = 'BLOCK' AND ts >= now() - (? * interval '1 minute') "
                        + "GROUP BY host ORDER BY c DESC, host LIMIT ?",
                (rs, i) -> new Dtos.HostCount(rs.getString("host"), rs.getLong("c")),
                minutes, limit);
    }

    public int deleteOlderThanDays(int days) {
        return jdbc.update("DELETE FROM request_logs WHERE ts < now() - (? * interval '1 day')", days);
    }

    private static String cut(String s, int max) {
        if (s == null) return null;
        return s.length() <= max ? s : s.substring(0, max);
    }

    private static String escapeLike(String s) {
        return s.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
    }
}
