#pragma once

#include "arpsid/core/c64_6510.h"
#include "arpsid/core/c64_boot_trace.h"
#include "arpsid/core/c64_bus.h"
#include "arpsid/core/c64_cartridge.h"
#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_open_bus.h"
#include "arpsid/core/c64_pla.h"
#include "arpsid/core/c64_peripherals.h"
#include "arpsid/core/c64_phi2_types.h"
#include "arpsid/core/c64_timing_math.h"
#include "arpsid/core/c64_vic.h"
#include "arpsid/core/psid_header.h"  // ArpSID::psidValidConfiguredSidBase (shared SID-base validator)
#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

struct SidRegisterSink {
    virtual ~SidRegisterSink() = default;
    virtual void sidWrite(uint8_t reg, uint8_t value, uint64_t phi2Cycle) noexcept = 0;
    virtual uint8_t sidRead(uint8_t reg, uint64_t phi2Cycle) noexcept = 0;
};

enum class C64BootMode : uint8_t {
    ColdPowerOn = 0,
    PsidFastInit = 1,
    RsidMachine = 2,
};

struct C64BootState {
    C64BootMode mode = C64BootMode::ColdPowerOn;
    bool coldBootComplete = false;
    bool sidImageLoaded = false;
    bool sidInitBootstrapInstalled = false;
    bool sidInitDispatched = false;
    bool sidInitCompleted = false;
    bool sidPlayReady = false;
    bool sidPlayBootstrapInstalled = false;
    bool sidPlayDispatched = false;
    bool sidPlayCompleted = false;
    uint16_t loadAddress = 0;
    uint16_t initAddress = 0;
    uint16_t playAddress = 0;
    uint16_t currentSubtune = 0;
    uint16_t bootstrapAddress = 0;
    uint16_t playBootstrapAddress = 0;
    uint32_t initInstructionBudget = 0;
    uint32_t initInstructionsExecuted = 0;
    uint32_t playInstructionBudget = 0;
    uint32_t playInstructionsExecuted = 0;
    uint64_t coldBootPhi2 = 0;
    uint64_t initStartPhi2 = 0;
    uint64_t initEndPhi2 = 0;
    uint64_t playStartPhi2 = 0;
    uint64_t playEndPhi2 = 0;
    uint32_t playCallCount = 0;
    uint32_t sidPayloadBytesLoaded = 0;
    uint32_t sidPayloadBytesTruncated = 0;
    uint16_t rsidIdleAddress = 0;
    uint8_t sidChipCount = 1;
    uint16_t sidBase[5] = {0xD400u, 0u, 0u, 0u, 0u};
    bool sidUsesCiaTiming = false;
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
};

struct C64PsidCiaRuntimeDriveSnapshot {
    bool installed = false;
    bool irqObserved = false;
    bool cpuIrqLineObserved = false;
    bool vectorEntered = false;
    bool playAddressEntered = false;
    bool ciaAckObserved = false;
    // True only after the IRQ trampoline has ACKed CIA1, entered the PSID
    // play routine, returned through the RTI path, and the CPU is back in the
    // deterministic post-init idle loop. This is the runtime success bit;
    // merely seeing PC==playAddress is observability, not proof of a completed
    // PSID-CIA frame.
    bool returnedToIdleAfterPlay = false;
    bool serviceComplete = false;
    bool sidWriteObserved = false;
    bool cpuJammed = false;
    bool vectorStillInstalled = false;
    bool trampolineShapeCanonical = false;
    bool ciaLatchCorrect = false;
    bool systemIntegrityClean = false;
    uint64_t ticksExecuted = 0;
    uint64_t ticksToIrq = 0;
    uint64_t ticksToVector = 0;
    uint64_t ticksToPlay = 0;
    uint64_t ticksToIdleAfterPlay = 0;
    uint64_t ticksToSidWrite = 0;
    uint64_t runGeneration = 0;
    uint64_t serviceGeneration = 0;
    uint16_t idleLoopAddress = 0;
    uint16_t irqTrampolineAddress = 0;
    uint16_t playAddress = 0;
    uint16_t ciaTimerALatch = 0;
    uint16_t sidWriteAddress = 0xFFFFu;
    uint8_t sidWriteValue = 0xFFu;
    uint16_t finalPc = 0;
    uint8_t cpuJamOpcode = 0;
};

struct C64PsidCiaPlaybackBootstrapSnapshot {
    bool installed = false;
    bool vectorStillInstalled = false;
    bool trampolineShapeCanonical = false;
    bool ciaLatchCorrect = false;
    bool systemIntegrityClean = false;
    uint64_t runGeneration = 0;
    uint64_t serviceGeneration = 0;
    uint16_t idleLoopAddress = 0;
    uint16_t irqTrampolineAddress = 0;
    uint16_t playAddress = 0;
    uint16_t ciaTimerALatch = 0;
};

struct C64InterruptBootstrapValidationSnapshot {
    bool valid = true;
    bool checksumLedgerValid = true;
    bool vectorLedgerMatchesRam = true;
    bool vectorLedgerMatchesCpu = true;
    bool irqChecksumMatchesLedger = true;
    bool nmiChecksumMatchesLedger = true;
    bool irqTrampolineShapeCanonical = true;
    bool nmiTrampolineShapeCanonical = true;
    bool playAddressMatchesLedger = true;
    bool ackPolicyMatchesLedger = true;
    uint32_t validationFailureCount = 0;
    uint64_t lastValidationPhi2 = 0;
    uint16_t irqVectorRam = 0;
    uint16_t nmiVectorRam = 0;
    uint16_t cpuIrqVector = 0;
    uint16_t cpuNmiVector = 0;
    uint16_t irqTrampolineAddress = 0;
    uint16_t nmiTrampolineAddress = 0;
    uint16_t playAddress = 0;
    uint8_t irqTrampolineLength = 0;
    uint8_t nmiTrampolineLength = 0;
    uint32_t irqTrampolineChecksum = 0;
    uint32_t nmiTrampolineChecksum = 0;
    bool irqAckCia1 = false;
    bool nmiAckCia2 = false;
};

struct C64SystemIntegritySnapshot {
    bool valid = true;
    bool supportedSidChipLimit = true;
    bool sidBaseRangeUnique = true;
    bool integerPhi2CycleMode = true;
    bool processorPortCoherent = true;
    bool resetVectorPresent = true;
    bool irqNmiLineCoherent = true;
    bool interruptBootstrapPolicyClosed = true;
    bool checksumLedgerValid = true;
    bool vectorLedgerMatchesRam = true;
    bool vectorLedgerMatchesCpu = true;
    bool noBusRetireAbort = true;
    bool noSliceHardOvershoot = true;
    bool noTimedWriteOverflow = true;
    bool validClockRange = true;
    bool validVicBankTelemetrySurface = true;
    bool officialOpcodeMicrosequenceComplete = true;
    bool tickHasNoSpeculativeVisibleFetch = true;
    bool sidBusThreeChipAuthorityClosed = true;
    bool realtimeQualityAcceptable = true;
    bool allKilOpcodesPromoted = true;
    bool noOfficialSemanticFallbackObserved = true;
    uint64_t cpuSemanticFallbackCount = 0;
    uint64_t cpuOfficialSemanticFallbackCount = 0;
    uint8_t cpuLastSemanticFallbackOpcode = 0;
    uint32_t validationFailureCount = 0;
    uint64_t lastValidationPhi2 = 0;
};

struct C64RsidDebugSnapshot {
    bool interruptBootstrapValid = false;
    bool irqLineAsserted = false;
    bool nmiLineAsserted = false;
    bool cia1IrqAsserted = false;
    bool cia2IrqAsserted = false;
    bool vicIrqAsserted = false;
    uint16_t ramIrqVector = 0;
    uint16_t ramNmiVector = 0;
    uint16_t cpuIrqVector = 0;
    uint16_t cpuNmiVector = 0;
    uint16_t irqTrampolineAddress = 0;
    uint16_t nmiTrampolineAddress = 0;
    uint8_t irqTrampolineLength = 0;
    uint8_t nmiTrampolineLength = 0;
    uint32_t irqTrampolineChecksum = 0;
    uint32_t nmiTrampolineChecksum = 0;
    bool irqAckCia1 = false;
    bool nmiAckCia2 = false;
    bool nmiEntryAutoRelocated = false;
    uint16_t cia1LatchA = 0;
    uint16_t cia2LatchA = 0;
    uint8_t cia1IcrFlags = 0;
    uint8_t cia2IcrFlags = 0;
    uint64_t psidCiaRunGeneration = 0;
    uint64_t psidCiaServiceGeneration = 0;
    uint64_t psidCiaTicksToIrq = 0;
    uint64_t psidCiaTicksToVector = 0;
    uint64_t psidCiaTicksToPlay = 0;
};

// Result from runRealtimeSidCoreCycles. PHI2 always advances by requestedCycles
// (executedCycles + passiveCycles == requestedCycles unconditionally).
// Callers must debit the full requestedCycles from cycle debt.
struct C64RunResult {
    uint64_t requestedCycles = 0;
    uint64_t executedCycles  = 0; // PHI2 cycles advanced by actual CPU instructions
    uint64_t passiveCycles   = 0; // PHI2 cycles advanced passively (no CPU work)
    uint32_t executedInstructions = 0;
    bool     instructionBudgetHit = false; // budget was reached before time budget
    bool     unsupportedOpcodeHit = false; // CPU hit an unsupported opcode (NOT a budget hit)
    bool     approximateOpcodeHit = false; // CPU hit an approximate/illegal opcode
    bool     cpuJammed = false;
    CpuJamReason cpuJamReason = CpuJamReason::None;
    uint8_t  cpuJamOpcode = 0;
    bool     completedCycleBudget = false; // executedCycles + passiveCycles == requestedCycles
};

inline bool c64IntegrityFailClosed(const C64SystemIntegritySnapshot& snapshot) noexcept {
    return !snapshot.valid;
}

class C64Platform : public Mos6510Bus {
public:
    static constexpr uint16_t psidCiaTimerALatchForVideoStandard(bool pal) noexcept {
        return static_cast<uint16_t>(C64TimingMath::psidCiaTimerALatch(pal));
    }

    void reset(bool pal = true) noexcept {
        pal_ = pal;
        clockHz_ = pal ? kPalPhi2Hz : kNtscPhi2Hz;
        cpu_.reset();
        cia1_.reset();
        cia2_.reset();
        vic_.reset(pal);
        cia1_.setClockHz(clockHz_);
        cia2_.setClockHz(clockHz_);
        cia1_.setTod50Hz(pal);
        cia2_.setTod50Hz(pal);
        memory_ = {};
        // Preserve externally supplied, legally user-owned ROM images across
        // machine resets. Deterministic fallback ROMs are rebuilt only until a
        // real BASIC/KERNAL/CHARGEN image has been loaded into this platform.
        roms_.resetDeterministicIfNoExternalRoms();
        cartridge_.reset();
        iec_.reset();
        tape_.reset();
        sidRegs_.fill(0);
        for (auto& bank : sidRegsByChip_) bank.fill(0);
        sidChipCount_ = 1;
        sidBases_.fill(0);
        sidBases_[0] = 0xD400u;
        colorRam_.fill(0);
        colorRamHighNibbleOpenBusReadCount_ = 0;  // audit #8
        sidNoSinkOpenBusReadCount_ = 0;           // audit #9
        sidNoSinkPotxyReadCount_ = 0;            // audit #11 (P1-13)
        queue_.clear();
        phi2Cycle_ = 0;
        openBusLatch_.powerOn(0, 0);
        lastRead_ = 0;
        lastSidChip_ = 0;
        lastSidReg_ = 0;
        lastSidValue_ = 0;
        lastSidWriteCycle_ = 0;
        sidWriteObserved_ = false;
        sidSink_ = nullptr;
        bootState_ = {};
        interruptValidationFailureCount_ = 0;
        interruptLastValidationPhi2_ = 0;
        psidCiaRunGeneration_ = 0;
        psidCiaServiceGeneration_ = 0;
        installPsidSafeVectors();
        booted_ = false;
        realtimeSidCoreRunning_ = false;
    }

    uint32_t clockHz() const noexcept { return clockHz_; }
    uint64_t phi2Cycle() const noexcept { return phi2Cycle_; }
    const Mos6510& cpu() const noexcept { return cpu_; }
    Mos6510& cpu() noexcept { return cpu_; }
    const VicII& vic() const noexcept { return vic_; }
    Cia6526& cia1() noexcept { return cia1_; }
    Cia6526& cia2() noexcept { return cia2_; }
    const Cia6526& cia1() const noexcept { return cia1_; }
    const Cia6526& cia2() const noexcept { return cia2_; }
    const std::array<uint8_t, 32>& sidRegisterImage() const noexcept { return sidRegs_; }
    const std::array<std::array<uint8_t, 32>, 5>& sidRegisterBanks() const noexcept { return sidRegsByChip_; }

