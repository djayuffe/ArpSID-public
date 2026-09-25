# ArpSID 0.0.690 pass380 v893 — low-level bit-exactness + bootstrap-mirror closure

Deep low-level audit of the 6510 CPU core, the SID chip core, and the C64
platform↔PHI2 authority boundary. Five hardware-exactness fixes, one P0
playback regression fix (v891 fallout), and eight stale test-contract
refreshes.

## Fixed — P0 playback regression (v891 fallout)

- **Truncated PSID bootstrap PHI2 mirror.** v891 grew the VBI play bootstrap
  from 6 to 10 bytes (`LDA #iomap / STA $01` banking prefix before
  `JSR play / JMP $FFFF`), but `runPsidVbiPlayViaPhi2_()` still copied only
  6 bytes of platform RAM into PHI2 RAM. The `JSR` operand was truncated, the
  CPU jumped to `$00xx`, hit a BRK jam, and **every direct `runPlay()` dispatch
  failed and rolled back**. The inspection-sync path similarly copied 12 of the
  16-byte CIA IRQ bootstrap, truncating its `PLA/TAY/PLA/TAX/PLA/RTI` epilogue.
  Both copies now use authoritative `C64Platform::kPsidPlayBootstrapCodeLength`
  / `kPsidCiaBootstrapCodeLength` constants, static_asserted against the
  installers' actual code arrays so the mirror can never silently drift again.
- **Interrupt-bootstrap validator rejected the v891 CIA bootstrap.** The
  validator whitelisted IRQ trampoline lengths 7/12 and their canonical shapes;
  the v891 16-byte shape (banking prefix after the `$DC0D` ack) always failed
  validation, and `runPsidCiaPlaybackIrqTicks()` / the CIA bootstrap snapshot
  reported `installed == false`. The 16-byte canonical shape (including the
  exact `psidPlayIomapForAddress` immediate) is now validated; 7/12 remain
  accepted for legacy ledgers.

## Fixed — 6510 CPU bit-exactness (`c64_cpu6510_micro.h`)

- **Taken-branch dummy-read bus addresses.** T2 dummy read now issues at the
  post-operand PC (the next-instruction byte, per 64doc) instead of the
  wrong-page target; the page-cross T3 dummy read now issues at the wrong-page
  address (old PCH | new PCL) instead of the final target. Cycle counts
  unchanged; the addresses on the bus during branch dead cycles now match
  hardware (matters for IO-mapped dummy reads).
- **ARR # ($6B) exact NMOS semantics.** Result `(A & imm) >> 1 | C << 7` with
  N = old C, Z from result, C = result bit 6, V = bit 6 ^ bit 5, plus the
  documented decimal-mode nibble fixups. Previously approximated as plain
  AND + ROR with rotate flags. ARR stays in the approximate-opcode ledger so
  the strict-RSID jam policy contract (v608/v612/v721/v725) is unchanged.
- **NMOS decimal-mode ADC/SBC flags.** Decimal ADC now takes Z from the binary
  sum and N/V from the pre-correction high-nibble intermediate; decimal SBC
  takes all of N/Z/C/V from the binary difference. Both accumulator results
  follow the canonical NMOS nibble-fixup algorithm.
- **CLI/SEI/PLP one-instruction IRQ delay.** The hardware interrupt poll
  samples I before those instructions' final write cycle: CLI with a pending
  IRQ lets one more instruction run; an IRQ arriving during SEI is still taken.
  Modeled as a one-shot delayed-I value (`iFlagPollDelayActive/Value` in
  `Cpu6510MicroState`) consumed at the next committed instruction boundary.
  RTI is deliberately NOT delayed (hardware-correct).

## Fixed — SID chip bit-exactness

- **TEST bit forces the pulse comparator HIGH** (reSID law, the basis of the
  test-bit digi technique). Pulse-only under TEST now renders full-scale
  `$FFF` / reads `$FF` from OSC3, mirrored identically across all three
  engines: `SIDVoice::renderFromPhase` (sid_chip.h), `SidReadbackModel`
  (c64_sid_readback.h), and `SidRegisterEngine::Voice::render`
  (sid_register_engine.h). Noise/tri/saw under TEST keep the pinned v864 law
  (held at 0); combined pulse waveforms stay pulled low by the grounded
  tri/saw bus.

## Stale test-contract refreshes (superseded by v873–v891 policies)

- `c64_audit_closure_v608_tests` + `c64_control_plane_v616_tests`: BASIC-flag
  RSIDs are now pinned as honestly refused (`UnsupportedRsidBasic`) instead of
  "loads with downgrade".
- `psid_load_failure_taxonomy_v873_tests`: 8-byte junk classifies as
  `ParseTooShort`, wrong magic as `BadMagic` (v891 granular mapping).
- `psid_rsid_c64_runtime_v261_tests` + `psid_rsid_runtime_comprehensive_v265_tests`:
  RSID fixtures used flag $0002 (BASIC) by accident; now $0004 (PAL).
- `c64_psid_cia_irq_service_v499_tests`: canonical IRQ trampoline length is
  `kPsidCiaBootstrapCodeLength` (16), not the pre-v891 12.
- `c64_psid_cia_render_scheduling_v872_tests`: rollback gate source-pin updated
  to the v873 `cpuJammed || overflowFatal` form.
- `c64_scope_telemetry_fix_closure_v744_tests`: PAL/NTSC resolver pin updated
  to the v882 `resolveC64ProjectionMirrorPal_()` shape.

## Added tests

- `C64SidBitExactV893Tests` (`source/tests/c64_sid_bitexact_v893_tests.cpp`):
  branch dummy-read addresses (no-cross / page-cross / not-taken), ARR exact
  flags (3 vectors), NMOS decimal ADC/SBC flag vectors, CLI delay + SEI
  pending-IRQ behavior, TEST-bit pulse law in SIDVoice and SidReadbackModel.

## Verification

- New `C64SidBitExactV893Tests`: PASS.
- Full C64/PSID/RSID/PHI2/CPU family sweep: **129/129 PASS** (was 109/129
  before this closure; 17 were real failures — bootstrap truncation, validator
  whitelist, stale contracts — and 3 were harness path artifacts).
- Full SID/forensic/SID-808 family sweep: **52/52 PASS**.
- All fixes verified causally: the 20 pre-existing failures reproduced
  identically against pristine (pre-v893) headers; zero regressions from the
  v893 CPU/SID changes across both sweeps.

## Known deviations (documented, deliberately not changed)

- Envelope gate-edge counter reset on 8580 (`Sid6581Envelope::gateOn/Off`):
  real hardware never resets the rate LFSR on gate on either chip model; the
  reset is a deliberate clean-retrigger model choice for the 8580 path.
- `onSustainChanged()` raises the envelope counter when sustain is raised
  mid-note; real SID hardware only ever decays (raising SR above the current
  level on hardware decays to 0). Deliberate host-automation guard.
- Noise under TEST reads 0 (pinned v864 closure); reSID models the shift
  register filling toward all-ones during TEST.
- Waveform 0 outputs 0 rather than a decaying floating-DAC value.
- The deferred `SidProjectionWriter` unification + C64/synth SID-engine split
  (v874) remains deferred pending on-device Logic audio verification.
