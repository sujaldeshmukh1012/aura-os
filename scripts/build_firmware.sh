#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${IDF_PATH:-}" ]; then
    echo "IDF_PATH is not set. Load the ESP-IDF environment first." >&2
    exit 2
fi
python_bin=${IDF_PYTHON_ENV_PATH:+$IDF_PYTHON_ENV_PATH/bin/python}
python_bin=${python_bin:-python3}
cd "$repo_root/firmware"
exec "$python_bin" "$IDF_PATH/tools/idf.py" -D IDF_TARGET=esp32s3 build
