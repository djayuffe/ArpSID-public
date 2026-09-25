#pragma once

#include "arpsid/core/c64_cpu6510_micro.h"
#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_phi2_types.h"
#include "arpsid/core/c64_vic.h"

namespace ArpSID::C64 {

class C64Phi2Machine final {
public:
    struct Snapshot final {
        Phi2MachineConfig cfg{};
        uint64_t phi2 = 0;
        bool resetLine = false;
        bool cpuExecutionSuppressed = false;
        OpenBusLatch openBus{};
        ProcessorPort6510 port{};
        MemoryMatrix mem{};
        Cpu6510Micro cpu{};
        Cia6526 cia1{};
        Cia6526 cia2{};
        VicII vic{};
        C64Phi2Diagnostics diag{};
        ISidRegisterWriteSink* sidSink = nullptr;
        IPhi2TraceSink* trace = nullptr;
    };

    void configure(const Phi2MachineConfig& cfg) noexcept { cfg_ = cfg; }
    // v838: live VIC-II fast toggle (no full reconfigure / state reset).
    void setVicFast(bool fast) noexcept { cfg_.vicFast = fast; }
    bool vicFast() const noexcept { return cfg_.vicFast; }
    // v839: live "6510 fast" toggle. CPU stays bit-exact; skips the per-cycle
    // diagnostics snapshot (the largest non-audio per-PHI2 work).
    void setCpuFast(bool fast) noexcept { cfg_.cpuFast = fast; }
    bool cpuFast() const noexcept { return cfg_.cpuFast; }

    void powerOn() noexcept {
        phi2_ = 0;
        resetLine_ = false;
        cpuExecutionSuppressed_ = false;
        openBus_.powerOn();
        port_.powerOn();
        cpu_.powerOn();
        cia1_.reset();
        cia2_.reset();
        const bool pal = cfg_.video != MachineVideoStandard::NTSC;
        cia1_.setClockHz(pal ? static_cast<uint32_t>(kC64PalCpuHzExact)
                             : static_cast<uint32_t>(kC64NtscCpuHzExact));
        cia2_.setClockHz(pal ? static_cast<uint32_t>(kC64PalCpuHzExact)
                             : static_cast<uint32_t>(kC64NtscCpuHzExact));
        vic_.reset(pal);
        mem_.attach(&port_, &openBus_, sidSink_, &cia1_, &cia2_, &vic_);
        mem_.powerOn(cfg_.deterministicPowerRam);
        installDeterministicKernalVectors_();
        diag_ = C64Phi2Diagnostics{};
    }

    void attachSidSink(ISidRegisterWriteSink* sink) noexcept {
        sidSink_ = sink;
        mem_.attach(&port_, &openBus_, sidSink_, &cia1_, &cia2_, &vic_);
    }
    void configureSidBases(const uint16_t* bases, uint8_t count) noexcept { mem_.configureSidBases(bases, count); }
    // Drive the cartridge GAME/EXROM lines into the PHI2 memory matrix so its
    // PLA decode matches the legacy platform for cartridge/Ultimax cases.
    void setCartridgeLines(bool gameHigh, bool exromHigh) noexcept { mem_.setCartridgeLines(gameHigh, exromHigh); }

    void attachTrace(IPhi2TraceSink* trace) noexcept { trace_ = trace; }
    void assertReset(bool asserted) noexcept { resetLine_ = asserted; cpu_.setResetLine(asserted); }
    void resetToVector() noexcept { cpu_.beginResetSequence(); ++diag_.resetEntries; }
    void setTrapBrkAsJam(bool enable) noexcept { cpu_.setTrapBrkAsJam(enable); }
    void setProgramCounter(uint16_t pc) noexcept { cpu_.setPc(pc); }

    // Explicit scheduler-level CPU-execution suppression. When set, tickPhi2()
    // still advances CIA/VIC/open-bus timing (and the PHI2 cycle counter) but the
    // 6510 is frozen — no bus request is issued and no micro-state advances. This
    // is the honest replacement for the previous passive-stepping trick of forging
    // cpu().state().jammed: a real KIL/JAM fault sets the CPU's jammed flag, while
    // this flag means "the scheduler chose not to run the CPU this window", so the
    // two conditions are no longer indistinguishable. It is transient scheduler
    // state (set for the duration of a passive step and cleared afterwards), not a
    // machine fault.
    void setCpuExecutionSuppressed(bool suppressed) noexcept { cpuExecutionSuppressed_ = suppressed; }
    bool cpuExecutionSuppressed() const noexcept { return cpuExecutionSuppressed_; }

