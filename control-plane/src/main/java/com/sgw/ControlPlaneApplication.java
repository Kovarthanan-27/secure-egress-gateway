package com.sgw;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableScheduling;

/**
 * Control plane of the Secure Egress Gateway.
 *
 * It owns the policy (rules + version), receives request logs from the proxies and
 * serves the dashboard APIs. It is NOT on the traffic path: if it goes down, the
 * proxies keep enforcing the last policy they downloaded.
 */
@SpringBootApplication
@EnableScheduling
public class ControlPlaneApplication {
    public static void main(String[] args) {
        SpringApplication.run(ControlPlaneApplication.class, args);
    }
}
