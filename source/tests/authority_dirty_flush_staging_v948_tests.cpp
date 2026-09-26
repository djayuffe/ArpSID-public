// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cmath>
#include <limits>

#include "parameter_ids.h"

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "missing file: " << path << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void requireContains(const std::string& haystack, const std::string& needle, const char* label) {
    if (haystack.find(needle) == std::string::npos) {
        std::cerr << "missing " << label << ": " << needle << "\n";
        std::exit(1);
    }
}

static void requireAbsent(const std::string& haystack, const std::string& needle, const char* label) {
    if (haystack.find(needle) != std::string::npos) {
        std::cerr << "forbidden " << label << ": " << needle << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");

    requireContains(kernel,
        "const float clean = ArpSID::sanitizeNormalizedParamValue(\n"
        "            pid,\n"
        "            value,\n"
        "            ArpSID::defaultNormalizedParamValue(pid));",
        "v948 async enqueue param-specific sanitize");
    requireContains(kernel,
        "paramIntentQueue_.push(static_cast<uint32_t>(pid), generation,\n"
        "                                                   clean, hostTime, sampleOffset)",
        "async queue carries clean value and ordering generation");
    requireContains(kernel,
        "publishParamIntentFallback_(pid, generation, clean);",
        "queue-full path publishes coherent latest fallback");
    requireContains(kernel,
        "runtimeExecutionOwner_->applyProjectedNormalizedParameter(\n"
        "                            static_cast<uint32_t>(i), clean);",
        "dirty fallback uses ordinary side-effect path");
    requireContains(kernel,
        "if (paramIntentWasSupersededByFallback_(pev)) continue;",
        "older queued generations cannot overwrite fallback");

    requireAbsent(kernel,
        "const float clamped = std::clamp(std::isfinite(value) ? value : kParamInfos[(size_t)pid].defaultNorm, 0.0f, 1.0f);",
        "old async enqueue generic clamp");
    requireAbsent(kernel,
        "renderParams_[(size_t)i] = params_[(size_t)i].load(std::memory_order_relaxed);\n"
        "                dirty_[(size_t)i].store(false, std::memory_order_release);",
        "old dirty flush raw copy without runtimeModel sync");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float clean = ArpSID::sanitizeNormalizedParamValue(
        ArpSID::kParamMasterVolume,
        nan,
        ArpSID::defaultNormalizedParamValue(ArpSID::kParamMasterVolume));
    if (!std::isfinite(clean)) {
        std::cerr << "sanitizeNormalizedParamValue returned non-finite for NaN\n";
        return 1;
    }

    std::cout << "AuthorityDirtyFlushStagingV948Tests PASS\n";
    return 0;
}
