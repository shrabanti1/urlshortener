-- Phase 4: users and URL ownership.
-- Apply as the app role:
--   psql -d urlshortener -U urlshortener -f db/migrations/002_users_and_ownership.sql

CREATE TABLE IF NOT EXISTS users (
    id            BIGSERIAL    PRIMARY KEY,
    email         TEXT         NOT NULL,
    password_hash TEXT         NOT NULL,
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

-- Case-insensitive uniqueness: Alice@x.com and alice@x.com are one account.
-- A plain UNIQUE(email) would let both exist and allow account hijacking by
-- registering a differently-cased duplicate.
CREATE UNIQUE INDEX IF NOT EXISTS users_email_lower_key
    ON users (LOWER(email));

-- Existing rows keep user_id NULL, so Phase 1-3 anonymous links still resolve.
ALTER TABLE urls
    ADD COLUMN IF NOT EXISTS user_id BIGINT
    REFERENCES users(id) ON DELETE CASCADE;

-- "List my URLs, newest first" is the query GET /api/urls runs.
-- Without this index it would scan the whole table for every request.
CREATE INDEX IF NOT EXISTS urls_user_id_created_at_idx
    ON urls (user_id, created_at DESC);
