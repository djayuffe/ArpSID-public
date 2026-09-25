# ArpSID v882 — final mirror PAL/NTSC resolver closure

- Hardened the C64 projection mirror PAL/NTSC resolver so an uninitialized or unknown live PSID runtime clock no longer defaults to NTSC.
- The resolver now explicitly accepts PAL and NTSC clocks and otherwise falls back to the runtime SID-clock policy used by non-PSID projection.
- Added `C64ProjectionMirrorAuthorityV882SourceTests` to lock the resolver shape and prevent reintroducing the unknown-clock-as-NTSC split.