    // Render-owned rollback journal for PSID/RSID bridge transactions.
    // This replaces the old full-C64Platform snapshot used by the audio thread:
    // only mutable machine substate is snapshotted, and RAM/ColorRAM are restored
    // through bounded dirty-cell journals. No heap allocation, locks or full 64K
    // platform copies occur while a play transaction is active.
    bool beginRenderMutationJournal() noexcept {
        if (renderJournal_.active) return false;
        renderJournal_ = {};
        renderJournal_.active = true;
        // v837: clear the O(1) dedup markers for this journal window.
        ramJournalSeen_.reset();
        colorJournalSeen_.reset();
        renderJournal_.phi2Cycle = phi2Cycle_;
        renderJournal_.openBusLatch = openBusLatch_;
        renderJournal_.lastRead = lastRead_;
        renderJournal_.lastSidReg = lastSidReg_;
        renderJournal_.lastSidValue = lastSidValue_;
        renderJournal_.lastSidWriteCycle = lastSidWriteCycle_;
        renderJournal_.sidWriteObserved = sidWriteObserved_;
        renderJournal_.cpu = cpu_;
        renderJournal_.vic = vic_;
        renderJournal_.cia1 = cia1_;
        renderJournal_.cia2 = cia2_;
        renderJournal_.queue = queue_;
        renderJournal_.sidRegs = sidRegs_;
        renderJournal_.sidRegsByChip = sidRegsByChip_;
        renderJournal_.sidBases = sidBases_;
        renderJournal_.sidChipCount = sidChipCount_;
        renderJournal_.lastSidChip = lastSidChip_;
        renderJournal_.colorRamHighNibbleOpenBusReadCount = colorRamHighNibbleOpenBusReadCount_;
        renderJournal_.sidNoSinkOpenBusReadCount = sidNoSinkOpenBusReadCount_;
        renderJournal_.sidNoSinkPotxyReadCount = sidNoSinkPotxyReadCount_;
        renderJournal_.booted = booted_;
        renderJournal_.realtimeSidCoreRunning = realtimeSidCoreRunning_;
        renderJournal_.bootState = bootState_;
        renderJournal_.irqVectorRamLedger = irqVectorRamLedger_;
        renderJournal_.nmiVectorRamLedger = nmiVectorRamLedger_;
        renderJournal_.cpuIrqVectorLedger = cpuIrqVectorLedger_;
        renderJournal_.cpuNmiVectorLedger = cpuNmiVectorLedger_;
        renderJournal_.interruptLedgerPlayAddress = interruptLedgerPlayAddress_;
        renderJournal_.irqTrampolineAddress = irqTrampolineAddress_;
        renderJournal_.nmiTrampolineAddress = nmiTrampolineAddress_;
        renderJournal_.irqTrampolineLength = irqTrampolineLength_;
        renderJournal_.nmiTrampolineLength = nmiTrampolineLength_;
        renderJournal_.irqTrampolineChecksum = irqTrampolineChecksum_;
        renderJournal_.nmiTrampolineChecksum = nmiTrampolineChecksum_;
        renderJournal_.irqAckCia1 = irqAckCia1_;
        renderJournal_.nmiAckCia2 = nmiAckCia2_;
        renderJournal_.interruptValidationFailureCount = interruptValidationFailureCount_;
        renderJournal_.interruptLastValidationPhi2 = interruptLastValidationPhi2_;
        renderJournal_.psidCiaRunGeneration = psidCiaRunGeneration_;
        renderJournal_.psidCiaServiceGeneration = psidCiaServiceGeneration_;
        renderJournal_.sidSink = sidSink_;
        return true;
    }

    void commitRenderMutationJournal() noexcept {
        renderJournal_.active = false;
        renderJournal_.dirtyRamCount = 0u;
        renderJournal_.dirtyColorCount = 0u;
        renderJournal_.overflow = false;
    }

    bool rollbackRenderMutationJournal() noexcept {
        if (!renderJournal_.active) return false;
        for (uint32_t i = renderJournal_.dirtyRamCount; i > 0u; --i) {
            const auto& e = renderJournal_.dirtyRam[i - 1u];
            memory_.write(e.address, e.value);
        }
        for (uint32_t i = renderJournal_.dirtyColorCount; i > 0u; --i) {
            const auto& e = renderJournal_.dirtyColor[i - 1u];
            colorRam_[e.address & 0x03FFu] = e.value;
        }
        phi2Cycle_ = renderJournal_.phi2Cycle;
        openBusLatch_ = renderJournal_.openBusLatch;
        lastRead_ = renderJournal_.lastRead;
        lastSidReg_ = renderJournal_.lastSidReg;
        lastSidValue_ = renderJournal_.lastSidValue;
        lastSidWriteCycle_ = renderJournal_.lastSidWriteCycle;
        sidWriteObserved_ = renderJournal_.sidWriteObserved;
        cpu_ = renderJournal_.cpu;
        vic_ = renderJournal_.vic;
        cia1_ = renderJournal_.cia1;
        cia2_ = renderJournal_.cia2;
        queue_ = renderJournal_.queue;
        sidRegs_ = renderJournal_.sidRegs;
        sidRegsByChip_ = renderJournal_.sidRegsByChip;
        sidBases_ = renderJournal_.sidBases;
        sidChipCount_ = renderJournal_.sidChipCount;
        lastSidChip_ = renderJournal_.lastSidChip;
        colorRamHighNibbleOpenBusReadCount_ = renderJournal_.colorRamHighNibbleOpenBusReadCount;
        sidNoSinkOpenBusReadCount_ = renderJournal_.sidNoSinkOpenBusReadCount;
        sidNoSinkPotxyReadCount_ = renderJournal_.sidNoSinkPotxyReadCount;
        booted_ = renderJournal_.booted;
        realtimeSidCoreRunning_ = renderJournal_.realtimeSidCoreRunning;
        bootState_ = renderJournal_.bootState;
        irqVectorRamLedger_ = renderJournal_.irqVectorRamLedger;
        nmiVectorRamLedger_ = renderJournal_.nmiVectorRamLedger;
        cpuIrqVectorLedger_ = renderJournal_.cpuIrqVectorLedger;
        cpuNmiVectorLedger_ = renderJournal_.cpuNmiVectorLedger;
        interruptLedgerPlayAddress_ = renderJournal_.interruptLedgerPlayAddress;
        irqTrampolineAddress_ = renderJournal_.irqTrampolineAddress;
        nmiTrampolineAddress_ = renderJournal_.nmiTrampolineAddress;
        irqTrampolineLength_ = renderJournal_.irqTrampolineLength;
        nmiTrampolineLength_ = renderJournal_.nmiTrampolineLength;
        irqTrampolineChecksum_ = renderJournal_.irqTrampolineChecksum;
        nmiTrampolineChecksum_ = renderJournal_.nmiTrampolineChecksum;
        irqAckCia1_ = renderJournal_.irqAckCia1;
        nmiAckCia2_ = renderJournal_.nmiAckCia2;
        interruptValidationFailureCount_ = renderJournal_.interruptValidationFailureCount;
        interruptLastValidationPhi2_ = renderJournal_.interruptLastValidationPhi2;
        psidCiaRunGeneration_ = renderJournal_.psidCiaRunGeneration;
        psidCiaServiceGeneration_ = renderJournal_.psidCiaServiceGeneration;
        sidSink_ = renderJournal_.sidSink;
        const bool ok = !renderJournal_.overflow;
        commitRenderMutationJournal();
        return ok;
    }

    bool renderMutationJournalActive() const noexcept { return renderJournal_.active; }
    bool renderMutationJournalOverflowed() const noexcept { return renderJournal_.overflow; }

    // Seed the SID register image from an externally-produced register snapshot
    // (e.g. from a PHI2-machine init run). Only writes writable registers so
    // read-only lanes ($D419-$D41C) are never clobbered. Does not call the
    // attached sidSink_ so there are no audio side-effects.
    void seedSidRegImageFrom(const std::array<uint8_t, 32>& regs) noexcept {
        for (uint8_t r = 0u; r < 32u; ++r) {
            if (c64SidRegWriteable(r)) {
                sidRegs_[r] = regs[r];
                sidRegsByChip_[0][r] = regs[r];
            }
        }
    }

    // One-way PHI2 -> inspection mirror. This updates the legacy platform's
    // public register/last-write telemetry without executing a second CPU path
    // and without calling the attached SID sink.
    void seedSidBusMirrorFrom(
        const std::array<std::array<uint8_t, 32>, 5>& banks,
        uint8_t lastChip,
        uint8_t lastReg,
        uint8_t lastValue,
        uint64_t lastCycle) noexcept {
        const std::size_t count = std::min<std::size_t>(sidChipCount_, banks.size());
        for (std::size_t chip = 0u; chip < count; ++chip) {
            for (uint8_t reg = 0u; reg < 32u; ++reg) {
                if (!c64SidRegWriteable(reg)) continue;
                sidRegsByChip_[chip][reg] = banks[chip][reg];
                if (chip == 0u) sidRegs_[reg] = banks[chip][reg];
            }
        }
        if (lastChip < sidChipCount_ && lastReg < 32u) {
            lastSidChip_ = lastChip;
            lastSidReg_ = lastReg;
            lastSidValue_ = lastValue;
            lastSidWriteCycle_ = lastCycle;
            sidWriteObserved_ = true;
        }
    }
    uint8_t sidChipCount() const noexcept { return sidChipCount_; }
    uint16_t sidBase(uint8_t chip) const noexcept { return chip < sidChipCount_ ? sidBases_[chip] : 0u; }
    uint8_t lastSidChip() const noexcept { return lastSidChip_; }
    const std::array<uint8_t, 0x400>& colorRam() const noexcept { return colorRam_; }
    // audit #8: observed CPU Color-RAM reads (high nibble = approximated open bus).
    uint32_t colorRamHighNibbleOpenBusReadCount() const noexcept { return colorRamHighNibbleOpenBusReadCount_; }
    // audit #9: observed no-sink SID reads (open-bus / un-modelled hardware).
    uint32_t sidNoSinkOpenBusReadCount() const noexcept { return sidNoSinkOpenBusReadCount_; }
    // audit #11 (P1-13): observed no-sink CPU reads of POTX ($D419) / POTY ($D41A).
    // These paddle/pot A-D ports depend on un-modelled external analog hardware, so a
    // no-sink read of them is a PotXYApprox blocker — not merely generic open bus.
    uint32_t sidNoSinkPotxyReadCount() const noexcept { return sidNoSinkPotxyReadCount_; }
    C64IecBus& iec() noexcept { return iec_; }
    const C64IecBus& iec() const noexcept { return iec_; }
    C64TapePort& tape() noexcept { return tape_; }
    const C64TapePort& tape() const noexcept { return tape_; }
    bool loadBasicRom(const uint8_t* data, size_t size) noexcept { return roms_.loadBasic(data, size); }
    bool loadKernalRom(const uint8_t* data, size_t size) noexcept { return roms_.loadKernal(data, size); }
    bool loadCharacterRom(const uint8_t* data, size_t size) noexcept { return roms_.loadCharacter(data, size); }
    bool loadBasicRom(const uint8_t* data, size_t size, C64RomTrust trust) noexcept { return roms_.loadBasic(data, size, trust); }
    bool loadKernalRom(const uint8_t* data, size_t size, C64RomTrust trust) noexcept { return roms_.loadKernal(data, size, trust); }
    bool loadCharacterRom(const uint8_t* data, size_t size, C64RomTrust trust) noexcept { return roms_.loadCharacter(data, size, trust); }
    bool setBasicRomTrust(C64RomTrust trust) noexcept { return roms_.setBasicRomTrust(trust); }
    bool setKernalRomTrust(C64RomTrust trust) noexcept { return roms_.setKernalRomTrust(trust); }
    bool setCharacterRomTrust(C64RomTrust trust) noexcept { return roms_.setCharacterRomTrust(trust); }
    bool hasExternalBasicRom() const noexcept { return roms_.hasExternalBasic(); }
    bool hasExternalKernalRom() const noexcept { return roms_.hasExternalKernal(); }
    bool hasExternalCharacterRom() const noexcept { return roms_.hasExternalCharacter(); }
    bool hasCompleteExternalRomSet() const noexcept { return roms_.hasCompleteExternalRomSet(); }
    C64RomTrust basicRomTrust() const noexcept { return roms_.basicRomTrust(); }
    C64RomTrust kernalRomTrust() const noexcept { return roms_.kernalRomTrust(); }
    C64RomTrust characterRomTrust() const noexcept { return roms_.characterRomTrust(); }
    C64RomIdentity basicRomIdentity() const noexcept { return roms_.basicRomIdentity(); }
    C64RomIdentity kernalRomIdentity() const noexcept { return roms_.kernalRomIdentity(); }
    C64RomIdentity characterRomIdentity() const noexcept { return roms_.characterRomIdentity(); }
    bool hasVerifiedStockRomSet() const noexcept { return roms_.hasVerifiedStockRomSet(); }
    bool hasUnverifiedCompleteExternalRomSet() const noexcept { return roms_.hasUnverifiedCompleteExternalRomSet(); }
    uint32_t basicRomChecksum() const noexcept { return roms_.checksumBasic(); }
    uint32_t kernalRomChecksum() const noexcept { return roms_.checksumKernal(); }
    uint32_t characterRomChecksum() const noexcept { return roms_.checksumCharacter(); }
    uint32_t basicRomCrc32() const noexcept { return roms_.crc32Basic(); }
    uint32_t kernalRomCrc32() const noexcept { return roms_.crc32Kernal(); }
    uint32_t characterRomCrc32() const noexcept { return roms_.crc32Character(); }

    uint8_t effectiveProcessorPort() const noexcept {
        const uint8_t dir = cpu_.state().portDirection;
        const uint8_t data = cpu_.state().portData;
        // 6510 $0001 readback: bits selected as outputs return PORT&DDR;
        // low-six floating input bits read as pulled high; bits 6/7 are always
        // high on the C64 board.  This is the exact bit surface used by the
        // PLA decode for LORAM/HIRAM/CHAREN and by tunes that probe $01.
        return uint8_t(0xC0u | (data & dir) | (uint8_t(0x3Fu) & uint8_t(~dir)));
    }

    bool irqLine() const noexcept { return cpu_.irqLine(); }
    bool nmiLine() const noexcept { return cpu_.nmiLine(); }
    void setTrapBrkAsJam(bool enable) noexcept { cpu_.setTrapBrkAsJam(enable); }
    bool trapBrkAsJam() const noexcept { return cpu_.trapBrkAsJam(); }
    const C64BootState& bootState() const noexcept { return bootState_; }

    void publishPsidCiaRuntimeDriveSnapshot(const C64PsidCiaRuntimeDriveSnapshot& s) noexcept {
        psidCiaRunGeneration_ = s.runGeneration;
        psidCiaServiceGeneration_ = s.serviceGeneration;
        bootState_.psidCiaRunGeneration = s.runGeneration;
        bootState_.psidCiaServiceGeneration = s.serviceGeneration;
        bootState_.psidCiaIrqObserved = s.irqObserved;
        bootState_.psidCiaCpuIrqLineObserved = s.cpuIrqLineObserved;
        bootState_.psidCiaVectorEntered = s.vectorEntered;
        bootState_.psidCiaPlayAddressEntered = s.playAddressEntered;
        bootState_.psidCiaAckObserved = s.ciaAckObserved;
        bootState_.psidCiaTicksToIrq = s.ticksToIrq;
        bootState_.psidCiaTicksToVector = s.ticksToVector;
        bootState_.psidCiaTicksToPlay = s.ticksToPlay;
    }

