#!/bin/sh
# Host test of the iBlue settings screens. Usage: sh openMenu/src/openmenu/test/run_iblue.sh
# (CI compiles the same files; see .github/workflows/build-newopenmenu.yml)
set -e
d=$(cd "$(dirname "$0")" && pwd)
src="$d/../src"
root="$d/../.."
out=$(mktemp -d)
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$src" -I"$root/bios_rom/include" -I"$root/bios_engine/include" -I"$d" \
    "$d/iblue_test.c" "$d/iblue_fake_board.c" "$d/fw/iblue_cfg_proto.c" \
    "$src/iblue_link.c" "$src/iblue_map.c" "$src/ui_iblue.c" -o "$out/iblue_test"
"$out/iblue_test"
rm -rf "$out"
