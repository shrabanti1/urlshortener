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