    void publishPhi2InspectionState(uint16_t pc, uint8_t a, uint8_t x, uint8_t y,
                                    uint8_t sp, uint8_t p, bool jammed,
                                    bool irqLine, bool nmiLine,
                                    uint8_t portDirection, uint8_t portData,
                                    uint64_t phi2Cycle,
                                    const Cia6526& cia1, const Cia6526& cia2,
                                    const VicII& vic) noexcept {
        Mos6510State& s = cpu_.state();
        s.pc = pc;
        s.a = a;
        s.x = x;
        s.y = y;
        s.sp = sp;
        s.p = static_cast<uint8_t>(p & ~Mos6510::B);
        s.jammed = jammed;
        s.irqLine = irqLine;
        s.nmiLine = nmiLine;
        s.portDirection = portDirection;
        s.portData = portData;
        s.phi2Cycle = phi2Cycle;
        phi2Cycle_ = phi2Cycle;
        memory_.write(0x0000u, portDirection);
        memory_.write(0x0001u, portData);
        cia1_ = cia1;
        cia2_ = cia2;
        vic_ = vic;
    }

    void coldBootForSidLoad() noexcept {
        // Deterministic C64 power-on surface used before PSID/RSID payload load.
        // This is intentionally not a fake direct-PC shortcut: it initializes the
        // 6510 processor port, stack, KERNAL-visible vectors, screen/color RAM,
        // CIA/VIC/IEC/tape side surfaces and records a boot-state stamp that GUI
        // telemetry can expose. It remains bounded and ROM-independent so the
        // audio thread never performs an unbounded KERNAL boot.
        cpu_.state().portDirection = 0x2Fu;
        cpu_.state().portData = 0x37u;
        memory_.write(0x0000u, 0x2Fu);
        memory_.write(0x0001u, 0x37u);
        cpu_.state().sp = 0xFFu;
        cpu_.state().p = static_cast<uint8_t>(Mos6510::I | Mos6510::U);
        cpu_.state().a = 0u;
        cpu_.state().x = 0u;
        cpu_.state().y = 0u;
        cpu_.state().jammed = false;
        cpu_.state().phi2Cycle = phi2Cycle_;
        // v872 hardening: a cold boot must reset the CIAs and VIC (power-on
        // semantics). loadPsid() is a per-tune boundary whose contract promises
        // reuse-safety without an intervening reset(), but coldBootForSidLoad()
        // previously left CIA/VIC timer state from the prior tune intact — so right
        // after loadPsid() a reused C64Runtime still held tune A's CIA1 Timer-A
        // latch. installPsidCiaPlaybackBootstrap() reads latchA()!=0xFFFF as
        // "tune-programmed", so that transient stale latch is a real correctness gap
        // for anything inspecting CIA state between load and init. (End to end it is
        // currently masked: runInit()'s reset-vector execution reprograms the CIA
        // before the bootstrap heuristic runs — see c64_psid_cia_latch_reuse_v872 —
        // but relying on that is fragile.) Reset restores the 0xFFFF sentinel per
        // tune; reset() preserves clockHz_, so re-apply clock/TOD to match reset(pal_).
        cia1_.reset();
        cia2_.reset();
        cia1_.setClockHz(clockHz_);
        cia2_.setClockHz(clockHz_);
        cia1_.setTod50Hz(pal_);
        cia2_.setTod50Hz(pal_);
        vic_.reset(pal_);
        psidCiaInstalledTimerALatchValid_ = false;  // v872 P0-2: re-armed per tune at bootstrap
        for (uint16_t i = 0; i < 1000u; ++i) memory_.write(static_cast<uint16_t>(0x0400u + i), 0x20u);
        for (uint16_t i = 0; i < 0x400u; ++i) colorRam_[i] = 0x0Eu;
        colorRamHighNibbleOpenBusReadCount_ = 0;  // audit #8
        sidNoSinkOpenBusReadCount_ = 0;           // audit #9
        sidNoSinkPotxyReadCount_ = 0;            // audit #11 (P1-13)
        // Common KERNAL/BASIC zero-page workspace values used by many players.
        memory_.write(0x0286u, 0x0Eu); // current text color
        memory_.write(0x0288u, 0x04u); // screen page high byte ($0400)
        memory_.write(0x0314u, 0x58u); memory_.write(0x0315u, 0xFFu); // IRQ vector -> safe RTI until a player installs one
        memory_.write(0x0318u, 0x00u); memory_.write(0x0319u, 0xFEu); // NMI vector
        installPsidSafeVectors();
        iec_.reset();
        tape_.reset();
        cia1_.setPortAInput(0xFFu);
        cia1_.setPortBInput(0xFFu);
        cia2_.setPortAInput(iec_.cia2PortAInputMask());
        booted_ = true;
        realtimeSidCoreRunning_ = false;
        bootState_.mode = C64BootMode::ColdPowerOn;
        bootState_.coldBootComplete = true;
        bootState_.coldBootPhi2 = phi2Cycle_;
        ARPSID_BOOT_TRACE("COLD",
            "power-on %s phi2=%llu port01=$%02X/$%02X sp=$%02X",
            pal_ ? "PAL" : "NTSC",
            static_cast<unsigned long long>(phi2Cycle_),
            static_cast<unsigned>(cpu_.state().portDirection),
            static_cast<unsigned>(cpu_.state().portData),
            static_cast<unsigned>(cpu_.state().sp));
    }

    void markSidImageLoaded(uint16_t loadAddress, uint16_t initAddress, uint16_t playAddress, uint16_t subtune,
                            uint32_t bytesLoaded = 0, uint32_t bytesTruncated = 0, bool usesCiaTiming = false) noexcept {
        bootState_.sidImageLoaded = true;
        bootState_.loadAddress = loadAddress;
        bootState_.initAddress = initAddress;
        bootState_.playAddress = playAddress;
        bootState_.currentSubtune = subtune;
        bootState_.sidPayloadBytesLoaded = bytesLoaded;
        bootState_.sidPayloadBytesTruncated = bytesTruncated;
        bootState_.sidUsesCiaTiming = usesCiaTiming;
        bootState_.sidChipCount = sidChipCount_;
        for (uint8_t i = 0; i < 5u; ++i) bootState_.sidBase[i] = sidBases_[i];
        bootState_.sidInitBootstrapInstalled = false;
        bootState_.sidInitDispatched = false;
        bootState_.sidInitCompleted = false;
        bootState_.sidPlayReady = false;
        bootState_.sidPlayBootstrapInstalled = false;
        bootState_.sidPlayDispatched = false;
        bootState_.sidPlayCompleted = false;
        bootState_.playBootstrapAddress = 0;
        bootState_.playInstructionBudget = 0;
        bootState_.playInstructionsExecuted = 0;
        bootState_.playStartPhi2 = 0;
        bootState_.playEndPhi2 = 0;
        bootState_.playCallCount = 0;
        resetSidInterruptBootstrapForNewSidInit_();
    }

    void resetSidInterruptBootstrapForNewSidInit_() noexcept {
        (void)cia1_.read(0x0Du);
        (void)cia2_.read(0x0Du);
        cpu_.irq(cia1_.irq() || vic_.irq());
        cpu_.nmi(cia2_.irq());
        bootState_.psidCiaIrqObserved = false;
        bootState_.psidCiaCpuIrqLineObserved = false;
        bootState_.psidCiaVectorEntered = false;
        bootState_.psidCiaPlayAddressEntered = false;
        bootState_.psidCiaAckObserved = false;
        bootState_.psidCiaTicksToIrq = 0;
        bootState_.psidCiaTicksToVector = 0;
        bootState_.psidCiaTicksToPlay = 0;
        bootState_.psidCiaServiceGeneration = 0;
    }

    void markSidInitStart(C64BootMode mode, uint16_t bootstrapAddress, uint16_t subtune, uint32_t budget) noexcept {
        bootState_.mode = mode;
        bootState_.bootstrapAddress = bootstrapAddress;
        bootState_.currentSubtune = subtune;
        bootState_.initInstructionBudget = budget;
        bootState_.initInstructionsExecuted = 0;
        bootState_.sidInitBootstrapInstalled = true;
        bootState_.sidInitDispatched = true;
        bootState_.sidInitCompleted = false;
        bootState_.sidPlayReady = false;
        bootState_.initStartPhi2 = phi2Cycle_;
        bootState_.initEndPhi2 = 0;
        // v872 P0-1/P0-2: baseline the CIA1 Timer-A latch/control write counters at
        // init start. installPsidCiaPlaybackBootstrap() compares against these to
        // learn whether the TUNE wrote the latch/control during init — the correct
        // signal, unlike latchA()!=0xFFFF which cannot represent an intentional $FFFF
        // and ignores the tune's $DC0E control mode entirely.
        cia1TimerALatchWritesAtInitStart_ = cia1_.timerALatchWriteCount();
        cia1TimerAControlWritesAtInitStart_ = cia1_.timerAControlWriteCount();
    }

    void markSidInitInstruction() noexcept { ++bootState_.initInstructionsExecuted; }

    void markSidInitComplete(bool ok) noexcept {
        bootState_.sidInitCompleted = ok;
        bootState_.sidPlayReady = ok && (bootState_.playAddress != 0u || bootState_.mode == C64BootMode::RsidMachine);
        bootState_.initEndPhi2 = phi2Cycle_;
    }

    void markSidPlayStart(uint16_t bootstrapAddress, uint32_t budget) noexcept {
        bootState_.playBootstrapAddress = bootstrapAddress;
        bootState_.playInstructionBudget = budget;
        bootState_.playInstructionsExecuted = 0;
        bootState_.sidPlayBootstrapInstalled = true;
        bootState_.sidPlayDispatched = true;
        bootState_.sidPlayCompleted = false;
        bootState_.playStartPhi2 = phi2Cycle_;
        bootState_.playEndPhi2 = 0;
    }

    void markSidPlayInstruction() noexcept { ++bootState_.playInstructionsExecuted; }

    void markSidPlayComplete(bool ok) noexcept {
        bootState_.sidPlayCompleted = ok;
        bootState_.playEndPhi2 = phi2Cycle_;
        if (ok) ++bootState_.playCallCount;
    }

    // v873 P0-12: init-time processor-port I/O map. A PSID init routine whose entry
    // sits in a RAM-under-ROM region is shadowed by ROM when $01 exposes that ROM,
    // so its RAM code never executes. Bank out ONLY the shadowing ROM, keeping I/O
    // visible for SID access; routines in normal RAM ($0000-$9FFF, $C000-$CFFF) keep
    // the full $37 map unchanged. Derived from the C64 PLA:
    //   $A000-$BFFF (under BASIC):  $36  (LORAM=0 -> RAM, KERNAL+I/O kept)
    //   $E000-$FFFF (under KERNAL): $35  (HIRAM=0 -> RAM, BASIC+I/O kept)
    //   otherwise:                  $37  (BASIC+KERNAL+I/O, unchanged)
    // ($D000-$DFFF is intentionally left at $37 — no real tune inits under I/O, and
    // exposing RAM there ($34) would also hide I/O.)
    static constexpr uint8_t psidInitIomapForAddress(uint16_t addr) noexcept {
        if (addr >= 0xA000u && addr <= 0xBFFFu) return 0x36u;
        if (addr >= 0xE000u)                    return 0x35u;
        return 0x37u;
    }

    // v891: frame-play routines need the same ROM-shadow banking normalization as
    // init. A PSID play routine can live under BASIC/KERNAL ROM even when init did
    // not. Keep I/O visible for SID/CIA/VIC and bank out only the ROM shadowing the
    // target code. CIA-play IRQ bootstrap also uses this before JSR play.
    static constexpr uint8_t psidPlayIomapForAddress(uint16_t addr) noexcept {
        return psidInitIomapForAddress(addr);
    }

    uint16_t installPsidInitBootstrap(uint16_t initAddr, uint8_t songIndex, uint16_t entry = 0x0334u,
                                      bool psidCiaIdleAfterInit = false) noexcept {
        uint16_t pc = entry;
        auto emit = [&](uint8_t b) noexcept { memory_.write(pc++, b); };
        emit(0x78u);                                          // SEI
        emit(0xA2u); emit(0xFFu); emit(0x9Au);                 // LDX #$FF; TXS
        emit(0xA9u); emit(0x2Fu); emit(0x85u); emit(0x00u);    // LDA #$2F; STA $00
        emit(0xA9u); emit(psidInitIomapForAddress(initAddr)); emit(0x85u); emit(0x01u); // LDA #iomap(init); STA $01
        emit(0xA9u); emit(songIndex);                         // A=subtune-1
        emit(0xA2u); emit(0x00u); emit(0xA0u); emit(0x00u);    // X=0; Y=0
        emit(0x20u); emit(uint8_t(initAddr & 0xFFu)); emit(uint8_t(initAddr >> 8u));
        if (psidCiaIdleAfterInit) {
            emit(0x58u);                                      // CLI
            const uint16_t idle = pc;
            emit(0xEAu);                                      // NOP
            emit(0x4Cu); emit(uint8_t(idle & 0xFFu)); emit(uint8_t(idle >> 8u)); // JMP idle
            bootState_.psidCiaIdleLoopAddress = idle;
            bootState_.rsidIdleAddress = idle;
        } else {
            emit(0x4Cu); emit(0xFFu); emit(0xFFu);             // JMP $FFFF deterministic halt
            bootState_.psidCiaIdleLoopAddress = 0u;
        }
        memory_.write(0xFFFCu, uint8_t(entry & 0xFFu));
        memory_.write(0xFFFDu, uint8_t(entry >> 8u));
        // Mirror into deterministic fallback KERNAL vector space only when no
        // external KERNAL ROM is installed. User-supplied original ROM images
        // are immutable; PSID bootstraps live in RAM-under-ROM and switch RAM in
        // when they need deterministic private vectors.
        if (roms_.canPatchKernalRom()) {
            roms_.pokeKernal(0xFFFCu, uint8_t(entry & 0xFFu));
            roms_.pokeKernal(0xFFFDu, uint8_t(entry >> 8u));
        }
        return entry;
    }

    // v893: authoritative installed-bootstrap code lengths. The PHI2 runtime
    // mirrors exactly this many bytes of platform RAM into PHI2 RAM before
    // dispatch. v891 grew the play bootstrap from 6 to 10 bytes (the
    // LDA #iomap / STA $01 banking prefix) while the PHI2 mirror kept copying
    // 6 — truncating the JSR operand, jamming every direct runPlay() call.
    // static_asserts in the installers keep these honest if the code grows.
    static constexpr uint16_t kPsidPlayBootstrapCodeLength = 10u;
    static constexpr uint16_t kPsidCiaBootstrapCodeLength = 16u;

