# Realtime buffering and cache notes

This pass tightens AUv2/AUv3 scratch-buffer behavior for lower allocator churn and better host burst tolerance.

## Changes

- AUv2 pending render-event capacity increased from 4096 to 8192.
- AUv2 scratch headroom increased from 1024 to 2048 frames.
- AUv2 owned scratch now grows only when needed. Existing capacity is reused.
- AUv3 scratch headroom increased from 1024 to 2048 frames.
- AUv3 scratch growth now uses a grow-only helper instead of repeated whole-buffer assign/resize patterns.
- AUv3 deallocateRenderResources preserves scratch capacity to reduce reopen/reconfigure allocator churn.
- AUv3 prepareStandalone now reuses existing scratch capacity instead of rewriting whole vectors.

## Intent

These changes reduce worst-case heap traffic and cold-to-warm transition churn around host buffer renegotiation, repeated transport/open cycles, and large slice sizes.
