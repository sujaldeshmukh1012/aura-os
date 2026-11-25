#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export PYTHONPATH="$repo_root/host/core:$repo_root/simulator${PYTHONPATH:+:$PYTHONPATH}"
cd "$repo_root"
python3 -m unittest discover -s host/core/tests -p 'test_*.py' -v
python3 -m unittest discover -s simulator/tests -p 'test_*.py' -v
