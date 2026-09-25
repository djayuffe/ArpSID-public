#pragma once

#include "arpsid/core/c64_boot_trace.h"
#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_sid_readback.h"
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/psid_header.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

#ifndef ARPSID_C64_PHYSICAL_ONLY
#define ARPSID_C64_PHYSICAL_ONLY 0
#endif

inline constexpr bool kC64PhysicalOnlyBuild = (ARPSID_C64_PHYSICAL_ONLY != 0);
inline constexpr bool c64LegacyMos6510RuntimePathsReachable() noexcept {
    (void)kC64PhysicalOnlyBuild;
    return false;
}

// ── Fix #1: Exactness downgrade reasons ──────────────────────────────────────
// Each bit explains a known downgrade/approximation. Zero = no known downgrade observed.
// Callers inspect individual bits rather than testing boolean exact/inexact.
enum class RsidExactnessDowngrade : uint32_t {
    None                    = 0,
    NotRsid                 = 1u << 0,  ///< File is PSID, not RSID
    Phi2NotActive           = 1u << 1,  ///< PHI2 machine not used for play
    FallbackInit            = 1u << 2,  ///< Retired ABI bit; runtime init now fails closed instead of falling back
    UnsupportedOpcode       = 1u << 3,  ///< CPU executed an unsupported opcode
    ApproximateOpcode       = 1u << 4,  ///< CPU executed an approximate illegal opcode
    VicBusStealApprox       = 1u << 5,  ///< VIC bus-steal modelled but not cycle-exact
    TimedWriteOverflow      = 1u << 6,  ///< Timed-write buffer overflowed (dropped writes)
    DroppedMultiSidWrites   = 1u << 7,  ///< Writes to chip >= activeSidChips were dropped
    LegacyFallback          = 1u << 8,  ///< Retired ABI bit; rsidLegacyInitFallbackCount stays zero in production runtime
    MissingRealRoms         = 1u << 9,  ///< Runtime is using deterministic fallback ROMs
    BasicStartupUnsupported = 1u << 10, ///< RSID BASIC startup flag is present
    CiaModelApprox          = 1u << 11, ///< CIA timer/IRQ model is not declared cycle-exact
    SidReadApprox           = 1u << 12, ///< SID readable-register/open-bus read approximation observed
    RmwSidWrite             = 1u << 13, ///< RMW dummy/final write touched SID IO space
    LegacyCpuPlayback       = 1u << 14, ///< RSID playback used legacy instruction-atomic CPU path
    SidHoleWrite            = 1u << 15, ///< Write attempted to SID readback/hole registers $D419-$D41F
    OpenBusApprox           = 1u << 16, ///< Program observed deterministic open-bus/unmapped IO readback
    StrictRsidNotPhi2       = 1u << 17, ///< Strict RSID could not be run entirely by C64Phi2Machine
    RomIdentityUnverified   = 1u << 18, ///< External ROM set is present but not known-stock identity verified
    InitBrkSentinel         = 1u << 19, ///< Init completed through BRK-as-JAM compatibility sentinel
    InvalidSidChipAccess    = 1u << 20, ///< Program addressed a SID chip index outside supported 0..4
};
inline constexpr RsidExactnessDowngrade operator|(RsidExactnessDowngrade a, RsidExactnessDowngrade b) noexcept {
    return static_cast<RsidExactnessDowngrade>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr bool rsidDowngradeHas(RsidExactnessDowngrade mask, RsidExactnessDowngrade bit) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(bit)) != 0u;
}
inline constexpr uint32_t rsidDowngradeMask(RsidExactnessDowngrade mask) noexcept {
    return static_cast<uint32_t>(mask);
}

// Compact policy/status codes used by GUI/HUD snapshots. These are deliberately
// separate from the deprecated boolean-looking rsidExactPlaybackActive spelling.
enum class RsidStrictStatusCode : uint8_t {
    None = 0,          ///< no active strict-PHI2 RSID path
    // audit P1.11: "Clean" is NOT merely "no observed downgrade". rsidStrictStatusCode()
    // only returns Clean when rsidKnownDowngradeFree() holds, i.e. BOTH the observed
    // exactness-downgrade ledger is empty AND physicalExactnessBlockerMask()==0 (no
    // CIA/VIC/SID-readback/ROM-identity/open-bus/legacy-path blockers). In normal
    // builds physicalExactnessBlockerMask() is non-zero (e.g. OpenBusModelApprox /
    // OpenBusModelApprox is normally set), so Clean implies genuine physical
    // exactness and is effectively unreachable without real verified ROMs and
    // cycle-exact peripherals. Do NOT relax this to "no downgrade bits only" — that
    // would reintroduce the wording trap where a downgraded-but-useful PHI2 run is
    // mislabelled as physically exact.
    Clean = 1,         ///< strict-PHI2 active, downgrade-free AND no physical-exactness blockers
    Downgraded = 2,    ///< strict-PHI2 path active with one or more known downgrades/blockers
};

// Coarse capability blockers for true "physical exact" C64 execution.  This is
// stronger than the event/ledger mask above: these bits describe subsystems that
// are not yet declared cycle-physical even before a tune hits a specific edge
// case.  GUI/release tooling can use this to avoid presenting a downgraded but
// useful PHI2 run as full C64 hardware closure.
enum class C64PhysicalExactnessBlocker : uint32_t {
    None                         = 0,
    MissingRealRoms              = 1u << 0,
    RomIdentityUnverified        = 1u << 1,
    Cia6526NotCycleExact         = 1u << 2,
    VicIINotCycleExact           = 1u << 3,
    SidReadbackModelApprox       = 1u << 4,
    SidReadbackApprox            = SidReadbackModelApprox, // legacy spelling/ABI alias
    OpenBusModelApprox           = 1u << 5,
    BasicStartupUnsupported      = 1u << 6,
    PsidCiaCompatibility         = 1u << 7,
    LegacyMos6510PlaybackObserved= 1u << 8,
    LegacyMos6510PathPresent     = 1u << 9,
    ObservedDowngradeLedger      = 1u << 10,
    // audit #7: a dedicated OBSERVED blocker for an actual SID-register open-bus
    // read (program read a SID address with no driving source, latching the open
    // bus). Distinct from the always-on capability blocker OpenBusModelApprox:
    // this bit is only set when sidOpenBusReadCount() > 0, so tooling can tell a
    // run that genuinely depended on SID open-bus readback from one that merely
    // *could* (every run sets OpenBusModelApprox).
    SidOpenBusApprox             = 1u << 11,
    // audit #8: an OBSERVED CPU read of Color RAM, whose high nibble is sourced from
    // the approximated open bus (Color RAM is only 4 bits wide). Set only when
    // colorRamHighNibbleOpenBusReadCount() > 0.
    ColorRamOpenBusApprox        = 1u << 12,
    // audit #11: an OBSERVED CPU read of POTX/POTY (SID paddle A-D converters),
    // whose value depends on external analog paddle hardware we do not model. Set
    // only when potxyReadCount() > 0.
    PotXYApprox                  = 1u << 13,
    LegacyMos6510Available       = LegacyMos6510PlaybackObserved, // legacy spelling/ABI alias
};
inline constexpr C64PhysicalExactnessBlocker operator|(C64PhysicalExactnessBlocker a, C64PhysicalExactnessBlocker b) noexcept {
    return static_cast<C64PhysicalExactnessBlocker>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr bool c64PhysicalBlockerHas(C64PhysicalExactnessBlocker mask, C64PhysicalExactnessBlocker bit) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(bit)) != 0u;
}
inline constexpr uint32_t c64PhysicalBlockerMask(C64PhysicalExactnessBlocker mask) noexcept {
    return static_cast<uint32_t>(mask);
}

// ── Fix #2: Strict vs compatibility RSID playback mode ────────────────────────
// Strict: BRK (0x00) vectors normally through the IRQ/BRK mechanism.
// CLI (0x58) is required before the play routine can be interrupted.
// Approximate opcodes cause CPU jam rather than best-effort execution.
// This is the correct model for hardware-validated RSID tunes.
// Compatible: BRK (0x00) acts as KIL/JAM — halts the CPU to signal init
// completion. Approximate opcodes are executed with best-effort
// results. Required for RSID init sequences that use BRK as a trap.
enum class RsidPlaybackMode : uint8_t {
    Strict     = 0,  ///< Hardware-accurate: BRK vectors, approx opcodes jam
    Compatible = 1,  ///< Legacy: BRK→JAM, approx opcodes best-effort
};

// Why a load returned false, so the AU/telemetry layer can distinguish an
// unparseable file from a spec-valid tune the runtime could not place in RAM
// (v873 audit item 7). None == the most recent load succeeded.
enum class PsidLoadFailure : uint8_t {
    None = 0,
    ParseFailed,               ///< psidParse() rejected the bytes / empty image
    ConfigureSidBasesFailed,   ///< non-contiguous / zero multi-SID base slot
    BootstrapRelocationFailed, ///< no free page for the bootstrap trampoline
    ParseTooShort,
    BadMagic,
    BadVersion,
    BadOffset,
    BadSongMetadata,
    BadRsidHeader,
    BadSidAddress,
    DuplicateSidBase,
    UnsupportedMusSpecific,
    UnsupportedRsidBasic,
    BadRelocationRange,
    EmptyPayload,
    InvalidParsedHeader,
};

// v964: human-readable label for the load-failure telemetry code (mirrors
// psidParseResultName). The GUI shows this on a failed .sid load so users see
// the exact rejection reason instead of a bare "load failed".
static inline const char* psidLoadFailureName(PsidLoadFailure f) noexcept {
    switch (f) {
        case PsidLoadFailure::None: return "None";
        case PsidLoadFailure::ParseFailed: return "ParseFailed";
        case PsidLoadFailure::ConfigureSidBasesFailed: return "ConfigureSidBasesFailed";
        case PsidLoadFailure::BootstrapRelocationFailed: return "BootstrapRelocationFailed";
        case PsidLoadFailure::ParseTooShort: return "ParseTooShort";
        case PsidLoadFailure::BadMagic: return "BadMagic";
        case PsidLoadFailure::BadVersion: return "BadVersion";
        case PsidLoadFailure::BadOffset: return "BadOffset";
        case PsidLoadFailure::BadSongMetadata: return "BadSongMetadata";
        case PsidLoadFailure::BadRsidHeader: return "BadRsidHeader";
        case PsidLoadFailure::BadSidAddress: return "BadSidAddress";
        case PsidLoadFailure::DuplicateSidBase: return "DuplicateSidBase";
        case PsidLoadFailure::UnsupportedMusSpecific: return "UnsupportedMusSpecific";
        case PsidLoadFailure::UnsupportedRsidBasic: return "UnsupportedRsidBasic";
        case PsidLoadFailure::BadRelocationRange: return "BadRelocationRange";
        case PsidLoadFailure::EmptyPayload: return "EmptyPayload";
        case PsidLoadFailure::InvalidParsedHeader: return "InvalidParsedHeader";
    }
    return "Unknown";
}

struct PsidHeader {
    bool valid = false;
    bool rsid = false;
    uint16_t version = 0;
    uint16_t dataOffset = 0;
    uint16_t loadAddress = 0;
    uint16_t initAddress = 0;
    uint16_t playAddress = 0;
    uint16_t songs = 0;
    uint16_t startSong = 0;
    uint32_t rawSpeed = 0;
    uint32_t speed = 0;
    uint32_t normalizedSpeed = 0;
    uint16_t flags = 0;
    uint8_t startPage = 0;
    uint8_t pageLength = 0;
    uint8_t secondSidAddress = 0;
    uint8_t thirdSidAddress = 0;
    uint8_t fourthSidAddress = 0;
    uint8_t fifthSidAddress = 0;
    uint8_t sidChipCount = 1;
    uint16_t sidBase[5] = {0xD400u, 0u, 0u, 0u, 0u};
    bool c64BasicFlag = false;
    // v873: tune-declared video clock + per-SID model (normalized from the flags
    // word). These record what the TUNE asks for; whether the render path honors
    // them is a separate model/clock-mode (Auto vs forced) decision.
    PsidClock clock = PsidClock::Unknown;
    // v873 audit item 3: normalized destination metadata so AU/render never re-decode raw
    // flags. sidModel[] is 5-wide (matches the sidBase[5]/render plumbing); slots 3/4 stay
    // Unknown until per-chip v4E-style metadata exists (max 3 SIDs today).
    PsidCompatibility compatibility = PsidCompatibility::Unknown;
    PsidSidModel sidModel[5] = {PsidSidModel::Unknown, PsidSidModel::Unknown, PsidSidModel::Unknown,
                               PsidSidModel::Unknown, PsidSidModel::Unknown};
    uint8_t sidChannel[5] = {0u, 0u, 0u, 0u, 0u};
    char name[33]{};
    char author[33]{};
    char released[33]{};
};

struct PsidImage {
    PsidHeader header{};
    uint16_t effectiveLoadAddress = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
};

class C64RuntimeSidSink : public SidRegisterSink {
public:
    struct Snapshot final {
        std::array<uint8_t, 32> regs{};
        std::array<std::array<uint8_t, 32>, 5> regsByChip{};
        std::array<uint8_t, 5> osc3ByChip{};
        std::array<uint8_t, 5> env3ByChip{};
        std::array<SidReadbackModel, 5> readbackByChip{};
        uint8_t lastChip = 0;
        uint8_t lastReg = 0;
        uint8_t lastValue = 0;
        uint64_t lastCycle = 0;
        uint32_t writeCount = 0;
        uint32_t sidReadApproximationCount = 0;
        uint32_t sidOpenBusReadCount = 0;
        uint32_t potxyReadCount = 0;
        uint32_t invalidSidChipReadCount = 0;
        uint32_t invalidSidChipWriteCount = 0;
        uint32_t droppedSidHoleWriteCount = 0;
    };

    C64RuntimeSidSink() noexcept {
        for (auto& readback : readbackByChip) readback.reset();
    }
    void sidWrite(uint8_t reg, uint8_t value, uint64_t phi2Cycle) noexcept override {
        const uint8_t chip = static_cast<uint8_t>(reg / 32u);
        const uint8_t local = static_cast<uint8_t>(reg & 0x1Fu);
        if (chip >= regsByChip.size()) {
            ++invalidSidChipWriteCount;
            return;
        }
        if (local > 0x18u) {
            ++droppedSidHoleWriteCount;
            return;
        }
        lastChip = chip;
        lastReg = local;
        lastValue = value;
        lastCycle = phi2Cycle;
        ++writeCount;
        regsByChip[chip][local] = value;
        readbackByChip[chip].write(phi2Cycle, local, value);
        regs[local] = regsByChip[0][local];
    }
    static constexpr bool sidRegReadable_(uint8_t local) noexcept {
        return local == 0x19u || local == 0x1Au || local == 0x1Bu || local == 0x1Cu;
    }
    uint8_t sidReadWithOpenBus(uint8_t reg, uint64_t phi2, uint8_t openBus) noexcept {
        const uint8_t chip = static_cast<uint8_t>(reg / 32u);
        const uint8_t local = static_cast<uint8_t>(reg & 0x1Fu);
        if (chip >= regsByChip.size()) {
            ++invalidSidChipReadCount;
            ++sidOpenBusReadCount;
            ++sidReadApproximationCount;
            return openBus;
        }
        if (!sidRegReadable_(local)) {
            ++sidOpenBusReadCount;
            ++sidReadApproximationCount;
            return openBus;
        }
        const uint8_t value = readbackByChip[chip].read(phi2, local, openBus);
        // audit #11: POTX/POTY reads depend on un-modelled paddle hardware.
        if (local == 0x19u || local == 0x1Au) ++potxyReadCount;
        if (local == 0x1Bu) osc3ByChip[chip] = value;
        if (local == 0x1Cu) env3ByChip[chip] = value;
        return value;
    }
    uint8_t sidRead(uint8_t reg, uint64_t phi2) noexcept override {
        return sidReadWithOpenBus(reg, phi2, 0xFFu);
    }

