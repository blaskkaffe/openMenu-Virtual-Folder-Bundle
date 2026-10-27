#!/usr/bin/env python3
"""Prints every row of the Settings menu (ui_settings.c) with its values and the lengths that matter on the screen.
Usage: python3 tools/settings_list.py   (from openMenu/)   Label column: about 17 characters before the value pill (10.7 px each from
x 110 to x 304); value: about 27 characters (from x 316 to the pill's end at x 615)."""
import re
SRC = open("src/openmenu/src/ui_settings.c").read()
LABEL_MAX, VALUE_MAX = 17, 27
arrays = {}
for m in re.finditer(r"static const char\* const (\w+)\[\] = \{(.*?)\};", SRC, re.S):
    arrays[m.group(1)] = re.findall(r'"([^"]*)"', m.group(2))
rows_src = SRC[SRC.index("static const row_def rows[]"):SRC.index("#define NET_ROWS")]
rows = []
for m in re.finditer(r'\{(UIS_ACTION_\w+|0), (-?\d+), "([^"]*)", "([^"]*)", "([^"]*)", ([^\n]*?)(?:\n\s*([^\n]*?))?\},\n', rows_src):
    action, icon, group, label, help_, a, b = m.groups()
    rest = (a + " " + (b or "")).replace("\n", " ")
    names = next((arrays[n] for n in re.findall(r"\b(\w+)\b", rest) if n in arrays), None)
    rows.append((group, label, help_, action, names))
net_first = len(rows) - 3
print(f"{'#':>2} {'Group':15} {'Label':20} {'len':>3}  Values")
for i, (group, label, help_, action, names) in enumerate(rows, 1):
    flags = " <- LONG LABEL" if len(label) > LABEL_MAX else ""
    if names:
        shown = names if len(names) <= 8 else names[:3] + ["... (%d values, e.g. %s)" % (len(names), names[len(names) // 2])]
        longest = max(len(n) for n in names)
        if longest > VALUE_MAX:
            flags += f" <- LONG VALUE ({longest})"
        vals = ", ".join(shown) + f"   [longest {longest}]"
    else:
        vals = {"UIS_ACTION_DATETIME": "(opens the editor; shows MM-DD HH:MM)", "UIS_ACTION_VMUCLOCK": "(action; shows Done (n) / No memory card / Failed (n/m))",
                "UIS_ACTION_BENCH": "(action)", "UIS_ACTION_ABOUT": "(opens a box)"}.get(action, "")
    where = "  [Internet screen]" if i > net_first else ""
    print(f"{i:>2} {group:15} {label:20} {len(label):>3}  {vals}{flags}{where}")
    print(f"{'':>2} {'':15} help ({len(help_)}): {help_}" + (" <- cut at 36" if len(help_) > 36 else ""))
