// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
// C64 runtime telemetry snapshot/gate — deterministic, RT-safe, zero allocation.

#include "arpsid/core/c64_platform.h"
#include "arpsid/core/scope_triple_buffer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstdio>

namespace ArpSID::C64 {

struct C64DebugEvent {
    uint8_t kind = 0;      ///< 0=none, 1=boot, 2=sid-load, 3=init, 4=play, 5=sid-write, 6=memory-change, 7=irq-nmi, 8=rom, 9=jam
    uint8_t a = 0;         ///< Kind-specific small value
    uint8_t b = 0;         ///< Kind-specific small value
    uint8_t c = 0;         ///< Kind-specific small value
    uint16_t address = 0;  ///< PC/SID/register/memory base depending on kind
    uint32_t value = 0;    ///< Count/hash/checksum depending on kind
    uint64_t cycle = 0;    ///< PHI2 cycle associated with the event
};

struct C64ChipSnapshot {
    bool valid = false;
    bool pal = true;
    bool realtimeRunning = false;
    bool booted = false;
    bool bootColdComplete = false;
    bool sidImageLoaded = false;
    bool sidInitBootstrapInstalled = false;
    bool sidInitDispatched = false;
    bool sidInitCompleted = false;
    bool sidPlayReady = false;
    bool sidPlayBootstrapInstalled = false;
    bool sidPlayDispatched = false;
    bool sidPlayCompleted = false;
    uint16_t sidLoadAddress = 0;
    uint16_t sidInitAddress = 0;
    uint16_t sidPlayAddress = 0;
    uint8_t sidChipCount = 1;
    std::array<uint16_t, 5> sidBase{{0xD400u, 0u, 0u, 0u, 0u}};
    uint16_t sidBootstrapAddress = 0;
    uint16_t sidPlayBootstrapAddress = 0;
    uint32_t sidInitInstructionsExecuted = 0;
    uint32_t sidPlayInstructionsExecuted = 0;
    uint64_t sidInitStartPhi2 = 0;
    uint64_t sidInitEndPhi2 = 0;
    uint64_t sidPlayStartPhi2 = 0;
    uint64_t sidPlayEndPhi2 = 0;
    uint32_t sidPlayCallCount = 0;
    uint64_t phi2Cycle = 0;
    uint64_t blockIndex = 0;
    uint32_t playCalls = 0;
    float playRateHz = 0.0f;
    uint64_t heavyTelemetryHostSampleCursor = 0;
    uint64_t heavyTelemetryNextSample = 0;
    uint64_t heavyTelemetryLastBlock = 0;
    uint64_t heavyTelemetryLastAudioPhaseSample = 0;
    uint64_t heavyTelemetryPeriodSamples = 0;
    uint64_t heavyTelemetrySkippedSnapshotCount = 0;
    uint64_t heavyTelemetryForcedSnapshotCount = 0;
    uint64_t heavyTelemetryCatchupClampCount = 0;
    bool heavyTelemetryDemandGated = false;
    bool scalarTelemetryEveryBlock = true;

    uint16_t cpuPc = 0;
    uint8_t cpuA = 0;
    uint8_t cpuX = 0;
    uint8_t cpuY = 0;
    uint8_t cpuSp = 0;
    uint8_t cpuStatus = 0;
    bool cpuJammed = false;
    bool irqLine = false;
    bool nmiLine = false;
    bool trapBrkAsJam = false;
    uint8_t processorPort = 0xFF;

    uint16_t vicRaster = 0;
    uint8_t vicCycle = 0;
    bool vicBadline = false;
    bool vicBa = true;
    bool vicAec = true;
    bool vicSpriteDma = false;
    bool vicIrq = false;
    uint8_t vicHalfCycle = 0;
    uint8_t vicMemoryBank = 0;
    uint8_t vicActiveSpriteMask = 0;
    uint64_t vicFrame = 0;
    uint32_t vicTotalStolen = 0;
    uint16_t vicFetchBase = 0;
    uint16_t vicLastFetchAddress = 0;

    uint8_t cia1Irq = 0;
    uint8_t cia2Irq = 0;
    uint8_t cia1IrqMask = 0;
    uint8_t cia2IrqMask = 0;
    bool cia1IrqLine = false;
    bool cia2IrqLine = false;
    CiaTimerPhaseSnapshot cia1Phase{};
    CiaTimerPhaseSnapshot cia2Phase{};

    bool iecAtn = true;
    bool iecClk = true;
    bool iecData = true;
    bool iecSrq = true;
    bool tapeMotor = false;
    bool tapeSense = true;
    bool tapeWrite = false;
    bool tapeRead = true;
    uint64_t tapePulseCount = 0;

