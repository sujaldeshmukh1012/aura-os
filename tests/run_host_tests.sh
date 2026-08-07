#!/bin/sh
set -eu

test_bin="${TMPDIR:-/tmp}/aura_logic_tests"
cc -std=c11 -Wall -Wextra -Werror \
    -Icomponents/aura_core/include \
    components/aura_core/aura_logic.c tests/test_aura_logic.c \
    -o "$test_bin"
"$test_bin"
