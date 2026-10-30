#!/bin/sh
# Host test of the game id block. Usage: sh openMenu/src/openmenu/test/run_iblue_gameid.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$d/../src" \
    "$d/iblue_gameid_test.c" "$d/../src/iblue_gameid.c" -o "$out/iblue_gameid_test"
"$out/iblue_gameid_test"
rm -rf "$out"
