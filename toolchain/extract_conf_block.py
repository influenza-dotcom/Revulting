#!/usr/bin/env python3
"""Pull the conf_block out of a workflow result JSON without hand-copying it
(hand-copying escaped JSON is how you get mangled tabs and lost newlines)."""
import json
import sys
import pathlib

src = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])

data = json.loads(src.read_text(encoding="utf-8", errors="replace"))
res = data.get("result", data)
block = res.get("conf_block", "")
out.write_text(block, encoding="utf-8", newline="\n")

print(f"wrote {len(block)} chars, {block.count(chr(10)) + 1} lines -> {out}")
print()
print("binding lines only:")
for line in block.splitlines():
    s = line.strip()
    if s and not s.startswith("#"):
        print("   " + s)

placeholders = res.get("left_as_placeholder", [])
if placeholders:
    print()
    print(f"left as placeholder ({len(placeholders)}):")
    for p in placeholders:
        print(f"   {p['name']}")
