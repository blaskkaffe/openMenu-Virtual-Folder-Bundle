# PS2 menu assets

The PS2-style menu (branch `PS2`) needs `PS2.PAK` in the root of the openMenu disc image, next to
`OPENMENU.INI`. It is made from a PS2 BIOS export (a folder or zip with `assets/textures`,
`assets/icons`, `assets/fonts`) and holds Sony's textures, icon models and font, so it is not part of
this repository: make it yourself.

    pip install pillow numpy
    python3 ps2pack.py ps2bios_export.zip PS2.PAK

Without `PS2.PAK` the menu falls back to the BIOS-style one. The file format is described at the top
of `ps2pack.py`; the reader is `openMenu/src/ps2_engine/src/ps2_pack.c`.
