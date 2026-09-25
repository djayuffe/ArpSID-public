# Render-adjacent sort classification

Every production `std::sort` call is explicitly classified at its call site with
`ARPSID_RT_SORT_CLASSIFICATION`.

- Render or MIDI-adjacent paths sort only bounded, already-allocated arrays with
  scalar/noexcept comparators. The supported macOS libc++ introsort performs no
  heap allocation.
- SID analogue calibration sorting is setup/control-only and never runs from the
  render callback.
- `std::stable_sort` remains forbidden in production realtime paths because its
  common implementation may allocate auxiliary storage.

The audit closure test counts production sort calls and classification markers so
new or moved calls cannot enter the release unnoticed.