    void captureSnapshot(Snapshot& out) const noexcept {
        out.regs = regs;
        out.regsByChip = regsByChip;
        out.osc3ByChip = osc3ByChip;
        out.env3ByChip = env3ByChip;
        out.readbackByChip = readbackByChip;
        out.lastChip = lastChip;
        out.lastReg = lastReg;
        out.lastValue = lastValue;
        out.lastCycle = lastCycle;
        out.writeCount = writeCount;
        out.sidReadApproximationCount = sidReadApproximationCount;
        out.sidOpenBusReadCount = sidOpenBusReadCount;
        out.potxyReadCount = potxyReadCount;
        out.invalidSidChipReadCount = invalidSidChipReadCount;
        out.invalidSidChipWriteCount = invalidSidChipWriteCount;
        out.droppedSidHoleWriteCount = droppedSidHoleWriteCount;
    }

    void restoreSnapshot(const Snapshot& in) noexcept {
        regs = in.regs;
        regsByChip = in.regsByChip;
        osc3ByChip = in.osc3ByChip;
        env3ByChip = in.env3ByChip;
        readbackByChip = in.readbackByChip;
        lastChip = in.lastChip;
        lastReg = in.lastReg;
        lastValue = in.lastValue;
        lastCycle = in.lastCycle;
        writeCount = in.writeCount;
        sidReadApproximationCount = in.sidReadApproximationCount;
        sidOpenBusReadCount = in.sidOpenBusReadCount;
        potxyReadCount = in.potxyReadCount;
        invalidSidChipReadCount = in.invalidSidChipReadCount;
        invalidSidChipWriteCount = in.invalidSidChipWriteCount;
        droppedSidHoleWriteCount = in.droppedSidHoleWriteCount;
    }

    std::array<uint8_t, 32> regs{};
    std::array<std::array<uint8_t, 32>, 5> regsByChip{};
    std::array<uint8_t, 5> osc3ByChip{};
    std::array<uint8_t, 5> env3ByChip{};
    std::array<SidReadbackModel, 5> readbackByChip{};
    uint8_t lastChip = 0;
    uint8_t lastReg = 0;
    uint8_t lastValue = 0;
    uint64_t lastCycle = 0;
    uint32_t writeCount = 0;
    uint32_t sidReadApproximationCount = 0;
    uint32_t sidOpenBusReadCount = 0;
    // audit #11: count CPU reads of POTX ($D419) / POTY ($D41A). These are readable
    // SID registers but their value is the paddle/pot A-D conversion, which depends
    // on external analog hardware we do not model — so a tune that reads them is not
    // physically exact.
    uint32_t potxyReadCount = 0;
    uint32_t invalidSidChipReadCount = 0;
    uint32_t invalidSidChipWriteCount = 0;
    uint32_t droppedSidHoleWriteCount = 0;
};

class C64RuntimePhi2SidSinkBridge final : public ISidRegisterWriteSink {
public:
    void attach(C64RuntimeSidSink* local, SidRegisterSink* external) noexcept {
        local_ = local;
        external_ = external;
    }
    void resetExactnessCounters() noexcept {
        sidReadApproximationCount_ = 0;
        sidOpenBusReadCount_ = 0;
        invalidSidChipReadCount_ = 0;
        invalidSidChipWriteCount_ = 0;
        rmwSidWriteCount_ = 0;
        droppedSidHoleWriteCount_ = 0;
    }

    void writeSidRegisterPhi2(uint64_t phi2,
                              uint8_t reg,
                              uint8_t value,
                              bool rmwDummy) noexcept override {
        const uint8_t chip = static_cast<uint8_t>(reg / 32u);
        if (chip >= 5u) {
            ++invalidSidChipWriteCount_;
            if (local_) ++local_->invalidSidChipWriteCount;
            return;
        }
        if ((reg & 0x1Fu) > 0x18u) {
            ++droppedSidHoleWriteCount_;
            if (local_) ++local_->droppedSidHoleWriteCount;
            return;
        }
        if (rmwDummy) ++rmwSidWriteCount_;
        if (local_) local_->sidWrite(reg, value, phi2);
        if (external_ && external_ != local_) external_->sidWrite(reg, value, phi2);
    }

    uint8_t readSidRegisterPhi2(uint64_t phi2,
                                uint8_t reg,
                                uint8_t openBus) noexcept override {
        const uint8_t chip = static_cast<uint8_t>(reg / 32u);
        const uint8_t local = static_cast<uint8_t>(reg & 0x1Fu);
        if (chip >= 5u) {
            ++invalidSidChipReadCount_;
            ++sidOpenBusReadCount_;
            ++sidReadApproximationCount_;
            if (local_) {
                ++local_->invalidSidChipReadCount;
                ++local_->sidOpenBusReadCount;
                ++local_->sidReadApproximationCount;
            }
            return openBus;
        }
        if (!C64RuntimeSidSink::sidRegReadable_(local)) {
            ++sidOpenBusReadCount_;
            ++sidReadApproximationCount_;
            if (local_) {
                ++local_->sidOpenBusReadCount;
                ++local_->sidReadApproximationCount;
            }
            return openBus;
        }
        if (local_) return local_->sidReadWithOpenBus(reg, phi2, openBus);
        if (external_) return external_->sidRead(reg, phi2);
        return openBus;
    }
    uint32_t sidReadApproximationCount() const noexcept { return sidReadApproximationCount_; }
    uint32_t sidOpenBusReadCount() const noexcept { return sidOpenBusReadCount_; }
    uint32_t invalidSidChipReadCount() const noexcept { return invalidSidChipReadCount_; }
    uint32_t invalidSidChipWriteCount() const noexcept { return invalidSidChipWriteCount_; }
    uint32_t rmwSidWriteCount() const noexcept { return rmwSidWriteCount_; }
    uint32_t droppedSidHoleWriteCount() const noexcept { return droppedSidHoleWriteCount_; }

private:
    C64RuntimeSidSink* local_ = nullptr;
    SidRegisterSink* external_ = nullptr;
    uint32_t sidReadApproximationCount_ = 0;
    uint32_t sidOpenBusReadCount_ = 0;
    uint32_t invalidSidChipReadCount_ = 0;
    uint32_t invalidSidChipWriteCount_ = 0;
    uint32_t rmwSidWriteCount_ = 0;
    uint32_t droppedSidHoleWriteCount_ = 0;
};

class PsidParser {
public:
    static uint16_t be16(const uint8_t* p) noexcept { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
    static uint32_t be32(const uint8_t* p) noexcept {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }
    static uint16_t le16(const uint8_t* p) noexcept { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }

    // Build the runtime image from an already-parsed low-level header — no re-parse, so
    // the AU layer and the runtime can share a single psidParse() (v873 audit item 4).
    static PsidImage fromParsed(const ArpSID::PsidHeader& parsed) noexcept {
        PsidImage img{};
        PsidHeader h{};
        h.valid = true;
        h.rsid = parsed.isRsid;
        h.version = parsed.version;
        h.dataOffset = parsed.dataOffset;
        h.loadAddress = parsed.loadAddr;
        h.initAddress = parsed.initAddr;
        h.playAddress = parsed.playAddr;
        h.songs = parsed.songs;
        h.startSong = parsed.startSong;
        h.rawSpeed = parsed.rawSpeed;
        h.speed = parsed.normalizedSpeed;
        h.normalizedSpeed = parsed.normalizedSpeed;
        h.flags = parsed.flags;
        h.startPage = parsed.startPage;
        h.pageLength = parsed.pageLength;
        h.secondSidAddress = parsed.secondSidAddress;
        h.thirdSidAddress = parsed.thirdSidAddress;
        h.fourthSidAddress = parsed.fourthSidAddress;
        h.fifthSidAddress = parsed.fifthSidAddress;
        h.sidChipCount = ArpSID::psidSidChipCount(parsed);
        for (uint8_t i = 0; i < 5u; ++i) h.sidBase[i] = ArpSID::psidSidBaseForChip(parsed, i);
        // Flags bit 1 is "C64 BASIC required" ONLY for RSID. For PSID the same bit
        // is the (unsupported) "PSID-specific / MUS" marker and must not be read as a
        // BASIC-startup request, or a plain PSID tune could be routed down the RSID
        // BASIC path.
        h.c64BasicFlag = parsed.isRsid && ((parsed.flags & 0x0002u) != 0u);
        h.clock = parsed.clock;
        h.compatibility = parsed.compatibility;
        h.sidModel[0] = parsed.sidModel[0];
        h.sidModel[1] = parsed.sidModel[1];
        h.sidModel[2] = parsed.sidModel[2];
        // sidModel[3]/[4] and sidChannel[] stay at their Unknown/0 defaults (max 3 SIDs).
        copyZ_(h.name, reinterpret_cast<const uint8_t*>(parsed.name), 32);
        copyZ_(h.author, reinterpret_cast<const uint8_t*>(parsed.author), 32);
        copyZ_(h.released, reinterpret_cast<const uint8_t*>(parsed.released), 32);

        img.header = h;
        img.effectiveLoadAddress = parsed.effectiveLoadAddr;
        img.payload = parsed.payload;
        img.payloadSize = parsed.payloadLen;
        return img;
    }

    static PsidImage parse(const uint8_t* data, size_t size) noexcept {
        ArpSID::PsidHeader parsed{};
        // Playback is sidtune-compatible: clamp sloppy song metadata instead of
        // rejecting the tune (validators/tests keep the default StrictSpec policy).
        const ArpSID::PsidParseResult result = ArpSID::psidParse(
            data, static_cast<uint32_t>(size), parsed, ArpSID::PsidParsePolicy::SidTuneCompatible);
        if (result != ArpSID::PsidParseResult::OK) return PsidImage{};
        return fromParsed(parsed);
    }

private:
    static void copyZ_(char* dst, const uint8_t* src, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) dst[i] = static_cast<char>(src[i]);
        dst[n] = '\0';
    }
};

class C64Runtime {
public:
    struct C64RenderTransaction final {
        bool active = false;
        bool platformJournalActive = false;
        uint64_t phi2Start = 0;
    };

    void reset(bool pal = true) noexcept {
        platform_.reset(pal);
        sink_ = {};
        platform_.attachSid(&sink_);
        loaded_ = {};
        resetPhi2Machine_(pal);
        phi2MachineReady_ = false;
        phi2InitUsed_ = false;
        rsidLegacyInitFallbackCount_ = 0;
        initBrkSentinelCount_ = 0;
        timedWriteOverflowSinceLoad_ = 0;
        droppedMultiSidWritesSinceLoad_ = 0;
        renderTxActive_ = false;
        renderTxDeferredFullRamSync_ = false;
        renderTxStartPhi2_ = 0;
        legacyRuntimePlaybackCount_ = 0;
        psidCiaCompatibilityServiceObservedCount_ = 0;
        psidCiaRunPlayCompatibilityCount_ = 0;
        psidCiaExplicitServiceCompatibilityCount_ = 0;
        psidCiaPhi2ServiceCount_ = 0;
        strictRsidNotPhi2Count_ = 0;
        phi2SidBridge_.resetExactnessCounters();
        usePhi2Machine_ = false;
    }

    // v873 audit P0-10: the init/play/CIA bootstraps live in a fixed $0334..$037C
    // window (cassette buffer, conventionally free). A tune whose load range covers
    // that window would have ~0x49 bytes silently overwritten by the bootstrap. Pick a
    // base that clears the tune: keep $0334 when it does not collide (common tunes stay
    // byte-identical), else relocate the whole block to a free always-RAM page.
    static constexpr uint16_t kPsidBootstrapDefaultBase = 0x0334u;
    static constexpr uint16_t kPsidBootstrapSpan        = 0x49u;  // covers init + play(+$1C) + cia(+$3C)
    static constexpr uint16_t kPsidBootstrapPlayOffset  = 0x1Cu;  // $0350 - $0334
    static constexpr uint16_t kPsidBootstrapCiaOffset   = 0x3Cu;  // $0370 - $0334
    uint16_t bootstrapBase_ = kPsidBootstrapDefaultBase;

    // Returns true and sets outBase when a non-colliding bootstrap base exists. Returns
    // false when the tune covers the default region AND every candidate page (v873 audit
    // item 12): the caller must then FAIL the load rather than fall back to $0334, which
    // would silently overwrite tune RAM — the exact corruption relocation exists to prevent.
    static bool choosePsidBootstrapBase_(uint16_t loadAddr, uint32_t loadLen,
                                         uint16_t& outBase) noexcept {
        auto overlaps = [](uint16_t base, uint16_t lo, uint32_t len) noexcept {
            const uint32_t bEnd = static_cast<uint32_t>(base) + kPsidBootstrapSpan;
            const uint32_t tEnd = static_cast<uint32_t>(lo) + len;
            return static_cast<uint32_t>(base) < tEnd && static_cast<uint32_t>(lo) < bEnd;
        };
        if (loadLen == 0u || !overlaps(kPsidBootstrapDefaultBase, loadAddr, loadLen)) {
            outBase = kPsidBootstrapDefaultBase;
            return true;
        }
        // Scan page-aligned bases in always-RAM $0400..$9F00 (executable regardless of
        // banking at reset) for the first that clears the tune.
        for (uint16_t page = 0x04u; page <= 0x9Fu; ++page) {
            const uint16_t base = static_cast<uint16_t>(page << 8u);
            if (!overlaps(base, loadAddr, loadLen)) { outBase = base; return true; }
        }
        return false;  // no safe page — fail the load, do not corrupt $0334/tune RAM
    }

    // Load from raw PSID/RSID bytes: parse once, record the parse result for
    // telemetry (v873 audit item 7), then hand the image to loadPsidImage_.
    bool loadPsid(const uint8_t* data, size_t size) noexcept {
        ArpSID::PsidHeader parsed{};
        lastParseResult_ = ArpSID::psidParse(
            data, static_cast<uint32_t>(size), parsed, ArpSID::PsidParsePolicy::SidTuneCompatible);
        if (lastParseResult_ != ArpSID::PsidParseResult::OK) {
            lastLoadFailure_ = loadFailureForParseResult_(lastParseResult_);
            return false;
        }
        return loadPsidImage_(PsidParser::fromParsed(parsed));
    }

