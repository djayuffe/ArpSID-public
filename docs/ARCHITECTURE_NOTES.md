# ArpSID architecture notes

## Authority rules

- Internal patch/program storage remains zero-based.
- User-facing preset labels are one-based.
- AUv2/AUv3 wrapper code must translate host events into canonical kernel ingress rather than inventing parallel policy.
- Host sample offsets must flow through `enqueueMidiIntent(...)` or an exact compatibility shim to avoid collapsing intra-block timing.

## Buffer and sample-rate negotiation

Negotiation is intentionally bounded. Invalid or extreme host values are sanitized before they become runtime authority. Scratch buffers are provisioned off the render thread and grown with headroom rather than exact-fit churn.

## Factory bank policy

The shipping bank is GM-addressable for convenience, but every slot should remain a plausible SID/C64-style synthesis voice rather than a fake sampled imitation.


## Install-path authority

User-local AUv2 installation is now shell-invoked from CMake instead of executed directly by path. That removes zip-permission fragility from the developer workflow and keeps bundle installation independent from filesystem execute-bit preservation.
