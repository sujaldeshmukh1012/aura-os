#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export PYTHONPATH="$repo_root/host/core:$repo_root/simulator${PYTHONPATH:+:$PYTHONPATH}"
exec python3 -m nexus_simulator "$@"
