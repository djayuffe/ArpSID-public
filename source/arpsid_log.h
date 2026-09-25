#pragma once
#include <cstdio>

#if defined(ARPSID_DEV_LOGGING) && ARPSID_DEV_LOGGING
  // FIX Bug#24: Removed std::fflush(stderr) — a blocking system call that caused
  // audio dropouts when ARPLOG fired on the audio thread. stderr on most platforms
  // is line-buffered; the '\n' in the format string ensures timely flushing.
  // Use __VA_OPT__ (C++20) when available; fall back to GNU ## extension otherwise.
  #if __cplusplus >= 202002L
    #define ARPLOG(fmt, ...) \
      do { \
        std::fprintf(stderr, "[ArpSID] %s:%d %s(): " fmt "\n", __FILE__, __LINE__, __func__ __VA_OPT__(,) __VA_ARGS__); \
      } while(0)
  #else
    #define ARPLOG(fmt, ...) \
      do { \
        std::fprintf(stderr, "[ArpSID] %s:%d %s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
      } while(0)
  #endif
#else
  #define ARPLOG(fmt, ...) do { (void)(fmt); } while(0)
#endif
