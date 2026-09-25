#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "source" / "au3" / "ArpSIDDSPKernel.hpp"

BAD_KERNEL_REFS = [
    "a.userSlotIndex",
    "mixByte(a.flags)",
    "mixByte(a.pad)",
    "vc.pulseWidth &",
    "vc.pulseWidth >>",
]
REQUIRED_KERNEL_REFS = [
    "a.factorySlotIndex",
    "a.reserved[0]",
    "a.reserved[1]",
    "a.reserved[2]",
    "vc.pulseWidthLo",
    "vc.pulseWidthHi",
]
REQUIRED_FILES = [
    "source/tests/kit_hash_field_contract_v632_tests.cpp",
    "source/tests/dsp_kernel_include_smoke_v633_tests.cpp",
    "source/tests/dsp_kernel_multi_tu_smoke_v634_tests.cpp",
    "scripts/macos_build_install_validate_auv2.sh",
]

def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)

if not KERNEL.exists():
    fail(f"missing {KERNEL}")

kernel = KERNEL.read_text(errors="ignore")
bad = [x for x in BAD_KERNEL_REFS if x in kernel]
if bad:
    fail("stale AUv2-breaking KIT hash refs found: " + ", ".join(bad))

missing = [x for x in REQUIRED_KERNEL_REFS if x not in kernel]
if missing:
    fail("required corrected KIT hash refs missing: " + ", ".join(missing))

missing_files = [x for x in REQUIRED_FILES if not (ROOT / x).exists()]
if missing_files:
    fail("required closure files missing: " + ", ".join(missing_files))

print("OK: source tree guard is compatible with the current package")
print("ROOT:", ROOT)
print("ArpSIDDSPKernel.hpp sha256:", hashlib.sha256(KERNEL.read_bytes()).hexdigest())
