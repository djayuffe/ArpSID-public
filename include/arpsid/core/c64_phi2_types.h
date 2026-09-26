// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

static constexpr double kC64PalCpuHzExact = 985248.0;
static constexpr double kC64NtscCpuHzExact = 1022727.0;

constexpr double sidFrequencyHz(uint16_t freqReg, double sidClockHz) noexcept {
    return (static_cast<double>(freqReg) * sidClockHz) / 16777216.0;
}

enum class MachineVideoStandard : uint8_t { PAL = 0, NTSC = 1 };
enum class RuntimeMode : uint8_t { PSIDFast = 0, RSIDStrict = 1, RawMachine = 2 };
enum class Phi2Owner : uint8_t { None = 0, Cpu = 1, Vic = 2 };
enum class BusAccess : uint8_t { None = 0, Read = 1, Write = 2 };
enum class RmwBusEventKind : uint8_t { None = 0, DummyWriteOldValue = 1, FinalWriteNewValue = 2 };
enum class InterruptEntryKind : uint8_t { None = 0, Reset = 1, Irq = 2, Nmi = 3, Brk = 4 };

// Explicit reason for a 6510 JAM/halt in the PHI2 micro CPU.
// This keeps strict-RSID telemetry honest: an approximate-opcode strict-policy
// stop is different from a real KIL opcode, unsupported opcode, or BRK trap.
enum class CpuJamReason : uint8_t {
    None = 0,
    KilOpcode = 1,
    UnsupportedOpcode = 2,
    ApproximateOpcodeStrictPolicy = 3,
    TrapBrkAsJam = 4,
};

struct Phi2MachineConfig {
    MachineVideoStandard video = MachineVideoStandard::PAL;
    RuntimeMode runtimeMode = RuntimeMode::PSIDFast;
    bool deterministicPowerRam = true;
    bool enableVicBusSteal = true;
    bool enableCartridge = false;
    bool traceEnabled = false;
    // v838: user-togglable "VIC-II fast" CPU-saving mode. Default off (full
    // cycle-accurate VIC). When on, the per-PHI2 VIC step skips the heavy
    // sprite/badline matrix-DMA event servicing and forces no bus-steal, while
    // still advancing the raster counter so $D011/$D012 reads work. Big per-cycle
    // saving for SID playback; accuracy cost only for tunes that depend on
    // cycle-exact badline/sprite bus timing.
    bool vicFast = false;
    // v839: user-togglable "6510 fast" CPU-saving mode. Default off. The 6510 CPU
    // stays bit-exact (this is NOT the policy-locked instruction-atomic engine);
    // what is skipped is the heavy per-PHI2-cycle diagnostics snapshot — the two
    // CIA timer-phase snapshots and the ~40-field telemetry copy that exist only
    // for the GUI/HUD. The exactness ledger reads its fields from live sources
    // (mem_.openBusReads(), cpu().unsupportedOpcodeTotal(), the rmw counter
    // incremented in the write branch), so gating the snapshot is audio-safe;
    // only the on-screen telemetry goes coarse while fast mode is on.
    bool cpuFast = false;
};

struct Phi2BusPhase {
    uint64_t cycle = 0;
    Phi2Owner owner = Phi2Owner::None;
    BusAccess access = BusAccess::None;
    uint16_t address = 0xFFFFu;
    uint8_t data = 0xFFu;
    uint8_t dataIn = 0xFFu;
    uint8_t dataOut = 0xFFu;
    bool rw = true;
    bool ba = true;
    bool aec = true;
    bool rdy = true;
    bool irqBeforeSample = false;
    bool nmiBeforeSample = false;
    bool resetBeforeSample = false;
    bool sidWrite = false;
    bool sidRead = false;
    uint8_t sidReg = 0;
    bool dummy = false;
    bool rmwDummyWrite = false;
    bool rmwFinalWrite = false;
    RmwBusEventKind rmwEventKind = RmwBusEventKind::None;
    bool vectorFetch = false;
    InterruptEntryKind interruptEntry = InterruptEntryKind::None;
    bool stackAccess = false;
    bool openBusSource = false;
};

enum class CpuAccessKind : uint8_t {
    None = 0,
    OpcodeFetch,
    Read,
    Write,
    DummyRead,
    DummyWrite,
    StackRead,
    StackWrite,
    VectorReadLow,
    VectorReadHigh
};

struct CpuBusRequest {
    CpuAccessKind kind = CpuAccessKind::None;
    uint16_t address = 0xFFFFu;
    uint8_t dataOut = 0xFFu;
    uint8_t dataIn = 0xFFu;
    bool rw = true;
    bool sync = false;
    bool rmwDummyWrite = false;
    bool rmwFinalWrite = false;
    RmwBusEventKind rmwEventKind = RmwBusEventKind::None;
    bool vectorFetch = false;
    InterruptEntryKind interruptEntry = InterruptEntryKind::None;
    bool stackAccess = false;
    bool dummy = false;

