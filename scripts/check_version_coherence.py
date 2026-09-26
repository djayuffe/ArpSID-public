#!/usr/bin/env python3
"""Check that every place carrying the ArpSID version agrees with VERSION.txt.

VERSION.txt holds a plain MAJOR.MINOR.PATCH version and is the single source of
truth. To release a new version, update VERSION.txt, project(VERSION) in
CMakeLists.txt, include/arpsid/version.h, the README title and add a
CHANGELOG.md entry; this script (also run by CTest) fails if any disagree.
"""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def text(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        fail(f"missing required version artifact: {rel}")
    return path.read_text(encoding="utf-8", errors="replace")


version = text("VERSION.txt").strip()
m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", version)
if not m:
    fail(f"VERSION.txt is {version!r}, expected plain MAJOR.MINOR.PATCH")
major, minor, patch = m.groups()

cmake = text("CMakeLists.txt")
if f"project(ArpSID VERSION {version} LANGUAGES CXX)" not in cmake:
    fail(f"CMakeLists.txt project(VERSION) does not match {version}")

header = text("include/arpsid/version.h")
for name, value in (("MAJOR", major), ("MINOR", minor), ("PATCH", patch)):
    if not re.search(rf"^#define ARPSID_PLUGIN_VERSION_{name} {value}$", header, re.M):
        fail(f"version.h ARPSID_PLUGIN_VERSION_{name} does not match {value}")
if f'#define ARPSID_PLUGIN_VERSION "{version}"' not in header:
    fail(f"version.h ARPSID_PLUGIN_VERSION does not match {version}")

if not text("README.md").startswith(f"# ArpSID {version}\n"):
    fail(f"README.md title does not name {version}")

if f"## [{version}]" not in text("CHANGELOG.md"):
    fail(f"CHANGELOG.md has no entry for {version}")

print(f"OK: version is coherent at {version}")
