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

-- Start ids at 62^3 so short codes are never trivially small.
--
-- Guarded so it is safe to re-run: the migration runner executes this file on
-- every boot, and an unconditional ALTER SEQUENCE would rewind the counter and
-- hand out ids that already exist.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM urls)
       AND (SELECT last_value FROM urls_id_seq) < 238328 THEN
        PERFORM setval('urls_id_seq', 238328, false);
    END IF;
END $$;