    uint16_t installPsidPlayBootstrap(uint16_t playAddr, uint16_t entry = 0x0350u) noexcept {
        // Proper PSID frame-play dispatch surface: preserve initialized C64
        // machine state, call the play routine as a subroutine, then halt at a
        // deterministic sentinel. Unlike the legacy direct-PC trampoline, this
        // does not reset $0000/$0001, SP or registers between frames; players
        // that keep state in zero page/stack/CIA/VIC now see the same machine
        // image produced by init and prior play calls.
        const uint8_t code[] = {
            0xA9u, psidPlayIomapForAddress(playAddr), 0x85u, 0x01u,       // LDA #iomap(play); STA $01
            0x20, uint8_t(playAddr & 0xFFu), uint8_t(playAddr >> 8u),
            0x4C, 0xFF, 0xFF
        };
        static_assert(sizeof(code) == kPsidPlayBootstrapCodeLength,
                      "PHI2 mirror copies kPsidPlayBootstrapCodeLength bytes — keep in sync");
        for (uint16_t i = 0; i < sizeof(code); ++i) memory_.write(static_cast<uint16_t>(entry + i), code[i]);
        memory_.write(0xFFFCu, uint8_t(entry & 0xFFu));
        memory_.write(0xFFFDu, uint8_t(entry >> 8u));
        if (roms_.canPatchKernalRom()) {
            roms_.pokeKernal(0xFFFCu, uint8_t(entry & 0xFFu));
            roms_.pokeKernal(0xFFFDu, uint8_t(entry >> 8u));
        }
        return entry;
    }

    uint16_t installPsidCiaPlaybackBootstrap(uint16_t playAddr, uint16_t entry = 0x0370u) noexcept {
        const uint8_t code[] = {
            0xADu, 0x0Du, 0xDCu,                             // LDA $DC0D, acknowledge CIA1 ICR
            0xA9u, psidPlayIomapForAddress(playAddr), 0x85u, 0x01u, // LDA #iomap(play); STA $01
            0x20u, uint8_t(playAddr & 0xFFu), uint8_t(playAddr >> 8u),
            0x68u, 0xA8u,                                    // PLA; TAY  (restore Y saved by $FF48)
            0x68u, 0xAAu,                                    // PLA; TAX  (restore X)
            0x68u,                                           // PLA       (restore A)
            0x40u                                            // RTI       (hardware IRQ frame)
        };
        static_assert(sizeof(code) == kPsidCiaBootstrapCodeLength,
                      "PHI2 mirror copies kPsidCiaBootstrapCodeLength bytes — keep in sync");
        for (uint16_t i = 0; i < sizeof(code); ++i) {
            memory_.write(static_cast<uint16_t>(entry + i), code[i]);
        }
        memory_.write(0x0314u, uint8_t(entry & 0xFFu));
        memory_.write(0x0315u, uint8_t(entry >> 8u));
        installPsidSafeVectors();
        memory_.write(0x0314u, uint8_t(entry & 0xFFu));
        memory_.write(0x0315u, uint8_t(entry >> 8u));

        // v856 missing-logic fix (tune-programmed CIA tempo): this bootstrap is
        // installed AFTER the tune's init routine ran. A CIA-speed tune's init
        // may already have programmed Timer A ($DC04/$DC05) for its own tempo
        // (multi-speed 2x/4x, tracker tempos). Unconditionally writing the
        // 50/60 Hz default here CLOBBERED that programming, forcing every such
        // tune back to single speed. The CIA reset latch is 0xFFFF, so a
        // non-0xFFFF latch after init proves the tune set its own tempo —
        // preserve it and only install the default when init left the timer
        // untouched. IRQ enable/timer start below applies in both cases.
        // v872 P0-1/P0-2: detect tune-authored programming from write-count deltas
        // captured at init start (markSidInitStart), NOT from the final latch value.
        // latchA()!=0xFFFF cannot represent an intentional $FFFF latch and says nothing
        // about the $DC0E control mode, so an intentional max-period tune was clobbered
        // to the default and every tune's one-shot/CNT/PB6 control bits were forced to
        // continuous. Write counts tell us exactly what the tune touched.
        const bool tuneProgrammedTimerA =
            cia1_.timerALatchWriteCount() != cia1TimerALatchWritesAtInitStart_;
        const bool tuneWroteTimerAControl =
            cia1_.timerAControlWriteCount() != cia1TimerAControlWritesAtInitStart_;

        // Preserve a tune-authored latch (including an intentional $FFFF); only install
        // the 50/60 Hz default when the tune's init did not write Timer A. Stop the
        // timer first only in the default-install case (matches the prior flow).
        if (!tuneProgrammedTimerA) {
            cia1_.write(0x0Eu, 0x00u);
            const uint16_t latch = expectedPsidCiaTimerALatch_();
            cia1_.write(0x04u, uint8_t(latch & 0xFFu));
            cia1_.write(0x05u, uint8_t(latch >> 8u));
        }
        // v872 P0-2: remember what was actually installed so integrity telemetry
        // validates against it instead of always against the VBI default.
        psidCiaInstalledTimerALatch_ = cia1_.latchA();
        psidCiaInstalledTimerALatchValid_ = true;
        (void)cia1_.read(0x0Du);
        cia1_.write(0x0Du, uint8_t(0x80u | Cia6526::IcrTimerA));
        if (tuneWroteTimerAControl) {
            // Preserve the tune's Timer-A control mode (one-shot, CNT source, PB6
            // output, serial), while still ensuring the timer is started and
            // force-loads the (preserved or default) latch so playback actually runs.
            const uint8_t cra = uint8_t((cia1_.peekControlA() | 0x01u | 0x10u) & 0xFFu);
            cia1_.write(0x0Eu, cra);
        } else {
            cia1_.write(0x0Eu, 0x11u); // default: force-load + start, continuous
        }
        cpu_.irq(cia1_.irq() || vic_.irq());

        bootState_.playAddress = playAddr;
        bootState_.playBootstrapAddress = entry;
        bootState_.sidPlayBootstrapInstalled = true;
        bootState_.sidPlayReady = playAddr != 0u;
        bootState_.sidUsesCiaTiming = true;
        bootState_.psidCiaIdleLoopAddress = bootState_.psidCiaIdleLoopAddress ? bootState_.psidCiaIdleLoopAddress
                                                                               : bootState_.rsidIdleAddress;

        installInterruptLedger_(playAddr, entry, static_cast<uint8_t>(sizeof(code)), true);
        return entry;
    }

    void installPsidSafeVectors() noexcept {
        // RAM underneath KERNAL contains safe interrupt vectors, while deterministic
        // KERNAL ROM exposes the same vectors by default. This is a bounded HLE
        // KERNAL surface for PSID/RSID projection without host-side ROM I/O:
        //   $FF48: PHA/TXA/PHA/TYA/PHA/JMP ($0314)  (CINV chain)
        //   $FE43: JMP ($0318)                      (NMINV chain)
        //   $EA31: LDA $DC0D; PLA/TAY; PLA/TAX; PLA; RTI
        //   $FE47: LDA $DD0D; RTI
        //   $FF8D: RESTOR-style vector copy from $FD30->$0314
        //   $E000: reset stub that sets $00/$01, calls RESTOR, CLI, RTS.
        const uint8_t irqEntry[] = {0x48u,0x8Au,0x48u,0x98u,0x48u,0x6Cu,0x14u,0x03u};
        for (uint16_t i = 0; i < sizeof(irqEntry); ++i) memory_.write(uint16_t(0xFF48u + i), irqEntry[i]);
        for (uint16_t a = 0xFF50u; a <= 0xFF57u; ++a) memory_.write(a, 0xEAu); // NOP pad
        memory_.write(0xFF58u, 0x40u); // default local RTI target for CINV
        const uint8_t nmiChain[] = {0x6Cu,0x18u,0x03u}; // JMP ($0318)
        for (uint16_t i = 0; i < sizeof(nmiChain); ++i) memory_.write(uint16_t(0xFE43u + i), nmiChain[i]);
        memory_.write(0xFE47u, 0xADu); memory_.write(0xFE48u, 0x0Du); memory_.write(0xFE49u, 0xDDu); memory_.write(0xFE4Au, 0x40u); // LDA $DD0D; RTI
        memory_.write(0xEA31u, 0xADu); memory_.write(0xEA32u, 0x0Du); memory_.write(0xEA33u, 0xDCu); // LDA $DC0D
        memory_.write(0xEA34u, 0x68u); memory_.write(0xEA35u, 0xA8u); memory_.write(0xEA36u, 0x68u); memory_.write(0xEA37u, 0xAAu); memory_.write(0xEA38u, 0x68u); memory_.write(0xEA39u, 0x40u);
        const uint8_t restor[] = {0xA2u,0x1Fu,0xBDu,0x30u,0xFDu,0x9Du,0x14u,0x03u,0xCAu,0x10u,0xF7u,0x60u};
        for (uint16_t i = 0; i < sizeof(restor); ++i) memory_.write(uint16_t(0xFF8Du + i), restor[i]);
        const uint8_t resetStub[] = {0x78u,0xD8u,0xA2u,0xFFu,0x9Au,0xA9u,0x2Fu,0x85u,0x00u,0xA9u,0x37u,0x85u,0x01u,0x20u,0x8Du,0xFFu,0x58u,0x60u};
        for (uint16_t i = 0; i < sizeof(resetStub); ++i) memory_.write(uint16_t(0xE000u + i), resetStub[i]);
        // Default vector table source for RESTOR. Only the first three vectors are
        // material for SID playback; the remaining bytes are deterministic zeros.
        for (uint16_t i = 0; i < 32u; ++i) memory_.write(uint16_t(0xFD30u + i), 0x00u);
        memory_.write(0xFD30u, 0x31u); memory_.write(0xFD31u, 0xEAu); // CINV -> $EA31
        memory_.write(0xFD32u, 0x58u); memory_.write(0xFD33u, 0xFFu); // CBINV/default -> $FF58
        memory_.write(0xFD34u, 0x47u); memory_.write(0xFD35u, 0xFEu); // NMINV -> $FE47
        memory_.write(0x0314u, 0x58u); memory_.write(0x0315u, 0xFFu);
        memory_.write(0x0318u, 0x47u); memory_.write(0x0319u, 0xFEu);
        memory_.write(0xFFFAu, 0x43u); memory_.write(0xFFFBu, 0xFEu);
        memory_.write(0xFFFCu, 0x00u); memory_.write(0xFFFDu, 0xE0u);
        memory_.write(0xFFFEu, 0x48u); memory_.write(0xFFFFu, 0xFFu);
        if (roms_.canPatchKernalRom()) {
            roms_.pokeKernal(0xFFFAu, 0x43u); roms_.pokeKernal(0xFFFbu, 0xFEu);
            roms_.pokeKernal(0xFFFCu, 0x00u); roms_.pokeKernal(0xFFFDu, 0xE0u);
            roms_.pokeKernal(0xFFFEu, 0x48u); roms_.pokeKernal(0xFFFFu, 0xFFu);
            for (uint16_t a = 0xFF48u; a <= 0xFF98u; ++a) roms_.pokeKernal(a, memory_.read(a));
            for (uint16_t a = 0xFE43u; a <= 0xFE4Au; ++a) roms_.pokeKernal(a, memory_.read(a));
            for (uint16_t a = 0xEA31u; a <= 0xEA39u; ++a) roms_.pokeKernal(a, memory_.read(a));
            for (uint16_t a = 0xE000u; a <= 0xE011u; ++a) roms_.pokeKernal(a, memory_.read(a));
            for (uint16_t a = 0xFD30u; a <= 0xFD4Fu; ++a) roms_.pokeKernal(a, memory_.read(a));
        }
        installInterruptLedger_(0u, 0xFF58u, 1u, false);
    }

    uint16_t installRsidBootstrap(uint16_t initAddr, uint8_t songNumber = 1u, uint16_t entry = 0x0334u,
                                  bool realtimeIdleAfterInit = false) noexcept {
        const uint8_t song = songNumber ? uint8_t(songNumber - 1u) : 0u;
        uint16_t pc = entry;
        auto emit = [&](uint8_t b) noexcept { memory_.write(pc++, b); };
        emit(0x78u); emit(0xA2u); emit(0xFFu); emit(0x9Au);                         // SEI; LDX #$FF; TXS
        emit(0xA9u); emit(0x2Fu); emit(0x8Du); emit(0x00u); emit(0x00u);             // LDA #$2F; STA $0000
        emit(0xA9u); emit(0x37u); emit(0x8Du); emit(0x01u); emit(0x00u);             // LDA #$37; STA $0001
        emit(0xA9u); emit(song);  emit(0xA2u); emit(0x00u); emit(0xA0u); emit(0x00u); // LDA #song; LDX #0; LDY #0
        emit(0x20u); emit(uint8_t(initAddr & 0xFFu)); emit(uint8_t(initAddr >> 8u));  // JSR init
        if (realtimeIdleAfterInit) {
            emit(0x58u);                                                             // CLI
            const uint16_t idle = pc;
            emit(0x4Cu); emit(uint8_t(idle & 0xFFu)); emit(uint8_t(idle >> 8u));      // JMP idle, IRQ-capable
            bootState_.rsidIdleAddress = idle;
        } else {
            emit(0x4Cu); emit(0xFFu); emit(0xFFu);                                   // JMP $FFFF sentinel
            bootState_.rsidIdleAddress = 0u;
        }
        cpu_.state().portDirection = 0x2Fu;
        cpu_.state().portData = 0x35u; // map RAM at vector fetch until bootstrap restores $37
        memory_.write(0xFFFCu, uint8_t(entry & 0xFFu));
        memory_.write(0xFFFDu, uint8_t(entry >> 8u));
        return entry;
    }

    void attachSid(SidRegisterSink* sid) noexcept { sidSink_ = sid; }

    bool configurePsidSidBases(const uint16_t* bases, uint8_t count) noexcept {
        if (!bases || count == 0u || count > 5u) return false;
        std::array<uint16_t, 5> candidate{};
        for (uint8_t i = 0; i < count; ++i) {
            // Route only through the shared validator so the runtime cannot accept a
            // base the parser rejects (e.g. $D800 color RAM, $DC00/$DD00 CIA) — those
            // would otherwise steal CIA/color-RAM addresses once the SID router keys
            // off configured windows (v874 audit P0-2).
            if (!ArpSID::psidValidConfiguredSidBase(bases[i], i)) return false;
            for (uint8_t j = 0; j < i; ++j) {
                if (candidate[j] == bases[i]) return false;
            }
            candidate[i] = bases[i];
        }
        sidBases_ = candidate;
        sidChipCount_ = count;
        bootState_.sidChipCount = count;
        for (uint8_t i = 0; i < 5u; ++i) bootState_.sidBase[i] = sidBases_[i];
        return true;
    }

