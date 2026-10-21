#!/usr/bin/env python3
"""Android 新文字仍取自项目中英表。| Android-added text still comes from the project bilingual table."""
import csv
import json
from pathlib import Path
import sys
from xml.sax.saxutils import escape

root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1])
with (root / "i18n/ui_text.tsv").open() as file:
    rows = [r for r in csv.DictReader(file, delimiter="\t") if r["键"].startswith("dmAndroid")]
# The Android reference lane keeps its added UI strings in a separate text
# table so importing it cannot rewrite unrelated project translations.
extra = Path(__file__).resolve().parents[1] / "i18n/android_ui_text.tsv"
if extra.is_file():
    with extra.open() as file:
        known = {r["键"] for r in rows}
        for row in csv.DictReader(file, delimiter="\t"):
            if row["键"].startswith("dmAndroid") and row["键"] not in known:
                rows.append(row)
if not rows:
    sys.exit("Missing Android text in dm/i18n/ui_text.tsv or dm/android/i18n/android_ui_text.tsv")
header = "// 自动生成，不手改。| Generated; do not edit.\n#pragma once\n"
for row in rows:
    header += "#define " + row["键"] + " " + json.dumps(row["English"]) + "\n"
(output / "strings_generated.h").write_text(header)
for directory, column in [("values", "English"), ("values-zh", "中文")]:
    target = output / "res" / directory
    target.mkdir(parents=True, exist_ok=True)
    xml = '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
    for row in rows:
        xml += '    <string name="' + row["键"] + '" formatted="false">' + escape(row[column]).replace("'", "\\'") + '</string>\n'
    (target / "strings.xml").write_text(xml + "</resources>\n")
