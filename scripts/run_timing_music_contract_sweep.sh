#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ARPSID_TIMING_SWEEP_BUILD:-${ROOT}/build-timing-music-contract-sweep}"

cmake -S "${ROOT}" -B "${BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DARPSID_BUILD_TESTS=ON

cmake --build "${BUILD}" --target \
  arpsid_release_gate_regression_tests \
  SidProjectionAppliedWriteObserverV874Tests \
  C64ProjectionMirrorQueueSelectiveV877Tests \
  C64ProjectionQueueClearV875Tests \
  C64ProjectionMirrorAuthorityV876SourceTests \
  C64ProjectionMirrorAuthorityV878SourceTests \
  C64ProjectionMirrorQueueV879Tests \
  C64ProjectionMirrorAuthorityV880SourceTests \
  C64ProjectionMirrorQueueOrderV881Tests \
  C64ProjectionMirrorAuthorityV882SourceTests \
  C64ProjectionMirrorQueueCompactionV883Tests \
  C64ProjectionMirrorAuthorityV884SourceTests \
  C64ProjectionMirrorBlockLocalV885Tests \
  C64ProjectionMirrorAuthorityV885SourceTests \
  SidRuntimeMixedUnresolvedClockLawV888Tests \
  SidRuntimeUnresolvedSample0OrderV889Tests \
  SidRuntimeUnresolvedSample0SampleOnlyV890Tests \
  SidRuntimeSampleOnlySubphaseV891Tests \
  SidRuntimeSameSamplePolicyV891Tests \
  SidRuntimeSampleOnlyPriorityV892Tests \
  SidWriteQueueRebaseV894Tests \
  C64ProjectionMirrorBehaviorV899Tests \
  ProjectionMirrorFinalClosureV902Tests \
  ProjectionMirrorFinalClosureV903Tests \
  ProjectionMirrorFinalClosureV904Tests \
  ProjectionMirrorFinalClosureV905Tests \
  Phase2TimingMusicClosureV906Tests \
  TimingMusicSweepClosureV907Tests \
  Phase2NoOutputFxClosureV908Tests \
  ClassicModeAuthorityClosureV909Tests \
  IngressParityTimingAuthorityV910Tests \
  -- -j"${ARPSID_TIMING_SWEEP_JOBS:-2}"

ctest --test-dir "${BUILD}" \
  -R 'ReleaseGateRegression|SidProjectionAppliedWriteObserverV874Tests|C64Projection|SidRuntime.*(SampleOnly|SameSample|Unresolved|MixedUnresolved)|SidWriteQueueRebase|ProjectionMirrorFinalClosureV90[2345678]|Phase2TimingMusicClosureV906Tests|TimingMusicSweepClosureV907Tests|Phase2NoOutputFxClosureV908Tests|ClassicModeAuthorityClosureV909Tests|IngressParityTimingAuthorityV910Tests' \
  --output-on-failure \
  --no-tests=error