    uint32_t loadBytesToRam(uint16_t address, const uint8_t* data, uint32_t size, uint32_t* truncated = nullptr) noexcept {
        if (truncated) *truncated = 0;
        if (!data || size == 0u) return 0u;
        const uint32_t capacity = 65536u - static_cast<uint32_t>(address);
        const uint32_t n = size < capacity ? size : capacity;
        for (uint32_t i = 0; i < n; ++i) {
            const auto a = static_cast<uint16_t>(address + i);
            logMemoryWrite_(a);
            memory_.write(a, data[i]);
        }
        if (truncated && size > n) *truncated = size - n;
        return n;
    }

    bool scheduleCpuWrite(uint16_t address, uint8_t value, uint64_t phi2Offset = 0) noexcept {
        return queue_.push(BusCycle{address, value, false, phi2Cycle_ + phi2Offset, 0, false});
    }
    bool scheduleCpuRead(uint16_t address, uint64_t phi2Offset = 0) noexcept {
        return queue_.push(BusCycle{address, 0, true, phi2Cycle_ + phi2Offset, 0, false});
    }
    bool scheduleProjectionMirrorCpuWrite(uint16_t address, uint8_t value, uint64_t phi2Offset = 0) noexcept {
        // Projection mirror observer writes are intentionally queued even at
        // phi2Offset==0. They are speculative until the block-end mirror publisher
        // decides to advance the cosmetic C64 platform; if it does not, they can be
        // discarded without having already mutated SID registers at sample zero.
        return queue_.push(BusCycle{address, value, false, phi2Cycle_ + phi2Offset, 0, true});
    }

    // v877 audit closure: discard only block-local projection-mirror writes. Do
    // not clear ordinary scheduled CPU/CIA/PSID events; the v875 queue-wide clear
    // was safe for the narrow synthetic test but too destructive for shared
    // C64Platform callers.
    void clearScheduledProjectionWrites() noexcept { queue_.clearPendingProjectionMirrorEvents(); }

    // v899: block-local flush — apply the VALUES of pending projection-mirror
    // events through the normal bus-application path (PLA visibility, SID
    // routing), then remove them so they cannot replay in a future block
    // (the v885 law). This replaces the destructive clear on the mirror
    // publisher's consumed path: the per-block mirror cycle cap stays a
    // deliberate performance guard, but capped-out writes now still land in
    // the SID register image instead of vanishing.
    void flushScheduledProjectionWrites() noexcept {
        queue_.flushPendingProjectionMirrorEvents(
            [this](const BusCycle& c) noexcept { applyBusCycle(c); });
    }

    // v585: sidSink_ is scoped to the call when a temporary sid is provided.
    // Previously, passing sid != nullptr permanently replaced sidSink_; any
    // temporary override (e.g. for passive-advance or per-instruction tracing)
    // silently leaked into subsequent calls. The fix saves sidSink_ before
    // setting the temporary and restores it on return. Callers that want to
    // permanently change the sink use attachSid() instead.
    void runCycles(uint64_t cycles, SidRegisterSink* sid = nullptr) noexcept {
        SidRegisterSink* const prev = sidSink_;
        if (sid) sidSink_ = sid;
        const uint64_t end = phi2Cycle_ + cycles;
        while (phi2Cycle_ < end) stepPhi2_();
        if (sid) sidSink_ = prev;
    }

    uint8_t executeInstruction(SidRegisterSink* sid = nullptr) noexcept {
        SidRegisterSink* const prev = sidSink_;
        if (sid) sidSink_ = sid;
        while (!vic_.cpuCanUseBus()) stepPhi2_(false);
        const uint64_t before = cpu_.state().phi2Cycle;
        const uint8_t opcodeHint = peekMappedNoSideEffects_(cpu_.state().pc);
        const uint8_t cycles = cpu_.tickWithOpcodeHint(*this, opcodeHint);
        const uint64_t target = cpu_.state().phi2Cycle;
        while (phi2Cycle_ < target) stepPhi2_(false);
        if (cycles == 0 && before == target) stepPhi2_();
        if (sid) sidSink_ = prev;
        return cycles;
    }

    uint8_t readOpenBus() const noexcept { return openBusLatch_.value(); }
    uint8_t openBusDecayMask() const noexcept { return openBusLatch_.decayMask; }
    uint64_t openBusLastDrivenPhi2() const noexcept { return openBusLatch_.lastDrivenPhi2; }
    uint64_t openBusAgePhi2() const noexcept {
        return phi2Cycle_ >= openBusLatch_.lastDrivenPhi2
            ? (phi2Cycle_ - openBusLatch_.lastDrivenPhi2)
            : 0u;
    }
    bool openBusDrivenWithinPersistence() const noexcept {
        return openBusLatch_.drivenWithinPersistence(phi2Cycle_);
    }
    uint8_t lastReadValue() const noexcept { return lastRead_; }
    uint8_t lastSidRegister() const noexcept { return lastSidReg_; }
    uint8_t lastSidValue() const noexcept { return lastSidValue_; }
    uint64_t lastSidWriteCycle() const noexcept { return lastSidWriteCycle_; }
    bool sidWriteObserved() const noexcept { return sidWriteObserved_; }

    uint8_t cpuRead(uint16_t address) noexcept override {
        const uint8_t v = readMapped_(address);
        latchOpenBus_(v);
        lastRead_ = v;
        return v;
    }

    void cpuWrite(uint16_t address, uint8_t value) noexcept override {
        latchOpenBus_(value);
        writeMapped_(address, value);
    }

    uint8_t peekMemory(uint16_t address) const noexcept { return memory_.read(address); }
    uint8_t peekMappedNoSideEffects(uint16_t address) const noexcept { return peekMappedNoSideEffects_(address); }
    void pokeMemory(uint16_t address, uint8_t value) noexcept { logMemoryWrite_(address); memory_.write(address, value); }
    // Color RAM ($D800-$DBFF) is a separate 4-bit device, not part of memory_.
    // pokeMemory() routes this range into the under-IO RAM map, so it never
    // reaches the visible colorRam_ that colorRam() exposes. Mirroring PHI2
    // color-RAM writes back to the platform must go through this accessor, which
    // journals the pre-write value so a render-transaction rollback restores it.
    void pokeColorRam(uint16_t address, uint8_t value) noexcept {
        if (!isColorRam(address)) return;
        logColorRamWrite_(address);
        colorRam_[address & 0x03FFu] = static_cast<uint8_t>(value & 0x0Fu);
    }
    C64VisibleDevice visibleDevice(uint16_t address) const noexcept { return c64PlaVisibleDeviceWithCartridge(address, effectiveProcessorPort(), cartridge_.exromLineHigh(), cartridge_.gameLineHigh()); }
    const C64RomSet& roms() const noexcept { return roms_; }
    C64RomSet& roms() noexcept { return roms_; }
    const C64CartridgeImage& cartridge() const noexcept { return cartridge_; }
    C64CartridgeImage& cartridge() noexcept { return cartridge_; }
    void attachCartridge(C64CartridgeMode mode) noexcept { cartridge_.setMode(mode); }
    void setCartridgeBank(uint8_t bank) noexcept { cartridge_.setBank(bank); }
    void setCartridgeLines(bool exromHigh, bool gameHigh) noexcept { cartridge_.setLines(exromHigh, gameHigh); }

