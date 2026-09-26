// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <cstddef>

// Compact-runtime sizing profile for SidRuntimeModel.
//
// The goal is to materially reduce per-instance memory footprint on AUv3/iOS and
// multi-instance sessions while keeping all storage bounded and allocation-free in
// steady state. All values can be overridden from the build system before this
// header is included.
//
// Previous defaults:
// ingress nodes = 8192
// ingress spill = 2048
// timed event queue = 16384
// merge lane = 512 per lane (5 lanes)
//
// Compact defaults in this pass:
// ingress nodes = 2048
// ingress spill = 512
// timed event queue = 4096
// merge lane = 256 per lane (5 lanes)
//
// This cuts the dominant SidRuntimeModel storage by several megabytes per instance
// while preserving a clear override path for full-size desktop builds.

#ifndef ARPSID_RUNTIME_INGRESS_CAPACITY
#define ARPSID_RUNTIME_INGRESS_CAPACITY 2048
#endif

#ifndef ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY
#define ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY 512
#endif

#ifndef ARPSID_RUNTIME_TIMED_EVENT_CAPACITY
#define ARPSID_RUNTIME_TIMED_EVENT_CAPACITY 4096
#endif

#ifndef ARPSID_RUNTIME_MERGE_LANE_CAPACITY
#define ARPSID_RUNTIME_MERGE_LANE_CAPACITY 256
#endif

namespace ArpSID {

static constexpr int kSidRuntimeIngressCapacity = ARPSID_RUNTIME_INGRESS_CAPACITY;
static constexpr int kSidRuntimeIngressSpillCapacity = ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY;
static constexpr int kSidRuntimeTimedEventCapacity = ARPSID_RUNTIME_TIMED_EVENT_CAPACITY;
static constexpr size_t kSidRuntimeMergeLaneCapacity = static_cast<size_t>(ARPSID_RUNTIME_MERGE_LANE_CAPACITY);

static_assert(kSidRuntimeIngressCapacity >= 256, "Ingress capacity too small for realistic bursts");
static_assert(kSidRuntimeIngressSpillCapacity >= 64, "Ingress spill capacity too small");
static_assert(kSidRuntimeTimedEventCapacity >= 512, "Timed event capacity too small");
static_assert((kSidRuntimeMergeLaneCapacity & (kSidRuntimeMergeLaneCapacity - 1)) == 0,
              "Merge lane capacity must be power of two");
static_assert(kSidRuntimeMergeLaneCapacity >= 64, "Merge lane capacity too small");

} // namespace ArpSID
