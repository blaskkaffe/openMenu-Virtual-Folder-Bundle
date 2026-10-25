#!/bin/sh
# Makes the pictures of the README (images/*.png) with the PC preview of the menu: the real engine code and the BIOS font and models of a
# boot ROM dump, drawn by a software rasteriser. Nothing of the ROM is stored in the repository, only these pictures of the menu.
#
#   make_screenshots.sh /path/to/dc_boot.bin [outdir]
#
# They are renders, not photos of a console: the PVR draws the same scene, the pictures show the layout and the look. Replace them with
# captures from a console whenever there are some.
set -e
ROM="$1"
OUT="${2:-$(dirname "$0")/../../../images}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../src"
TMP="$(mktemp -d)"
[ -f "$ROM" ] || { echo "usage: $0 dc_boot.bin [outdir]"; exit 1; }
mkdir -p "$OUT"
gcc -O2 -o "$TMP/bios_preview" "$SRC/bios_engine/tools/bios_preview.c" "$SRC"/bios_engine/src/*.c "$SRC"/bios_rom/src/*.c \
    -I"$SRC/bios_engine/include" -I"$SRC/bios_rom/include" -lm

shot() { # name frames selected script [VAR=value ...]: the variables are for the preview
    name="$1"; frames="$2"; sel="$3"; scr="$4"; shift 4
    env "$@" "$TMP/bios_preview" "$ROM" "$TMP/$name.ppm" "$frames" "$sel" "$scr" >/dev/null 2>&1
    python3 "$HERE/ppm2png.py" "$TMP/$name.ppm" "$OUT/$name.png"
}

shot main 240 0 -1 BIOS_PREVIEW_LABEL_FILE="$HERE/main.lbl"
shot settings 100 2 -2 BIOS_PREVIEW_LABEL_FILE="$HERE/settings.lbl"
shot list 70 2 -3 BIOS_PREVIEW_LABEL_FILE="$HERE/list.lbl" BIOS_PREVIEW_CASE=1
# the File screen: the card grid with a keyboard in port A and a mouse in port B, the Serial VMU on A1
shot file-grid 100 0 -14 BIOS_PREVIEW_PORTS=15 BIOS_PREVIEW_PADLABELS="Keyboard,Mouse" BIOS_PREVIEW_SD=0
shot file-window 200 0 -14 BIOS_PREVIEW_KEYS="40:A" BIOS_PREVIEW_SD=0
shot file-card-menu 270 0 -14 BIOS_PREVIEW_KEYS="40:A,120:L,140:A,200:D,204:D,208:D"
shot file-full-size 340 0 -14 BIOS_PREVIEW_KEYS="40:A,120:L,140:A,200:D,204:D,208:D,230:A"
shot file-look 400 0 -14 BIOS_PREVIEW_KEYS="40:A,120:L,140:A,200:D,204:D,230:A"
python3 "$HERE/ppm2png.py" --grid "$OUT/preview.png" "$TMP/main.ppm" "$TMP/list.ppm" "$TMP/file-grid.ppm" "$TMP/settings.ppm"
rm -rf "$TMP"
echo "pictures in $OUT"