    // Realtime SID-core boot/run surface. This is the single deterministic
    // C64 platform execution surface used by SID projection, PSID/C64Runtime
    // parity and GUI/HUD telemetry. It is intentionally bounded: the caller
    // supplies a maximum instruction count so audio render can advance CIA/VIC/
    // bus/SID time for the full PHI2 budget without risking an unbounded CPU
    // loop on hostile or unfinished C64 code.
    void bootFromResetVector() noexcept {
        const uint8_t lo = readMapped_(0xFFFCu);
        const uint8_t hi = readMapped_(0xFFFDu);
        cpu_.state().pc = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8u));
        cpu_.state().sp = 0xFFu;
        cpu_.state().p = static_cast<uint8_t>(Mos6510::I | Mos6510::U);
        cpu_.state().jammed = false;
        cpu_.state().phi2Cycle = phi2Cycle_;
        booted_ = true;
    }

    void bootFromResetVectorPreservingMachine() noexcept {
        const uint8_t lo = readMapped_(0xFFFCu);
        const uint8_t hi = readMapped_(0xFFFDu);
        cpu_.state().pc = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8u));
        cpu_.state().jammed = false;
        cpu_.state().phi2Cycle = phi2Cycle_;
        booted_ = true;
    }

    void startRealtimeSidCore() noexcept { if (!booted_) bootFromResetVector(); realtimeSidCoreRunning_ = true; }
    void stopRealtimeSidCore() noexcept { realtimeSidCoreRunning_ = false; }
    bool realtimeSidCoreRunning() const noexcept { return realtimeSidCoreRunning_; }
    bool booted() const noexcept { return booted_; }

    RselCorpusResult runRsidCorpus(const RselCorpusCase* cases, size_t count, SidRegisterSink* sid = nullptr) noexcept {
        RselCorpusResult result{};
        if (!cases && count != 0) return result;
        for (size_t i = 0; i < count; ++i) {
            const auto& c = cases[i];
            if (c.resetVector == 0u || c.maxInstructions == 0u) { ++result.casesFailed; continue; }
            reset(pal_);
            pokeMemory(0xFFFCu, uint8_t(c.resetVector & 0xFFu));
            pokeMemory(0xFFFDu, uint8_t(c.resetVector >> 8u));
            cpu_.state().portDirection = 0x2Fu;
            cpu_.state().portData = 0x35u; // expose RAM vectors during corpus fixture boot
            setTrapBrkAsJam(false);
            bootFromResetVector();
            startRealtimeSidCore();
            uint32_t executed = 0;
            while (executed < c.maxInstructions && !cpu_.state().jammed) { executeInstruction(sid); ++executed; }
            ++result.casesRun;
            if (executed > 0u) ++result.casesPassed; else ++result.casesFailed;
        }
        result.complete = result.casesRun == count && result.casesFailed == 0u;
        return result;
    }

    [[nodiscard]] C64RunResult runRealtimeSidCoreCycles(uint64_t cycles, uint32_t maxInstructions = 256u, SidRegisterSink* sid = nullptr) noexcept {
        C64RunResult result{};
        result.requestedCycles = cycles;
        // Scope the temporary sink to this call only. Previously passing a
        // temporary `sid` permanently replaced sidSink_ and never restored it,
        // so later C64 SID writes could be routed to a stale/foreign sink
        // (corrupting the bridge, telemetry images, and timed-write capture).
        SidRegisterSink* const prevSidSink = sidSink_;
        if (sid) sidSink_ = sid;
        struct SidSinkRestore {
            C64Platform* self; SidRegisterSink* prev; bool active;
            ~SidSinkRestore() noexcept { if (active) self->sidSink_ = prev; }
        } sinkRestore{this, prevSidSink, sid != nullptr};
        if (!booted_) bootFromResetVector();
        const uint64_t start = phi2Cycle_;
        const uint64_t end = phi2Cycle_ + cycles;
        const uint32_t instructionBudget = maxInstructions != 0u
            ? maxInstructions
            : static_cast<uint32_t>(std::min<uint64_t>(std::max<uint64_t>(cycles / 2u, 1u), 65535u));
        uint32_t executed = 0;
        while (realtimeSidCoreRunning_ && phi2Cycle_ < end && executed < instructionBudget && !cpu_.state().jammed) {
            const uint64_t before = phi2Cycle_;
            const uint8_t opcodeHint = peekMappedNoSideEffects_(cpu_.state().pc);
            const uint64_t instructionCycles = estimateInstructionCyclesNoSideEffects_(opcodeHint);
            const uint32_t halfStallsBefore = vic_.previewStolen(8u) * 2u;
            const uint64_t stallCycles = static_cast<uint64_t>(halfStallsBefore / 2u);
            if (instructionCycles + stallCycles > end - phi2Cycle_) break;
            (void)executeInstruction(sidSink_);
            // Half-cycle contention approximation: VIC BA/AEC low windows stall
            // CPU retirement while CIA/VIC/SID wall-clock continues. We keep the
            // model bounded and deterministic for audio-thread projection.
            for (uint32_t h = 0; h < halfStallsBefore && phi2Cycle_ < end; h += 2u) stepPhi2_(false);
            ++executed;
            if (phi2Cycle_ <= before) stepPhi2_(false);
        }
        result.executedInstructions = executed;
        result.cpuJammed = cpu_.state().jammed;
        result.instructionBudgetHit = (executed >= instructionBudget) && (phi2Cycle_ < end);
        // Record cycles consumed by CPU instructions before passive advance.
        result.executedCycles = (phi2Cycle_ >= start) ? (phi2Cycle_ - start) : 0u;
        // Unconditional passive catch-up: always advances PHI2 to exactly end.
        // executedCycles + passiveCycles == requestedCycles after this point.
        // Callers must debit the full requestedCycles from their cycle debt,
        // not just executedCycles, because the C64 wall clock has advanced by
        // the full budget regardless of CPU vs passive split.
        while (phi2Cycle_ < end) stepPhi2_(false);
        result.passiveCycles = (phi2Cycle_ >= start) ? (phi2Cycle_ - start) - result.executedCycles : 0u;
        result.completedCycleBudget = (phi2Cycle_ >= end);
        return result;
    }

    C64PsidCiaRuntimeDriveSnapshot runPsidCiaPlaybackIrqTicks(uint64_t maxTicks) noexcept {
        C64PsidCiaRuntimeDriveSnapshot out{};
        out.installed = irqAckCia1_ &&
                        irqTrampolineAckLengthSupported_() &&
                        bootState_.sidPlayBootstrapInstalled &&
                        bootState_.sidUsesCiaTiming &&
                        bootState_.playAddress != 0u;
        out.runGeneration = ++psidCiaRunGeneration_;
        bootState_.psidCiaRunGeneration = out.runGeneration;
        out.idleLoopAddress = bootState_.psidCiaIdleLoopAddress;
        out.irqTrampolineAddress = irqTrampolineAddress_;
        out.playAddress = bootState_.playAddress;
        out.ciaTimerALatch = cia1_.latchA();
        const uint64_t start = phi2Cycle_;
        const uint64_t deadline = phi2Cycle_ + maxTicks;
        uint32_t executed = 0;

        if (!booted_) bootFromResetVectorPreservingMachine();
        realtimeSidCoreRunning_ = true;

        while (phi2Cycle_ < deadline && !cpu_.state().jammed) {
            capturePsidCiaRuntimeEdges_(out, start);
            if (out.returnedToIdleAfterPlay) break;

            const uint64_t before = phi2Cycle_;
            if (vic_.cpuCanUseBus() && executed < 128u) {
                (void)executeInstruction(sidSink_);
                ++executed;
            } else {
                stepPhi2_();
                executed = 0;
            }
            if (phi2Cycle_ <= before) stepPhi2_();
            capturePsidCiaRuntimeEdges_(out, start);
        }

        out.vectorStillInstalled = vectorLedgerMatchesRam_() && vectorLedgerMatchesCpu_();
        out.trampolineShapeCanonical = irqTrampolineCanonical_();
        out.ciaLatchCorrect = cia1_.latchA() == expectedInstalledCiaTimerALatch_(); // v872 P0-2
        out.ticksExecuted = phi2Cycle_ >= start ? (phi2Cycle_ - start) : 0u;
        out.finalPc = cpu_.state().pc;
        out.cpuJammed = cpu_.state().jammed;
        out.cpuJamOpcode = 0u;
        out.serviceComplete = out.playAddressEntered && out.ciaAckObserved &&
                              out.returnedToIdleAfterPlay && !out.cpuJammed;
        const auto integrity = validateC64SystemIntegrity();
        out.systemIntegrityClean = integrity.valid && out.serviceComplete;
        bootState_.psidCiaIrqObserved = out.irqObserved;
        bootState_.psidCiaCpuIrqLineObserved = out.cpuIrqLineObserved;
        bootState_.psidCiaVectorEntered = out.vectorEntered;
        bootState_.psidCiaPlayAddressEntered = out.playAddressEntered;
        bootState_.psidCiaAckObserved = out.ciaAckObserved;
        bootState_.psidCiaTicksToIrq = out.ticksToIrq;
        bootState_.psidCiaTicksToVector = out.ticksToVector;
        bootState_.psidCiaTicksToPlay = out.ticksToPlay;
        bootState_.psidCiaServiceGeneration = out.serviceGeneration;
        return out;
    }

    C64PsidCiaRuntimeDriveSnapshot runPsidCiaPlaybackServiceTicks(uint64_t maxTicks) noexcept {
        return runPsidCiaPlaybackIrqTicks(maxTicks);
    }

    C64PsidCiaPlaybackBootstrapSnapshot psidCiaPlaybackBootstrapSnapshot() noexcept {
        C64PsidCiaPlaybackBootstrapSnapshot out{};
        out.installed = irqAckCia1_ &&
                        irqTrampolineAckLengthSupported_() &&
                        bootState_.sidPlayBootstrapInstalled &&
                        bootState_.sidUsesCiaTiming &&
                        bootState_.playAddress != 0u;
        out.vectorStillInstalled = vectorLedgerMatchesRam_() && vectorLedgerMatchesCpu_();
        out.trampolineShapeCanonical = irqTrampolineCanonical_();
        out.ciaLatchCorrect = cia1_.latchA() == expectedInstalledCiaTimerALatch_(); // v872 P0-2
        out.systemIntegrityClean = out.installed &&
                                   out.vectorStillInstalled &&
                                   out.trampolineShapeCanonical &&
                                   out.ciaLatchCorrect;
        out.runGeneration = bootState_.psidCiaRunGeneration;
        out.serviceGeneration = bootState_.psidCiaServiceGeneration;
        out.idleLoopAddress = bootState_.psidCiaIdleLoopAddress;
        out.irqTrampolineAddress = irqTrampolineAddress_;
        out.playAddress = bootState_.playAddress;
        out.ciaTimerALatch = cia1_.latchA();
        return out;
    }

    // Side-effect-free computation of the interrupt-bootstrap validation snapshot.
    // Everything read here is const; the failure counter and last-validation cycle
    // are maintained only by the mutating validate...() wrapper below.
    C64InterruptBootstrapValidationSnapshot computeInterruptBootstrapValidation_() const noexcept {
        C64InterruptBootstrapValidationSnapshot out{};
        out.irqVectorRam = readVectorRam_(0x0314u);
        out.nmiVectorRam = readVectorRam_(0x0318u);
        out.cpuIrqVector = readVectorCpu_(0xFFFEu);
        out.cpuNmiVector = readVectorCpu_(0xFFFAu);
        out.irqTrampolineAddress = irqTrampolineAddress_;
        out.nmiTrampolineAddress = nmiTrampolineAddress_;
        out.irqTrampolineLength = irqTrampolineLength_;
        out.nmiTrampolineLength = nmiTrampolineLength_;
        out.playAddress = interruptLedgerPlayAddress_;
        out.irqAckCia1 = irqAckCia1_;
        out.nmiAckCia2 = nmiAckCia2_;
        out.irqTrampolineChecksum = checksumMemory_(irqTrampolineAddress_, irqTrampolineLength_);
        out.nmiTrampolineChecksum = checksumMemory_(nmiTrampolineAddress_, nmiTrampolineLength_);
        out.vectorLedgerMatchesRam = vectorLedgerMatchesRam_();
        out.vectorLedgerMatchesCpu = vectorLedgerMatchesCpu_();
        out.irqChecksumMatchesLedger = out.irqTrampolineChecksum == irqTrampolineChecksum_;
        out.nmiChecksumMatchesLedger = out.nmiTrampolineChecksum == nmiTrampolineChecksum_;
        out.irqTrampolineShapeCanonical = irqTrampolineCanonical_();
        out.nmiTrampolineShapeCanonical = nmiTrampolineCanonical_();
        out.playAddressMatchesLedger = interruptLedgerPlayAddress_ == 0u ||
                                       interruptLedgerPlayAddress_ == bootState_.playAddress;
        out.ackPolicyMatchesLedger = ((irqAckCia1_ && irqTrampolineAckLengthSupported_()) ||
                                      (!irqAckCia1_ && irqTrampolineLength_ == 1u)) &&
                                     ((nmiAckCia2_ && nmiTrampolineLength_ == 4u) ||
                                      (!nmiAckCia2_ && nmiTrampolineLength_ == 1u));
        out.checksumLedgerValid = out.irqChecksumMatchesLedger && out.nmiChecksumMatchesLedger;
        out.valid = out.checksumLedgerValid &&
                    out.vectorLedgerMatchesRam &&
                    out.vectorLedgerMatchesCpu &&
                    out.irqTrampolineShapeCanonical &&
                    out.nmiTrampolineShapeCanonical &&
                    out.playAddressMatchesLedger &&
                    out.ackPolicyMatchesLedger;
        out.validationFailureCount = interruptValidationFailureCount_;
        out.lastValidationPhi2 = phi2Cycle_;
        return out;
    }

    // Pure inspection: same snapshot, no telemetry mutation. Per-cycle inspectors
    // (e.g. PSID-CIA edge capture) MUST use this — the mutating variant increments
    // interruptValidationFailureCount_ on every invalid-window call, so polling it
    // per PHI2 tick would inflate the failure count by thousands per service call
    // and turn a poll-frequency measurement into a false "failure" reading.
    C64InterruptBootstrapValidationSnapshot peekInstalledInterruptBootstrap() const noexcept {
        return computeInterruptBootstrapValidation_();
    }

    C64InterruptBootstrapValidationSnapshot validateInstalledInterruptBootstrap() noexcept {
        C64InterruptBootstrapValidationSnapshot out = computeInterruptBootstrapValidation_();
        if (!out.valid) ++interruptValidationFailureCount_;
        out.validationFailureCount = interruptValidationFailureCount_;
        interruptLastValidationPhi2_ = phi2Cycle_;
        return out;
    }

    C64SystemIntegritySnapshot validateC64SystemIntegrity() noexcept {
        C64SystemIntegritySnapshot out{};
        const auto boot = validateInstalledInterruptBootstrap();
        out.supportedSidChipLimit = sidChipCount_ >= 1u && sidChipCount_ <= 5u;
        out.sidBusThreeChipAuthorityClosed = sidChipCount_ <= 3u;
        out.sidBaseRangeUnique = sidBasesValidUnique_();
        out.integerPhi2CycleMode = cpu_.state().phi2Cycle == phi2Cycle_;
        out.processorPortCoherent = memory_.read(0x0000u) == cpu_.state().portDirection &&
                                    memory_.read(0x0001u) == cpu_.state().portData;
        out.resetVectorPresent = readVectorCpu_(0xFFFCu) != 0u;
        out.irqNmiLineCoherent = (cpu_.irqLine() == (cia1_.irq() || vic_.irq())) &&
                                 (cpu_.nmiLine() == cia2_.irq());
        out.interruptBootstrapPolicyClosed = boot.valid;
        out.checksumLedgerValid = boot.checksumLedgerValid;
        out.vectorLedgerMatchesRam = boot.vectorLedgerMatchesRam;
        out.vectorLedgerMatchesCpu = boot.vectorLedgerMatchesCpu;
        out.noBusRetireAbort = true;
        out.noSliceHardOvershoot = true;
        out.noTimedWriteOverflow = queueDepthSafe_();
        out.validClockRange = (clockHz_ >= 900000u && clockHz_ <= 1100000u);
        out.validVicBankTelemetrySurface = true;
        out.officialOpcodeMicrosequenceComplete = c64Mos6510AllOfficialOpcodesHavePromotedMicroSequence();
        out.tickHasNoSpeculativeVisibleFetch = true;
        out.realtimeQualityAcceptable = true;
        out.allKilOpcodesPromoted = c64Mos6510AllKilOpcodesHavePromotedMicroSequence();
        out.cpuSemanticFallbackCount = cpu_.state().semanticFallbackCount;
        out.cpuOfficialSemanticFallbackCount = cpu_.state().officialSemanticFallbackCount;
        out.cpuLastSemanticFallbackOpcode = cpu_.state().lastSemanticFallbackOpcode;
        out.noOfficialSemanticFallbackObserved = cpu_.state().officialSemanticFallbackCount == 0u;
        out.validationFailureCount = boot.validationFailureCount;
        out.lastValidationPhi2 = phi2Cycle_;
        out.valid = out.supportedSidChipLimit &&
                    out.sidBaseRangeUnique &&
                    out.integerPhi2CycleMode &&
                    out.processorPortCoherent &&
                    out.resetVectorPresent &&
                    out.irqNmiLineCoherent &&
                    out.interruptBootstrapPolicyClosed &&
                    out.noTimedWriteOverflow &&
                    out.validClockRange &&
                    out.validVicBankTelemetrySurface &&
                    out.officialOpcodeMicrosequenceComplete &&
                    out.tickHasNoSpeculativeVisibleFetch &&
                    out.sidBusThreeChipAuthorityClosed &&
                    out.realtimeQualityAcceptable &&
                    out.allKilOpcodesPromoted &&
                    out.noOfficialSemanticFallbackObserved;
        return out;
    }

    C64RsidDebugSnapshot rsidDebugSnapshot() noexcept {
        C64RsidDebugSnapshot out{};
        const auto boot = validateInstalledInterruptBootstrap();
        out.interruptBootstrapValid = boot.valid;
        out.irqLineAsserted = cpu_.irqLine();
        out.nmiLineAsserted = cpu_.nmiLine();
        out.cia1IrqAsserted = cia1_.irq();
        out.cia2IrqAsserted = cia2_.irq();
        out.vicIrqAsserted = vic_.irq();
        out.ramIrqVector = boot.irqVectorRam;
        out.ramNmiVector = boot.nmiVectorRam;
        out.cpuIrqVector = boot.cpuIrqVector;
        out.cpuNmiVector = boot.cpuNmiVector;
        out.irqTrampolineAddress = boot.irqTrampolineAddress;
        out.nmiTrampolineAddress = boot.nmiTrampolineAddress;
        out.irqTrampolineLength = boot.irqTrampolineLength;
        out.nmiTrampolineLength = boot.nmiTrampolineLength;
        out.irqTrampolineChecksum = boot.irqTrampolineChecksum;
        out.nmiTrampolineChecksum = boot.nmiTrampolineChecksum;
        out.irqAckCia1 = boot.irqAckCia1;
        out.nmiAckCia2 = boot.nmiAckCia2;
        out.nmiEntryAutoRelocated = nmiTrampolineAddress_ != 0xFE00u;
        out.cia1LatchA = cia1_.latchA();
        out.cia2LatchA = cia2_.latchA();
        out.cia1IcrFlags = cia1_.irqFlags();
        out.cia2IcrFlags = cia2_.irqFlags();
        out.psidCiaRunGeneration = bootState_.psidCiaRunGeneration;
        out.psidCiaServiceGeneration = bootState_.psidCiaServiceGeneration;
        out.psidCiaTicksToIrq = bootState_.psidCiaTicksToIrq;
        out.psidCiaTicksToVector = bootState_.psidCiaTicksToVector;
        out.psidCiaTicksToPlay = bootState_.psidCiaTicksToPlay;
        return out;
    }

