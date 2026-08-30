-- Phase 5: click analytics.
--   psql -d urlshortener -U urlshortener -f db/migrations/003_click_events.sql

CREATE TABLE IF NOT EXISTS click_events (
    id         BIGSERIAL   PRIMARY KEY,
    url_id     BIGINT      NOT NULL REFERENCES urls(id) ON DELETE CASCADE,
    clicked_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- All optional: a browser may send none of these.
    referrer   TEXT,
    user_agent TEXT,

    -- NOT the raw IP. A salted hash: good enough to count unique-ish visitors,
    -- but not reversible to an address. See src/utils/IpHash.cpp.
    ip_hash    TEXT
);

-- Every analytics query filters by url_id and a time range, so this composite
-- index serves all of them. clicked_at DESC matches "most recent first".
CREATE INDEX IF NOT EXISTS click_events_url_id_clicked_at_idx
    ON click_events (url_id, clicked_at DESC);
