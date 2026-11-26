package com.sgw;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

/** The log table is append-only and grows forever unless something prunes it. */
@Component
public class RetentionJob {

    private static final Logger log = LoggerFactory.getLogger(RetentionJob.class);

    private final LogRepository logs;
    private final int retentionDays;

    public RetentionJob(LogRepository logs, @Value("${gateway.log-retention-days}") int retentionDays) {
        this.logs = logs;
        this.retentionDays = retentionDays;
    }

    @Scheduled(initialDelay = 60_000, fixedDelay = 3_600_000)
    public void prune() {
        int removed = logs.deleteOlderThanDays(retentionDays);
        if (removed > 0) log.info("Retention: deleted {} log rows older than {} days", removed, retentionDays);
    }
}
