#!/usr/bin/env bash
# Restores a backup. DESTRUCTIVE: overwrites the current database.
#
# Practise this on a throwaway server before you need it. An untested backup
# is a guess, not a recovery plan.
set -euo pipefail

FILE="${1:?usage: restore.sh <backup.sql.gz>}"
DB_USER="${DB_USER:-urlshortener}"
DB_NAME="${DB_NAME:-urlshortener}"

[ -s "$FILE" ] || { echo "no such backup: $FILE" >&2; exit 1; }
gzip -t "$FILE" || { echo "backup is corrupt: $FILE" >&2; exit 1; }

read -rp "This will OVERWRITE ${DB_NAME}. Type the database name to confirm: " reply
[ "$reply" = "$DB_NAME" ] || { echo "aborted"; exit 1; }

gunzip -c "$FILE" | docker compose exec -T postgres psql -U "$DB_USER" -d "$DB_NAME"
echo "restored from ${FILE}"
