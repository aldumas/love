#!/usr/bin/env python3
# Extract the base64 image blobs from nogame.lua into a Ruby hash literal.
import re, sys

src = open("/home/adam/src/love/src/scripts/nogame.lua").read().splitlines()

start_re = re.compile(r'R\.(\w+)\[(\d+)\]\.(\w+)_png\s*=\s*love\.data\.decode\("data",\s*"base64",\s*"')

# data[group][scale][name] = base64 string
data = {}
i = 0
n = len(src)
while i < n:
    m = start_re.search(src[i])
    if not m:
        i += 1
        continue
    group, scale, name = m.group(1), int(m.group(2)), m.group(3)
    # any base64 already on the start line after the opening quote (usually just "\")
    tail = src[i][m.end():]
    chunks = []
    first = tail.rstrip("\\").strip()
    if first and first != '"':
        chunks.append(first)
    i += 1
    while i < n:
        line = src[i].strip()
        if line == '")' or line == '"' or line.startswith('")'):
            break
        chunks.append(line.rstrip("\\").strip())
        i += 1
    i += 1  # skip the closing line
    b64 = "".join(chunks)
    data.setdefault(group, {}).setdefault(scale, {})[name] = b64

# sanity: decode each to confirm valid base64 / PNG signature
import base64
for g in data:
    for s in data[g]:
        for nm, b in data[g][s].items():
            raw = base64.b64decode(b)
            assert raw[:8] == b"\x89PNG\r\n\x1a\n", f"{g}[{s}].{nm} not PNG ({raw[:8]!r})"

# emit Ruby
out = []
out.append("  # Base64-encoded PNG art, extracted verbatim from src/scripts/nogame.lua")
out.append("  # (regenerate with testing/mruby/tools/extract_nogame_art.py). Keyed")
out.append("  # [group][dpiscale][name]; decoded to textures in Scene#load.")
out.append("  NOGAME_ART = {")
for g in ["bg", "chain", "duckloon"]:
    out.append(f"    {g!r} => {{")
    for s in sorted(data[g]):
        out.append(f"      {s} => {{")
        for nm in sorted(data[g][s]):
            out.append(f"        {nm!r} => {data[g][s][nm]!r},")
        out.append("      },")
    out.append("    },")
out.append("  }.freeze")
print("\n".join(out))

# report to stderr
total = sum(len(data[g][s]) for g in data for s in data[g])
print(f"# extracted {total} blobs across {len(data)} groups", file=sys.stderr)
for g in data:
    for s in sorted(data[g]):
        print(f"#   {g}[{s}]: {sorted(data[g][s])}", file=sys.stderr)
