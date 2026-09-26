# C64 / PSID / RSID exactness boundaries

## Strict RSID

Strict RSID is allowed to claim a strict PHI2 path only when the PHI2 machine is
active and ready. If PHI2 cannot be used, strict mode refuses and increments the
strict-not-PHI2 counter. It must not fall back to Mos6510 semantic playback and
still report clean strict execution.

## Compatible RSID / PSID

The Mos6510 semantic path is useful compatibility infrastructure. It is not a
cycle-physical proof. Any path using it must remain visible as compatible,
downgraded, or otherwise non-physical-exact in telemetry.

## PSID `playAddress == 0`

A PSID with no discrete play address is routed through the explicit continuous
machine runner. It must not be sent through the RSID-only runner, because that
runner intentionally refuses non-RSID images.

## Observed risk parity

RSID has a detailed downgrade ledger. PSID uses compact physical-risk reporting,
but observed physical risks still set `ObservedDowngradeLedger`, including timed
write overflow, dropped multi-SID writes, SID read/open-bus risk, invalid chip
access, SID hole writes, RMW SID writes, opcode fallback/approximation and
PSID-CIA compatibility service.

## SID `$D41D`

`$D400-$D418` are physical live SID registers. `$D41D` is a pseudo/system byte
used by ArpSID register images for model/PAL-style state. Generic write paths
must route local register `0x1D` through `writeSystemByte()`, not the physical
`write()` path.
