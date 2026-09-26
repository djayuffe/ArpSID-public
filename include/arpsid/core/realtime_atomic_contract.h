// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <atomic>

namespace ArpSID {

static_assert(std::atomic<float>::is_always_lock_free,
              "ArpSID realtime targets require lock-free atomic<float>");
static_assert(std::atomic<double>::is_always_lock_free,
              "ArpSID realtime targets require lock-free atomic<double>");

} // namespace ArpSID
