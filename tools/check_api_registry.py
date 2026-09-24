#!/usr/bin/env python3
"""Fail if src/config.h and the host-API registry in src/api.cpp have drifted apart.

A setting missing from the registry does not break anything loudly: it simply never appears in a host
UI, and nobody notices for a release or two. That silence is the whole reason this check exists. Run
it in CI; it needs nothing but Python.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent

def config_fields():
    text = (root / "src" / "config.h").read_text(encoding="utf-8")
    start = text.index("struct Config {") + len("struct Config {")
    body = text[start:text.index("\n};", start)]
    pattern = re.compile(r"^\s*(?:bool|int|float|double|std::string)\s+(\w+)\s*=", re.M)
    return [m.group(1) for m in pattern.finditer(body)]

def config_clamps():
    """{key: (lo, hi)} for every Clamp() in ValidateConfig.

    ValidateConfig is the authority on what a value may be: anything outside its clamp is thrown away
    on the next load, so a registry range that disagrees makes a host offer settings that do not stick.
    Three ranges in the registry were originally guessed and three were wrong -- which is why this is
    checked rather than trusted.
    """
    text = (root / "src" / "config.cpp").read_text(encoding="utf-8")
    start = text.index("void ValidateConfig")
    body = text[start:text.index("\n}", start)]
    out = {}
    for m in re.finditer(r"g_config\.(\w+)\s*=\s*Clamp\(\s*g_config\.\1\s*,\s*([-\d.]+)f?\s*,\s*([-\d.]+)f?\s*\)", body):
        out[m.group(1)] = (float(m.group(2)), float(m.group(3)))
    return out


def registry_ranges():
    text = (root / "src" / "api.cpp").read_text(encoding="utf-8")
    start = text.index("const Entry kSettings[] = {")
    body = text[start:text.index("\n};", start)]
    out = {}
    for m in re.finditer(
            r'^\s*\{\s*"([^"]+)",.*?,\s*(RELIMITER_TYPE_\w+),\s*([-\d.]+),\s*([-\d.]+),',
            body, re.M):
        out[m.group(1)] = (float(m.group(3)), float(m.group(4)))
    return out


def registry_keys():
    text = (root / "src" / "api.cpp").read_text(encoding="utf-8")
    start = text.index("const Entry kSettings[] = {")
    body = text[start:text.index("\n};", start)]
    return [m.group(1) for m in re.finditer(r'^\s*\{\s*"([^"]+)"', body, re.M)]

fields, keys = config_fields(), registry_keys()
missing = [f for f in fields if f not in keys]
extra = [k for k in keys if k not in fields]
dupes = sorted({k for k in keys if keys.count(k) > 1})

problems = []
if not fields:
    problems.append("parsed no fields out of struct Config -- this checker is broken, not the code")
if missing:
    problems.append(
        "in struct Config but not in the api.cpp registry, so no host UI can see them:\n    "
        + "\n    ".join(missing))
if extra:
    problems.append(
        "in the api.cpp registry but not in struct Config, so get/set will never find them:\n    "
        + "\n    ".join(extra))
if dupes:
    problems.append(
        "listed twice in the registry, which makes one copy unreachable:\n    " + "\n    ".join(dupes))

# Ranges, against ValidateConfig. Only where a clamp exists: plenty of settings have none, and
# inventing a range for those is the mistake this is here to stop, not one to enforce.
clamps, ranges = config_clamps(), registry_ranges()
wrong = []
for key, (lo, hi) in clamps.items():
    if key not in ranges:
        continue
    rlo, rhi = ranges[key]
    # A clamp of the form `if (x != 0) Clamp(x, lo, hi)` means 0 is special and legal below the range,
    # so a registry low bound equal to the clamp's low bound is right and so is one of 0.
    if rhi != hi or rlo not in (lo, 0.0):
        wrong.append(f"{key}: registry says {rlo:g}..{rhi:g}, ValidateConfig clamps to {lo:g}..{hi:g}")
if wrong:
    problems.append(
        "range disagrees with ValidateConfig, so a host would offer values that do not stick:\n    "
        + "\n    ".join(wrong))

if problems:
    print("Host API registry has drifted from struct Config.\n")
    for p in problems:
        print("  - " + p + "\n")
    print("Fix: add or remove the matching line in kSettings in src/api.cpp.")
    sys.exit(1)

print(f"Host API registry matches struct Config ({len(fields)} settings).")