    uint8_t openBus = 0;
    uint8_t openBusDecayMask = 0xFFu;
    bool openBusDrivenWithinPersistence = true;
    uint64_t openBusAgePhi2 = 0;
    uint64_t openBusLastDrivenPhi2 = 0;
    uint32_t sidOpenBusReadCount = 0;
    uint32_t colorRamOpenBusReadCount = 0;
    uint32_t potxyOpenBusReadCount = 0;
    uint8_t lastRead = 0;
    uint8_t lastSidReg = 0;
    uint8_t lastSidValue = 0;
    uint64_t lastSidWriteCycle = 0;

    // Render-published open-bus and SID-bus scope rings (128 bipolar samples).
    // Written by the render thread, read by the GUI via C64TelemetryGate seqlock.
    // openBusScopeWritePos and sidBusScopeWritePos are the next-write indices; wrap mod kBusScopeLen.
    // sidBusHeat is a normalized [0..1] energy measure of recent SID bus activity.
    static constexpr size_t kBusScopeLen = 128;
    std::array<float, kBusScopeLen> openBusScope{};
    std::array<float, kBusScopeLen> sidBusScope{};
    uint8_t openBusScopeWritePos = 0;
    uint8_t sidBusScopeWritePos = 0;
    uint8_t busScopeDecimation = 1;
    uint8_t busScopeSourceLen = 128;
    uint8_t busScopeSnapshotLen = 128;
    float sidBusHeat = 0.0f;

    std::array<uint8_t, 32> sidRegs{};
    static constexpr size_t kMemoryWindowCount = 8;
    static constexpr size_t kMemoryWindowBytes = 64;
    std::array<uint16_t, kMemoryWindowCount> memoryWindowBase{};
    std::array<std::array<uint8_t, kMemoryWindowBytes>, kMemoryWindowCount> memoryWindow{};
    std::array<uint32_t, kMemoryWindowCount> memoryWindowHash{};
    std::array<uint8_t, kMemoryWindowCount> memoryWindowChangedBytes{}; ///< Bytes changed versus previous published GUI snapshot.
    std::array<uint64_t, kMemoryWindowCount> memoryWindowChangedByteMask{}; ///< Bit i = byte i changed versus previous GUI snapshot.
    std::array<uint8_t, kMemoryWindowCount> memoryWindowFirstChangedOffset{}; ///< 0..63, or 0xFF when unchanged.
    std::array<uint8_t, kMemoryWindowCount> memoryWindowLastChangedOffset{};  ///< 0..63, or 0xFF when unchanged.
    uint32_t memoryWindowDirtyMask = 0;
    uint32_t memoryWindowChangedMask = 0; ///< Windows with byte/base changes versus previous published snapshot.
    uint32_t memoryWindowCombinedHash = 0;
    std::array<uint16_t, 3> disasmPc{};
    std::array<std::array<uint8_t, 3>, 3> disasmBytes{};
    std::array<uint8_t, 3> disasmByteCount{};
    std::array<std::array<char, 64>, 3> disasmLine{};
    std::array<std::array<char, 32>, 3> disasmText{}; // legacy ABI mirror of disasmLine
    std::array<uint16_t, 8> disasmCallStack{};
    std::array<char, 33> psidTitle{};
    std::array<char, 33> psidAuthor{};
    std::array<char, 33> psidReleased{};
    uint16_t psidSongs = 0;
    uint16_t psidCurrentSubtune = 0;
    bool psidCiaIrqObserved = false;
    bool psidCiaCpuIrqLineObserved = false;
    bool psidCiaVectorEntered = false;
    bool psidCiaPlayAddressEntered = false;
    bool psidCiaAckObserved = false;
    uint64_t psidCiaTicksToIrq = 0;
    uint64_t psidCiaTicksToVector = 0;
    uint64_t psidCiaTicksToPlay = 0;
    uint64_t psidCiaRunGeneration = 0;
    uint64_t psidCiaServiceGeneration = 0;
    uint16_t psidCiaIdleLoopAddress = 0;

    // Original-ROM/cache observability. These booleans/checksums are copied
    // from the render-owned C64 platform into the GUI snapshot so RSID readiness
    // is visible without probing filesystem/cache state from the UI or audio thread.
    bool externalKernalRom = false;
    bool externalBasicRom = false;
    bool externalCharacterRom = false;
    bool externalCompleteRomSet = false;
    uint32_t kernalRomChecksum = 0;
    uint32_t basicRomChecksum = 0;
    uint32_t characterRomChecksum = 0;

