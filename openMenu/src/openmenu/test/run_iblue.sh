#!/bin/sh
# Host test of the iBlue settings screens. Usage: sh openMenu/src/openmenu/test/run_iblue.sh
# (CI compiles the same files; see .github/workflows/build-newopenmenu.yml)
set -e
d=$(cd "$(dirname "$0")" && pwd)
src="$d/../src"
root="$d/../.."
out=$(mktemp -d)
# the vendored brctl protocol header must match the firmware's, when that checkout is next to this one
fw="$d/../../../../../iBlueControlMod/main/iblue/brctl_proto.h"
if [ -f "$fw" ]; then
    cmp "$fw" "$src/brctl/brctl_proto.h" || { echo "src/brctl/brctl_proto.h differs from the firmware's" >&2; exit 1; }
fi
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$src" -I"$root/bios_rom/include" -I"$root/bios_engine/include" -I"$d" \
    "$d/iblue_test.c" "$d/iblue_fake_board.c" \
    "$src/iblue_link.c" "$src/iblue_map.c" "$src/ui_iblue.c" -o "$out/iblue_test"
"$out/iblue_test"
rm -rf "$out"
