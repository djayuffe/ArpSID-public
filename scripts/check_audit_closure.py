#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.json"

REQUIRED_FILES = [
    "source/tests/kit_sid808_factory_voice_precedence_v637_tests.cpp",
    "source/tests/drsid_kit_payload_v638_tests.cpp",
    "source/tests/digi_slot_bridge_contract_v639_tests.cpp",
    "source/tests/drsid_factory_slot_audio_shape_v640_tests.cpp",
    "source/tests/digi_nonzero_slot_runtime_v641_tests.cpp",
    "source/tests/sid808_projection_sync_contract_v642_tests.cpp",
    "source/tests/kit_target_authority_v643_tests.cpp",
    "source/tests/drum_bridge_runtime_authority_v644_tests.cpp",
    "source/tests/full_closure_manifest_v645_tests.cpp",
    "source/tests/drsid_kit_voice_deep_override_v647_tests.cpp",
    "source/tests/digi_kit_dense_projection_v648_tests.cpp",
    "source/tests/bridge_drsid_accessor_removed_v649_tests.cpp",
    "source/tests/kit_mixed_target_runtime_v650_tests.cpp",
    "scripts/run_full_closure_validation.sh",
]

CODE_FIXABLE = {"P1-01", "P1-02", "P1-03", "P1-04", "P1-05", "P1-06", "P1-07", "P1-08"}
ACCEPTABLE = {"FIXED", "FIXED_WITH_SCOPE", "FIXED_FOR_CURRENT_ARCHITECTURE", "FIXED_COMPAT_DEPRECATED", "FIXED_FOR_FOCUSED_AUDIO_PATHS", "MOSTLY_FIXED"}

def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)

if not MATRIX.exists():
    fail(f"missing {MATRIX}")

items = json.loads(MATRIX.read_text())
seen = {item["id"]: item for item in items}

for ident in CODE_FIXABLE:
    if ident not in seen:
        fail(f"missing audit item {ident}")
    if seen[ident]["status"] not in ACCEPTABLE:
        fail(f"code-fixable item {ident} is not closed enough: {seen[ident]['status']}")

for rel in REQUIRED_FILES:
    if not (ROOT / rel).exists():
        fail(f"missing evidence file {rel}")

kernel = (ROOT / "source/au3/ArpSIDDSPKernel.hpp").read_text(errors="ignore")
if "target == ArpSID::GUI::KitEngineTarget::DrSID ||\n                       componentFlavor_ == ArpSID::ComponentFlavor::DrumMachine" in kernel:
    fail("DrumMachine still forces DrSID branch")
if "constexpr std::uint8_t slotIndex = 0u;" not in kernel or "oneShot.activeSlotCount = 1u;" not in kernel:
    fail("kernel dense Digi one-shot projection is missing")

digi = (ROOT / "include/arpsid/engines/digi_sampler_engine.h").read_text(errors="ignore")
if "singleSlotProj.activeSlotCount = static_cast<std::uint8_t>(std::min<int>(" not in digi:
    fail("DigiSampler triggerSlotAt active range contract missing")

print("OK: audit closure matrix is internally consistent for the current package")