    static constexpr size_t kDebugEventCount = 16;
    std::array<C64DebugEvent, kDebugEventCount> debugEvents{}; ///< Compact render-published debug events; UI formats strings.
    uint8_t debugEventCount = 0;
    uint32_t debugEventDropped = 0;
    uint64_t debugEventSequence = 0;
};

inline uint8_t c64OpcodeSize(uint8_t op) noexcept {
    switch (op) {
        case 0x20: case 0x4C: case 0x6C: case 0x0D: case 0x1D: case 0x19: case 0x2D: case 0x3D: case 0x39:
        case 0x4D: case 0x5D: case 0x59: case 0x6D: case 0x7D: case 0x79: case 0x8D: case 0x9D: case 0x99:
        case 0xAD: case 0xBD: case 0xB9: case 0xCD: case 0xDD: case 0xD9: case 0xED: case 0xFD: case 0xF9:
        case 0x0E: case 0x1E: case 0x2E: case 0x3E: case 0x4E: case 0x5E: case 0x6E: case 0x7E:
        case 0x8C: case 0x8E: case 0xAC: case 0xAE: case 0xBC: case 0xBE: case 0xCC: case 0xCE: case 0xEC: case 0xEE:
            return 3;
        case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xB0: case 0xD0: case 0xF0:
        case 0x09: case 0x29: case 0x49: case 0x69: case 0xA0: case 0xA2: case 0xA9: case 0xC0: case 0xC9: case 0xE0: case 0xE9:
        case 0x24: case 0x84: case 0x85: case 0x86: case 0x94: case 0x95: case 0x96: case 0xA4: case 0xA5: case 0xA6:
        case 0xB4: case 0xB5: case 0xB6: case 0xC4: case 0xC5: case 0xC6: case 0xD5: case 0xE4: case 0xE5: case 0xE6: case 0xF5:
        case 0x05: case 0x06: case 0x15: case 0x16: case 0x25: case 0x26: case 0x35: case 0x36: case 0x45: case 0x46:
        case 0x55: case 0x56: case 0x65: case 0x66: case 0x75: case 0x76:
            return 2;
        default: return 1;
    }
}

inline const char* c64OpcodeMnemonic(uint8_t op) noexcept {
    switch (op) {
        case 0x00: return "BRK"; case 0x20: return "JSR"; case 0x40: return "RTI"; case 0x60: return "RTS";
        case 0x4C: return "JMP"; case 0x6C: return "JMP"; case 0xEA: return "NOP";
        case 0xA9: case 0xA5: case 0xB5: case 0xAD: case 0xBD: case 0xB9: case 0xA1: case 0xB1: return "LDA";
        case 0xA2: case 0xA6: case 0xB6: case 0xAE: case 0xBE: return "LDX";
        case 0xA0: case 0xA4: case 0xB4: case 0xAC: case 0xBC: return "LDY";
        case 0x85: case 0x95: case 0x8D: case 0x9D: case 0x99: case 0x81: case 0x91: return "STA";
        case 0x86: case 0x96: case 0x8E: return "STX"; case 0x84: case 0x94: case 0x8C: return "STY";
        case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71: return "ADC";
        case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1: return "SBC";
        case 0x29: case 0x25: case 0x35: case 0x2D: case 0x3D: case 0x39: case 0x21: case 0x31: return "AND";
        case 0x09: case 0x05: case 0x15: case 0x0D: case 0x1D: case 0x19: case 0x01: case 0x11: return "ORA";
        case 0x49: case 0x45: case 0x55: case 0x4D: case 0x5D: case 0x59: case 0x41: case 0x51: return "EOR";
        case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD: case 0xD9: case 0xC1: case 0xD1: return "CMP";
        case 0xE0: case 0xE4: case 0xEC: return "CPX"; case 0xC0: case 0xC4: case 0xCC: return "CPY";
        case 0xE8: return "INX"; case 0xC8: return "INY"; case 0xCA: return "DEX"; case 0x88: return "DEY";
        case 0x18: return "CLC"; case 0x38: return "SEC"; case 0x58: return "CLI"; case 0x78: return "SEI"; case 0xB8: return "CLV"; case 0xD8: return "CLD"; case 0xF8: return "SED";
        case 0x10: return "BPL"; case 0x30: return "BMI"; case 0x50: return "BVC"; case 0x70: return "BVS"; case 0x90: return "BCC"; case 0xB0: return "BCS"; case 0xD0: return "BNE"; case 0xF0: return "BEQ";
        case 0x0A: case 0x06: case 0x16: case 0x0E: case 0x1E: return "ASL";
        case 0x2A: case 0x26: case 0x36: case 0x2E: case 0x3E: return "ROL";
        case 0x4A: case 0x46: case 0x56: case 0x4E: case 0x5E: return "LSR";
        case 0x6A: case 0x66: case 0x76: case 0x6E: case 0x7E: return "ROR";
        case 0xE6: case 0xF6: case 0xEE: case 0xFE: return "INC";
        case 0xC6: case 0xD6: case 0xCE: case 0xDE: return "DEC";
        case 0x24: case 0x2C: return "BIT";
        case 0xAA: return "TAX"; case 0xA8: return "TAY"; case 0x8A: return "TXA"; case 0x98: return "TYA";
        case 0xBA: return "TSX"; case 0x9A: return "TXS"; case 0x48: return "PHA"; case 0x68: return "PLA"; case 0x08: return "PHP"; case 0x28: return "PLP";
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: return "NOP";
        case 0xA7: case 0xB7: case 0xAF: case 0xBF: case 0xA3: case 0xB3: return "LAX";
        case 0x87: case 0x97: case 0x8F: case 0x83: return "SAX";
        case 0xC7: case 0xD7: case 0xCF: case 0xDF: case 0xDB: case 0xC3: case 0xD3: return "DCP";
        case 0xE7: case 0xF7: case 0xEF: case 0xFF: case 0xFB: case 0xE3: case 0xF3: return "ISC";
        case 0x07: case 0x17: case 0x0F: case 0x1F: case 0x1B: case 0x03: case 0x13: return "SLO";
        case 0x27: case 0x37: case 0x2F: case 0x3F: case 0x3B: case 0x23: case 0x33: return "RLA";
        case 0x47: case 0x57: case 0x4F: case 0x5F: case 0x5B: case 0x43: case 0x53: return "SRE";
        case 0x67: case 0x77: case 0x6F: case 0x7F: case 0x7B: case 0x63: case 0x73: return "RRA";
        default: return (op & 0x03u) == 0x03u ? "ILL" : "???";
    }
}

inline void c64FormatDisassemblyLine(char (&out)[64], uint16_t pc, uint8_t op, uint8_t b1, uint8_t b2) noexcept {
    // Deterministic, allocation-free, locale-independent formatting. This runs on
    // the render thread during heavy telemetry snapshots, where std::snprintf is
    // deliberately avoided: it can take a per-call locale lock in some libc
    // implementations, which is not realtime-safe. Output is byte-identical to the
    // previous "%04X  %02X ... %s" snprintf layout (uppercase hex, a fixed 10-char
    // opcode-byte column, and two-space gutters), and the tail is zero-filled so
    // the fixed 64-byte field is fully deterministic.
    static constexpr char kHex[] = "0123456789ABCDEF";
    const uint8_t n = c64OpcodeSize(op);
    const char* m = c64OpcodeMnemonic(op);
    size_t p = 0;
    auto put = [&](char c) noexcept { if (p < 63u) out[p++] = c; };
    auto hex8 = [&](uint8_t v) noexcept { put(kHex[(v >> 4) & 0x0Fu]); put(kHex[v & 0x0Fu]); };
    // Program counter: exactly four uppercase hex digits + a two-space gutter.
    put(kHex[(pc >> 12) & 0x0Fu]); put(kHex[(pc >> 8) & 0x0Fu]);
    put(kHex[(pc >> 4) & 0x0Fu]);  put(kHex[pc & 0x0Fu]);
    put(' '); put(' ');
    // Opcode-byte column, always exactly 10 chars wide to keep the mnemonic aligned.
    hex8(op);
    if (n == 3)      { put(' '); hex8(b1); put(' '); hex8(b2); put(' '); put(' '); }
    else if (n == 2) { put(' '); hex8(b1); put(' '); put(' '); put(' '); put(' '); put(' '); }
    else             { put(' '); put(' '); put(' '); put(' '); put(' '); put(' '); put(' '); put(' '); }
    // Mnemonic (bounded copy).
    for (size_t i = 0; m && m[i]; ++i) put(m[i]);
    for (size_t i = p; i < 64u; ++i) out[i] = '\0';
    out[63] = '\0';
}

inline void c64Disassemble6510(char* out, uint16_t pc, uint8_t op, uint8_t b1, uint8_t b2) noexcept {
    if (!out) return;
    char tmp[64]{};
    c64FormatDisassemblyLine(tmp, pc, op, b1, b2);
    for (int i = 0; i < 64; ++i) out[i] = tmp[i];
}

inline void c64Disassemble6510(char (&out)[64], uint16_t pc, uint8_t op, uint8_t b1, uint8_t b2) noexcept {
    c64FormatDisassemblyLine(out, pc, op, b1, b2);
}

inline void c64CopyFixedAscii(char* dst, size_t dstSize, const char* src) noexcept {
    if (!dst || dstSize == 0u) return;
    size_t i = 0;
    if (src) {
        for (; i + 1u < dstSize && src[i]; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
    for (++i; i < dstSize; ++i) dst[i] = '\0';
}


inline uint32_t c64TelemetryFnv1a32(const uint8_t* data, size_t size, uint32_t seed = 2166136261u) noexcept {
    uint32_t h = seed ? seed : 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint32_t>(data[i]);
        h *= 16777619u;
    }
    return h;
}

class C64TelemetryGate {
public:
    void publish(const C64ChipSnapshot& snapshot) noexcept {
        C64ChipSnapshot snap = snapshot;
        const bool havePrevious = havePrevious_;
        const C64ChipSnapshot& previous = previous_;
        uint32_t changedMask = 0u;
        for (size_t w = 0; w < C64ChipSnapshot::kMemoryWindowCount; ++w) {
            uint16_t changed = 0u;
            uint64_t byteMask = 0u;
            uint8_t first = 0xFFu;
            uint8_t last = 0xFFu;
            const bool fullWindowChanged = (!havePrevious || !previous.valid || previous.memoryWindowBase[w] != snap.memoryWindowBase[w]);
            if (fullWindowChanged) {
                changed = static_cast<uint16_t>(C64ChipSnapshot::kMemoryWindowBytes);
                byteMask = ~uint64_t{0};
                first = 0u;
                last = static_cast<uint8_t>(C64ChipSnapshot::kMemoryWindowBytes - 1u);
            } else {
                for (size_t i = 0; i < C64ChipSnapshot::kMemoryWindowBytes; ++i) {
                    if (previous.memoryWindow[w][i] != snap.memoryWindow[w][i]) {
                        ++changed;
                        byteMask |= (uint64_t{1} << i);
                        if (first == 0xFFu) first = static_cast<uint8_t>(i);
                        last = static_cast<uint8_t>(i);
                    }
                }
            }
            snap.memoryWindowChangedBytes[w] = static_cast<uint8_t>(std::min<uint16_t>(changed, 255u));
            snap.memoryWindowChangedByteMask[w] = byteMask;
            snap.memoryWindowFirstChangedOffset[w] = first;
            snap.memoryWindowLastChangedOffset[w] = last;
            if (changed != 0u) changedMask |= (1u << w);
        }
        snap.memoryWindowChangedMask = changedMask;

        // Compact debug event extraction. This deliberately stores only numeric
        // facts in the render-published snapshot; Objective-C/AppKit string
        // formatting happens later on the UI thread. No allocation, no file I/O,
        // no AppKit, and no unbounded log growth occurs here.
        uint8_t eventCount = 0u;
        uint32_t dropped = previous.debugEventDropped;
        auto addEvent = [&](uint8_t kind, uint8_t a, uint8_t b, uint8_t c, uint16_t address, uint32_t value, uint64_t cycle) noexcept {
            if (eventCount < C64ChipSnapshot::kDebugEventCount) {
                C64DebugEvent& ev = snap.debugEvents[eventCount++];
                ev.kind = kind; ev.a = a; ev.b = b; ev.c = c;
                ev.address = address; ev.value = value; ev.cycle = cycle;
            } else {
                ++dropped;
            }
        };
        if (!havePrevious || !previous.valid) {
            addEvent(1u, snap.bootColdComplete ? 1u : 0u, snap.sidImageLoaded ? 1u : 0u, snap.sidPlayReady ? 1u : 0u, snap.cpuPc, static_cast<uint32_t>(snap.blockIndex), snap.phi2Cycle);
        } else {
            if (!previous.sidImageLoaded && snap.sidImageLoaded) addEvent(2u, static_cast<uint8_t>(snap.psidCurrentSubtune & 0xFFu), static_cast<uint8_t>(snap.psidSongs & 0xFFu), 0u, snap.sidLoadAddress, snap.sidInitAddress, snap.phi2Cycle);
            if (!previous.sidInitCompleted && snap.sidInitCompleted) addEvent(3u, 0u, 0u, 0u, snap.sidInitAddress, snap.sidInitInstructionsExecuted, snap.sidInitEndPhi2);
            if (previous.sidPlayCallCount != snap.sidPlayCallCount) addEvent(4u, static_cast<uint8_t>(snap.sidPlayCallCount & 0xFFu), 0u, 0u, snap.sidPlayAddress, snap.sidPlayInstructionsExecuted, snap.sidPlayEndPhi2);
            if (previous.lastSidWriteCycle != snap.lastSidWriteCycle || previous.lastSidReg != snap.lastSidReg || previous.lastSidValue != snap.lastSidValue) addEvent(5u, snap.lastSidReg, snap.lastSidValue, 0u, static_cast<uint16_t>(0xD400u + snap.lastSidReg), 0u, snap.lastSidWriteCycle);
            if (changedMask != 0u) addEvent(6u, static_cast<uint8_t>(changedMask & 0xFFu), static_cast<uint8_t>(snap.memoryWindowDirtyMask & 0xFFu), 0u, 0u, snap.memoryWindowCombinedHash, snap.phi2Cycle);
            if (previous.irqLine != snap.irqLine || previous.nmiLine != snap.nmiLine) addEvent(7u, snap.irqLine ? 1u : 0u, snap.nmiLine ? 1u : 0u, 0u, snap.cpuPc, snap.cpuStatus, snap.phi2Cycle);
            if (previous.externalCompleteRomSet != snap.externalCompleteRomSet) addEvent(8u, snap.externalKernalRom ? 1u : 0u, snap.externalBasicRom ? 1u : 0u, snap.externalCharacterRom ? 1u : 0u, 0u, snap.kernalRomChecksum ^ snap.basicRomChecksum ^ snap.characterRomChecksum, snap.phi2Cycle);
            if (!previous.cpuJammed && snap.cpuJammed) addEvent(9u, snap.trapBrkAsJam ? 1u : 0u, 0u, 0u, snap.cpuPc, snap.cpuStatus, snap.phi2Cycle);
        }
        snap.debugEventCount = eventCount;
        snap.debugEventDropped = dropped;
        snap.debugEventSequence = havePrevious ? (previous.debugEventSequence + 1u) : 1u;
        triple_.writeSlot() = snap;
        triple_.publish();
        previous_ = snap;
        havePrevious_ = true;
        published_.store(true, std::memory_order_release);
    }

    bool read(C64ChipSnapshot& out) const noexcept {
        if (!published_.load(std::memory_order_acquire)) return false;
        triple_.peekLatest(out);
        return out.valid;
    }

    void clear() noexcept {
        C64ChipSnapshot idle{};
        idle.valid = false;
        publish(idle);
    }

    bool hasSnapshot() const noexcept { return published_.load(std::memory_order_acquire); }

private:
    mutable ScopeTripleBuffer<C64ChipSnapshot> triple_{};
    C64ChipSnapshot previous_{};
    bool havePrevious_ = false;
    std::atomic<bool> published_{false};
};

inline C64ChipSnapshot c64BuildTelemetrySnapshot(const C64Platform& platform,
                                                  bool pal,
                                                  uint64_t blockIndex,
                                                  uint32_t playCalls,
                                                  float playRateHz,
                                                  const char* psidTitle = nullptr,
                                                  const char* psidAuthor = nullptr,
                                                  const char* psidReleased = nullptr,
                                                  uint16_t psidSongs = 0,
                                                  uint16_t psidCurrentSubtune = 0,
                                                  const uint8_t* sidRegisterImageOverride = nullptr,
                                                  int sidRegisterImageOverrideCount = 0,
                                                  uint64_t heavyTelemetryHostSampleCursor = 0,
                                                  uint64_t heavyTelemetryNextSample = 0,
                                                  uint64_t heavyTelemetryLastBlock = 0,
                                                  uint64_t heavyTelemetryLastAudioPhaseSample = 0,
                                                  uint64_t heavyTelemetryPeriodSamples = 0,
                                                  uint64_t heavyTelemetrySkippedSnapshotCount = 0,
                                                  uint64_t heavyTelemetryForcedSnapshotCount = 0,
                                                  uint64_t heavyTelemetryCatchupClampCount = 0,
                                                  bool heavyTelemetryDemandGated = false,
                                                  bool scalarTelemetryEveryBlock = true) noexcept {
    C64ChipSnapshot s{};
    s.valid = true;
    s.pal = pal;
    s.realtimeRunning = platform.realtimeSidCoreRunning();
    s.booted = platform.booted();
    const auto& boot = platform.bootState();
    s.bootColdComplete = boot.coldBootComplete;
    s.sidImageLoaded = boot.sidImageLoaded;
    s.sidInitBootstrapInstalled = boot.sidInitBootstrapInstalled;
    s.sidInitDispatched = boot.sidInitDispatched;
    s.sidInitCompleted = boot.sidInitCompleted;
    s.sidPlayReady = boot.sidPlayReady;
    s.sidPlayBootstrapInstalled = boot.sidPlayBootstrapInstalled;
    s.sidPlayDispatched = boot.sidPlayDispatched;
    s.sidPlayCompleted = boot.sidPlayCompleted;
    s.sidLoadAddress = boot.loadAddress;
    s.sidInitAddress = boot.initAddress;
    s.sidPlayAddress = boot.playAddress;
    s.sidChipCount = boot.sidChipCount;
    for (uint8_t i = 0; i < 5u; ++i) s.sidBase[i] = boot.sidBase[i];
    s.sidBootstrapAddress = boot.bootstrapAddress;
    s.sidPlayBootstrapAddress = boot.playBootstrapAddress;
    s.sidInitInstructionsExecuted = boot.initInstructionsExecuted;
    s.sidPlayInstructionsExecuted = boot.playInstructionsExecuted;
    s.sidInitStartPhi2 = boot.initStartPhi2;
    s.sidInitEndPhi2 = boot.initEndPhi2;
    s.sidPlayStartPhi2 = boot.playStartPhi2;
    s.sidPlayEndPhi2 = boot.playEndPhi2;
    s.sidPlayCallCount = boot.playCallCount;
    s.phi2Cycle = platform.phi2Cycle();
    s.blockIndex = blockIndex;
    s.playCalls = playCalls;
    s.playRateHz = std::isfinite(playRateHz) ? playRateHz : 0.0f;
    s.heavyTelemetryHostSampleCursor = heavyTelemetryHostSampleCursor;
    s.heavyTelemetryNextSample = heavyTelemetryNextSample;
    s.heavyTelemetryLastBlock = heavyTelemetryLastBlock;
    s.heavyTelemetryLastAudioPhaseSample = heavyTelemetryLastAudioPhaseSample;
    s.heavyTelemetryPeriodSamples = heavyTelemetryPeriodSamples;
    s.heavyTelemetrySkippedSnapshotCount = heavyTelemetrySkippedSnapshotCount;
    s.heavyTelemetryForcedSnapshotCount = heavyTelemetryForcedSnapshotCount;
    s.heavyTelemetryCatchupClampCount = heavyTelemetryCatchupClampCount;
    s.heavyTelemetryDemandGated = heavyTelemetryDemandGated;
    s.scalarTelemetryEveryBlock = scalarTelemetryEveryBlock;

    const auto& cpu = platform.cpu().state();
    s.cpuPc = cpu.pc;
    s.cpuA = cpu.a;
    s.cpuX = cpu.x;
    s.cpuY = cpu.y;
    s.cpuSp = cpu.sp;
    s.cpuStatus = cpu.p;
    s.cpuJammed = cpu.jammed;
    s.irqLine = platform.irqLine();
    s.nmiLine = platform.nmiLine();
    s.trapBrkAsJam = platform.trapBrkAsJam();
    s.processorPort = platform.effectiveProcessorPort();

    const auto& vic = platform.vic();
    s.vicRaster = vic.rasterLine();
    s.vicCycle = static_cast<uint8_t>(std::min<uint16_t>(vic.cycleInLine(), 255u));
    s.vicBadline = vic.badline();
    s.vicBa = vic.ba();
    s.vicAec = vic.aec();
    s.vicSpriteDma = vic.spriteDma();
    s.vicIrq = vic.irq();
    s.vicHalfCycle = vic.halfCycle();
    s.vicMemoryBank = vic.memoryBank();
    for (uint8_t sprite = 0; sprite < 8u; ++sprite)
        if (vic.spriteDmaActive(sprite)) s.vicActiveSpriteMask |= static_cast<uint8_t>(1u << sprite);
    s.vicFrame = vic.frame();
    s.vicTotalStolen = vic.totalStolen();
    s.vicFetchBase = vic.fetchBase();
    s.vicLastFetchAddress = vic.lastFetchAddress();

    s.cia1Irq = platform.cia1().irqFlags();
    s.cia2Irq = platform.cia2().irqFlags();
    s.cia1IrqMask = platform.cia1().irqMask();
    s.cia2IrqMask = platform.cia2().irqMask();
    s.cia1IrqLine = platform.cia1().irq();
    s.cia2IrqLine = platform.cia2().irq();
    s.cia1Phase = platform.cia1().timerPhaseSnapshot();
    s.cia2Phase = platform.cia2().timerPhaseSnapshot();
    s.iecAtn = platform.iec().atn();
    s.iecClk = platform.iec().clk();
    s.iecData = platform.iec().data();
    s.iecSrq = platform.iec().srq();
    s.tapeMotor = platform.tape().motor();
    s.tapeSense = platform.tape().sense();
    s.tapeWrite = platform.tape().write();
    s.tapeRead = platform.tape().read();
    s.tapePulseCount = platform.tape().pulseCount();
    s.openBus = platform.readOpenBus();
    s.openBusDecayMask = platform.openBusDecayMask();
    s.openBusDrivenWithinPersistence = platform.openBusDrivenWithinPersistence();
    s.openBusAgePhi2 = platform.openBusAgePhi2();
    s.openBusLastDrivenPhi2 = platform.openBusLastDrivenPhi2();
    s.sidOpenBusReadCount = platform.sidNoSinkOpenBusReadCount();
    s.colorRamOpenBusReadCount = platform.colorRamHighNibbleOpenBusReadCount();
    s.potxyOpenBusReadCount = platform.sidNoSinkPotxyReadCount();
    s.lastRead = platform.lastReadValue();
    s.lastSidReg = platform.lastSidRegister();
    s.lastSidValue = platform.lastSidValue();
    s.lastSidWriteCycle = platform.lastSidWriteCycle();
    s.sidRegs = platform.sidRegisterImage();
    if (sidRegisterImageOverride) {
        const int overrideCount = std::clamp(sidRegisterImageOverrideCount, 0, static_cast<int>(s.sidRegs.size()));
        for (int i = 0; i < overrideCount; ++i) s.sidRegs[static_cast<size_t>(i)] = sidRegisterImageOverride[i];
    }
    // Always publish a compact, live raw-memory cockpit. These are copied from
    // the render-owned C64 authority into the seqlock snapshot once per audio
    // block; the GUI never peeks the mutable runtime directly.
    s.memoryWindowBase[0] = 0x0000u;                                      // zero page / CPU port
    s.memoryWindowBase[1] = 0x0100u;                                      // stack
    s.memoryWindowBase[2] = static_cast<uint16_t>(s.cpuPc & 0xFFC0u);      // current PC page
    s.memoryWindowBase[3] = 0x0400u;                                      // default text screen
    s.memoryWindowBase[4] = 0xA000u;                                      // BASIC/RAM-under-ROM view per PLA
    s.memoryWindowBase[5] = 0xD000u;                                      // VIC/IO window
    s.memoryWindowBase[6] = 0xD400u;                                      // SID register mirror
    s.memoryWindowBase[7] = 0xD800u;                                      // color RAM nibble window
    for (size_t w = 0; w < C64ChipSnapshot::kMemoryWindowCount; ++w) {
        const uint16_t base = s.memoryWindowBase[w];
        for (size_t i = 0; i < C64ChipSnapshot::kMemoryWindowBytes; ++i) {
            if (w == 6u && i < s.sidRegs.size()) {
                // The $D400 window is an IO window. Publish the live SID register
                // mirror, not the hidden underlying RAM byte, so the GUI shows the
                // actual C64/SID bus state.
                s.memoryWindow[w][i] = s.sidRegs[i];
            } else if (w == 7u) {
                // Color RAM is a separate 4-bit static RAM behind the $D800-$DBFF
                // IO aperture. Publish the visible bus value, not hidden main RAM.
                s.memoryWindow[w][i] = static_cast<uint8_t>(0xF0u | (platform.colorRam()[(base + i) & 0x03FFu] & 0x0Fu));
            } else {
                s.memoryWindow[w][i] = platform.peekMemory(static_cast<uint16_t>(base + static_cast<uint16_t>(i)));
            }
        }
    }
    uint32_t combinedMemoryHash = 2166136261u;
    uint32_t dirtyMask = 0u;
    for (size_t w = 0; w < C64ChipSnapshot::kMemoryWindowCount; ++w) {
        const uint32_t h = c64TelemetryFnv1a32(s.memoryWindow[w].data(), s.memoryWindow[w].size());
        s.memoryWindowHash[w] = h;
        combinedMemoryHash ^= h + 0x9E3779B9u + (combinedMemoryHash << 6u) + (combinedMemoryHash >> 2u);
        // In a render-published snapshot we do not retain GUI history here; mark a
        // window dirty when its current content is non-zero or when it is an IO
        // aperture with a live mirror. The GUI can diff against its previous copy
        // without touching the render-owned C64 runtime.
        bool nonZero = (w == 6u || w == 7u);
        for (uint8_t b : s.memoryWindow[w]) nonZero = nonZero || (b != 0u);
        if (nonZero) dirtyMask |= (1u << w);
    }
    s.memoryWindowCombinedHash = combinedMemoryHash;
    s.memoryWindowDirtyMask = dirtyMask;

    uint16_t pc = s.cpuPc;
    for (size_t i = 0; i < s.disasmText.size(); ++i) {
        const uint8_t op = platform.peekMemory(pc);
        const uint8_t b1 = platform.peekMemory(static_cast<uint16_t>(pc + 1u));
        const uint8_t b2 = platform.peekMemory(static_cast<uint16_t>(pc + 2u));
        const uint8_t n = c64OpcodeSize(op);
        s.disasmPc[i] = pc;
        s.disasmBytes[i][0] = op;
        s.disasmBytes[i][1] = b1;
        s.disasmBytes[i][2] = b2;
        s.disasmByteCount[i] = n;
        c64Disassemble6510(s.disasmLine[i].data(), pc, op, b1, b2);
        // Legacy 32-char ABI mirror. Bounded, allocation-free, locale-free copy
        // (no std::snprintf on the render thread).
        c64CopyFixedAscii(s.disasmText[i].data(), s.disasmText[i].size(), s.disasmLine[i].data());
        pc = static_cast<uint16_t>(pc + n);
    }
    c64CopyFixedAscii(s.psidTitle.data(), s.psidTitle.size(), psidTitle);
    c64CopyFixedAscii(s.psidAuthor.data(), s.psidAuthor.size(), psidAuthor);
    c64CopyFixedAscii(s.psidReleased.data(), s.psidReleased.size(), psidReleased);
    s.psidSongs = psidSongs;
    s.psidCurrentSubtune = psidCurrentSubtune;
    s.psidCiaIrqObserved = boot.psidCiaIrqObserved;
    s.psidCiaCpuIrqLineObserved = boot.psidCiaCpuIrqLineObserved;
    s.psidCiaVectorEntered = boot.psidCiaVectorEntered;
    s.psidCiaPlayAddressEntered = boot.psidCiaPlayAddressEntered;
    s.psidCiaAckObserved = boot.psidCiaAckObserved;
    s.psidCiaTicksToIrq = boot.psidCiaTicksToIrq;
    s.psidCiaTicksToVector = boot.psidCiaTicksToVector;
    s.psidCiaTicksToPlay = boot.psidCiaTicksToPlay;
    s.psidCiaRunGeneration = boot.psidCiaRunGeneration;
    s.psidCiaServiceGeneration = boot.psidCiaServiceGeneration;
    s.psidCiaIdleLoopAddress = boot.psidCiaIdleLoopAddress;
    s.externalKernalRom = platform.hasExternalKernalRom();
    s.externalBasicRom = platform.hasExternalBasicRom();
    s.externalCharacterRom = platform.hasExternalCharacterRom();
    s.externalCompleteRomSet = platform.hasCompleteExternalRomSet();
    s.kernalRomChecksum = platform.kernalRomChecksum();
    s.basicRomChecksum = platform.basicRomChecksum();
    s.characterRomChecksum = platform.characterRomChecksum();
    return s;
}

} // namespace ArpSID::C64
