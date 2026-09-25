#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_REVISION = 970
EXPECTED_VERSION = "0.0.690-pass380-v970-test-build-graph-closure"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def text(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        fail(f"missing required version artifact: {rel}")
    return path.read_text(encoding="utf-8", errors="replace")


version = text("VERSION.txt").strip()
if version != EXPECTED_VERSION:
    fail(f"VERSION.txt is {version!r}, expected {EXPECTED_VERSION!r}")

for rel in ("README.md", "STATUS.md", "TODO.md", "RELEASE_NOTES_V970.md"):
    body = text(rel)
    if not re.search(r"\bv970\b", body, re.IGNORECASE):
        fail(f"{rel} does not identify v970")

for rel in ("README.md", "AI2AI.md", "RELEASE_SOURCE_ONLY_CLOSURE.md"):
    if EXPECTED_VERSION not in text(rel):
        fail(f"{rel} does not identify the exact package qualifier")

# The highest closure revision represented by production release metadata and
# tests must agree with VERSION.txt. This prevents a hash-consistent but stale
# v951/v952 label around v961+ production code.
revisions: set[int] = set()
for path in (ROOT / "source" / "tests").glob("*v[0-9]*_tests.cpp"):
    for m in re.finditer(r"v(\d+)", path.name, re.IGNORECASE):
        revisions.add(int(m.group(1)))
for rel in ("CMakeLists.txt", "STATUS.md", "TODO.md"):
    for m in re.finditer(r"\bv(\d+)\b", text(rel), re.IGNORECASE):
        revisions.add(int(m.group(1)))
if not revisions:
    fail("could not discover any closure revisions")
maximum = max(revisions)
if maximum != EXPECTED_REVISION:
    fail(f"highest packaged closure revision is v{maximum}, expected v{EXPECTED_REVISION}")

if not (ROOT / "source/tests/canonical_render_pipeline_closure_v965_tests.cpp").is_file():
    fail("v965 executable closure test is missing")

if not (ROOT / "source/tests/parameter_presentation_authority_v966_tests.cpp").is_file():
    fail("v966 executable closure test is missing")

if not (ROOT / "source/tests/drsid_quick_kit_base_profile_parity_v967_tests.cpp").is_file():
    fail("v967 executable closure test is missing")

if not (ROOT / "source/tests/drsid_user_kit_save_mode_normalization_v968_tests.cpp").is_file():
    fail("v968 executable closure test is missing")

if not (ROOT / "source/tests/test_suite_integrity_v969_tests.cpp").is_file():
    fail("v969 executable closure test is missing")

if not (ROOT / "source/tests/build_graph_forensic_patchbank_lib_v970_tests.cpp").is_file():
    fail("v970 executable closure test is missing")

print(f"OK: package identity is coherent at {EXPECTED_VERSION}")
print(f"OK: highest closure revision is v{maximum}")
