#!/usr/bin/env bash
# Nightly PostgreSQL backup. A volume is not a backup: it protects against a
# container being removed, not against a bad migration, a dropped table, or
# the whole server disappearing.
set -euo pipefail

BACKUP_DIR="${BACKUP_DIR:-/var/backups/urlshortener}"
RETAIN_DAYS="${RETAIN_DAYS:-14}"
DB_USER="${DB_USER:-urlshortener}"
DB_NAME="${DB_NAME:-urlshortener}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="${BACKUP_DIR}/${DB_NAME}-${STAMP}.sql.gz"

mkdir -p "$BACKUP_DIR"

# --clean --if-exists makes the dump safe to restore over an existing database.
docker compose exec -T postgres \
    pg_dump -U "$DB_USER" -d "$DB_NAME" --clean --if-exists \
    | gzip -9 > "$OUT"

# A zero-byte or truncated dump is worse than no dump, because it looks fine.
if [ ! -s "$OUT" ] || ! gzip -t "$OUT" 2>/dev/null; then
    echo "BACKUP FAILED: ${OUT} is empty or corrupt" >&2
    rm -f "$OUT"
    exit 1
fi

find "$BACKUP_DIR" -name "${DB_NAME}-*.sql.gz" -mtime "+${RETAIN_DAYS}" -delete

echo "backup ok: ${OUT} ($(du -h "$OUT" | cut -f1))"
