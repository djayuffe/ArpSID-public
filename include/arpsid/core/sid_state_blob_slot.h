// sid_state_blob_slot.h
// ArpSID — fixed-capacity byte-blob payload for OwnershipMailbox handoff.
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ArpSID {

// Pre-allocated, fixed-capacity byte buffer used as the payload of an
// OwnershipMailbox<FixedBlobSlot<Cap>> when transferring a serialized state-root
// snapshot from a producer thread (the realtime render thread on AUv3) to a
// non-realtime consumer (getState/UI) WITHOUT sharing storage between them.
//
// The buffer is allocated once, in the default constructor (mailbox construction
// time — non-realtime). Thereafter the producer only memcpys encoded bytes into
// `bytes` and stores `len`; no allocation happens on the producer (realtime) path.
// Because the mailbox hands ownership of a whole slot to one side at a time, the
// consumer can decode from its owned buffer with no risk of the producer
// overwriting it mid-decode (no torn read / data race).
template <std::size_t Cap>
struct FixedBlobSlot {
    static constexpr std::size_t capacity = Cap;
    std::unique_ptr<uint8_t[]> bytes{std::make_unique<uint8_t[]>(Cap)};
    std::size_t len = 0;
};

}  // namespace ArpSID
