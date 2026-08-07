#!/bin/sh
set -eu

aura_backup_dir=${1:-"$PWD/backups"}
aura_stamp=$(date -u +%Y%m%dT%H%M%SZ)
aura_db_name="aura-${aura_stamp}.db"

mkdir -p "$aura_backup_dir"
docker compose exec -T api python -c \
  "import sqlite3; source=sqlite3.connect('/data/aura.db'); target=sqlite3.connect('/data/${aura_db_name}'); source.backup(target); target.close(); source.close()"
docker compose cp "api:/data/${aura_db_name}" "$aura_backup_dir/$aura_db_name"
docker compose exec -T api python -c \
  "from pathlib import Path; Path('/data/${aura_db_name}').unlink(missing_ok=True)"

echo "Backup written to $aura_backup_dir/$aura_db_name"
