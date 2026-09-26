#!/usr/bin/env python3
"""Fail fast if the DrSID/SID-808/DIGI kit audit fixes or their regression
guards have been lost from the source tree."""
from __future__ import annotations
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
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

def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)

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

print("OK: audit-fix regression guards and source contracts are present")