    void tickPhi2() noexcept {
        openBus_.decayToPhi2(phi2_);
        const bool irqBefore = cpu_.state().irqLine;
        const bool cia1Edge = cia1_.tick();
        const bool cia2Edge = cia2_.tick();
        (void)cia1Edge;
        (void)cia2Edge;
        // v838: VIC-II fast mode advances raster + bus via stepHalfCycles (skips
        // the heavy sprite/badline matrix-DMA event servicing in step()).
        if (cfg_.vicFast) (void)vic_.stepHalfCycles(2u); else (void)vic_.tick();
        const bool irqLine = cia1_.irq() || vic_.irq();
        cpu_.setIrqLine(irqLine);
        cpu_.setNmiLineLow(cia2_.irq());
        cpu_.setResetLine(resetLine_);
        Phi2BusPhase phase{};
        phase.cycle = phi2_;
        // BA is the VIC's three-cycle early warning and is wired to RDY; AEC is
        // the actual bus grant. Conflating them steals the warning cycles too
        // and makes every badline/sprite DMA window several cycles too long.
        // v838: VIC-fast forces the CPU to always own the bus (no badline/sprite
        // bus-steal stalls) — the main CPU-time saving for cycle-bound playback.
        const bool aecHigh = cfg_.vicFast || !cfg_.enableVicBusSteal || vic_.aec();
        const bool baHigh = cfg_.vicFast || !cfg_.enableVicBusSteal || vic_.ba();
        cpu_.setAec(aecHigh);
        cpu_.setRdy(baHigh);
        phase.owner = aecHigh ? Phi2Owner::Cpu : Phi2Owner::Vic;
        phase.ba = vic_.ba();
        phase.aec = aecHigh;
        phase.rdy = baHigh;
        phase.irqBeforeSample = cia1_.irq() || vic_.irq();
        phase.nmiBeforeSample = cia2_.irq();
        phase.resetBeforeSample = resetLine_;

        // Freeze the 6510 when execution is suppressed (passive scheduler stepping):
        // CIA/VIC/open-bus above still advance, but tickPhi2Begin() is not called so
        // no bus request is issued and no CPU micro-state advances. A real KIL/JAM
        // fault freezes the CPU independently via tickPhi2Begin()'s jammed check.
        if (!cpuExecutionSuppressed_) {
        CpuBusRequest req = cpu_.tickPhi2Begin();
        uint8_t dataIn = openBus_.value();
        if (req.active()) {
            phase.dummy = req.dummy;
            phase.stackAccess = req.stackAccess;
            phase.vectorFetch = req.vectorFetch;
            if (req.vectorFetch) {
                if (cpu_.state().resetSequence) phase.interruptEntry = InterruptEntryKind::Reset;
                else if (cpu_.state().nmiSequence) phase.interruptEntry = InterruptEntryKind::Nmi;
                else if (cpu_.state().brkSequence) phase.interruptEntry = InterruptEntryKind::Brk;
                else if (cpu_.state().irqSequence) phase.interruptEntry = InterruptEntryKind::Irq;
            }
            if (req.isWrite()) {
                phase.rmwEventKind = req.rmwDummyWrite
                    ? RmwBusEventKind::DummyWriteOldValue
                    : (req.rmwFinalWrite ? RmwBusEventKind::FinalWriteNewValue : RmwBusEventKind::None);
                phase.rmwFinalWrite = req.rmwFinalWrite;
                mem_.cpuWrite(phi2_, req.address, req.dataOut, phase, req.rmwDummyWrite);
                if (phase.sidWrite) {
                    ++diag_.sidWrites;
                    if (req.rmwDummyWrite) ++diag_.rmwDummySidWrites;
                }
            } else {
                dataIn = mem_.cpuRead(phi2_, req.address, phase);
                if (phase.sidRead) ++diag_.sidReads;
            }
            cpu_.tickPhi2End(dataIn);
        }
        } // end !cpuExecutionSuppressed_

        // v839: "6510 fast" skips this per-cycle diagnostics snapshot (two CIA
        // timer-phase snapshots + ~40-field telemetry copy). It is pure
        // observability for the GUI/HUD; the exactness ledger and audio path read
        // their inputs from live sources (mem_.openBusReads(), the rmw counter
        // incremented in the write branch, cpu().unsupportedOpcodeTotal()), so the
        // CPU/CIA/VIC/SID emulation stays bit-exact — only the on-screen telemetry
        // goes coarse while the toggle is on. The cycle counter (++phi2_) below is
        // always advanced.
        if (!cfg_.cpuFast) {
        const auto cia1After = cia1_.timerPhaseSnapshot();
        const auto cia2After = cia2_.timerPhaseSnapshot();
        diag_.cia1TimerAUnderflows = cia1After.timerAUnderflows;
        diag_.cia1TimerBUnderflows = cia1After.timerBUnderflows;
        diag_.cia1IrqEdges = cia1After.irqEdges;
        diag_.cia1CntRisingEdges = cia1After.cntRisingEdges;
        diag_.cia1IrqLevel = cia1After.irqLevel;
        diag_.cia1FlagLatched = cia1After.flagLatched;
        diag_.cia1TodLatched = cia1After.todLatched;
        diag_.cia1TodStopped = cia1After.todStopped;
        diag_.cia1TodAlarmWriteMode = cia1After.todAlarmWriteMode;
        diag_.cia1SerialBitsRemaining = cia1After.serialBitsRemaining;
        diag_.cia1SerialSelfClock = cia1After.serialSelfClock;
        diag_.cia2TimerAUnderflows = cia2After.timerAUnderflows;
        diag_.cia2TimerBUnderflows = cia2After.timerBUnderflows;
        diag_.cia2NmiEdges = cia2After.irqEdges;
        diag_.cia2CntRisingEdges = cia2After.cntRisingEdges;
        diag_.cia2IrqLevel = cia2After.irqLevel;
        diag_.cia2FlagLatched = cia2After.flagLatched;
        diag_.cia2TodLatched = cia2After.todLatched;
        diag_.cia2TodStopped = cia2After.todStopped;
        diag_.cia2TodAlarmWriteMode = cia2After.todAlarmWriteMode;
        diag_.cia2SerialBitsRemaining = cia2After.serialBitsRemaining;
        diag_.cia2SerialSelfClock = cia2After.serialSelfClock;
        diag_.irqLineLatched = cpu_.irqLineLatchCount();
        diag_.nmiEdgeLatched = cpu_.nmiEdgeLatchCount();
        diag_.vicRasterLine = vic_.rasterLine();
        diag_.vicCycleInLine = vic_.cycleInLine();
        diag_.vicBank = vic_.memoryBank();
        diag_.vicFetchBase = vic_.fetchBase();
        if (vic_.badline()) ++diag_.vicBadlineCycles;
        if (vic_.spriteDma()) ++diag_.vicSpriteDmaCycles;
        if (!irqBefore && irqLine) ++diag_.irqLineRisingEdges;
        if (phase.vectorFetch && phase.interruptEntry == InterruptEntryKind::Irq) { ++diag_.irqVectorFetches; ++diag_.irqEntries; }
        if (phase.vectorFetch && phase.interruptEntry == InterruptEntryKind::Nmi) { ++diag_.nmiVectorFetches; ++diag_.nmiEntries; }
        if (phase.vectorFetch && phase.interruptEntry == InterruptEntryKind::Brk) ++diag_.brkVectorFetches;
        diag_.phi2Cycles = phi2_ + 1u;
        diag_.openBusReads = mem_.openBusReads();
        diag_.ioHiddenSidStoresToRam = mem_.ioHiddenSidStoresToRam();
        diag_.phi2DirtyWrites = mem_.dirtyWriteCount();
        diag_.phi2LastDirtyAddress = mem_.lastDirtyAddress();
        diag_.phi2LastDirtyValue = mem_.lastDirtyValue();
        diag_.unsupportedOpcodeCount = cpu_.unsupportedOpcodeTotal();
        diag_.lastUnsupportedOpcode = cpu_.lastUnsupportedOpcode();
        diag_.approximateOpcodeCount = cpu_.approximateOpcodeTotal();
        diag_.lastApproximateOpcode = cpu_.lastApproximateOpcode();
        diag_.lastCpuJamReason = cpu_.lastJamReason();
        diag_.lastCpuJamOpcode = cpu_.lastJamOpcode();
        if (!aecHigh) ++diag_.vicStolenCycles;
        } // end !cfg_.cpuFast diagnostics snapshot
        if (trace_) trace_->onPhi2(phase);
        ++phi2_;
    }

