-- Phase 1 schema for the URL shortener.
-- Apply with:
--   psql -d urlshortener -U urlshortener -f db/schema.sql
--
-- Apply as the urlshortener role, NOT as a superuser: the creating role
-- owns the table and its sequence, and the app can only use what it owns.

CREATE TABLE IF NOT EXISTS urls (
    id           BIGSERIAL    PRIMARY KEY,
    original_url TEXT         NOT NULL,
    short_code   VARCHAR(16)  NOT NULL UNIQUE,
    created_at   TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

-- Start ids at 62^3 so the first short code is "1000" rather than "1".
-- Codes stay 4 characters until id reaches 62^4 (14,776,336).
-- NOTE: this only makes codes look less trivial. It does NOT hide the fact
-- that ids are sequential; see README for the tradeoff.
ALTER SEQUENCE urls_id_seq RESTART WITH 238328;