    bool active() const noexcept { return kind != CpuAccessKind::None; }
    bool isWrite() const noexcept {
        return kind == CpuAccessKind::Write ||
               kind == CpuAccessKind::DummyWrite ||
               kind == CpuAccessKind::StackWrite;
    }
};

struct SidPhi2Write {
    uint64_t phi2 = 0;
    uint8_t reg = 0;
    uint8_t value = 0;
    bool rmwDummy = false;
    bool fromCpu = true;
};

struct C64Phi2Diagnostics {
    uint64_t phi2Cycles = 0;
    uint64_t sidWrites = 0;
    uint64_t sidReads = 0;
    uint64_t rmwDummySidWrites = 0;
    uint64_t irqEntries = 0;
    uint64_t nmiEntries = 0;
    uint64_t resetEntries = 0;
    uint64_t irqLineLatched = 0;
    uint64_t irqLineRisingEdges = 0;
    uint64_t nmiEdgeLatched = 0;
    uint64_t irqVectorFetches = 0;
    uint64_t nmiVectorFetches = 0;
    uint64_t brkVectorFetches = 0;
    uint64_t vicStolenCycles = 0;
    uint64_t openBusReads = 0;
    uint64_t ioHiddenSidStoresToRam = 0;
    uint64_t phi2DirtyWrites = 0;
    uint16_t phi2LastDirtyAddress = 0xFFFFu;
    uint8_t phi2LastDirtyValue = 0xFFu;
    uint64_t sampleSplitSidEvents = 0;
    uint64_t maxSidWritesPerSample = 0;
    uint64_t unsupportedOpcodeCount = 0;
    uint8_t lastUnsupportedOpcode = 0;
    uint64_t approximateOpcodeCount = 0;   // best-effort illegal opcodes executed
    uint8_t lastApproximateOpcode = 0;
    CpuJamReason lastCpuJamReason = CpuJamReason::None;
    uint8_t lastCpuJamOpcode = 0;
    uint64_t cia1TimerAUnderflows = 0;
    uint64_t cia1TimerBUnderflows = 0;
    uint64_t cia1IrqEdges = 0;
    uint64_t cia1CntRisingEdges = 0;
    bool cia1IrqLevel = false;
    bool cia1FlagLatched = false;
    bool cia1TodLatched = false;
    bool cia1TodStopped = false;
    bool cia1TodAlarmWriteMode = false;
    uint8_t cia1SerialBitsRemaining = 0;
    bool cia1SerialSelfClock = false;
    uint64_t cia2TimerAUnderflows = 0;
    uint64_t cia2TimerBUnderflows = 0;
    uint64_t cia2NmiEdges = 0;
    uint64_t cia2CntRisingEdges = 0;
    bool cia2IrqLevel = false;
    bool cia2FlagLatched = false;
    bool cia2TodLatched = false;
    bool cia2TodStopped = false;
    bool cia2TodAlarmWriteMode = false;
    uint8_t cia2SerialBitsRemaining = 0;
    bool cia2SerialSelfClock = false;
    uint64_t vicBadlineCycles = 0;
    uint64_t vicSpriteDmaCycles = 0;
    uint16_t vicRasterLine = 0;
    uint16_t vicCycleInLine = 0;
    uint8_t vicBank = 0;
    uint16_t vicFetchBase = 0;
    uint64_t psidSyntheticCiaIrqReports = 0;
    bool basicStartupRequested = false;
    bool basicStartupExecuted = false;
};

class ISidRegisterWriteSink {
public:
    virtual ~ISidRegisterWriteSink() = default;
    virtual void writeSidRegisterPhi2(uint64_t phi2,
                                      uint8_t reg,
                                      uint8_t value,
                                      bool rmwDummy) noexcept = 0;
    virtual uint8_t readSidRegisterPhi2(uint64_t,
                                        uint8_t,
                                        uint8_t openBus) noexcept {
        return openBus;
    }
};

class IPhi2TraceSink {
public:
    virtual ~IPhi2TraceSink() = default;
    virtual void onPhi2(const Phi2BusPhase& phase) noexcept = 0;
};

static constexpr uint16_t kVectorNmiLo   = 0xFFFAu;
static constexpr uint16_t kVectorNmiHi   = 0xFFFBu;
static constexpr uint16_t kVectorResetLo = 0xFFFCu;
static constexpr uint16_t kVectorResetHi = 0xFFFDu;
static constexpr uint16_t kVectorIrqLo   = 0xFFFEu;
static constexpr uint16_t kVectorIrqHi   = 0xFFFFu;

} // namespace ArpSID::C64
