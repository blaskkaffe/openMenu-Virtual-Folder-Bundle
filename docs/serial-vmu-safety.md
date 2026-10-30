# Serial VMU safety

Status: design, no code yet.

## Goal

Serial VMU must never overwrite a VMU that is not a Serial VMU, and must never
let an old or foreign VMU overwrite newer saves on the SD card. This matters
when the configured port/socket sees different devices: official controllers,
BlueRetro, Pico2Maple, USB4Maple and similar.

## Scope and order of work

1. **Serial VMU in this fork, with the safety tag built in from the start.**
   The fork's menu is a separate, smaller code base. It has the settings
   (`sf_serial_vmu`, `sf_serial_vmu_multislot`) and the SD layer
   (`savefile_sd_available`, `sd_savefile.c`) but no backup/restore logic.
   Derek's 1.7.1 logic lives in `ui_menu_credits.c` and is not copied. It is the
   reference for the on-card formats only (below), so SD cards stay
   interchangeable with his build.
2. **Visible text image** (`ICONDATA_VMS`), after 1 works.

## Existing SD card formats (keep unchanged)

| File | Content |
|---|---|
| `/sd/OPENMENU/SAVES/<GAMEID>/SLOT<n>.VMU` | 128 KB image, 256 blocks of 512 bytes, n = 1..5 |
| `/sd/OPENMENU/SAVES/<GAMEID>/SLOT<n>.TXT` | optional slot label |
| `/sd/OPENMENU/SAVES/<GAMEID>/TITLE.TXT` | game title |
| `/sd/OPENMENU/LASTDISC.TXT` | pending-backup record, e.g. `MK5115450-2`; deleted after a successful backup |
| `/cd/EMPTY.VMU` | BIOS-formatted blank image used for a wipe |

Behaviour to match: restore writes all 256 blocks to the VMU; backup reads all
256 blocks to the SD file; slot 1 is used silently when multi-slot is off.

## The tag

Stored in the root block (block 255) at offset `0x60`. In the BIOS-formatted
`EMPTY.VMU` that range (`0x60`-`0x1FF`) is all zero, so a normal format leaves
a VMU untagged. The tag is part of the 128 KB image, so it travels to the SD
card and back with the existing block copy and needs no separate copy code.

| Offset | Size | Field |
|---|---|---|
| `0x60` | 4 | magic `OMSV` |
| `0x64` | 1 | version (1) |
| `0x65` | 1 | slot (1..5) |
| `0x66` | 2 | revision, little endian |
| `0x68` | 4 | lineage ID, random, created when the slot is first made |
| `0x6C` | 4 | launch nonce; `0` = clean, nonzero = launched and not yet backed up |
| `0x70` | 16 | game ID, NUL padded |
| `0x80` | 4 | CRC32 of bytes `0x60`-`0x7F` |

A tag is valid only if magic, version and CRC match. Anything else counts as
untagged. No timestamp: ordering uses the revision only.

## State machine

- **Restore (SD to VMU).** Stamp the tag into the in-memory buffer before the
  write loop: same lineage and revision as the SD copy, fresh nonzero nonce.
  Write the same nonce into `LASTDISC.TXT`.
- **Backup (VMU to SD, next boot).** After a successful write of the SD file,
  set revision + 1 and nonce 0 in both the SD image and the VMU's block 255.
  Delete `LASTDISC.TXT`.
- So `V.nonce == 0` means the VMU's content is on the SD card at `V.revision`.
  `V.nonce != 0` means the VMU holds play data not yet backed up.

## `LASTDISC.TXT`

Line 1 stays `<GAMEID>-<slot>` so older parsers and Derek's builds still read
it. Following lines add `lineage`, `revision` and `nonce` as hex. A file with
only line 1 is accepted and means no verification data is available.

## Decisions

Let V be the tag on the VMU and S the tag on the SD slot file.

### Before a restore (would overwrite the VMU)

| VMU state | Action |
|---|---|
| Blank or freshly formatted (no files, no tag) | Proceed. |
| No tag, has files | **Always confirm**, every mode. Choices: overwrite, choose another device, cancel. |
| Tagged, `nonce != 0` | Warn: unsaved play data. Choices: back it up first, overwrite anyway, cancel. |
| Tagged, `nonce == 0`, matching SD file with same lineage and `S.revision >= V.revision` | Proceed. |
| Tagged, `nonce == 0`, SD file missing, other lineage, or `S.revision < V.revision` | Warn: this VMU's content is not on this SD card. Same choices. |

### Before a backup (would overwrite the SD file)

| Check | Action |
|---|---|
| V.nonce and V.lineage match `LASTDISC.TXT`, and S.revision == V.revision (or S absent) | Proceed. |
| VMU untagged, or tag mismatches `LASTDISC.TXT` (wrong VMU) | Warn. |
| S.revision > V.revision (SD copy is newer) | Warn. |
| S.revision < V.revision, or other lineage (VMU is newer, or the SD card was swapped) | Warn. |

### Warning choices (same three every time)

1. Overwrite anyway.
2. Keep both: write to another slot (needs multi-slot; with multi-slot off,
   offer cancel only).
3. Do nothing. On a failed backup `LASTDISC.TXT` is kept so the backup can be
   retried.

## Legacy and first use

- A `SLOTn.VMU` on the SD card with no tag is trusted and tagged on the next
  write (revision 1, new lineage).
- A first-time slot (no SD file): "copy the current VMU" or "wipe from
  `EMPTY.VMU`" stay as options. Copying a VMU with files is allowed here
  because the user chose it explicitly. The result is tagged either way.
- `LASTDISC.TXT` from an older build (one line, no nonce) with an untagged VMU:
  one-time confirm before the backup.

## Phase 2: visible image

Layout B. The tag is the source of truth. openMenu writes an `ICONDATA_VMS`
file generated from the tag, with rows like:

```
SERIAL VMU
MK5115450
SLOT 2  R012
```

The VMU LCD is 48x32 px, so a 4x5 font gives about 12 characters per row. The
file is regenerated on every backup and restore, and ignored when reading. If
a user deletes it, it comes back on the next write.

## To verify on hardware before coding

1. A BIOS format leaves `0x60`-`0x1FF` zero, or at least invalidates the tag.
2. A game save does not touch block 255.
3. BIOS file copy does not need to preserve the tag.
4. VM2, VMUPro, USB4Maple, Pico2Maple, VMU Explorer and VMU Tool keep the tag.
5. Whether anything displays root-block bytes (expected: no).

## Open questions

- Port selector when no VMU is in the configured slot: should the fork keep
  Derek's device picker, or require the configured slot?
- Should `LASTDISC.TXT` extra lines be hex text or a fixed-format line?