    void runPhi2(uint64_t cycles) noexcept {
        while (cycles--) tickPhi2();
    }

    uint64_t phi2Cycle() const noexcept { return phi2_; }
    // Align the PHI2 cycle counter to match an external clock (e.g. the legacy
    // C64Platform after init). Must be called before the first tickPhi2 run so
    // that PlayBase cycle deltas start from a coherent origin.
    void setPhi2Cycle(uint64_t cycle) noexcept { phi2_ = cycle; }
    ProcessorPort6510& port() noexcept { return port_; }
    const ProcessorPort6510& port() const noexcept { return port_; }
    OpenBusLatch& openBus() noexcept { return openBus_; }
    const OpenBusLatch& openBus() const noexcept { return openBus_; }
    MemoryMatrix& memory() noexcept { return mem_; }
    const MemoryMatrix& memory() const noexcept { return mem_; }
    Cpu6510Micro& cpu() noexcept { return cpu_; }
    const Cpu6510Micro& cpu() const noexcept { return cpu_; }
    Cia6526& cia1() noexcept { return cia1_; }
    const Cia6526& cia1() const noexcept { return cia1_; }
    Cia6526& cia2() noexcept { return cia2_; }
    const Cia6526& cia2() const noexcept { return cia2_; }
    VicII& vic() noexcept { return vic_; }
    const VicII& vic() const noexcept { return vic_; }
    const C64Phi2Diagnostics& diagnostics() const noexcept { return diag_; }
    void setPsidSyntheticCiaIrqReports(uint64_t count) noexcept { diag_.psidSyntheticCiaIrqReports = count; }
    void setBasicStartupState(bool requested, bool executed) noexcept { diag_.basicStartupRequested = requested; diag_.basicStartupExecuted = executed; }
    const Phi2MachineConfig& config() const noexcept { return cfg_; }