private:
    struct DirtyByteEntry final {
        uint16_t address = 0;
        uint8_t value = 0;
    };

    struct RenderMutationJournal final {
        static constexpr uint32_t kMaxDirtyRam = 8192u;
        static constexpr uint32_t kMaxDirtyColor = 1024u;
        bool active = false;
        bool overflow = false;
        uint32_t dirtyRamCount = 0u;
        uint32_t dirtyColorCount = 0u;
        std::array<DirtyByteEntry, kMaxDirtyRam> dirtyRam{};
        std::array<DirtyByteEntry, kMaxDirtyColor> dirtyColor{};
        uint64_t phi2Cycle = 0;
        OpenBusLatch openBusLatch{};
        uint8_t lastRead = 0;
        uint8_t lastSidReg = 0;
        uint8_t lastSidValue = 0;
        uint64_t lastSidWriteCycle = 0;
        bool sidWriteObserved = false;
        Mos6510 cpu{};
        VicII vic{};
        Cia6526 cia1{};
        Cia6526 cia2{};
        SidBusQueue queue{};
        std::array<uint8_t, 32> sidRegs{};
        std::array<std::array<uint8_t, 32>, 5> sidRegsByChip{};
        std::array<uint16_t, 5> sidBases{{0xD400u, 0u, 0u, 0u, 0u}};
        uint8_t sidChipCount = 1;
        uint8_t lastSidChip = 0;
        uint32_t colorRamHighNibbleOpenBusReadCount = 0;
        uint32_t sidNoSinkOpenBusReadCount = 0;
        uint32_t sidNoSinkPotxyReadCount = 0;
        bool booted = false;
        bool realtimeSidCoreRunning = false;
        C64BootState bootState{};
        uint16_t irqVectorRamLedger = 0xFF58u;
        uint16_t nmiVectorRamLedger = 0xFE00u;
        uint16_t cpuIrqVectorLedger = 0xFF48u;
        uint16_t cpuNmiVectorLedger = 0xFE00u;
        uint16_t interruptLedgerPlayAddress = 0;
        uint16_t irqTrampolineAddress = 0xFF58u;
        uint16_t nmiTrampolineAddress = 0xFE00u;
        uint8_t irqTrampolineLength = 1;
        uint8_t nmiTrampolineLength = 1;
        uint32_t irqTrampolineChecksum = 0;
        uint32_t nmiTrampolineChecksum = 0;
        bool irqAckCia1 = false;
        bool nmiAckCia2 = false;
        uint32_t interruptValidationFailureCount = 0;
        uint64_t interruptLastValidationPhi2 = 0;
        uint64_t psidCiaRunGeneration = 0;
        uint64_t psidCiaServiceGeneration = 0;
        SidRegisterSink* sidSink = nullptr;
    };

    void logMemoryWrite_(uint16_t address) noexcept {
        if (!renderJournal_.active) return;
        // v837: O(1) dedup (was an O(n) linear scan → O(n^2) per play call).
        // Record each cell's pre-mutation value exactly once per journal window.
        if (ramJournalSeen_.test(address)) return;
        if (renderJournal_.dirtyRamCount >= RenderMutationJournal::kMaxDirtyRam) {
            renderJournal_.overflow = true;
            return;
        }
        ramJournalSeen_.set(address);
        renderJournal_.dirtyRam[renderJournal_.dirtyRamCount++] = DirtyByteEntry{address, memory_.read(address)};
    }

    void logColorRamWrite_(uint16_t address) noexcept {
        if (!renderJournal_.active) return;
        const uint16_t cell = static_cast<uint16_t>(address & 0x03FFu);
        // v837: O(1) dedup (was an O(n) linear scan → O(n^2) per play call).
        if (colorJournalSeen_.test(cell)) return;
        if (renderJournal_.dirtyColorCount >= RenderMutationJournal::kMaxDirtyColor) {
            renderJournal_.overflow = true;
            return;
        }
        colorJournalSeen_.set(cell);
        renderJournal_.dirtyColor[renderJournal_.dirtyColorCount++] = DirtyByteEntry{cell, colorRam_[cell]};
    }

    uint16_t expectedPsidCiaTimerALatch_() const noexcept {
        return psidCiaTimerALatchForVideoStandard(pal_);
    }
    // v872 P0-2: the latch integrity should be validated against — the tune's own
    // programmed value if the bootstrap preserved one, else the VBI default.
    uint16_t expectedInstalledCiaTimerALatch_() const noexcept {
        return psidCiaInstalledTimerALatchValid_ ? psidCiaInstalledTimerALatch_
                                                 : expectedPsidCiaTimerALatch_();
    }

    uint32_t checksumMemory_(uint16_t address, uint8_t length) const noexcept {
        uint32_t h = 2166136261u;
        for (uint8_t i = 0; i < length; ++i) {
            h ^= memory_.read(static_cast<uint16_t>(address + i));
            h *= 16777619u;
        }
        return h;
    }

    uint16_t readVectorRam_(uint16_t address) const noexcept {
        return static_cast<uint16_t>(memory_.read(address) |
                                     (static_cast<uint16_t>(memory_.read(static_cast<uint16_t>(address + 1u))) << 8u));
    }

    uint16_t readVectorCpu_(uint16_t address) const noexcept {
        return static_cast<uint16_t>(peekMappedNoSideEffects_(address) |
                                     (static_cast<uint16_t>(peekMappedNoSideEffects_(static_cast<uint16_t>(address + 1u))) << 8u));
    }

    void installInterruptLedger_(uint16_t playAddr, uint16_t irqAddress, uint8_t irqLength, bool irqAckCia1) noexcept {
        interruptLedgerPlayAddress_ = playAddr;
        irqTrampolineAddress_ = irqAddress;
        irqTrampolineLength_ = irqLength;
        irqTrampolineChecksum_ = checksumMemory_(irqTrampolineAddress_, irqTrampolineLength_);
        nmiTrampolineAddress_ = readVectorRam_(0x0318u);
        if (nmiTrampolineAddress_ == 0u) nmiTrampolineAddress_ = 0xFE47u;
        nmiTrampolineLength_ = (nmiTrampolineAddress_ == 0xFE47u) ? 4u : 1u;
        nmiTrampolineChecksum_ = checksumMemory_(nmiTrampolineAddress_, nmiTrampolineLength_);
        irqVectorRamLedger_ = readVectorRam_(0x0314u);
        nmiVectorRamLedger_ = readVectorRam_(0x0318u);
        cpuIrqVectorLedger_ = readVectorCpu_(0xFFFEu);
        cpuNmiVectorLedger_ = readVectorCpu_(0xFFFAu);
        irqAckCia1_ = irqAckCia1;
        nmiAckCia2_ = (nmiTrampolineAddress_ == 0xFE47u && nmiTrampolineLength_ == 4u);
    }

    bool vectorLedgerMatchesRam_() const noexcept {
        return readVectorRam_(0x0314u) == irqVectorRamLedger_ &&
               readVectorRam_(0x0318u) == nmiVectorRamLedger_;
    }

    bool vectorLedgerMatchesCpu_() const noexcept {
        return readVectorCpu_(0xFFFEu) == cpuIrqVectorLedger_ &&
               readVectorCpu_(0xFFFAu) == cpuNmiVectorLedger_;
    }

    // v893: accepted CIA-acked IRQ trampoline lengths. v891 grew the CIA
    // playback bootstrap from 12 to 16 bytes (LDA #iomap / STA $01 banking
    // prefix after the $DC0D ack) but the validator still whitelisted only
    // 7/12 — every validation of a correctly-installed v891 bootstrap failed.
    // 7 and 12 remain accepted for legacy ledger shapes.
    bool irqTrampolineAckLengthSupported_() const noexcept {
        return irqTrampolineLength_ == 7u || irqTrampolineLength_ == 12u ||
               irqTrampolineLength_ == kPsidCiaBootstrapCodeLength;
    }

    bool irqTrampolineCanonical_() const noexcept {
        if (irqTrampolineLength_ == 1u) {
            return memory_.read(irqTrampolineAddress_) == 0x40u;
        }
        if (irqTrampolineLength_ == kPsidCiaBootstrapCodeLength) {
            // v891 shape: LDA $DC0D / LDA #iomap / STA $01 / JSR play /
            // PLA/TAY/PLA/TAX/PLA / RTI (16 bytes).
            return memory_.read(irqTrampolineAddress_) == 0xADu &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 1u)) == 0x0Du &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 2u)) == 0xDCu &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 3u)) == 0xA9u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 4u)) ==
                       psidPlayIomapForAddress(interruptLedgerPlayAddress_) &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 5u)) == 0x85u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 6u)) == 0x01u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 7u)) == 0x20u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 8u)) == uint8_t(interruptLedgerPlayAddress_ & 0xFFu) &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 9u)) == uint8_t(interruptLedgerPlayAddress_ >> 8u) &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 10u)) == 0x68u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 11u)) == 0xA8u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 12u)) == 0x68u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 13u)) == 0xAAu &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 14u)) == 0x68u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 15u)) == 0x40u;
        }
        if (irqTrampolineLength_ == 7u) {
            return memory_.read(irqTrampolineAddress_) == 0xADu &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 1u)) == 0x0Du &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 2u)) == 0xDCu &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 3u)) == 0x20u &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 4u)) == uint8_t(interruptLedgerPlayAddress_ & 0xFFu) &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 5u)) == uint8_t(interruptLedgerPlayAddress_ >> 8u) &&
                   memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 6u)) == 0x40u;
        }
        if (irqTrampolineLength_ != 12u) return false;
        return memory_.read(irqTrampolineAddress_) == 0xADu &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 1u)) == 0x0Du &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 2u)) == 0xDCu &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 3u)) == 0x20u &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 4u)) == uint8_t(interruptLedgerPlayAddress_ & 0xFFu) &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 5u)) == uint8_t(interruptLedgerPlayAddress_ >> 8u) &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 6u)) == 0x68u &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 7u)) == 0xA8u &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 8u)) == 0x68u &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 9u)) == 0xAAu &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 10u)) == 0x68u &&
               memory_.read(static_cast<uint16_t>(irqTrampolineAddress_ + 11u)) == 0x40u;
    }

    bool nmiTrampolineCanonical_() const noexcept {
        if (nmiTrampolineLength_ == 1u) return memory_.read(nmiTrampolineAddress_) == 0x40u;
        if (nmiTrampolineLength_ != 4u) return false;
        return memory_.read(nmiTrampolineAddress_) == 0xADu &&
               memory_.read(static_cast<uint16_t>(nmiTrampolineAddress_ + 1u)) == 0x0Du &&
               memory_.read(static_cast<uint16_t>(nmiTrampolineAddress_ + 2u)) == 0xDDu &&
               memory_.read(static_cast<uint16_t>(nmiTrampolineAddress_ + 3u)) == 0x40u;
    }

    bool sidBasesValidUnique_() const noexcept {
        if (sidChipCount_ == 0u || sidChipCount_ > 5u) return false;
        for (uint8_t i = 0; i < sidChipCount_; ++i) {
            const uint16_t base = sidBases_[i];
            if (!ArpSID::psidValidConfiguredSidBase(base, i)) return false;
            for (uint8_t j = 0; j < i; ++j) {
                if (sidBases_[j] == base) return false;
            }
        }
        return true;
    }

    bool queueDepthSafe_() const noexcept {
        return queue_.size() <= SidBusQueue::kCapacity;
    }

    bool psidCiaPcInIdleLoop_(uint16_t pc) const noexcept {
        const uint16_t idle = bootState_.psidCiaIdleLoopAddress;
        return idle != 0u && pc >= idle && pc <= static_cast<uint16_t>(idle + 3u);
    }

    void capturePsidCiaRuntimeEdges_(C64PsidCiaRuntimeDriveSnapshot& out, uint64_t startPhi2) noexcept {
        const uint64_t elapsed = phi2Cycle_ >= startPhi2 ? phi2Cycle_ - startPhi2 : 0u;
        if (!out.irqObserved && ((cia1_.irqFlags() & Cia6526::IcrTimerA) != 0u || cia1_.irq())) {
            out.irqObserved = true;
            out.ticksToIrq = elapsed;
        }
        if (!out.cpuIrqLineObserved && cpu_.irqLine()) {
            out.cpuIrqLineObserved = true;
            out.ticksToIrq = out.ticksToIrq ? out.ticksToIrq : elapsed;
        }
        const uint16_t pc = cpu_.state().pc;
        if (!out.vectorEntered && (pc == cpuIrqVectorLedger_ || pc == irqTrampolineAddress_)) {
            out.vectorEntered = true;
            out.ticksToVector = elapsed;
        }
        if (!out.ciaAckObserved && out.irqObserved &&
            !cia1_.irq() && ((cia1_.irqFlags() & Cia6526::IcrTimerA) == 0u)) {
            out.ciaAckObserved = true;
        }
        if (!out.playAddressEntered && bootState_.playAddress != 0u && pc == bootState_.playAddress) {
            out.playAddressEntered = true;
            out.ticksToPlay = elapsed;
            out.serviceGeneration = ++psidCiaServiceGeneration_;
        }
        if (out.playAddressEntered && out.ciaAckObserved && !out.returnedToIdleAfterPlay &&
            psidCiaPcInIdleLoop_(pc)) {
            out.returnedToIdleAfterPlay = true;
            out.ticksToIdleAfterPlay = elapsed;
        }
    }

    bool sidAddressToChipReg_(uint16_t a, uint8_t& chip, uint8_t& reg) const noexcept {
        // Exact configured SID base windows win first. This lets secondary SIDs
        // at $D420/$D500/etc. own their explicit 32-byte windows.
        for (uint8_t i = 0; i < sidChipCount_; ++i) {
            const uint16_t base = sidBases_[i];
            if (base != 0u && a >= base && a < static_cast<uint16_t>(base + 0x20u)) {
                chip = i;
                reg = static_cast<uint8_t>(a - base);
                return true;
            }
        }
        // Physical primary SID mirrors every $20 from $D400-$D7FF. Keep this
        // fallback even when multi-SID is configured, otherwise unassigned
        // primary mirrors such as $D458/$D598 lose $D418 digi writes as soon as
        // a second SID exists. Exact secondary windows above still override it.
        if (sidBases_[0] == 0xD400u && isSid(a)) {
            chip = 0u;
            reg = static_cast<uint8_t>(a & 0x1Fu);
            return true;
        }
        return false;
    }
    // True when `a` is in the primary SID mirror ($D400-$D7FF) OR any configured
    // extra-SID window (e.g. a stereo/3-SID tune's SID2 at $DE00-$DFE0). The IO
    // read/write/peek routers key SID handling off THIS, not isSid(), so a legally
    // configured $DE00-$DFE0 secondary SID is routed instead of being dropped as
    // unmapped IO (v874 audit P0-1). Configured windows are validated to the legal
    // $D420-$D7E0 / $DE00-$DFE0 ranges, so this never shadows VIC/CIA/color RAM.
    bool addressInConfiguredSidWindow_(uint16_t a) const noexcept {
        for (uint8_t i = 0; i < sidChipCount_; ++i) {
            const uint16_t base = sidBases_[i];
            if (base != 0u && a >= base && a < static_cast<uint16_t>(base + 0x20u)) return true;
        }
        return isSid(a);
    }
    static bool isSid(uint16_t a) noexcept { return a >= 0xD400u && a <= 0xD7FFu; }
    static bool isVic(uint16_t a) noexcept { return a >= 0xD000u && a <= 0xD3FFu; }
    static bool isCia1(uint16_t a) noexcept { return a >= 0xDC00u && a <= 0xDCFFu; }
    static bool isCia2(uint16_t a) noexcept { return a >= 0xDD00u && a <= 0xDDFFu; }
    static bool isColorRam(uint16_t a) noexcept { return a >= 0xD800u && a <= 0xDBFFu; }

    struct NoSideEffectBus final : Mos6510Bus {
        explicit NoSideEffectBus(C64Platform* p) noexcept : platform(p) {}
        uint8_t cpuRead(uint16_t address) noexcept override {
            return platform ? platform->peekMappedNoSideEffects_(address) : 0xFFu;
        }
        void cpuWrite(uint16_t, uint8_t) noexcept override {}
        C64Platform* platform = nullptr;
    };

    uint64_t estimateInstructionCyclesNoSideEffects_(uint8_t opcodeHint) noexcept {
        Mos6510 cpuCopy = cpu_;
        NoSideEffectBus bus(this);
        const uint64_t before = cpuCopy.state().phi2Cycle;
        const uint8_t nominal = cpuCopy.tickWithOpcodeHint(bus, opcodeHint);
        const uint64_t after = cpuCopy.state().phi2Cycle;
        if (after > before) return after - before;
        return nominal ? static_cast<uint64_t>(nominal) : 1u;
    }

    void stepPhi2_(bool advanceCpuState = true) noexcept {
        while (const SidBusEvent* ev = queue_.nextBeforeOrAt(phi2Cycle_)) applyBusCycle(ev->cycle);
        updatePeripheralInputs_();
        (void)cia1_.tick();
        (void)cia2_.tick();
        (void)vic_.tick();
        cpu_.irq(cia1_.irq() || vic_.irq());
        cpu_.nmi(cia2_.irq());
        if (advanceCpuState) cpu_.advance(1);
        ++phi2Cycle_;
        decayOpenBus_();
    }

    void updateVicBankFromCia2_() noexcept {
        // Real C64 VIC bank selection is driven by CIA2 PA0/PA1. The two bits
        // are inverted on the board; VicII::setMemoryBank uses the physical CIA
        // value directly and internally maps 0->$C000, 1->$8000, 2->$4000,
        // 3->$0000. Re-evaluate after PRA/DDRA writes so bank switching is not
        // delayed until an unrelated helper touches the VIC.
        vic_.setMemoryBank(static_cast<uint8_t>(cia2_.portAPins() & 0x03u));
    }

    void updatePeripheralInputs_() noexcept {
        // CIA2 sees IEC clock/data on PA6/PA7. Tape sense/read are exposed on
        // CIA1 PB4/PB5 in this deterministic surface; exact KERNAL protocol stays
        // outside render, but RSID fixtures can now exercise the lines.
        cia2_.setPortAInput(iec_.cia2PortAInputMask());
        updateVicBankFromCia2_();
        uint8_t pb = 0xFFu;
        if (!tape_.sense()) pb &= uint8_t(~0x10u);
        if (!tape_.read())  pb &= uint8_t(~0x20u);
        cia1_.setPortBInput(pb);
    }

    void applyBusCycle(const BusCycle& c) noexcept {
        if (!c.rw) cpuWrite(c.address, c.data);
        else (void)cpuRead(c.address);
    }

    uint8_t peekMappedNoSideEffects_(uint16_t a) const noexcept {
        if (a == 0x0000u) return cpu_.state().portDirection;
        if (a == 0x0001u) return effectiveProcessorPort();

        switch (visibleDevice(a)) {
            case C64VisibleDevice::BasicRom: return roms_.readBasic(a);
            case C64VisibleDevice::KernalRom: return roms_.readKernal(a);
            case C64VisibleDevice::CharacterRom: return roms_.readCharacter(a);
            case C64VisibleDevice::CartridgeLo: return cartridge_.readRomL(a - 0x8000u);
            case C64VisibleDevice::CartridgeHi: return cartridge_.readRomH(a - ((a >= 0xE000u) ? 0xE000u : 0xA000u));
            case C64VisibleDevice::Io:
                if (addressInConfiguredSidWindow_(a)) {
                    uint8_t chip = 0, r = 0;
                    if (sidAddressToChipReg_(a, chip, r)) return sidReadNoSink_(chip, r);
                    return readOpenBus();
                }
                if (isColorRam(a)) return uint8_t((colorRam_[a & 0x03FFu] & 0x0Fu) | (readOpenBus() & 0xF0u));
                if (isCia1(a) && ((a & 0x0Fu) == 0x0Du)) {
                    return uint8_t((cia1_.irq() ? 0x80u : 0x00u) | (cia1_.irqFlags() & 0x1Fu));
                }
                if (isCia2(a) && ((a & 0x0Fu) == 0x0Du)) {
                    return uint8_t((cia2_.irq() ? 0x80u : 0x00u) | (cia2_.irqFlags() & 0x1Fu));
                }
                return memory_.read(a);
            case C64VisibleDevice::Ram:
            default:
                return memory_.read(a);
        }
    }

    uint8_t readMapped_(uint16_t a) noexcept {
        if (a == 0x0000u) return cpu_.state().portDirection;
        if (a == 0x0001u) return effectiveProcessorPort();

        switch (visibleDevice(a)) {
            case C64VisibleDevice::BasicRom: return roms_.readBasic(a);
            case C64VisibleDevice::KernalRom: return roms_.readKernal(a);
            case C64VisibleDevice::CharacterRom: return roms_.readCharacter(a);
            case C64VisibleDevice::CartridgeLo: return cartridge_.readRomL(a - 0x8000u);
            case C64VisibleDevice::CartridgeHi: return cartridge_.readRomH(a - ((a >= 0xE000u) ? 0xE000u : 0xA000u));
            case C64VisibleDevice::Io:
                if (addressInConfiguredSidWindow_(a)) {
                    uint8_t chip = 0, r = 0;
                    if (sidAddressToChipReg_(a, chip, r)) {
                        const uint8_t flat = static_cast<uint8_t>(chip * 32u + r);
                        return sidSink_ ? sidSink_->sidRead(flat, phi2Cycle_) : sidReadNoSink_(chip, r);
                    }
                    return readOpenBus();
                }
                if (isVic(a)) return vic_.read(static_cast<uint8_t>(a & 0x3Fu));
                if (isCia1(a)) return cia1_.read(static_cast<uint8_t>(a & 0x0Fu));
                if (isCia2(a)) return cia2_.read(static_cast<uint8_t>(a & 0x0Fu));
                // audit #8: CPU read of Color RAM — high nibble is approximated open bus.
                if (isColorRam(a)) { ++colorRamHighNibbleOpenBusReadCount_; return uint8_t((colorRam_[a & 0x03FFu] & 0x0Fu) | (readOpenBus() & 0xF0u)); }
                return readOpenBus();
            case C64VisibleDevice::Ram:
            default:
                return memory_.read(a);
        }
    }


    uint8_t sidReadNoSink_(uint8_t chip, uint8_t r) const noexcept {
        (void)chip;
        r &= 0x1Fu;
        // Physical SID readback: POTX/POTY are readable analog paddle ports and
        // float high when disconnected; OSC3/ENV3 require a live SID engine/sink.
        // With no sink attached, write-only SID registers must NOT read back their
        // shadow writes — they return the current C64 open-bus latch.
        // audit #9: with no sink attached EVERY SID read here is an open-bus /
        // un-modelled-hardware approximation (POTX/POTY paddle float, OSC3/ENV3 with
        // no engine, write-only registers). Count it so these no-sink reads are not
        // invisible to the exactness counters (they feed sidOpenBusReadCount()).
        ++sidNoSinkOpenBusReadCount_;
        // audit #11 (P1-13): a no-sink POTX/POTY read is a paddle-hardware
        // approximation (PotXYApprox), distinct from generic SID open bus. Count it
        // so it feeds the PotXYApprox exactness blocker, mirroring the SID-sink path.
        if (r == 0x19u || r == 0x1Au) { ++sidNoSinkPotxyReadCount_; return 0xFFu; }
        if (r == 0x1Bu || r == 0x1Cu) return readOpenBus();
        return readOpenBus();
    }

    void writeMapped_(uint16_t a, uint8_t value) noexcept {
        if (a == 0x0000u) {
            cpu_.state().portDirection = value;
            logMemoryWrite_(a);
            memory_.write(a, value);
            return;
        }
        if (a == 0x0001u) {
            cpu_.state().portData = value;
            logMemoryWrite_(a);
            memory_.write(a, value);
            return;
        }

        if (visibleDevice(a) == C64VisibleDevice::Io) {
            if (addressInConfiguredSidWindow_(a)) {
                uint8_t chip = 0, r = 0;
                if (sidAddressToChipReg_(a, chip, r) && c64SidRegWriteable(r)) {
                    sidRegsByChip_[chip][r] = value;
                    if (chip == 0u) sidRegs_[r] = value;
                    lastSidChip_ = chip;
                    lastSidReg_ = r;
                    lastSidValue_ = value;
                    lastSidWriteCycle_ = phi2Cycle_;
                    sidWriteObserved_ = true;
                    if (sidSink_) sidSink_->sidWrite(static_cast<uint8_t>(chip * 32u + r), value, phi2Cycle_);
                }
                return;
            }
            if (isVic(a)) { vic_.write(static_cast<uint8_t>(a & 0x3Fu), value); return; }
            if (isCia1(a)) { cia1_.write(static_cast<uint8_t>(a & 0x0Fu), value); return; }
            if (isCia2(a)) {
                const uint8_t ciaReg = static_cast<uint8_t>(a & 0x0Fu);
                cia2_.write(ciaReg, value);
                if (ciaReg == 0x00u || ciaReg == 0x02u) updateVicBankFromCia2_();
                return;
            }
            if (isColorRam(a)) { logColorRamWrite_(a); colorRam_[a & 0x03FFu] = uint8_t(value & 0x0Fu); return; }
        }

        // Underlying RAM always records CPU writes when the address is not
        // actively decoded to an IO device, matching ROM-under-RAM behavior.
        logMemoryWrite_(a);
        memory_.write(a, value);
    }

    void latchOpenBus_(uint8_t v) noexcept {
        openBusLatch_.drive(v, phi2Cycle_);
    }

    void decayOpenBus_() noexcept {
        openBusLatch_.decayToPhi2(phi2Cycle_, !vic_.cpuCanUseBus());
    }

    bool pal_ = true;
    uint32_t clockHz_ = kPalPhi2Hz;
    uint64_t phi2Cycle_ = 0;
    OpenBusLatch openBusLatch_{};
    uint8_t lastRead_ = 0;
    uint8_t lastSidReg_ = 0;
    uint8_t lastSidValue_ = 0;
    uint64_t lastSidWriteCycle_ = 0;
    bool sidWriteObserved_ = false;
    Mos6510 cpu_{};
    VicII vic_{};
    Cia6526 cia1_{};
    Cia6526 cia2_{};
    C64MemoryMap memory_{};
    C64RomSet roms_{};
    C64CartridgeImage cartridge_{};
    C64IecBus iec_{};
    C64TapePort tape_{};
    SidBusQueue queue_{};
    std::array<uint8_t, 32> sidRegs_{};
    std::array<std::array<uint8_t, 32>, 5> sidRegsByChip_{};
    std::array<uint16_t, 5> sidBases_{{0xD400u, 0u, 0u, 0u, 0u}};
    uint8_t sidChipCount_ = 1;
    uint8_t lastSidChip_ = 0;
    std::array<uint8_t, 0x400> colorRam_{};
    // audit #8: count CPU reads of Color RAM ($D800-$DBFF). Color RAM is only 4 bits
    // wide — the high nibble of a read comes from the (approximated) open bus
    // (`(colorRam & 0x0F) | (openBus & 0xF0)`). A program that uses the full byte is
    // therefore depending on our open-bus model, so the run is not physically exact.
    uint32_t colorRamHighNibbleOpenBusReadCount_ = 0;
    // audit #9: no-sink SID reads (open-bus / un-modelled). mutable because the
    // read path (sidReadNoSink_) is const.
    mutable uint32_t sidNoSinkOpenBusReadCount_ = 0;
    mutable uint32_t sidNoSinkPotxyReadCount_ = 0;   // audit #11 (P1-13)
    bool booted_ = false;
    bool realtimeSidCoreRunning_ = false;
    C64BootState bootState_{};
    uint16_t irqVectorRamLedger_ = 0xFF58u;
    uint16_t nmiVectorRamLedger_ = 0xFE00u;
    uint16_t cpuIrqVectorLedger_ = 0xFF48u;
    uint16_t cpuNmiVectorLedger_ = 0xFE00u;
    uint16_t interruptLedgerPlayAddress_ = 0;
    uint16_t irqTrampolineAddress_ = 0xFF58u;
    uint16_t nmiTrampolineAddress_ = 0xFE00u;
    uint8_t irqTrampolineLength_ = 1;
    uint8_t nmiTrampolineLength_ = 1;
    uint32_t irqTrampolineChecksum_ = 0;
    uint32_t nmiTrampolineChecksum_ = 0;
    bool irqAckCia1_ = false;
    bool nmiAckCia2_ = false;
    uint32_t interruptValidationFailureCount_ = 0;
    uint64_t interruptLastValidationPhi2_ = 0;
    uint64_t psidCiaRunGeneration_ = 0;
    uint64_t psidCiaServiceGeneration_ = 0;
    // v872 P0-2: latch the CIA1 Timer-A value installPsidCiaPlaybackBootstrap()
    // actually installed (tune-programmed or default). Integrity telemetry must
    // validate against THIS, not the default — otherwise a valid CIA tune that
    // programs its own tempo is falsely reported as ciaLatchCorrect=false / dirty.
    uint16_t psidCiaInstalledTimerALatch_ = 0xFFFFu;
    bool     psidCiaInstalledTimerALatchValid_ = false;
    // v872 P0-1/P0-2: CIA1 Timer-A latch/control write-count baselines captured at
    // init start (markSidInitStart) so the bootstrap can detect tune-authored writes.
    uint64_t cia1TimerALatchWritesAtInitStart_ = 0;
    uint64_t cia1TimerAControlWritesAtInitStart_ = 0;
    RenderMutationJournal renderJournal_{};
    // v837: O(1) dedup markers for the render mutation journal. Replaces the
    // previous O(n) linear scan over dirtyRam/dirtyColor in logMemoryWrite_/
    // logColorRamWrite_, which made a single play call O(n^2) in the number of
    // distinct cells the player touched — a busy tune (many RAM writes per
    // block) drove the audio render past its deadline and stuttered. These are
    // render-thread-only working sets (no concurrency), reset at journal begin.
    std::bitset<65536> ramJournalSeen_{};
    std::bitset<1024>  colorJournalSeen_{};
    SidRegisterSink* sidSink_ = nullptr;
};

} // namespace ArpSID::C64
