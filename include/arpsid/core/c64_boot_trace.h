// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_boot_trace.h - opt-in, layering-clean RSID/PSID boot/init/play tracer.
//
// This is the canonical debug surface for the Commodore 64 SID boot sequence:
// cold power-on, payload load, INIT dispatch/result and the first PLAY service
// of every loaded tune. It exists so a developer can answer "why did this PSID
// fall back to the legacy CPU?" or "did the RSID init actually run through the
// PHI2 machine?" without attaching a debugger to the audio thread.
//
// Design contract:
//   * Self-contained. It only pulls <cstdio>, so include/arpsid/core/ headers
//     may use it without depending on the source/ tree (no layering violation).
//   * Compiles to nothing unless boot tracing is explicitly enabled, so the
//     release audio path keeps its bit-exact, allocation-free behaviour.
//   * One line per boot-state transition, never one line per audio sample. The
//     traced events (load/init/first-play) are one-shot, so a buffered stderr
//     fprintf without fflush is acceptable exactly as in arpsid_log.h.
//
// Enable in one of two ways:
//   -DARPSID_BOOT_TRACE_ENABLE=1   boot tracing only (recommended for triage)
//   -DARPSID_DEV_LOGGING=1         all ArpSID dev logging, boot tracing included

#pragma once

#include <cstdio>

// Boot tracing rides on the same master switch as ARPLOG so "enable all dev
// logging" keeps working, but it can also be turned on by itself for a focused
// boot/init investigation that does not want per-block engine spam.
#if !defined(ARPSID_BOOT_TRACE_ENABLE)
  #if defined(ARPSID_DEV_LOGGING) && ARPSID_DEV_LOGGING
    #define ARPSID_BOOT_TRACE_ENABLE 1
  #else
    #define ARPSID_BOOT_TRACE_ENABLE 0
  #endif
#endif

#if ARPSID_BOOT_TRACE_ENABLE
  // tag is a short stage label (LOAD/INIT/PLAY/COLD) so boot output can be
  // grepped per stage. The trailing '\n' flushes line-buffered stderr.
  #if __cplusplus >= 202002L
    #define ARPSID_BOOT_TRACE(tag, fmt, ...) \
      do { \
        std::fprintf(stderr, "[ArpSID/BOOT][%s] " fmt "\n", tag __VA_OPT__(,) __VA_ARGS__); \
      } while (0)
  #else
    #define ARPSID_BOOT_TRACE(tag, fmt, ...) \
      do { \
        std::fprintf(stderr, "[ArpSID/BOOT][%s] " fmt "\n", tag, ##__VA_ARGS__); \
      } while (0)
  #endif
#else
  #define ARPSID_BOOT_TRACE(tag, fmt, ...) do { (void)sizeof(tag); } while (0)
#endif
