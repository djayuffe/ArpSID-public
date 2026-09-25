# ArpSID 0.0.690 pass380 v870 Release Notes

## Summary

v870 closes the SID-808 musical-shape regression reported after v869. v869 made the SID808 bridge audible and diagnosable, but several drum families were still effectively static one-shot register writes: Kick lacked a real pitch sweep, OpenHat did not ring, Clap collapsed to one noise tick, Cowbell had one weak pulse partial, and Tom was too high/static. v870 adds bounded render-scheduled drum microprograms for those families and installs a fresh AUv2 build with AU/Logic cache refresh.

## Fixed

- SID808 now has a bounded per-voice multi-stage scheduler instead of the old one delayed stage slot that only served snare.
- Kick starts above the body pitch and drops through punch, body, and tail stages without extra gate clicks.
- Tom starts high and drops through body/tail stages instead of staying high/static.
- OpenHat uses long noise/filter/level stages and a longer hold window so it rings beyond the attack.
- Clap uses multiple gated noise bursts plus a lower tail, so it no longer behaves as a single noise tick.
- Cowbell alternates pulse partials, pulse widths, and a band-pass shaped ringing tail.
- SID808 filter selection is drum-shaped: snare keeps BP+HP, hats use high-pass shaping, and cowbell uses band-pass shaping.
- SID808 `allNotesOff()` now force-idles the three SID voices and clears filter routing, preventing old long tails from contaminating explicit mute/setup boundaries.
- `SidRuntimeHostSurface` now includes the standard headers for `std::size_t`, `std::int32_t`, and `std::uint32_t` directly, so clean-room builds do not depend on incidental includes.

## Guards

- `Sid808MusicalShapeV870Tests`
- retained `Sid808AudibleAuthorityV865Tests`
- retained `Sid808SnareOneShotV867Tests`
- retained `Sid808SnareCompleteClosureV868Tests`
- retained `Sid808SnareBodyRoutingV869Tests`

## Validation

- Focused SID808 cluster: 5/5 PASS
- Full CTest: 391/391 PASS
- AUv2 build/sign: PASS
- AUv2 user install: PASS
- Strict installed AUv2 verification: PASS
- `auval -a`: lists `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`
- AU/Logic cache backup: `~/Library/Caches/ArpSID-cleared-logic-au-cache-20260704-201718`
- Installed AUv2 SHA256: `6c8a5f336c7e73643b7037d8fc05755c76e1ad509897dcc1910dae0d902a7042`
