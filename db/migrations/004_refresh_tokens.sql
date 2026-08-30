-- Phase 8 follow-up: revocable refresh tokens.
--
-- An access JWT is stateless and therefore cannot be revoked: logging out or
-- changing a password leaves existing tokens valid until they expire. The fix
-- is a short-lived access token plus a long-lived refresh token that IS stored,
-- so it can be revoked.
--
--   psql -d urlshortener -U urlshortener -f db/migrations/004_refresh_tokens.sql

CREATE TABLE IF NOT EXISTS refresh_tokens (
    id          BIGSERIAL   PRIMARY KEY,
    user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,

    -- SHA-256 of the token, never the token itself. This table is a credential
    -- store: a database leak must not hand out working sessions.
    token_hash  TEXT        NOT NULL UNIQUE,

    -- Set when a token is used (rotation) or explicitly logged out.
    revoked_at  TIMESTAMPTZ,

    -- Rotation chain: which token replaced this one. Lets us detect reuse of
    -- an already-rotated token, which means it was stolen.
    replaced_by BIGINT      REFERENCES refresh_tokens(id) ON DELETE SET NULL,

    expires_at  TIMESTAMPTZ NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- "Revoke every token for this user" (logout-everywhere, password change).
CREATE INDEX IF NOT EXISTS refresh_tokens_user_id_idx
    ON refresh_tokens (user_id);

-- Cleanup of expired rows.
CREATE INDEX IF NOT EXISTS refresh_tokens_expires_at_idx
    ON refresh_tokens (expires_at);
