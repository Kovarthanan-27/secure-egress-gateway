package com.sgw;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Base64;

/**
 * Two separate trust domains, two separate credentials:
 *  - /api/proxy/**  -> machine-to-machine, shared secret in the X-API-Key header
 *  - every other /api/** -> human admin, HTTP Basic credentials
 * Comparisons are constant-time so response timing cannot leak how many characters matched.
 */
@Component
public class AuthFilter extends OncePerRequestFilter {

    private final byte[] proxyKey;
    private final byte[] adminHeader;

    public AuthFilter(@Value("${gateway.proxy-api-key}") String proxyKey,
                      @Value("${gateway.admin-user}") String adminUser,
                      @Value("${gateway.admin-password}") String adminPassword) {
        this.proxyKey = proxyKey.getBytes(StandardCharsets.UTF_8);
        String basic = Base64.getEncoder()
                .encodeToString((adminUser + ":" + adminPassword).getBytes(StandardCharsets.UTF_8));
        this.adminHeader = ("Basic " + basic).getBytes(StandardCharsets.UTF_8);
    }

    @Override
    protected boolean shouldNotFilter(HttpServletRequest request) {
        return !request.getRequestURI().startsWith("/api/");  // health checks etc. stay open
    }

    @Override
    protected void doFilterInternal(HttpServletRequest req, HttpServletResponse res, FilterChain chain)
            throws ServletException, IOException {
        boolean proxyCall = req.getRequestURI().startsWith("/api/proxy/");
        boolean authorized = proxyCall
                ? same(proxyKey, req.getHeader("X-API-Key"))
                : same(adminHeader, req.getHeader("Authorization"));

        if (!authorized) {
            // Deliberately no WWW-Authenticate header: it would make browsers pop up their own login box.
            res.setStatus(HttpServletResponse.SC_UNAUTHORIZED);
            res.setContentType("application/json");
            res.getWriter().write("{\"error\":\"unauthorized\"}");
            return;
        }
        chain.doFilter(req, res);
    }

    private static boolean same(byte[] expected, String provided) {
        byte[] given = provided == null ? new byte[0] : provided.getBytes(StandardCharsets.UTF_8);
        return MessageDigest.isEqual(expected, given);
    }
}