    void captureSnapshot(Snapshot& out) const noexcept {
        out.cfg = cfg_;
        out.phi2 = phi2_;
        out.resetLine = resetLine_;
        out.cpuExecutionSuppressed = cpuExecutionSuppressed_;
        out.openBus = openBus_;
        out.port = port_;
        out.mem = mem_;
        out.cpu = cpu_;
        out.cia1 = cia1_;
        out.cia2 = cia2_;
        out.vic = vic_;
        out.diag = diag_;
        out.sidSink = sidSink_;
        out.trace = trace_;
    }

    void restoreSnapshot(const Snapshot& in) noexcept {
        cfg_ = in.cfg;
        phi2_ = in.phi2;
        resetLine_ = in.resetLine;
        cpuExecutionSuppressed_ = in.cpuExecutionSuppressed;
        openBus_ = in.openBus;
        port_ = in.port;
        mem_ = in.mem;
        cpu_ = in.cpu;
        cia1_ = in.cia1;
        cia2_ = in.cia2;
        vic_ = in.vic;
        diag_ = in.diag;
        sidSink_ = in.sidSink;
        trace_ = in.trace;
        cpu_.setResetLine(resetLine_);
        mem_.attach(&port_, &openBus_, sidSink_, &cia1_, &cia2_, &vic_);
    }

private:
    // Install deterministic PSID-safe HLE KERNAL vectors. This is not a full
    // ROM; it is the minimum vector/ACK surface needed by PSID/RSID player code
    // when a plugin host has not supplied real ROM bytes.
    //
    // $FFFA -> $FE43 : JMP ($0318)  (NMINV)
    // $FFFC -> $E000 : safe reset/init stub
    // $FFFE -> $FF48 : PHA/TXA/PHA/TYA/PHA,JMP ($0314) (CINV)
    // $EA31          : LDA $DC0D, PLA/TAY, PLA/TAX, PLA, RTI
    // $FE47          : LDA $DD0D, RTI
    // $FF8D RESTOR   : copy vector table $FD30..$FD4F to $0314..$0333
    void installDeterministicKernalVectors_() noexcept {
        auto poke = [&](uint16_t a, uint8_t v) noexcept { mem_.pokeKernalRom(a, v); };
        auto pokeRam = [&](uint16_t a, uint8_t v) noexcept { mem_.pokeRam(a, v); };
        auto vec = [&](uint16_t a, uint16_t target) noexcept {
            poke(a, uint8_t(target & 0xFFu));
            poke(uint16_t(a + 1u), uint8_t(target >> 8u));
        };

        // RAM vector defaults used by the indirect KERNAL IRQ/NMI stubs.
        pokeRam(0x0314u, 0x31u); pokeRam(0x0315u, 0xEAu); // CINV -> $EA31
        pokeRam(0x0318u, 0x47u); pokeRam(0x0319u, 0xFEu); // NMINV -> $FE47

        // Reset stub at $E000: SEI; CLD; LDX #$FF; TXS; set $00/$01;
        // JSR $FF8D (RESTOR); CLI; RTS. It is safe for synthetic PSID boot.
        const uint8_t resetStub[] = {
            0x78u, 0xD8u, 0xA2u, 0xFFu, 0x9Au,
            0xA9u, 0x2Fu, 0x8Du, 0x00u, 0x00u,
            0xA9u, 0x37u, 0x8Du, 0x01u, 0x00u,
            0x20u, 0x8Du, 0xFFu,
            0x58u, 0x60u
        };
        for (uint16_t i = 0; i < sizeof(resetStub); ++i) poke(uint16_t(0xE000u + i), resetStub[i]);

        // IRQ entry at $FF48: PHA; TXA; PHA; TYA; PHA; JMP ($0314)
        const uint8_t irqEntry[] = {0x48u, 0x8Au, 0x48u, 0x98u, 0x48u, 0x6Cu, 0x14u, 0x03u};
        for (uint16_t i = 0; i < sizeof(irqEntry); ++i) poke(uint16_t(0xFF48u + i), irqEntry[i]);

        // NMI entry at $FE43: JMP ($0318)
        poke(0xFE43u, 0x6Cu); poke(0xFE44u, 0x18u); poke(0xFE45u, 0x03u);
        // NMI ACK helper at $FE47: LDA $DD0D; RTI
        poke(0xFE47u, 0xADu); poke(0xFE48u, 0x0Du); poke(0xFE49u, 0xDDu); poke(0xFE4Au, 0x40u);
        // Default IRQ helper at $EA31: LDA $DC0D; PLA; TAY; PLA; TAX; PLA; RTI
        const uint8_t irqAck[] = {0xADu, 0x0Du, 0xDCu, 0x68u, 0xA8u, 0x68u, 0xAAu, 0x68u, 0x40u};
        for (uint16_t i = 0; i < sizeof(irqAck); ++i) poke(uint16_t(0xEA31u + i), irqAck[i]);

        // RESTOR vector table and copy loop. FD30 holds 16 little-endian vectors;
        // only CINV/NMINV are semantically important for PSID, the rest are safe.
        for (uint16_t i = 0; i < 32u; ++i) poke(uint16_t(0xFD30u + i), 0x00u);
        poke(0xFD30u, 0x31u); poke(0xFD31u, 0xEAu); // $0314 CINV
        poke(0xFD34u, 0x47u); poke(0xFD35u, 0xFEu); // $0318 NMINV
        const uint8_t restor[] = {
            0xA2u, 0x1Fu,             // LDX #$1F
            0xBDu, 0x30u, 0xFDu,       // LDA $FD30,X
            0x9Du, 0x14u, 0x03u,       // STA $0314,X
            0xCAu,                    // DEX
            0x10u, 0xF7u,              // BPL loop
            0x60u                     // RTS
        };
        for (uint16_t i = 0; i < sizeof(restor); ++i) poke(uint16_t(0xFF8Du + i), restor[i]);

        vec(0xFFFAu, 0xFE43u);
        vec(0xFFFCu, 0xE000u);
        vec(0xFFFEu, 0xFF48u);
    }

    Phi2MachineConfig cfg_{};
    uint64_t phi2_ = 0;
    bool resetLine_ = false;
    bool cpuExecutionSuppressed_ = false;
    OpenBusLatch openBus_{};
    ProcessorPort6510 port_{};
    MemoryMatrix mem_{};
    Cpu6510Micro cpu_{};
    Cia6526 cia1_{};
    Cia6526 cia2_{};
    VicII vic_{};
    ISidRegisterWriteSink* sidSink_ = nullptr;
    IPhi2TraceSink* trace_ = nullptr;
    C64Phi2Diagnostics diag_{};
};

} // namespace ArpSID::C64
