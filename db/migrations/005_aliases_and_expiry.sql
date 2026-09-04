-- Custom aliases and link expiry.
--
-- Aliases need no new column: short_code already exists and is UNIQUE, which
-- is exactly the guarantee an alias needs. is_custom only records how the code
-- came about, so generated codes can be retried on collision while a custom
-- alias reports 409 to the user.

ALTER TABLE urls
    ADD COLUMN IF NOT EXISTS is_custom BOOLEAN NOT NULL DEFAULT FALSE;

-- NULL means "never expires", which is the behaviour every existing row had.
ALTER TABLE urls
    ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ;

-- The cleanup job scans for expired rows. A partial index keeps it tiny: rows
-- that never expire are not indexed at all.
CREATE INDEX IF NOT EXISTS urls_expires_at_idx
    ON urls (expires_at)
    WHERE expires_at IS NOT NULL;
