-- Secure Egress Gateway - database schema (runs once, on first start of the postgres container).

-- Single-row table: the "version" of the whole policy. Every rule change bumps it in the
-- SAME transaction as the change, so proxies can ask "anything newer than version N?" cheaply.
CREATE TABLE policy_meta (
    id             SMALLINT PRIMARY KEY CHECK (id = 1),
    version        BIGINT      NOT NULL DEFAULT 1,
    default_action VARCHAR(5)  NOT NULL DEFAULT 'ALLOW' CHECK (default_action IN ('ALLOW', 'BLOCK')),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);
INSERT INTO policy_meta (id) VALUES (1);

CREATE TABLE rules (
    id         BIGSERIAL PRIMARY KEY,
    pattern    VARCHAR(253) NOT NULL UNIQUE,        -- normalised: lower-case, no "*." prefix
    action     VARCHAR(5)   NOT NULL CHECK (action IN ('ALLOW', 'BLOCK')),
    category   VARCHAR(64)  NOT NULL DEFAULT 'Uncategorized',
    reason     VARCHAR(255) NOT NULL DEFAULT '',
    enabled    BOOLEAN      NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ  NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- Append-only event log written by the proxies (in batches).
-- rule_id has NO foreign key on purpose: deleting a rule must not rewrite history,
-- and a FK check on every insert would slow the write path for no benefit.
CREATE TABLE request_logs (
    id             BIGSERIAL PRIMARY KEY,
    ts             TIMESTAMPTZ  NOT NULL,
    client_ip      VARCHAR(64),
    host           VARCHAR(255) NOT NULL,
    port           INTEGER      NOT NULL,
    method         VARCHAR(16)  NOT NULL,
    decision       VARCHAR(5)   NOT NULL CHECK (decision IN ('ALLOW', 'BLOCK')),
    rule_id        BIGINT,
    rule_pattern   VARCHAR(253),
    category       VARCHAR(64),
    reason         VARCHAR(512),
    policy_version BIGINT,
    decision_us    BIGINT,
    bytes_up       BIGINT       NOT NULL DEFAULT 0,
    bytes_down     BIGINT       NOT NULL DEFAULT 0,
    duration_ms    BIGINT       NOT NULL DEFAULT 0
);
-- Dashboards always ask "latest N" or "last X minutes" -> index on time (newest first).
CREATE INDEX idx_logs_ts          ON request_logs (ts DESC);
-- "show me only BLOCK events, newest first"
CREATE INDEX idx_logs_decision_ts ON request_logs (decision, ts DESC);
-- "top blocked domains" and host search
CREATE INDEX idx_logs_host        ON request_logs (host);

-- Seed rules so the demo works immediately.
INSERT INTO rules (pattern, action, category, reason) VALUES
    ('example.org',            'BLOCK', 'Demo',         'Demo rule: blocked so you can see the block page'),
    ('facebook.com',           'BLOCK', 'Social Media', 'Social media is not permitted on the corporate network'),
    ('developers.facebook.com','ALLOW', 'Developer',    'Exception: engineers need the Facebook developer docs'),
    ('instagram.com',          'BLOCK', 'Social Media', 'Social media is not permitted on the corporate network'),
    ('tiktok.com',             'BLOCK', 'Social Media', 'Social media is not permitted on the corporate network'),
    ('doubleclick.net',        'BLOCK', 'Ads & Tracking','Advertising and tracking network'),
    ('googlesyndication.com',  'BLOCK', 'Ads & Tracking','Advertising and tracking network'),
    ('bet365.com',             'BLOCK', 'Gambling',     'Gambling sites are blocked by policy');
UPDATE policy_meta SET version = 2 WHERE id = 1;  -- seed counts as a policy change
