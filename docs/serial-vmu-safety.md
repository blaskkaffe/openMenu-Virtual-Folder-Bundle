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
  The SD file itself is not modified (its nonce stays 0). Write lineage,
  revision and nonce into `LASTTAG.TXT`.
- **Backup (VMU to SD, next boot).** After a successful write of the SD file,
  set revision + 1 and nonce 0 in both the SD image and the VMU's block 255.
  Delete `LASTDISC.TXT` and `LASTTAG.TXT`.
- The VMU is **clean** when `V.nonce == 0` or `V.nonce == S.nonce`: its content
  is on the SD card. It is **dirty** (play data not yet backed up) when
  `V.nonce != 0` and `V.nonce != S.nonce`. The `S.nonce` comparison matters when
  Derek's build does the backup: it copies the VMU's tag, nonce included, into
  the SD file without clearing it on the VMU, and that state must read as
  clean.

## Launch records

`LASTDISC.TXT` stays byte-for-byte in Derek's format (`<GAMEID>-<slot>`, one
line, nothing else). His parser reads at most 31 bytes and splits at the last
`-` in the whole string, so any extra line would corrupt the game ID he
reads.

The verification data goes in a separate file,
`/sd/OPENMENU/LASTTAG.TXT`, one line of hex: `lineage revision nonce`. His
build never opens it.

It is valid only together with a `LASTDISC.TXT` whose game ID and slot match.
If `LASTDISC.TXT` is missing (his build backed up and deleted it) or does not
match, `LASTTAG.TXT` is stale: ignore it and delete it.

## Decisions

Let V be the tag on the VMU and S the tag on the SD slot file.

### Before a restore (would overwrite the VMU)

| VMU state | Action |
|---|---|
| Blank or freshly formatted (no files, no tag) | Proceed. |
| No tag, has files | **Always confirm**, every mode. Choices: overwrite, choose another device, cancel. |
| Tagged, dirty (see state machine) | Warn: unsaved play data. Choices: back it up first, overwrite anyway, cancel. |
| Tagged, clean, matching SD file with same lineage and `S.revision >= V.revision` | Proceed. |
| Tagged, clean, SD file missing, other lineage, or `S.revision < V.revision` | Warn: this VMU's content is not on this SD card. Same choices. |

### Before a backup (would overwrite the SD file)

| Check | Action |
|---|---|
| V.nonce and V.lineage match `LASTTAG.TXT`, and S.revision == V.revision (or S absent) | Proceed. |
| VMU untagged, or tag mismatches `LASTTAG.TXT` (wrong VMU) | Warn. |
| `LASTTAG.TXT` missing or stale while `LASTDISC.TXT` exists (the launch was by Derek's build) | Compare V and S tags only; same lineage and equal or newer V proceeds, anything else warns. |
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
- `LASTDISC.TXT` with no `LASTTAG.TXT` (written by Derek's build) and an
  untagged VMU: one-time confirm before the backup.

## Upgrade: no nag on every start

Prompts can only come from VMUs that are untagged and contain files.
Blank VMUs and tagged VMUs never prompt. The rule is at most one prompt per
physical VMU:

- Every proceeding outcome (overwrite, adopt, first-time copy) stamps the tag,
  so that VMU never asks again. Only "cancel" asks again next time.
- The default choice on the untagged prompt is **Adopt, keep a rescue copy**:
  the VMU's current 128 KB is written to
  `/sd/OPENMENU/SAVES/_RESCUE/` before anything is overwritten. Nothing is
  lost even if the prompt is dismissed without reading. This runs once per
  VMU, because the VMU is tagged afterwards.
- No silent auto-adoption. A VMU whose directory and FAT match an SD slot file
  is still not proven identical, since a game can rewrite a save in place
  without changing either.
- A user with an existing dedicated Serial VMU therefore sees one prompt on the
  first launch after upgrading.
- Optional: a settings action "Adopt VMU in Serial slot" to do it deliberately.

## Faster transfers

The SD file stays the full 128 KB image (256 x 512 bytes), so Derek's builds
and VMU tools read it unchanged. Only what travels over the Maple bus shrinks.

**Restore.** For each block: read the VMU block, compare with the target block,
write only if different. This is correct for any layout and needs no knowledge
of the VMU filesystem. Optional further skip: if the FAT marks a user block
(0-199) free in the target image, skip it without reading. Stale bytes then stay
in unreachable free blocks, which neither the BIOS nor games see.

**Backup.** Read the root block, FAT and directory first (15 blocks). Then read
only the blocks the FAT marks as used, plus the hidden area (blocks 200-240).
Free user blocks go into the SD image as zeros.

**Safety rules.**

- Validate the FAT before trusting it; on any inconsistency, fall back to the
  full copy.
- Write order on restore: data blocks, then directory and FAT, then the root
  block with the tag last. A power loss mid-restore leaves the old structure.
- The hidden area stays on the compare-and-write rule (not assumed free).
- Measure first: time a full 256-block read and write on a real VMU, a VM2, a
  VMUPro and a Pico2Maple before choosing how far to optimise.

## Compatibility with Derek's 1.7.1

Checked against the 1.7.1 source (`ui_menu_credits.c`):

| Point | Finding |
|---|---|
| SD layout | Unchanged: `SAVES/<GAMEID>/SLOTn.VMU`, `.TXT`, `TITLE.TXT`. His code requires `SLOTn.VMU` to be exactly 131072 bytes and never lists directories, so extra files and the `_RESCUE` folder are ignored. |
| `LASTDISC.TXT` | Must stay one line, `<GAMEID>-<slot>`. His reader takes 31 bytes and splits at the last `-`. The extra data therefore lives in `LASTTAG.TXT`, which he never opens. |
| Image content | His restore and backup copy all 256 blocks as opaque data, so the tag at `0x60` and zeroed free blocks pass through unchanged and unexamined. |
| Mixed use | A VMU restored by our build and backed up by his comes back with the tag copied into the SD file unbumped; the `S.nonce` comparison treats that as clean. A VMU restored by his build from our image carries nonce 0 and is clean. |
| Paths | His path buffers are 64 bytes; new paths stay short (`LASTTAG.TXT`, `_RESCUE`). |

Not verifiable from source: whether the BIOS, games and the virtual VMU devices
keep bytes at `0x60`. That is the hardware list above and must pass before
release.

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
- Rescue copies in `_RESCUE`: keep all, or cap the count to limit SD usage?