    // Load from a header the caller already parsed, so the AU layer and the
    // runtime share a single psidParse() (v873 audit item 4). The caller is
    // responsible for having checked the parse result before calling this.
    bool loadPsidParsed(const ArpSID::PsidHeader& parsed) noexcept {
        lastParseResult_ = ArpSID::PsidParseResult::OK;
        if (!parsed.payload || parsed.payloadLen == 0u || parsed.effectiveLoadAddr == 0u) {
            lastLoadFailure_ = parsed.payloadLen == 0u ? PsidLoadFailure::EmptyPayload
                                                       : PsidLoadFailure::InvalidParsedHeader;
            return false;
        }
        if (parsed.dataOffset == 0u || parsed.songs == 0u || parsed.startSong == 0u) {
            lastLoadFailure_ = PsidLoadFailure::InvalidParsedHeader;
            return false;
        }
        if (!ArpSID::psidRelocationRangeValid(parsed.startPage, parsed.pageLength,
                                               parsed.effectiveLoadAddr, parsed.payloadLen)) {
            lastLoadFailure_ = PsidLoadFailure::BadRelocationRange;
            return false;
        }
        return loadPsidImage_(PsidParser::fromParsed(parsed));
    }

    bool loadPsidImage_(const PsidImage& img) noexcept {
        if (!img.header.valid || !img.payload || img.payloadSize == 0u) {
            lastLoadFailure_ = img.payloadSize == 0u ? PsidLoadFailure::EmptyPayload
                                                     : PsidLoadFailure::InvalidParsedHeader;
            return false;
        }
        if (!ArpSID::psidRelocationRangeValid(img.header.startPage, img.header.pageLength,
                                               img.effectiveLoadAddress, img.payloadSize)) {
            lastLoadFailure_ = PsidLoadFailure::BadRelocationRange;
            return false;
        }
        lastLoadFailure_ = PsidLoadFailure::None;

        // Per-load reset boundary. loadPsid() must be safe when callers reuse a
        // C64Runtime directly without calling reset() first. Do not let stale SID
        // register banks, PHI2 readiness, retired fallback telemetry or exactness
        // downgrade counters leak from one tune to the next.
        sink_ = {};
        phi2MachineReady_ = false;
        phi2InitUsed_ = false;
        rsidLegacyInitFallbackCount_ = 0;
        initBrkSentinelCount_ = 0;
        timedWriteOverflowSinceLoad_ = 0;
        droppedMultiSidWritesSinceLoad_ = 0;
        renderTxActive_ = false;
        renderTxDeferredFullRamSync_ = false;
        renderTxStartPhi2_ = 0;
        legacyRuntimePlaybackCount_ = 0;
        psidCiaCompatibilityServiceObservedCount_ = 0;
        psidCiaRunPlayCompatibilityCount_ = 0;
        psidCiaExplicitServiceCompatibilityCount_ = 0;
        psidCiaPhi2ServiceCount_ = 0;
        strictRsidNotPhi2Count_ = 0;
        phi2SidBridge_.resetExactnessCounters();
        usePhi2Machine_ = false;
        bootTraceFirstPlayLogged_ = false;

        ARPSID_BOOT_TRACE("LOAD",
            "%s load=$%04X init=$%04X play=$%04X songs=%u start=%u sidChips=%u base0=$%04X payload=%u bytes",
            img.header.rsid ? "RSID" : "PSID",
            static_cast<unsigned>(img.effectiveLoadAddress),
            static_cast<unsigned>(img.header.initAddress),
            static_cast<unsigned>(img.header.playAddress),
            static_cast<unsigned>(img.header.songs),
            static_cast<unsigned>(img.header.startSong),
            static_cast<unsigned>(img.header.sidChipCount),
            static_cast<unsigned>(img.header.sidBase[0]),
            static_cast<unsigned>(img.payloadSize));

        // Always enter a deterministic C64 power-on state before loading a SID.
        // This fixes the old direct-load shortcut where a SID init routine saw
        // whatever reset leftovers happened to be in zero page, stack, CIA/VIC
        // state or processor-port banking.
        platform_.coldBootForSidLoad();
        platform_.attachSid(&sink_);
        uint16_t bases[5] = {0xD400u, 0u, 0u, 0u, 0u};
        for (uint8_t i = 0; i < img.header.sidChipCount && i < 5u; ++i) bases[i] = img.header.sidBase[i];
        if (!platform_.configurePsidSidBases(bases, img.header.sidChipCount)) {
            // A false return must mean "nothing loaded". Publishing loaded_ before
            // this late failure (e.g. non-contiguous multi-SID metadata that yields a
            // zero base slot) would leave the runtime looking valid — a subsequent
            // runInit() could then act on a partially published image (v873 P1-4).
            loaded_ = {};
            lastLoadFailure_ = PsidLoadFailure::ConfigureSidBasesFailed;
            return false;
        }
        // Publish only after every failure point above has passed.
        loaded_ = img;
        uint32_t truncated = 0;
        const uint32_t loadedBytes = platform_.loadBytesToRam(img.effectiveLoadAddress, img.payload, static_cast<uint32_t>(img.payloadSize), &truncated);
        // Choose a bootstrap base that does not overwrite the loaded tune (v873 P0-10).
        // If no safe page exists, fail the load rather than corrupt $0334 (audit item 12).
        if (!choosePsidBootstrapBase_(img.effectiveLoadAddress, loadedBytes, bootstrapBase_)) {
            loaded_ = {};
            lastLoadFailure_ = PsidLoadFailure::BootstrapRelocationFailed;
            return false;
        }
        const uint16_t defaultSong = img.header.startSong ? img.header.startSong : 1u;
        platform_.markSidImageLoaded(img.effectiveLoadAddress,
                                     img.header.initAddress ? img.header.initAddress : img.effectiveLoadAddress,
                                     img.header.playAddress,
                                     defaultSong,
                                     loadedBytes, truncated, runtimeHeaderUsesCiaTimingForSong_(defaultSong));
        resetPhi2Machine_(platform_.clockHz() == kPalPhi2Hz);
        phi2InitUsed_ = false;
        mirrorPlatformRamToPhi2_();
        mirrorPlatformAllRomsToPhi2_();
        phi2Machine_.setBasicStartupState(loaded_.header.c64BasicFlag, false);
        return true;
    }

    bool runInit(uint16_t song = 0, uint32_t maxInstructions = 4096) noexcept {
        if (!loaded_.header.valid) return false;
        // Defensive-in-depth subtune clamp. The kernel already clamps before it
        // calls runInit(), but runInit() is a public entry point and must never
        // init a non-existent song (out-of-range from a corrupt persisted state,
        // a direct caller, or a future call site) — that would run the wrong /
        // undefined tune. Clamp to [1, songs]; the 8-bit song index the C64 init
        // vector receives in A is bounded regardless of header song count.
        const uint16_t songCount = loaded_.header.songs ? loaded_.header.songs : 1u;
        uint16_t selectedSong = song ? song : (loaded_.header.startSong ? loaded_.header.startSong : 1u);
        if (selectedSong < 1u) selectedSong = 1u;
        if (selectedSong > songCount) selectedSong = songCount;
        const uint8_t songIndex = static_cast<uint8_t>((selectedSong - 1u) & 0xFFu);
        const uint16_t initAddr = loaded_.header.initAddress ? loaded_.header.initAddress : loaded_.effectiveLoadAddress;
        const bool rsid = loaded_.header.rsid;
        const bool ciaTiming = runtimeHeaderUsesCiaTimingForSong_(selectedSong);
        const uint16_t boot = rsid ? platform_.installRsidBootstrap(initAddr, selectedSong, bootstrapBase_, true)
                                   : platform_.installPsidInitBootstrap(initAddr, songIndex, bootstrapBase_, ciaTiming);
        platform_.markSidImageLoaded(loaded_.effectiveLoadAddress, initAddr, loaded_.header.playAddress, selectedSong,
                                     platform_.bootState().sidPayloadBytesLoaded, platform_.bootState().sidPayloadBytesTruncated, ciaTiming);
        platform_.markSidInitStart(rsid ? C64BootMode::RsidMachine : C64BootMode::PsidFastInit, boot, selectedSong, maxInstructions);
        ARPSID_BOOT_TRACE("INIT",
            "dispatch %s song=%u/%u init=$%04X boot=$%04X ciaTiming=%d strictRsid=%d budget=%u",
            rsid ? "RSID" : "PSID",
            static_cast<unsigned>(selectedSong),
            static_cast<unsigned>(loaded_.header.songs),
            static_cast<unsigned>(initAddr),
            static_cast<unsigned>(boot),
            ciaTiming ? 1 : 0,
            (rsid && rsidPlaybackMode_ == RsidPlaybackMode::Strict) ? 1 : 0,
            static_cast<unsigned>(maxInstructions));
        platform_.setTrapBrkAsJam(true);
        platform_.bootFromResetVector();

        bool ok = false;
        phi2InitUsed_ = false;

        // One CPU authority for every SID file type: execute init through the
        // PHI2 microcycle machine. PSID differs only in its bootstrap completion
        // sentinel (CIA idle loop or $FFFF), not in CPU/bus ownership.
        const uint32_t phi2Budget = maxInstructions * 12u;
        const uint64_t phi2InitStart = platform_.phi2Cycle();
        ok = runRsidInitViaPhi2_(phi2Budget);
        if (ok) {
            phi2InitUsed_ = true;
            // Keep the public compatibility/inspection surface coherent without
            // allowing it to execute audio. The PHI2 machine remains authoritative.
            syncPlatformRamFromPhi2Writes_(phi2InitStart);
            syncPlatformSidMirrorFromSink_();
            syncPlatformInspectionFromPhi2_();
        } else {
            phi2InitUsed_ = false;
            if (rsid) ++strictRsidNotPhi2Count_;
            ok = false;
        }

        platform_.markSidInitComplete(ok);
        ARPSID_BOOT_TRACE("INIT",
            "result ok=%d path=%s strictRefuseCount=%u legacyFallbackCount=%u initInsExec=%u initCycles=%llu",
            ok ? 1 : 0,
            phi2InitUsed_ ? "PHI2" : (ok ? "legacy" : "refused"),
            static_cast<unsigned>(strictRsidNotPhi2Count_),
            static_cast<unsigned>(rsidLegacyInitFallbackCount_),
            static_cast<unsigned>(platform_.bootState().initInstructionsExecuted),
            static_cast<unsigned long long>(platform_.bootState().initEndPhi2 - platform_.bootState().initStartPhi2));
        if (ok && rsid) {
            platform_.setTrapBrkAsJam(false);
            platform_.startRealtimeSidCore();
        }
        if (ok && !rsid && ciaTiming && loaded_.header.playAddress != 0u) {
            const uint16_t idle = platform_.bootState().psidCiaIdleLoopAddress;
            if (idle != 0u) {
                platform_.cpu().state().pc = idle;
                platform_.cpu().state().jammed = false;
                platform_.cpu().state().p = static_cast<uint8_t>(platform_.cpu().state().p & ~Mos6510::I);
            }
            platform_.installPsidCiaPlaybackBootstrap(loaded_.header.playAddress,
                static_cast<uint16_t>(bootstrapBase_ + kPsidBootstrapCiaOffset));
            if (phi2InitUsed_) syncPsidCiaBootstrapIntoPhi2_();
            platform_.setTrapBrkAsJam(false);
            platform_.startRealtimeSidCore();
        }
        if (ok) {
            if (phi2InitUsed_) {
                // PHI2 machine is already in the correct post-init state.
                // Only update the ready flag without resetting the machine.
                phi2MachineReady_ = loaded_.header.valid && platform_.bootState().sidInitCompleted;
            } else {
                platform_.markSidInitComplete(false);
                phi2MachineReady_ = false;
                return false;
            }
        }
        return ok;
    }

    // v855 P0 timing fix: `externalTimedSink` routes every SID write the play
    // routine performs into the caller's audio timed-write bridge with its exact
    // PHI2 cycle stamp. Previously the discrete VBI/CIA/RSID play paths attached
    // only the internal runtime sink_, so play writes updated the platform SID
    // mirror but never reached c64SidBridge_.timedWrites — the renderer then only
    // saw the block-edge reseeded register image, quantizing all play-routine
    // updates to buffer boundaries (up to ~12-23 ms displacement at 512-1024
    // frame blocks). Pass the render-side bridge here for sample-exact playback.
    bool runPlay(uint32_t maxInstructions = 4096,
                 SidRegisterSink* externalTimedSink = nullptr) noexcept {
        if (!loaded_.header.valid) return false;
        if (!platform_.bootState().sidInitCompleted) return false;

        // Log only the first PLAY service after a load (the boot milestone), not
        // one stderr line per ~50/60 Hz video frame for the life of the tune.
        if (!bootTraceFirstPlayLogged_) {
            bootTraceFirstPlayLogged_ = true;
            ARPSID_BOOT_TRACE("PLAY",
                "first service %s play=$%04X ciaTiming=%d phi2Ready=%d strictMode=%d",
                loaded_.header.rsid ? "RSID" : "PSID",
                static_cast<unsigned>(loaded_.header.playAddress),
                platform_.bootState().sidUsesCiaTiming ? 1 : 0,
                phi2MachineReady_ ? 1 : 0,
                (rsidPlaybackMode_ == RsidPlaybackMode::Strict) ? 1 : 0);
        }

        // RSID is machine-driven: there is no mandatory play address and the
        // tune is expected to advance from CIA/VIC IRQs and normal C64 bus
        // cycles. RSID playback must never fall through to the instruction-atomic
        // Mos6510 compatibility service. That path collapses per-cycle SID writes
        // and cannot be advertised as physical C64 timing.
        if (loaded_.header.rsid) {
            // Use the physical VIC frame length (PAL 63*312 = 19656, NTSC 65*263 =
            // 17095) rather than an integer PHI2/50 or /60 division (which yields
            // 19704/16420 and drifts ~48 cyc/frame on PAL). This matches the AU
            // scheduler's VBI cadence so direct runPlay() and rendered playback
            // agree (v873 P1-6).
            const bool pal = (platform_.clockHz() == kPalPhi2Hz);
            const uint64_t frameCycles = C64TimingMath::psidVbiFrameCycles(pal);
            if (!usePhi2Machine_ || !phi2MachineReady_) {
                ++strictRsidNotPhi2Count_;
                ARPSID_BOOT_TRACE("PLAY",
                    "RSID refusal usePhi2=%d phi2Ready=%d refuseCount=%u",
                    usePhi2Machine_ ? 1 : 0, phi2MachineReady_ ? 1 : 0,
                    static_cast<unsigned>(strictRsidNotPhi2Count_));
                platform_.markSidPlayComplete(false);
                return false;
            }
            const uint32_t rsidBudget = static_cast<uint32_t>(std::min<uint64_t>(
                std::max<uint64_t>(maxInstructions, frameCycles), UINT32_MAX));
            const C64RunResult r = runRsidMachineCycles(frameCycles, rsidBudget, externalTimedSink);
            return (r.executedCycles + r.passiveCycles) > 0u &&
                   !r.cpuJammed && !r.instructionBudgetHit;
        }

        if (loaded_.header.playAddress == 0u) return false;
        if (platform_.bootState().sidUsesCiaTiming) {
            // PSID-CIA playback runs through the PHI2 machine too: CIA1 Timer A
            // IRQ, $FF48/$0314 vectoring, $DC0D ACK and play routine entry all
            // use the same per-cycle CPU/memory/SID authority as RSID.
            if (!phi2MachineReady_) {
                platform_.markSidPlayComplete(false);
                return false;
            }
            platform_.markSidPlayStart(platform_.bootState().playBootstrapAddress, maxInstructions);
            // v873 P0: make direct CIA-timed runPlay() transaction-safe, matching the
            // VBI branch below. A failed service (CPU jam, or play not acknowledged /
            // not returned to idle within the tick budget) otherwise publishes partial
            // PHI2 RAM / SID / $D418 state via runPsidCiaPhi2PlaybackServiceTicks_'s
            // internal syncs. The AU render path already wraps this in its own
            // transaction (renderTxActive_ true → ownTx false → the outer owner keeps
            // sole rollback authority, no nested rollback); a direct/public caller now
            // owns one here and rolls back on failure.
            const bool ownTx = !renderTxActive_;
            C64RenderTransaction tx{};
            if (ownTx) tx = beginRenderTransaction();
            const auto snap = runPsidCiaPhi2PlaybackServiceTicks_(262144u, externalTimedSink);
            const bool ok = snap.serviceComplete;
            if (ownTx) {
                if (ok) commitRenderTransaction(tx);
                else rollbackRenderTransaction(tx);
            }
            platform_.markSidPlayComplete(ok);
            return ok;
        }
        const uint16_t boot = platform_.installPsidPlayBootstrap(loaded_.header.playAddress,
            static_cast<uint16_t>(bootstrapBase_ + kPsidBootstrapPlayOffset));
        platform_.markSidPlayStart(boot, maxInstructions);
        if (phi2MachineReady_) {
            // v872 P1-1: make direct runPlay() transaction-safe. A failed/budgeted
            // VBI play leaves partial PHI2/sink writes (e.g. a $D418 write before the
            // routine budgets out); the AU render path wraps this in its own
            // transaction, but a direct C64Runtime::runPlay() caller (or a test) would
            // otherwise get contaminated SID state. If no transaction is already
            // active, own one here and roll back on failure; when the render path
            // already opened one, beginRenderTransaction() returns inactive and the
            // outer owner keeps sole rollback authority (no nested rollback).
            const bool ownTx = !renderTxActive_;
            C64RenderTransaction tx{};
            if (ownTx) tx = beginRenderTransaction();
            const bool ok = runPsidVbiPlayViaPhi2_(boot, maxInstructions, externalTimedSink);
            if (ownTx) {
                if (ok) commitRenderTransaction(tx);
                else rollbackRenderTransaction(tx);
            }
            platform_.markSidPlayComplete(ok);
            return ok;
        }
        platform_.markSidPlayComplete(false);
        return false;
    }

    C64Platform& platform() noexcept { return platform_; }
    const C64Platform& platform() const noexcept { return platform_; }
    C64RuntimeSidSink& sidSink() noexcept { return sink_; }
    const C64RuntimeSidSink& sidSink() const noexcept { return sink_; }
    const PsidImage& image() const noexcept { return loaded_; }

    // v838: live "VIC-II fast" toggle. Default OFF (full cycle-accurate VIC). It
    // lightens the per-cycle VIC across ALL playback paths (RSID continuous and
    // PSID-VBI passive advancement both run the PHI2 machine) WITHOUT touching the
    // ARPSID_C64_PHYSICAL_ONLY policy: the CPU/CIA/SID stay bit-exact; only the
    // VIC sprite/badline DMA + bus-steal is skipped. The retired instruction-atomic
    // playback path is deliberately NOT re-enabled — it is locked out by the
    // physical-only build policy (see c64_physical_only_policy_v738).
    void setVicFast(bool fast) noexcept { phi2Machine_.setVicFast(fast); }
    bool vicFast() const noexcept { return phi2Machine_.vicFast(); }
    void setCpuFast(bool fast) noexcept { phi2Machine_.setCpuFast(fast); }
    bool cpuFast() const noexcept { return phi2Machine_.cpuFast(); }

    void enablePhi2Machine(bool enabled) noexcept {
        usePhi2Machine_ = enabled;
        if (enabled && loaded_.header.valid && platform_.bootState().sidInitCompleted) {
            if (phi2InitUsed_) {
                // PHI2 machine was already set up during init via tickPhi2().
                // Do NOT re-sync from the legacy platform — that would clobber
                // the exact PHI2 state (cycle counter, CIA timers, CPU micro-t)
                // that was accumulated during bus-cycle-accurate init.
                phi2MachineReady_ = true;
            } else {
                // Defensive recovery for externally manipulated test fixtures;
                // production init is PHI2-only and should already be ready here.
                syncPhi2MachineFromPlatform_();
            }
        }
    }

    bool phi2MachineEnabled() const noexcept { return usePhi2Machine_; }
    bool phi2MachineReady() const noexcept { return phi2MachineReady_; }
    // True only when init ran through the bus-cycle-accurate PHI2 machine.
    // Successful production init should always set this; false means playback is unavailable.
    bool phi2InitUsed() const noexcept { return phi2InitUsed_; }
    uint32_t rsidLegacyInitFallbackCount() const noexcept { return rsidLegacyInitFallbackCount_; }
    // Load diagnostics (v873 audit item 7): the parse result and failure reason
    // from the most recent loadPsid()/loadPsidParsed() call.
    ArpSID::PsidParseResult lastParseResult() const noexcept { return lastParseResult_; }
    PsidLoadFailure lastLoadFailure() const noexcept { return lastLoadFailure_; }
    static PsidLoadFailure loadFailureForParseResultPublic(ArpSID::PsidParseResult r) noexcept {
        return loadFailureForParseResult_(r);
    }
    uint32_t initBrkSentinelCount() const noexcept { return initBrkSentinelCount_; }
    uint32_t sidReadApproximationCount() const noexcept {
        return satAdd32_(phi2SidBridge_.sidReadApproximationCount(), sink_.sidReadApproximationCount);
    }
    uint32_t sidOpenBusReadCount() const noexcept {
        // audit #9: include no-sink platform SID reads so they are not invisible to
        // the exactness counters (they set SidOpenBusApprox just like sink reads).
        return satAdd32_(phi2SidBridge_.sidOpenBusReadCount(),
                         satAdd32_(sink_.sidOpenBusReadCount, platform_.sidNoSinkOpenBusReadCount()));
    }
    // audit #11: observed POTX/POTY reads (paddle/pot A-D, un-modelled analog HW).
    uint32_t potxyReadCount() const noexcept { return sink_.potxyReadCount; }
    uint32_t invalidSidChipReadCount() const noexcept {
        return satAdd32_(phi2SidBridge_.invalidSidChipReadCount(), sink_.invalidSidChipReadCount);
    }
    uint32_t invalidSidChipWriteCount() const noexcept {
        return satAdd32_(phi2SidBridge_.invalidSidChipWriteCount(), sink_.invalidSidChipWriteCount);
    }
    uint32_t sidHoleWriteCount() const noexcept {
        return satAdd32_(phi2SidBridge_.droppedSidHoleWriteCount(), sink_.droppedSidHoleWriteCount);
    }
    uint64_t rmwSidWriteCount() const noexcept {
        return std::max<uint64_t>(phi2SidBridge_.rmwSidWriteCount(),
                                  phi2Machine_.diagnostics().rmwDummySidWrites);
    }
    // Whether the current loaded RSID tune is being driven by the PHI2 machine
    // end-to-end in any policy mode. This is a path/capability statement, not a
    // strictness claim and not a claim that every peripheral is physically exact.
    bool rsidPhi2PlaybackActive() const noexcept {
        return loaded_.header.valid && loaded_.header.rsid &&
               phi2InitUsed_ && usePhi2Machine_ && phi2MachineReady_;
    }
    bool rsidPhi2PathActiveAnyMode() const noexcept { return rsidPhi2PlaybackActive(); }
    bool rsidStrictPhi2PathActive() const noexcept {
        return rsidPlaybackMode_ == RsidPlaybackMode::Strict && rsidPhi2PlaybackActive();
    }
    bool rsidKnownDowngradeFree() const noexcept {
        return rsidExactnessDowngradeReasons() == RsidExactnessDowngrade::None &&
               physicalExactnessBlockerMask() == 0u;
    }
    bool rsidPhysicallyExact() const noexcept {
        return rsidStrictPhi2PathActive() && rsidKnownDowngradeFree();
    }
    // Compact GUI/HUD encoding for the legacy diagnostic field:
    //   0 = no RSID strict-PHI2 path is active, or playback is unavailable
    //   1 = strict-PHI2 path is active and no known downgrade was observed
    //   2 = strict-PHI2 path is active, but the exactness ledger has one or more
    //       known downgrades (CIA/VIC/HLE/SID-read/open-bus/etc.).
    // This deliberately separates "PHI2 discipline" from "physically exact".
    uint8_t rsidStrictStatusCode() const noexcept {
        if (!rsidStrictPhi2PathActive()) return static_cast<uint8_t>(RsidStrictStatusCode::None);
        return rsidKnownDowngradeFree()
            ? static_cast<uint8_t>(RsidStrictStatusCode::Clean)
            : static_cast<uint8_t>(RsidStrictStatusCode::Downgraded);
    }
    uint32_t rsidExactnessDowngradeMask() const noexcept {
        return rsidDowngradeMask(rsidExactnessDowngradeReasons());
    }
    uint8_t rsidPlaybackModeCode() const noexcept {
        if (!loaded_.header.valid || !loaded_.header.rsid) return 0u;
        return rsidPlaybackMode_ == RsidPlaybackMode::Strict ? 1u : 2u;
    }
    // Deprecated compatibility spelling. This means physically exact / no known
    // downgrade, not merely "strict PHI2 path is active".
    bool rsidExactPlaybackActive() const noexcept { return rsidPhysicallyExact(); }

    // Returns a bitmask of all observed conditions that downgrade the RSID PHI2 path.
    // Zero (None) means no known downgrade was observed. Each bit is independently observable.
    RsidExactnessDowngrade rsidExactnessDowngradeReasons() const noexcept {
        RsidExactnessDowngrade r = RsidExactnessDowngrade::None;
        if (!loaded_.header.valid || !loaded_.header.rsid)
            return r | RsidExactnessDowngrade::NotRsid;
        if (!phi2InitUsed_ || !usePhi2Machine_ || !phi2MachineReady_)
            r = r | RsidExactnessDowngrade::Phi2NotActive;
        if (rsidLegacyInitFallbackCount_ > 0u)
            r = r | RsidExactnessDowngrade::FallbackInit | RsidExactnessDowngrade::LegacyFallback;
        if (phi2Machine_.cpu().unsupportedOpcodeTotal() != 0u)
            r = r | RsidExactnessDowngrade::UnsupportedOpcode;
        if (phi2Machine_.cpu().approximateOpcodeTotal() != 0u)
            r = r | RsidExactnessDowngrade::ApproximateOpcode;
        if (phi2Machine_.config().enableVicBusSteal && !VicII::kBusStealIsCycleExact)
            r = r | RsidExactnessDowngrade::VicBusStealApprox;
        if (timedWriteOverflowSinceLoad_ > 0u)
            r = r | RsidExactnessDowngrade::TimedWriteOverflow;
        if (droppedMultiSidWritesSinceLoad_ > 0u)
            r = r | RsidExactnessDowngrade::DroppedMultiSidWrites;
        if (!platform_.hasCompleteExternalRomSet())
            r = r | RsidExactnessDowngrade::MissingRealRoms;
        else if (!platform_.hasVerifiedStockRomSet())
            r = r | RsidExactnessDowngrade::RomIdentityUnverified;
        if (loaded_.header.c64BasicFlag)
            r = r | RsidExactnessDowngrade::BasicStartupUnsupported;
        if (!Cia6526::kTimerIrqModelIsCycleExact)
            r = r | RsidExactnessDowngrade::CiaModelApprox;
        if (phi2SidBridge_.sidReadApproximationCount() > 0u || sink_.sidReadApproximationCount > 0u)
            r = r | RsidExactnessDowngrade::SidReadApprox;
        if (phi2SidBridge_.rmwSidWriteCount() > 0u || phi2Machine_.diagnostics().rmwDummySidWrites > 0u)
            r = r | RsidExactnessDowngrade::RmwSidWrite;
        if (legacyRuntimePlaybackCount_ > 0u)
            r = r | RsidExactnessDowngrade::LegacyCpuPlayback;
        if (strictRsidNotPhi2Count_ > 0u)
            r = r | RsidExactnessDowngrade::StrictRsidNotPhi2;
        if (initBrkSentinelCount_ > 0u)
            r = r | RsidExactnessDowngrade::InitBrkSentinel;
        if (phi2SidBridge_.invalidSidChipReadCount() > 0u ||
            phi2SidBridge_.invalidSidChipWriteCount() > 0u ||
            sink_.invalidSidChipReadCount > 0u ||
            sink_.invalidSidChipWriteCount > 0u)
            r = r | RsidExactnessDowngrade::InvalidSidChipAccess;
        if (phi2Machine_.memory().openBusReads() > 0u ||
            phi2Machine_.diagnostics().openBusReads > 0u)
            r = r | RsidExactnessDowngrade::OpenBusApprox;
        if (phi2SidBridge_.droppedSidHoleWriteCount() > 0u ||
            sink_.droppedSidHoleWriteCount > 0u ||
            phi2Machine_.memory().droppedSidHoleWrites() > 0u)
            r = r | RsidExactnessDowngrade::SidHoleWrite;
        return r;
    }

    // Saturating 32-bit add: long-running sessions with pathological digi/SID
    // abuse must never wrap an exactness counter back to zero (which would
    // silently clear the downgrade condition). Saturate at UINT32_MAX instead.
    static uint32_t satAdd32_(uint32_t a, uint32_t b) noexcept {
        const uint64_t s = static_cast<uint64_t>(a) + static_cast<uint64_t>(b);
        return s > static_cast<uint64_t>(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(s);
    }

    // Notify the runtime that timed writes overflowed this render block.
    // Called from the kernel render path to propagate overflow into exactness.
    void notifyTimedWriteOverflow(uint32_t overflowCount) noexcept {
        if (overflowCount > 0u)
            timedWriteOverflowSinceLoad_ = satAdd32_(timedWriteOverflowSinceLoad_, overflowCount);
    }
    void notifyDroppedMultiSidWrites(uint32_t droppedCount) noexcept {
        if (droppedCount > 0u)
            droppedMultiSidWritesSinceLoad_ = satAdd32_(droppedMultiSidWritesSinceLoad_, droppedCount);
    }

    C64RenderTransaction beginRenderTransaction() noexcept {
        C64RenderTransaction tx{};
        if (renderTxActive_) return tx;
        renderTxActive_ = true;
        renderTxDeferredFullRamSync_ = false;
        renderTxStartPhi2_ = phi2Machine_.phi2Cycle();
        phi2Machine_.captureSnapshot(renderTxPhi2Snapshot_);
        sink_.captureSnapshot(renderTxSinkSnapshot_);
        tx.active = true;
        tx.phi2Start = renderTxStartPhi2_;
        tx.platformJournalActive = platform_.beginRenderMutationJournal();
        return tx;
    }

    void commitRenderTransaction(C64RenderTransaction& tx) noexcept {
        if (!tx.active || !renderTxActive_) return;
        if (tx.platformJournalActive) {
            platform_.commitRenderMutationJournal();
        }
        const bool deferredFullRamSync = renderTxDeferredFullRamSync_;
        const uint64_t deferredStartPhi2 = renderTxStartPhi2_;
        renderTxActive_ = false;
        renderTxDeferredFullRamSync_ = false;
        renderTxStartPhi2_ = 0;
        tx.active = false;
        if (deferredFullRamSync) {
            syncPlatformRamFromPhi2Writes_(deferredStartPhi2);
        }
    }

    bool rollbackRenderTransaction(C64RenderTransaction& tx) noexcept {
        if (!tx.active || !renderTxActive_) return false;
        phi2Machine_.restoreSnapshot(renderTxPhi2Snapshot_);
        sink_.restoreSnapshot(renderTxSinkSnapshot_);
        bool platformOk = false;
        if (tx.platformJournalActive) {
            platformOk = platform_.rollbackRenderMutationJournal();
        }
        renderTxActive_ = false;
        renderTxDeferredFullRamSync_ = false;
        renderTxStartPhi2_ = 0;
        tx.active = false;
        return platformOk;
    }

    // v872 P1-4 contamination recovery. rollbackRenderTransaction() restores the
    // authoritative PHI2 machine and runtime SID sink from full value snapshots, so
    // those are always correct even when it returns false. A false return means only
    // the platform's *bounded* mutation journal could not prove a full restore of the
    // platform inspection/bootstrap mirror (journal overflow) — that stale residue
    // would otherwise leak into the next play's bootstrap RAM copies. Re-seed the
    // entire platform mirror (RAM + SID image + inspection) from the authoritative
    // PHI2 state so playback resumes from a coherent surface after a failed rollback.
    // O(64K) and only invoked on the rare contamination path.
    void resyncPlatformFromAuthoritativePhi2() noexcept {
        if (!phi2MachineReady_) return;
        MemoryMatrix& memory = phi2Machine_.memory();
        for (uint32_t a = 0u; a <= 0xFFFFu; ++a) {
            const uint16_t addr = static_cast<uint16_t>(a);
            if (addr >= 0xD800u && addr <= 0xDBFFu) {
                platform_.pokeColorRam(addr, memory.peekColorRam(addr));
            } else {
                platform_.pokeMemory(addr, memory.peekRam(addr));
            }
        }
        memory.clearDirtyWriteLog();
        syncPlatformSidMirrorFromSink_();
        syncPlatformInspectionFromPhi2_();
    }

    // Fix #2: set/query the RSID playback mode for this runtime instance.
    RsidPlaybackMode rsidPlaybackMode() const noexcept { return rsidPlaybackMode_; }
    void setRsidPlaybackMode(RsidPlaybackMode m) noexcept {
        rsidPlaybackMode_ = m;
        // Propagate to CPU: Strict = BRK vectors normally; Compatible = BRK→JAM.
        phi2Machine_.cpu().setTrapBrkAsJam(m == RsidPlaybackMode::Compatible);
        // Propagate to CPU approximate-opcode policy.
        phi2Machine_.cpu().setApproximateOpcodePolicy(
            m == RsidPlaybackMode::Strict
                ? ArpSID::C64::ApproximateOpcodePolicy::Jam
                : ArpSID::C64::ApproximateOpcodePolicy::Allow);
    }
    C64Phi2Machine& phi2Machine() noexcept { return phi2Machine_; }
    const C64Phi2Machine& phi2Machine() const noexcept { return phi2Machine_; }
    const C64Phi2Diagnostics& phi2Diagnostics() const noexcept { return phi2Machine_.diagnostics(); }
    uint64_t psidSyntheticCiaIrqReportCount() const noexcept {
        return platform_.bootState().psidCiaIrqObserved ? platform_.bootState().psidCiaRunGeneration : 0u;
    }
    bool basicStartupRequested() const noexcept { return loaded_.header.c64BasicFlag; }
    bool basicStartupExecuted() const noexcept { return false; }

    // Attach a PHI2 bus-trace sink (e.g. FixedPhi2Trace from c64_phi2_trace.h)
    // so callers/diagnostics can capture the exact per-cycle bus phases the
    // PHI2 machine produces. Pass nullptr to detach. The sink is invoked on
    // the audio thread inside tickPhi2(), so it must be RT-safe (FixedPhi2Trace
    // is: fixed storage, no allocation, drop-counted). The attachment survives
    // resetPhi2Machine_/powerOn (the machine does not clear its trace pointer).
    void attachPhi2Trace(IPhi2TraceSink* sink) noexcept { phi2Machine_.attachTrace(sink); }
    uint64_t phi2UnsupportedOpcodeCount() const noexcept {
        return phi2Machine_.cpu().unsupportedOpcodeTotal();
    }
    uint64_t phi2ApproximateOpcodeCount() const noexcept {
        return phi2Machine_.cpu().approximateOpcodeTotal();
    }

    uint32_t legacyRuntimePlaybackCount() const noexcept { return legacyRuntimePlaybackCount_; }
    uint32_t strictRsidNotPhi2Count() const noexcept { return strictRsidNotPhi2Count_; }
    uint32_t psidCiaLegacyServiceCount() const noexcept { return psidCiaCompatibilityServiceObservedCount_; }
    uint32_t psidCiaCompatibilityServiceObservedCount() const noexcept { return psidCiaCompatibilityServiceObservedCount_; }
    uint32_t psidCiaRunPlayCompatibilityCount() const noexcept { return psidCiaRunPlayCompatibilityCount_; }
    uint32_t psidCiaExplicitServiceCompatibilityCount() const noexcept { return psidCiaExplicitServiceCompatibilityCount_; }
    uint32_t psidCiaPhi2ServiceCount() const noexcept { return psidCiaPhi2ServiceCount_; }
    bool psidCiaCompatibilityServiceUsed() const noexcept { return psidCiaCompatibilityServiceObservedCount_ > 0u; }
    bool psidCiaPhysicalPhi2ServiceActive() const noexcept { return psidCiaPhi2ServiceCount_ > 0u; }
    bool physicalOnlyBuildEnabled() const noexcept { return kC64PhysicalOnlyBuild; }
    bool legacyMos6510RuntimePathsReachable() const noexcept { return c64LegacyMos6510RuntimePathsReachable(); }

    C64PhysicalExactnessBlocker physicalExactnessBlockers() const noexcept {
        C64PhysicalExactnessBlocker b = C64PhysicalExactnessBlocker::None;
        if (!platform_.hasCompleteExternalRomSet())
            b = b | C64PhysicalExactnessBlocker::MissingRealRoms;
        else if (!platform_.hasVerifiedStockRomSet())
            b = b | C64PhysicalExactnessBlocker::RomIdentityUnverified;
        if (!Cia6526::kTimerIrqModelIsCycleExact)
            b = b | C64PhysicalExactnessBlocker::Cia6526NotCycleExact;
        if (!VicII::kBusStealIsCycleExact)
            b = b | C64PhysicalExactnessBlocker::VicIINotCycleExact;
        if (!SidReadbackModel::kCycleExact)
            b = b | C64PhysicalExactnessBlocker::SidReadbackModelApprox;
        // Same distinction for open bus: event ledgers record observed reads,
        // while this mask reports that the current open-bus model is deterministic
        // and not a capacitive board-level physical model.
        b = b | C64PhysicalExactnessBlocker::OpenBusModelApprox;
        // audit #7: in addition to the always-on capability blocker above, set a
        // dedicated OBSERVED blocker if this run actually latched the SID open bus
        // (sidOpenBusReadCount counts $D400-region reads with no driving source).
        if (sidOpenBusReadCount() > 0u)
            b = b | C64PhysicalExactnessBlocker::SidOpenBusApprox;
        // audit #8: observed CPU Color-RAM read whose high nibble came from open bus.
        if (platform_.colorRamHighNibbleOpenBusReadCount() > 0u)
            b = b | C64PhysicalExactnessBlocker::ColorRamOpenBusApprox;
        // audit #11: observed CPU read of POTX/POTY (un-modelled paddle hardware).
        // Both the SID-sink read path and the no-sink platform read path count these;
        // either is a PotXYApprox blocker (P1-13: no-sink POTX/POTY reads previously
        // only counted as generic SID open bus).
        if (potxyReadCount() > 0u || platform_.sidNoSinkPotxyReadCount() > 0u)
            b = b | C64PhysicalExactnessBlocker::PotXYApprox;
        if (loaded_.header.valid && loaded_.header.rsid && loaded_.header.c64BasicFlag)
            b = b | C64PhysicalExactnessBlocker::BasicStartupUnsupported;
        if (psidCiaCompatibilityServiceUsed())
            b = b | C64PhysicalExactnessBlocker::PsidCiaCompatibility;
        // Runtime execution is PHI2-only. The compatibility helper is retained as
        // an ABI/policy query but is permanently false; source-level C64Platform
        // test fixtures do not make the production C64Runtime fallback-reachable.
        if (c64LegacyMos6510RuntimePathsReachable())
            b = b | C64PhysicalExactnessBlocker::LegacyMos6510PathPresent;
        if (legacyRuntimePlaybackCount_ > 0u)
            b = b | C64PhysicalExactnessBlocker::LegacyMos6510PlaybackObserved;
        return b;
    }
    uint32_t physicalExactnessBlockerMask() const noexcept {
        return c64PhysicalBlockerMask(physicalExactnessBlockers());
    }

    // Combined release-gate mask: capability blockers plus the fact that this
    // run has observed one or more event-ledger downgrades.  The detailed event
    // reasons remain available through rsidExactnessDowngradeMask(); this bit is
    // a compact guard for GUI/HUD/release tooling that needs one “not physical
    // exact” decision surface.
    C64PhysicalExactnessBlocker totalPhysicalRiskBlockers() const noexcept {
        C64PhysicalExactnessBlocker b = physicalExactnessBlockers();
        if (loaded_.header.valid && loaded_.header.rsid &&
            rsidExactnessDowngradeReasons() != RsidExactnessDowngrade::None)
            b = b | C64PhysicalExactnessBlocker::ObservedDowngradeLedger;
        // audit #13: PSID parity. The RSID event-downgrade ledger above is RSID-only,
        // so a PSID tune that observed real physical approximations used to leave the
        // compact ObservedDowngradeLedger "not physically exact" bit clear. Set it for
        // PSID too whenever this run observed an approximation (open-bus SID/Color-RAM
        // reads, paddle POTX/POTY reads, or the PSID CIA compatibility service), giving
        // PSID risk reporting parity with RSID's ledger.
        if (loaded_.header.valid && !loaded_.header.rsid) {
            const bool psidObservedRisk =
                // PSID parity: event risks that are detailed in the RSID downgrade
                // ledger must still flip the compact observed-risk blocker for PSID.
                // PSID does not use rsidExactnessDowngradeReasons() because that API
                // deliberately reports NotRsid for non-RSID images; keep this list in
                // parity with the observed portions of that ledger.
                timedWriteOverflowSinceLoad_ > 0u ||
                droppedMultiSidWritesSinceLoad_ > 0u ||
                sidReadApproximationCount() > 0u ||
                sidOpenBusReadCount() > 0u ||
                platform_.colorRamHighNibbleOpenBusReadCount() > 0u ||
                potxyReadCount() > 0u ||
                platform_.sidNoSinkPotxyReadCount() > 0u ||
                invalidSidChipReadCount() > 0u ||
                invalidSidChipWriteCount() > 0u ||
                sidHoleWriteCount() > 0u ||
                rmwSidWriteCount() > 0u ||
                phi2Machine_.cpu().unsupportedOpcodeTotal() != 0u ||
                phi2Machine_.cpu().approximateOpcodeTotal() != 0u ||
                platform_.cpu().state().semanticFallbackCount != 0u ||
                platform_.cpu().state().approximateIllegalOpcodeCount != 0u ||
                psidCiaCompatibilityServiceUsed();
            if (psidObservedRisk)
                b = b | C64PhysicalExactnessBlocker::ObservedDowngradeLedger;
        }
        return b;
    }
    uint32_t totalPhysicalRiskMask() const noexcept {
        return c64PhysicalBlockerMask(totalPhysicalRiskBlockers());
    }
    bool hasAnyPhysicalExactnessRisk() const noexcept {
        return totalPhysicalRiskMask() != 0u;
    }

    bool isRsid() const noexcept {
        return loaded_.header.valid && loaded_.header.rsid;
    }

    bool usesCiaTimingForSong(uint16_t song) const noexcept {
        if (!loaded_.header.valid) return false;
        const uint16_t selectedSong = song ? song : (loaded_.header.startSong ? loaded_.header.startSong : 1u);
        return runtimeHeaderUsesCiaTimingForSong_(selectedSong);
    }

    bool usesCiaTiming() const noexcept {
        return loaded_.header.valid && platform_.bootState().sidUsesCiaTiming;
    }

    C64PsidCiaPlaybackBootstrapSnapshot psidCiaPlaybackBootstrapSnapshot() noexcept {
        return platform_.psidCiaPlaybackBootstrapSnapshot();
    }

    // Runs the real C64 machine for up to `cycles` PHI2 clocks.
    //
    // PHI2-machine path (runRsidPhi2MachineCycles): may STOP EARLY on an
    // instruction-budget hit, CPU jam, or unsupported opcode — in that case
    // executedCycles+passiveCycles < cycles and the unconsumed remainder stays
    // as debt to be retried next block. result.completedCycleBudget reports
    // whether the full budget was actually reached; callers should debit only
    // (executedCycles+passiveCycles), not the full request, on this path.
    C64RunResult runRsidMachineCycles(uint64_t cycles, uint32_t maxInstructions = 0u,
                                      SidRegisterSink* sid = nullptr) noexcept {
        C64RunResult empty{};
        empty.requestedCycles = cycles;
        if (!loaded_.header.valid || !loaded_.header.rsid || !platform_.bootState().sidInitCompleted) return empty;
        if (usePhi2Machine_ && phi2MachineReady_) {
            return runRsidPhi2MachineCycles(cycles, maxInstructions, sid);
        }
        ++strictRsidNotPhi2Count_;
        empty.completedCycleBudget = false;
        platform_.markSidPlayComplete(false);
        return empty;
    }


    // Continuous machine execution for tunes that do not have a discrete PSID
    // play routine. RSID still uses the strict/compatible RSID runner above;
    // PSID playAddress==0 is explicitly supported here instead of being routed
    // through runRsidMachineCycles(), which intentionally refuses non-RSID images.
    C64RunResult runContinuousMachineCycles(uint64_t cycles, uint32_t maxInstructions = 0u,
                                            SidRegisterSink* sid = nullptr) noexcept {
        C64RunResult empty{};
        empty.requestedCycles = cycles;
        if (!loaded_.header.valid || !platform_.bootState().sidInitCompleted) return empty;
        if (loaded_.header.rsid) return runRsidMachineCycles(cycles, maxInstructions, sid);
        if (loaded_.header.playAddress != 0u) return empty;
        if (usePhi2Machine_ && phi2MachineReady_) {
            return runPsidPhi2MachineCycles(cycles, maxInstructions, sid);
        }

        empty.completedCycleBudget = false;
        platform_.markSidPlayComplete(false);
        return empty;
    }

    C64RunResult runRsidPhi2MachineCycles(uint64_t cycles,
                                          uint32_t maxInstructions = 0u,
                                          SidRegisterSink* sid = nullptr) noexcept {
        C64RunResult result{};
        result.requestedCycles = cycles;
        if (!loaded_.header.valid || !loaded_.header.rsid ||
            !platform_.bootState().sidInitCompleted || !phi2MachineReady_) {
            return result;
        }

        const uint32_t instructionBudget = maxInstructions != 0u
            ? maxInstructions
            : static_cast<uint32_t>(std::min<uint64_t>(std::max<uint64_t>(cycles / 2u, 1u), 65535u));
        const uint64_t startPhi2 = phi2Machine_.phi2Cycle();
        const uint64_t endPhi2 = startPhi2 + cycles;
        uint64_t executedCycles = 0;
        uint32_t completedInstructions = 0;
        const uint64_t unsupportedAtStart = phi2Machine_.cpu().unsupportedOpcodeTotal();
        const uint64_t approximateAtStart = phi2Machine_.cpu().approximateOpcodeTotal();
        const uint64_t retiredAtStart = phi2Machine_.cpu().retiredInstructionCount();

        phi2SidBridge_.attach(&sink_, sid);
        phi2Machine_.attachSidSink(&phi2SidBridge_);
        platform_.markSidPlayStart(phi2Machine_.cpu().pc(), maxInstructions);

        if (phi2Machine_.cpu().state().jammed) {
            result.cpuJammed = true;
            result.cpuJamReason = phi2Machine_.cpu().lastJamReason();
            result.cpuJamOpcode = phi2Machine_.cpu().lastJamOpcode();
            result.completedCycleBudget = false;
            platform_.markSidPlayComplete(false);
            return result;
        }

        while (phi2Machine_.phi2Cycle() < endPhi2) {
            const bool jammedBefore = phi2Machine_.cpu().state().jammed;
            const uint64_t retiredBefore = phi2Machine_.cpu().retiredInstructionCount();
            phi2Machine_.tickPhi2();
            const auto& after = phi2Machine_.cpu().state();
            if (!jammedBefore) ++executedCycles;

            const uint64_t retiredAfter = phi2Machine_.cpu().retiredInstructionCount();
            if (retiredAfter != retiredBefore) {
                const uint64_t delta = retiredAfter - retiredBefore;
                for (uint64_t i = 0; i < delta; ++i) platform_.markSidPlayInstruction();
                completedInstructions = static_cast<uint32_t>(
                    std::min<uint64_t>(retiredAfter - retiredAtStart, UINT32_MAX));
                if (completedInstructions >= instructionBudget &&
                    phi2Machine_.phi2Cycle() < endPhi2) {
                    result.instructionBudgetHit = true;
                    break;
                }
            }
            if (after.jammed) break;
        }

        const uint64_t actualEnd = phi2Machine_.phi2Cycle();
        result.executedCycles = std::min<uint64_t>(executedCycles, actualEnd - startPhi2);
        result.passiveCycles = (actualEnd - startPhi2) > result.executedCycles
            ? (actualEnd - startPhi2) - result.executedCycles
            : 0u;
        result.executedInstructions = completedInstructions;
        result.cpuJammed = phi2Machine_.cpu().state().jammed;
        result.cpuJamReason = phi2Machine_.cpu().lastJamReason();
        result.cpuJamOpcode = phi2Machine_.cpu().lastJamOpcode();
        result.completedCycleBudget = (actualEnd >= endPhi2);
        // An unsupported opcode is a CPU implementation gap / malformed-or-unsupported
        // tune, NOT an instruction-budget exhaustion. Report it as its own condition so
        // telemetry/GUI does not conflate "increase budget" with "CPU gap". cpuJammed is
        // still asserted so the play-complete gate below stays correct.
        if (phi2Machine_.cpu().unsupportedOpcodeTotal() != unsupportedAtStart) {
            result.unsupportedOpcodeHit = true;
            result.cpuJammed = true;
        }
        if (phi2Machine_.cpu().approximateOpcodeTotal() != approximateAtStart) {
            result.approximateOpcodeHit = true;
        }
        platform_.markSidPlayComplete(result.completedCycleBudget &&
                                      !result.cpuJammed &&
                                      !result.instructionBudgetHit &&
                                      !result.unsupportedOpcodeHit);
        // RSID continuous execution is machine-driven; the PHI2 machine is the sole
        // authority. Publish the same RAM + CPU/CIA/VIC inspection surface the PSID
        // paths do, so the public platform mirror (GUI/telemetry/debug) does not go
        // stale after a free-running RSID frame (v873 P1-1).
        syncPlatformRamFromPhi2Writes_(startPhi2);
        syncPlatformInspectionFromPhi2_();
        syncPlatformSidMirrorFromSink_();
        return result;
    }

    C64RunResult runPsidPhi2MachineCycles(uint64_t cycles,
                                          uint32_t maxInstructions = 0u,
                                          SidRegisterSink* sid = nullptr) noexcept {
        C64RunResult result{};
        result.requestedCycles = cycles;
        if (!loaded_.header.valid || loaded_.header.rsid ||
            loaded_.header.playAddress != 0u ||
            !platform_.bootState().sidInitCompleted || !phi2MachineReady_) {
            return result;
        }

        const uint32_t instructionBudget = maxInstructions != 0u
            ? maxInstructions
            : static_cast<uint32_t>(std::min<uint64_t>(std::max<uint64_t>(cycles / 2u, 1u), 65535u));
        const uint64_t startPhi2 = phi2Machine_.phi2Cycle();
        const uint64_t endPhi2 = startPhi2 + cycles;
        uint64_t executedCycles = 0;
        uint32_t completedInstructions = 0;
        const uint64_t unsupportedAtStart = phi2Machine_.cpu().unsupportedOpcodeTotal();
        const uint64_t approximateAtStart = phi2Machine_.cpu().approximateOpcodeTotal();
        const uint64_t retiredAtStart = phi2Machine_.cpu().retiredInstructionCount();

        phi2SidBridge_.attach(&sink_, sid);
        phi2Machine_.attachSidSink(&phi2SidBridge_);
        platform_.markSidPlayStart(phi2Machine_.cpu().pc(), maxInstructions);

        if (phi2Machine_.cpu().state().jammed) {
            result.cpuJammed = true;
            result.cpuJamReason = phi2Machine_.cpu().lastJamReason();
            result.cpuJamOpcode = phi2Machine_.cpu().lastJamOpcode();
            result.completedCycleBudget = false;
            platform_.markSidPlayComplete(false);
            return result;
        }

        while (phi2Machine_.phi2Cycle() < endPhi2) {
            const bool jammedBefore = phi2Machine_.cpu().state().jammed;
            const uint64_t retiredBefore = phi2Machine_.cpu().retiredInstructionCount();
            phi2Machine_.tickPhi2();
            const auto& after = phi2Machine_.cpu().state();
            if (!jammedBefore) ++executedCycles;

            const uint64_t retiredAfter = phi2Machine_.cpu().retiredInstructionCount();
            if (retiredAfter != retiredBefore) {
                const uint64_t delta = retiredAfter - retiredBefore;
                for (uint64_t i = 0; i < delta; ++i) platform_.markSidPlayInstruction();
                completedInstructions = static_cast<uint32_t>(
                    std::min<uint64_t>(retiredAfter - retiredAtStart, UINT32_MAX));
                if (completedInstructions >= instructionBudget &&
                    phi2Machine_.phi2Cycle() < endPhi2) {
                    result.instructionBudgetHit = true;
                    break;
                }
            }
            if (after.jammed) break;
        }

        const uint64_t actualEnd = phi2Machine_.phi2Cycle();
        result.executedCycles = std::min<uint64_t>(executedCycles, actualEnd - startPhi2);
        result.passiveCycles = (actualEnd - startPhi2) > result.executedCycles
            ? (actualEnd - startPhi2) - result.executedCycles
            : 0u;
        result.executedInstructions = completedInstructions;
        result.cpuJammed = phi2Machine_.cpu().state().jammed;
        result.cpuJamReason = phi2Machine_.cpu().lastJamReason();
        result.cpuJamOpcode = phi2Machine_.cpu().lastJamOpcode();
        result.completedCycleBudget = (actualEnd >= endPhi2);
        if (phi2Machine_.cpu().unsupportedOpcodeTotal() != unsupportedAtStart) {
            result.unsupportedOpcodeHit = true;
            result.cpuJammed = true;
        }
        if (phi2Machine_.cpu().approximateOpcodeTotal() != approximateAtStart) {
            result.approximateOpcodeHit = true;
        }
        platform_.markSidPlayComplete(result.completedCycleBudget &&
                                      !result.cpuJammed &&
                                      !result.instructionBudgetHit &&
                                      !result.unsupportedOpcodeHit);
        syncPlatformRamFromPhi2Writes_(startPhi2);
        syncPlatformInspectionFromPhi2_();
        syncPlatformSidMirrorFromSink_();
        return result;
    }

    C64RunResult runPsidVbiPassivePhi2Cycles(uint64_t cycles,
                                             SidRegisterSink* sid = nullptr) noexcept {
        C64RunResult result{};
        result.requestedCycles = cycles;
        if (!loaded_.header.valid || loaded_.header.rsid ||
            loaded_.header.playAddress == 0u ||
            !platform_.bootState().sidInitCompleted || !phi2MachineReady_) {
            return result;
        }

        phi2SidBridge_.attach(&sink_, sid);
        phi2Machine_.attachSidSink(&phi2SidBridge_);
        // Passive VBI catch-up advances CIA/VIC timing between play calls while the
        // 6510 stays frozen. Use explicit execution suppression rather than forging
        // cpu().state().jammed so a genuine KIL/JAM fault is not masqueraded by the
        // scheduler (v872 P1-6). The flag is scoped to this passive window and
        // cleared before returning, so the CPU leaves passive stepping in its honest
        // state; the following runPlay() starts from a coherent, un-forged CPU.
        phi2Machine_.setCpuExecutionSuppressed(true);
        const uint64_t startPhi2 = phi2Machine_.phi2Cycle();
        const uint64_t endPhi2 = startPhi2 + cycles;
        while (phi2Machine_.phi2Cycle() < endPhi2) {
            phi2Machine_.tickPhi2();
        }
        phi2Machine_.setCpuExecutionSuppressed(false);
        const uint64_t actualEnd = phi2Machine_.phi2Cycle();
        result.passiveCycles = actualEnd >= startPhi2 ? (actualEnd - startPhi2) : 0u;
        result.completedCycleBudget = (actualEnd >= endPhi2);
        syncPlatformRamFromPhi2Writes_(startPhi2);
        syncPlatformInspectionFromPhi2_();
        syncPlatformSidMirrorFromSink_();
        return result;
    }

    C64PsidCiaRuntimeDriveSnapshot runPsidCiaPlaybackIrqTicks(uint64_t maxTicks) noexcept {
        return runPsidCiaPhi2PlaybackServiceTicks_(maxTicks);
    }

    C64PsidCiaRuntimeDriveSnapshot runPsidCiaPhi2PlaybackServiceTicks(uint64_t maxTicks) noexcept {
        return runPsidCiaPhi2PlaybackServiceTicks_(maxTicks);
    }

    // v855 P0 timing fix: `externalTimedSink` receives every play-routine SID
    // write with its exact PHI2 cycle stamp (see runPlay()).
    C64PsidCiaRuntimeDriveSnapshot runPsidCiaPlaybackServiceTicks(
            uint64_t maxTicks,
            SidRegisterSink* externalTimedSink = nullptr) noexcept {
        return runPsidCiaPhi2PlaybackServiceTicks_(maxTicks, externalTimedSink);
    }

    C64InterruptBootstrapValidationSnapshot validateInstalledInterruptBootstrap() noexcept {
        return platform_.validateInstalledInterruptBootstrap();
    }

    C64SystemIntegritySnapshot validateC64SystemIntegrity() noexcept {
        return platform_.validateC64SystemIntegrity();
    }

    C64RsidDebugSnapshot rsidDebugSnapshot() noexcept {
        return platform_.rsidDebugSnapshot();
    }

private:
    void resetPhi2Machine_(bool pal) noexcept {
        Phi2MachineConfig cfg{};
        cfg.video = pal ? MachineVideoStandard::PAL : MachineVideoStandard::NTSC;
        cfg.runtimeMode = RuntimeMode::RSIDStrict;
        cfg.deterministicPowerRam = true;
        phi2Machine_.configure(cfg);
        phi2Machine_.powerOn();
        uint16_t bases[5] = {0xD400u, 0u, 0u, 0u, 0u};
        uint8_t count = 1u;
        if (loaded_.header.valid) {
            count = static_cast<uint8_t>(std::clamp<int>(loaded_.header.sidChipCount, 1, 5));
            for (uint8_t i = 0; i < count; ++i) bases[i] = loaded_.header.sidBase[i] ? loaded_.header.sidBase[i] : (i == 0u ? 0xD400u : 0u);
        }
        phi2Machine_.configureSidBases(bases, count);
        phi2Machine_.setTrapBrkAsJam(rsidPlaybackMode_ == RsidPlaybackMode::Compatible);
        phi2Machine_.cpu().setApproximateOpcodePolicy(
            rsidPlaybackMode_ == RsidPlaybackMode::Strict
                ? ArpSID::C64::ApproximateOpcodePolicy::Jam
                : ArpSID::C64::ApproximateOpcodePolicy::Allow);
        phi2SidBridge_.attach(&sink_, nullptr);
        phi2Machine_.attachSidSink(&phi2SidBridge_);
    }

    void mirrorPlatformRamToPhi2_() noexcept {
        for (uint32_t a = 0; a <= 0xFFFFu; ++a) {
            phi2Machine_.memory().pokeRam(static_cast<uint16_t>(a),
                                          platform_.peekMemory(static_cast<uint16_t>(a)));
        }
    }

    // Mirror all ROM regions from the legacy platform into the PHI2 machine.
    // KERNAL: provides IRQ/NMI/BRK vectors and safe-vector handlers.
    // BASIC: needed by players that rely on BASIC ROM floating-point routines.
    // CHAR: mirrored for completeness; SID players rarely access it.
    // Color RAM and processor port RAM bytes are also mirrored so C64 I/O
    // initialization code in the bootstrap sees coherent machine state.
    void mirrorPlatformKernalRomToPhi2_() noexcept {
        for (uint32_t a = 0xE000u; a <= 0xFFFFu; ++a) {
            phi2Machine_.memory().pokeKernalRom(
                static_cast<uint16_t>(a),
                platform_.roms().readKernal(static_cast<uint16_t>(a)));
        }
    }

    void mirrorPlatformAllRomsToPhi2_() noexcept {
        // KERNAL ($E000-$FFFF)
        for (uint32_t a = 0xE000u; a <= 0xFFFFu; ++a) {
            phi2Machine_.memory().pokeKernalRom(
                static_cast<uint16_t>(a),
                platform_.roms().readKernal(static_cast<uint16_t>(a)));
        }
        // BASIC ($A000-$BFFF)
        for (uint32_t a = 0xA000u; a <= 0xBFFFu; ++a) {
            phi2Machine_.memory().pokeBasicRom(
                static_cast<uint16_t>(a),
                platform_.roms().readBasic(static_cast<uint16_t>(a)));
        }
        // CHAR ROM ($D000-$DFFF)
        for (uint32_t a = 0xD000u; a <= 0xDFFFu; ++a) {
            phi2Machine_.memory().pokeCharRom(
                static_cast<uint16_t>(a),
                platform_.roms().readCharacter(static_cast<uint16_t>(a)));
        }
        // Color RAM ($D800-$DBFF) — 1KB visible through IO space
        for (uint32_t a = 0xD800u; a <= 0xDBFFu; ++a) {
            phi2Machine_.memory().pokeColorRam(
                static_cast<uint16_t>(a),
                static_cast<uint8_t>(platform_.colorRam()[a & 0x03FFu] & 0x0Fu));
        }
    }

    // Run PSID or RSID init through the PHI2 machine from the reset vector.
    // Completion is the format bootstrap's idle loop, $FFFF halt sentinel, or
    // a compatibility BRK sentinel after at least one SID write.
    //
    // The RSID bootstrap installs itself at 0x0334 and writes the reset vector
    // to 0xFFFC in RAM (portData=0x35 maps 0xE000-0xFFFF to RAM at fetch time).
    // The bootstrap:
    // SEI; LDX#$FF; TXS; set portDir/Data; LDA#song; LDX#0; LDY#0;
    // JSR initAddr ← init routine (ends with RTS in correct C64 players)
    // CLI
    // JMP idle ← completion signal
    //
    // Completion conditions accepted:
    // 1. PC == idle (normal: init returned via RTS → CLI → JMP self)
    // 2. PC == CLI instruction just after JSR (RTS popped the bootstrap return
    // address and is about to execute CLI before the idle loop)
    // Both are equivalent; (2) catches the exact tick before idle is reached.
    bool runRsidInitViaPhi2_(uint32_t maxCycles) noexcept {
        // Full mirror: RAM (64KB) + KERNAL ROM + color RAM already done in
        // loadPsid and kept current. Re-mirror now to pick up the bootstrap
        // code that platform_.installRsidBootstrap() wrote after loadPsid.
        resetPhi2Machine_(platform_.clockHz() == kPalPhi2Hz);
        phi2Machine_.setPhi2Cycle(platform_.phi2Cycle());
        mirrorPlatformRamToPhi2_();
        mirrorPlatformAllRomsToPhi2_();

        // Processor port: portData=0x35 maps 0xE000-0xFFFF to RAM so the
        // reset vector fetch at 0xFFFC reads the value the bootstrap wrote to
        // RAM. The bootstrap restores portData=0x37 on its first instructions,
        // switching KERNAL ROM back to visible for the init routine proper.
        phi2Machine_.port().ddr  = 0x2Fu;
        phi2Machine_.port().data = 0x35u;
        phi2Machine_.memory().pokeRam(0x0000u, 0x2Fu);
        phi2Machine_.memory().pokeRam(0x0001u, 0x35u);

        // Attach the SID bridge so init writes land in sink_.regs[].
        phi2SidBridge_.attach(&sink_, nullptr);
        phi2Machine_.attachSidSink(&phi2SidBridge_);

        // A terminal BRK is a compatibility completion sentinel only. Strict
        // RSID mode executes hardware BRK vectoring and must never convert BRK
        // into JAM merely to make init appear complete. PSID retains its legacy
        // sentinel behavior because its contract is explicitly compatible.
        const bool allowInitBrkSentinel =
            !loaded_.header.rsid || rsidPlaybackMode_ == RsidPlaybackMode::Compatible;
        phi2Machine_.setTrapBrkAsJam(allowInitBrkSentinel);

        // Begin reset sequence — the PHI2 machine fetches its reset vector and
        // starts executing the RSID bootstrap.
        phi2Machine_.resetToVector();

        const bool rsid = loaded_.header.rsid;
        const uint16_t idle = rsid
            ? platform_.bootState().rsidIdleAddress
            : platform_.bootState().psidCiaIdleLoopAddress;
        const uint64_t unsupportedAtStart = phi2Machine_.cpu().unsupportedOpcodeTotal();

        // The CLI instruction immediately before the idle JMP. When RTS returns
        // from the init routine, the next PC is the CLI (=idle-1 byte).
        const uint16_t cliBeforeIdle = (idle != 0u) ? static_cast<uint16_t>(idle - 1u) : 0u;
        uint64_t retiredSeen = phi2Machine_.cpu().retiredInstructionCount();

        for (uint32_t c = 0u; c < maxCycles; ++c) {
            phi2Machine_.tickPhi2();
            const uint64_t retiredNow = phi2Machine_.cpu().retiredInstructionCount();
            while (retiredSeen < retiredNow) {
                platform_.markSidInitInstruction();
                ++retiredSeen;
            }

            if (phi2Machine_.cpu().unsupportedOpcodeTotal() != unsupportedAtStart) {
                phi2Machine_.setTrapBrkAsJam(false);
                return false; // unsupported opcode; fail closed
            }

            const auto& s = phi2Machine_.cpu().state();

            if (!s.active && !s.resetSequence && !s.irqSequence &&
                !s.nmiSequence && !s.brkSequence && s.pc == kC64HaltAddr) {
                phi2Machine_.setTrapBrkAsJam(false);
                return true;
            }

            // BRK-as-halt is a compatibility-only init sentinel. In Strict mode
            // a jam means the CPU hit a true unsupported/KIL state and init fails.
            if (s.jammed) {
                phi2Machine_.setTrapBrkAsJam(false);
                const uint16_t prevPc = static_cast<uint16_t>(s.pc - 1u);
                const bool terminalBrkSentinel = (phi2Machine_.memory().peekRam(prevPc) == 0x00u && sink_.writeCount > 0u);
                if (allowInitBrkSentinel && terminalBrkSentinel) {
                    // Some PSID/RSID fixtures and older tunes use BRK as an init
                    // completion sentinel instead of RTS.  Treat only a post-write BRK as a
                    // compatibility completion marker; KIL/JAM opcodes remain a
                    // hard init failure and cannot masquerade as a clean boot.
                    if (idle != 0u) {
                        phi2Machine_.cpu().state().pc = idle;
                        phi2Machine_.cpu().state().jammed = false;
                    }
                    ++initBrkSentinelCount_;
                    return true;
                }
                return false;
            }

            // Accept completion when the CPU is between instructions at either
            // the idle address or the CLI instruction just before it.
            if (!s.active && !s.resetSequence && !s.irqSequence &&
                !s.nmiSequence && !s.brkSequence) {
                const bool atIdle = (idle != 0u && s.pc == idle);
                // Reaching CLI immediately after the bootstrap JSR proves the
                // init routine returned via RTS. This is normal completion in
                // every mode; unlike BRK-as-JAM it is not a compatibility path.
                const bool atReturnedCli = (idle != 0u && s.pc == cliBeforeIdle);
                if (atIdle || atReturnedCli) {
                    phi2Machine_.setTrapBrkAsJam(false);
                    return true;
                }
            }
        }
        phi2Machine_.setTrapBrkAsJam(false);
        return false; // budget exhausted
    }

    void syncPsidCiaBootstrapIntoPhi2_() noexcept {
        // installPsidCiaPlaybackBootstrap() mutates only the IRQ vector,
        // trampoline, and CIA setup after init. Copy exactly those deltas;
        // never mirror all legacy RAM here because PHI2 init may have produced
        // tune state that the compatibility platform did not execute.
        for (uint16_t a = 0x0314u; a <= 0x0319u; ++a)
            phi2Machine_.memory().pokeRam(a, platform_.peekMemory(a));
        const uint16_t entry = platform_.bootState().playBootstrapAddress;
        if (entry != 0u) {
            // v893: the entry may hold either the 10-byte VBI play bootstrap or
            // the 16-byte CIA IRQ bootstrap; the previous hardcoded 12 truncated
            // the CIA bootstrap's PLA/TAX/PLA/RTI epilogue. Copy the maximum of
            // the two authoritative code lengths (the bootstrap page is reserved,
            // so the extra bytes cannot alias tune state).
            constexpr uint16_t kBootstrapCopyLen =
                (C64Platform::kPsidCiaBootstrapCodeLength > C64Platform::kPsidPlayBootstrapCodeLength)
                    ? C64Platform::kPsidCiaBootstrapCodeLength
                    : C64Platform::kPsidPlayBootstrapCodeLength;
            for (uint16_t i = 0u; i < kBootstrapCopyLen; ++i) {
                const uint16_t a = static_cast<uint16_t>(entry + i);
                phi2Machine_.memory().pokeRam(a, platform_.peekMemory(a));
            }
        }
        phi2Machine_.cia1() = platform_.cia1();
        phi2Machine_.cia2() = platform_.cia2();
        phi2Machine_.cpu().state().irqLine =
            phi2Machine_.cia1().irq() || phi2Machine_.vic().irq();
    }


    bool psidCiaPhi2PcInIdleLoop_(uint16_t pc) const noexcept {
        const uint16_t idle = platform_.bootState().psidCiaIdleLoopAddress;
        return idle != 0u && pc >= idle && pc <= static_cast<uint16_t>(idle + 3u);
    }

    void capturePsidCiaPhi2Edges_(C64PsidCiaRuntimeDriveSnapshot& out, uint64_t startPhi2) noexcept {
        const uint64_t now = phi2Machine_.phi2Cycle();
        const uint64_t elapsed = now >= startPhi2 ? now - startPhi2 : 0u;
        if (!out.irqObserved && ((phi2Machine_.cia1().irqFlags() & Cia6526::IcrTimerA) != 0u ||
                                 phi2Machine_.cia1().irq())) {
            out.irqObserved = true;
            out.ticksToIrq = elapsed;
        }
        if (!out.cpuIrqLineObserved && phi2Machine_.cpu().state().irqLine) {
            out.cpuIrqLineObserved = true;
            out.ticksToIrq = out.ticksToIrq ? out.ticksToIrq : elapsed;
        }
        const uint16_t pc = phi2Machine_.cpu().pc();
        // Side-effect-free read: this runs twice per PHI2 tick inside the service
        // loop, so it must NOT use the mutating validator (which would inflate
        // interruptValidationFailureCount_ by once per tick during any window the
        // vector is transiently invalid). The CPU IRQ vector ($FFFE, KERNAL-mapped)
        // is a stable ROM constant for the common PSID-CIA case, so the platform
        // mirror is authoritative for it here.
        const uint16_t vector = platform_.peekInstalledInterruptBootstrap().cpuIrqVector;
        if (!out.vectorEntered && (pc == vector || pc == out.irqTrampolineAddress)) {
            out.vectorEntered = true;
            out.ticksToVector = elapsed;
        }
        if (!out.ciaAckObserved && out.irqObserved &&
            !phi2Machine_.cia1().irq() &&
            ((phi2Machine_.cia1().irqFlags() & Cia6526::IcrTimerA) == 0u)) {
            out.ciaAckObserved = true;
        }
        if (!out.sidWriteObserved &&
            sink_.writeCount > 0u &&
            sink_.lastCycle >= startPhi2) {
            out.sidWriteObserved = true;
            out.ticksToSidWrite = sink_.lastCycle - startPhi2;
            out.sidWriteAddress = static_cast<uint16_t>(
                0xD400u + static_cast<uint16_t>(sink_.lastChip) * 0x20u + sink_.lastReg);
            out.sidWriteValue = sink_.lastValue;
        }
        if (!out.playAddressEntered && platform_.bootState().playAddress != 0u &&
            pc == platform_.bootState().playAddress) {
            out.playAddressEntered = true;
            out.ticksToPlay = elapsed;
            out.serviceGeneration = ++psidCiaPhi2ServiceCount_;
        }
        if (out.playAddressEntered && out.ciaAckObserved && !out.returnedToIdleAfterPlay &&
            psidCiaPhi2PcInIdleLoop_(pc)) {
            out.returnedToIdleAfterPlay = true;
            out.ticksToIdleAfterPlay = elapsed;
        }
    }

    C64PsidCiaRuntimeDriveSnapshot runPsidCiaPhi2PlaybackServiceTicks_(
            uint64_t maxTicks,
            SidRegisterSink* externalTimedSink = nullptr) noexcept {
        C64PsidCiaRuntimeDriveSnapshot out{};
        out.installed = platform_.bootState().sidPlayBootstrapInstalled &&
                        platform_.bootState().sidUsesCiaTiming &&
                        platform_.bootState().playAddress != 0u;
        out.runGeneration = platform_.bootState().psidCiaRunGeneration + 1u;
        out.idleLoopAddress = platform_.bootState().psidCiaIdleLoopAddress;
        out.irqTrampolineAddress = platform_.bootState().playBootstrapAddress;
        out.playAddress = platform_.bootState().playAddress;
        out.ciaTimerALatch = phi2Machine_.cia1().latchA();
        if (!out.installed || !phi2MachineReady_) return out;

        // v855: CIA-service play writes now reach the caller's audio timed-write
        // bridge with exact PHI2 cycle stamps (was nullptr → block-quantized).
        phi2SidBridge_.attach(&sink_, externalTimedSink);
        phi2Machine_.attachSidSink(&phi2SidBridge_);
        const uint64_t start = phi2Machine_.phi2Cycle();
        const uint64_t deadline = start + maxTicks;

        while (phi2Machine_.phi2Cycle() < deadline && !phi2Machine_.cpu().state().jammed) {
            capturePsidCiaPhi2Edges_(out, start);
            if (out.returnedToIdleAfterPlay) break;
            phi2Machine_.tickPhi2();
            capturePsidCiaPhi2Edges_(out, start);
        }

        // The PHI2 machine is authoritative for PSID-CIA service, while the
        // platform remains the public inspection/bootstrap surface. Mirror final
        // RAM values for every PHI2 write so callers see the same machine state.
        syncPlatformRamFromPhi2Writes_(start);
        syncPlatformInspectionFromPhi2_();

        out.ticksExecuted = phi2Machine_.phi2Cycle() >= start ? (phi2Machine_.phi2Cycle() - start) : 0u;
        out.finalPc = phi2Machine_.cpu().pc();
        out.cpuJammed = phi2Machine_.cpu().state().jammed;
        out.cpuJamOpcode = phi2Machine_.cpu().lastJamOpcode();
        out.vectorStillInstalled = platform_.psidCiaPlaybackBootstrapSnapshot().vectorStillInstalled;
        out.trampolineShapeCanonical = platform_.psidCiaPlaybackBootstrapSnapshot().trampolineShapeCanonical;
        out.ciaLatchCorrect = phi2Machine_.cia1().latchA() == platform_.psidCiaPlaybackBootstrapSnapshot().ciaTimerALatch;
        out.serviceComplete = out.playAddressEntered && out.ciaAckObserved &&
                              out.returnedToIdleAfterPlay && !out.cpuJammed;
        out.systemIntegrityClean = out.installed && out.vectorStillInstalled &&
                                   out.trampolineShapeCanonical && out.ciaLatchCorrect &&
                                   out.serviceComplete;
        syncPlatformSidMirrorFromSink_();
        platform_.publishPsidCiaRuntimeDriveSnapshot(out);
        return out;
    }

    void syncPlatformRamFromPhi2Writes_(uint64_t phi2Start) noexcept {
        MemoryMatrix& memory = phi2Machine_.memory();
        if (memory.dirtyWriteLogWrapped()) {
            if (renderTxActive_ && platform_.renderMutationJournalActive()) {
                renderTxDeferredFullRamSync_ = true;
                return;
            }
            for (uint32_t a = 0u; a <= 0xFFFFu; ++a) {
                const uint16_t addr = static_cast<uint16_t>(a);
                if (addr >= 0xD800u && addr <= 0xDBFFu) {
                    platform_.pokeColorRam(addr, memory.peekColorRam(addr));
                } else {
                    platform_.pokeMemory(addr, memory.peekRam(addr));
                }
            }
            memory.clearDirtyWriteLog();
            return;
        }
        const size_t n = memory.dirtyWriteLogSize();
        for (size_t i = 0; i < n; ++i) {
            const C64DirtyWrite write = memory.dirtyWriteAt(i);
            if (write.phi2 < phi2Start) continue;
            if (write.address >= 0xD800u && write.address <= 0xDBFFu) {
                // Color RAM ($D800-$DBFF) is stored in the PHI2 colorRam_, not
                // ram_; peekRam() would mirror a stale byte. Route it to the
                // platform's visible color RAM so colorRam()/telemetry stay correct.
                platform_.pokeColorRam(write.address, memory.peekColorRam(write.address));
            } else {
                platform_.pokeMemory(write.address, memory.peekRam(write.address));
            }
        }
        memory.clearDirtyWriteLog();
    }

    void syncPlatformSidMirrorFromSink_() noexcept {
        platform_.seedSidBusMirrorFrom(sink_.regsByChip,
                                       sink_.lastChip,
                                       sink_.lastReg,
                                       sink_.lastValue,
                                       sink_.lastCycle);
    }

    void syncPlatformInspectionFromPhi2_() noexcept {
        const Cpu6510MicroState& ms = phi2Machine_.cpu().state();
        platform_.publishPhi2InspectionState(
            ms.pc, ms.a, ms.x, ms.y, ms.sp, ms.p, ms.jammed,
            phi2Machine_.cia1().irq() || phi2Machine_.vic().irq(),
            phi2Machine_.cia2().irq(),
            phi2Machine_.port().ddr,
            phi2Machine_.port().data,
            phi2Machine_.phi2Cycle(),
            phi2Machine_.cia1(),
            phi2Machine_.cia2(),
            phi2Machine_.vic());
    }

    void syncPhi2MachineFromPlatform_() noexcept {
        resetPhi2Machine_(platform_.clockHz() == kPalPhi2Hz);
        // Align the PHI2 machine counter to the legacy platform so that both
        // clocks share the same origin. Without this, the PlayBase cycle
        // selection in the render path would compute deltaCycles relative to
        // the wrong epoch, collapsing all SID writes to sample 0 or N-1.
        phi2Machine_.setPhi2Cycle(platform_.phi2Cycle());
        mirrorPlatformRamToPhi2_();
        // Mirror all ROM regions (KERNAL, BASIC, CHAR) and color RAM so the
        // PHI2 machine has a fully coherent memory image matching the legacy
        // platform's post-init state.
        mirrorPlatformAllRomsToPhi2_();
        // Keep CIA/VIC peripheral state coherent when a PSID init ran through
        // the legacy bootstrap but subsequent PSID-CIA playback is executed by
        // the PHI2 machine.  Without this, the C64Phi2Machine would have RAM and
        // CPU state from init, but freshly reset CIA timers/vectors, so CIA-driven
        // PSID tunes would never raise the intended Timer A IRQ.
        phi2Machine_.cia1() = platform_.cia1();
        phi2Machine_.cia2() = platform_.cia2();
        phi2Machine_.vic() = platform_.vic();
        const Mos6510State& legacy = platform_.cpu().state();
        Cpu6510MicroState& micro = phi2Machine_.cpu().state();
        micro.pc = legacy.pc;
        micro.a = legacy.a;
        micro.x = legacy.x;
        micro.y = legacy.y;
        micro.sp = legacy.sp;
        micro.p = static_cast<uint8_t>(legacy.p | kFlagUnused);
        micro.active = false;
        micro.jammed = legacy.jammed;
        micro.resetSequence = false;
        micro.irqSequence = false;
        micro.nmiSequence = false;
        micro.brkSequence = false;
        micro.t = 0;
        phi2Machine_.port().ddr = legacy.portDirection;
        phi2Machine_.port().data = legacy.portData;
        phi2Machine_.memory().pokeRam(0x0000u, legacy.portDirection);
        phi2Machine_.memory().pokeRam(0x0001u, legacy.portData);
        phi2Machine_.setBasicStartupState(loaded_.header.c64BasicFlag, false);
        phi2Machine_.setPsidSyntheticCiaIrqReports(psidSyntheticCiaIrqReportCount());
        phi2MachineReady_ = loaded_.header.valid && platform_.bootState().sidInitCompleted;
    }

    bool runtimeHeaderUsesCiaTimingForSong_(uint16_t song) const noexcept {
        if (!loaded_.header.valid) return false;
        if (loaded_.header.rsid) return true;
        const uint16_t oneBased = song ? song : (loaded_.header.startSong ? loaded_.header.startSong : 1u);
        const uint16_t bit = oneBased > 32u ? 31u : static_cast<uint16_t>(oneBased - 1u);
        return ((loaded_.header.normalizedSpeed >> bit) & 1u) != 0u;
    }

    static PsidLoadFailure loadFailureForParseResult_(ArpSID::PsidParseResult r) noexcept {
        switch (r) {
            case ArpSID::PsidParseResult::OK: return PsidLoadFailure::None;
            case ArpSID::PsidParseResult::TooShort: return PsidLoadFailure::ParseTooShort;
            case ArpSID::PsidParseResult::BadMagic: return PsidLoadFailure::BadMagic;
            case ArpSID::PsidParseResult::BadVersion: return PsidLoadFailure::BadVersion;
            case ArpSID::PsidParseResult::BadOffset: return PsidLoadFailure::BadOffset;
            case ArpSID::PsidParseResult::BadSongCount:
            case ArpSID::PsidParseResult::BadStartSong: return PsidLoadFailure::BadSongMetadata;
            case ArpSID::PsidParseResult::BadRsidHeader: return PsidLoadFailure::BadRsidHeader;
            case ArpSID::PsidParseResult::BadSidAddress: return PsidLoadFailure::BadSidAddress;
            case ArpSID::PsidParseResult::DuplicateSidBase: return PsidLoadFailure::DuplicateSidBase;
            case ArpSID::PsidParseResult::UnsupportedMusSpecific: return PsidLoadFailure::UnsupportedMusSpecific;
            case ArpSID::PsidParseResult::UnsupportedRsidBasic: return PsidLoadFailure::UnsupportedRsidBasic;
            case ArpSID::PsidParseResult::BadRelocationRange: return PsidLoadFailure::BadRelocationRange;
            case ArpSID::PsidParseResult::UnsupportedMultiSid: return PsidLoadFailure::ConfigureSidBasesFailed;
        }
        return PsidLoadFailure::ParseFailed;
    }

    bool runPsidVbiPlayViaPhi2_(uint16_t boot, uint32_t maxInstructions,
                                SidRegisterSink* externalTimedSink = nullptr) noexcept {
        if (!phi2MachineReady_ || boot == 0u || maxInstructions == 0u) return false;
        // The platform helper owns only bootstrap layout/telemetry. Copy the full
        // installed bootstrap into authoritative PHI2 RAM, then dispatch without
        // resetting registers, stack, CIA, VIC, processor port, or cycle phase.
        // v893: the copy length is the platform's authoritative code length —
        // a hardcoded 6 here truncated the v891 10-byte bootstrap (banking
        // prefix + JSR play + JMP halt) at the JSR operand, jamming every
        // direct runPlay() dispatch into garbage at $00xx.
        for (uint16_t i = 0u; i < C64Platform::kPsidPlayBootstrapCodeLength; ++i) {
            const uint16_t a = static_cast<uint16_t>(boot + i);
            phi2Machine_.memory().pokeRam(a, platform_.peekMemory(a));
        }
        // v855: VBI play writes now reach the caller's audio timed-write bridge
        // with exact PHI2 cycle stamps (was nullptr → block-quantized).
        phi2SidBridge_.attach(&sink_, externalTimedSink);
        phi2Machine_.attachSidSink(&phi2SidBridge_);
        phi2Machine_.setTrapBrkAsJam(true);
        phi2Machine_.setProgramCounter(boot);
        phi2Machine_.cpu().state().jammed = false;

        const uint64_t startPhi2 = phi2Machine_.phi2Cycle();
        const uint64_t deadline = startPhi2 + static_cast<uint64_t>(maxInstructions) * 12u;
        const uint64_t retiredStart = phi2Machine_.cpu().retiredInstructionCount();
        const uint64_t unsupportedStart = phi2Machine_.cpu().unsupportedOpcodeTotal();
        uint64_t retiredSeen = retiredStart;
        // v856 missing-logic fix (RTI-exit tunes): the bootstrap is JSR play /
        // JMP halt, which only an RTS-exiting play unwinds cleanly. A play
        // routine that exits with RTI (a common PSID class written for IRQ-entry
        // environments) pops OUR 2-byte JSR frame plus one deeper byte as a
        // 3-byte IRQ frame: SP lands at exactly entrySp+1 and PC is garbage —
        // previously a guaranteed jam/budget-out, rolled back by the kernel on
        // EVERY frame (audibly choppy/silent for that whole tune class). Detect
        // the exact signature (RTI retired with SP == entrySp+1, wrap-safe 8-bit
        // compare) and accept the frame as complete. Tune-internal nested RTIs
        // unwind their own frames (SP below entrySp) and never match.
        const uint8_t entrySp = phi2Machine_.cpu().state().sp;
        const uint8_t rtiExitSp = static_cast<uint8_t>(entrySp + 1u);

        while (phi2Machine_.phi2Cycle() < deadline) {
            const auto& before = phi2Machine_.cpu().state();
            if (!before.active && !before.resetSequence && !before.irqSequence &&
                !before.nmiSequence && !before.brkSequence &&
                before.pc == kC64HaltAddr) {
                phi2Machine_.setTrapBrkAsJam(false);
                syncPlatformRamFromPhi2Writes_(startPhi2);
                syncPlatformInspectionFromPhi2_();
                syncPlatformSidMirrorFromSink_();
                return true;
            }
            phi2Machine_.tickPhi2();
            const uint64_t retiredNow = phi2Machine_.cpu().retiredInstructionCount();
            const bool retiredThisTick = retiredSeen < retiredNow;
            while (retiredSeen < retiredNow) {
                platform_.markSidPlayInstruction();
                ++retiredSeen;
            }
            // v856: accept an RTI-exit frame (see entrySp comment above). Require
            // at least one real play instruction retired so the dispatch JSR
            // itself cannot satisfy the condition.
            if (retiredThisTick &&
                retiredNow > retiredStart + 1u &&
                phi2Machine_.cpu().opcode() == 0x40u &&
                phi2Machine_.cpu().state().sp == rtiExitSp) {
                phi2Machine_.setTrapBrkAsJam(false);
                syncPlatformRamFromPhi2Writes_(startPhi2);
                syncPlatformInspectionFromPhi2_();
                syncPlatformSidMirrorFromSink_();
                return true;
            }
            if (phi2Machine_.cpu().state().jammed &&
                phi2Machine_.cpu().lastJamOpcode() == 0x00u &&
                sink_.lastCycle >= startPhi2) {
                // Compatibility PSID fixtures commonly terminate a play call
                // with BRK instead of RTS. The BRK still executed through PHI2;
                // accept it only after this call produced a SID bus write.
                phi2Machine_.cpu().state().jammed = false;
                phi2Machine_.setTrapBrkAsJam(false);
                syncPlatformRamFromPhi2Writes_(startPhi2);
                syncPlatformInspectionFromPhi2_();
                syncPlatformSidMirrorFromSink_();
                return true;
            }
            if (retiredNow - retiredStart >= maxInstructions ||
                phi2Machine_.cpu().unsupportedOpcodeTotal() != unsupportedStart ||
                phi2Machine_.cpu().state().jammed) {
                break;
            }
        }
        phi2Machine_.setTrapBrkAsJam(false);
        syncPlatformRamFromPhi2Writes_(startPhi2);
        return false;
    }

    C64Platform platform_{};
    C64RuntimeSidSink sink_{};
    C64Phi2Machine phi2Machine_{};
    C64RuntimePhi2SidSinkBridge phi2SidBridge_{};
    C64Phi2Machine::Snapshot renderTxPhi2Snapshot_{};
    C64RuntimeSidSink::Snapshot renderTxSinkSnapshot_{};
    bool renderTxActive_ = false;
    bool renderTxDeferredFullRamSync_ = false;
    uint64_t renderTxStartPhi2_ = 0;
    PsidImage loaded_{};
    bool usePhi2Machine_ = false;
    bool phi2MachineReady_ = false;
    // Set when runRsidInitViaPhi2_() succeeds so enablePhi2Machine() and
    // syncPhi2MachineFromPlatform_() know not to reset the exact PHI2 state.
    bool phi2InitUsed_ = false;
    // Retained ABI counter for retired legacy init fallback. Production runtime
    // fails closed instead of incrementing it.
    uint32_t rsidLegacyInitFallbackCount_ = 0;
    ArpSID::PsidParseResult lastParseResult_ = ArpSID::PsidParseResult::OK;
    PsidLoadFailure lastLoadFailure_ = PsidLoadFailure::None;
    uint32_t initBrkSentinelCount_ = 0;
    // Fix #1: cumulative timed-write overflow since the last load. Any non-zero
    // value downgrades exactness because SID writes were dropped.
    uint32_t timedWriteOverflowSinceLoad_ = 0;
    // Cumulative multi-SID writes that the render path could not map to an active/rendered chip.
    uint32_t droppedMultiSidWritesSinceLoad_ = 0;
    uint32_t legacyRuntimePlaybackCount_ = 0;
    // Retained ABI counters for the retired PSID-CIA compatibility service.
    // Production runtime keeps them at zero and uses psidCiaPhi2ServiceCount_.
    uint32_t psidCiaCompatibilityServiceObservedCount_ = 0;
    uint32_t psidCiaRunPlayCompatibilityCount_ = 0;
    uint32_t psidCiaExplicitServiceCompatibilityCount_ = 0;
    uint32_t psidCiaPhi2ServiceCount_ = 0;
    uint32_t strictRsidNotPhi2Count_ = 0;
    // Fix #2: playback mode for this RSID instance (Strict or Compatible).
    RsidPlaybackMode rsidPlaybackMode_ = RsidPlaybackMode::Strict;
    // Boot tracer state: runPlay() is called once per ~50/60 Hz video frame, so
    // the tracer logs the very first PLAY service of each loaded tune (the boot
    // milestone) plus any later refusal, instead of one stderr line per frame.
    bool bootTraceFirstPlayLogged_ = false;
};

} // namespace ArpSID::C64
