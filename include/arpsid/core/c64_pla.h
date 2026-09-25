#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

// Deterministic C64 PLA / memory-visibility model.
//
// This is not a transistor PLA; it is the bus-visible memory selection law needed
// by SID/PSID replay so code sees RAM, ROM and IO at the same addresses a real
// C64 would expose through the 6510 processor port ($0000/$0001). RAM always
// exists underneath ROM/IO and writes always update underlying RAM unless they are
// decoded as device writes. This keeps PSID/RSID loaders honest while avoiding a
// second SID authority.
struct C64PlaLines {
    bool loram = true;
    bool hiram = true;
    bool charen = true;
};

enum class C64VisibleDevice : uint8_t {
    Ram = 0,
    BasicRom = 1,
    KernalRom = 2,
    CharacterRom = 3,
    Io = 4,
    CartridgeLo = 5,
    CartridgeHi = 6,
};

// Trust level for externally supplied ROM images.  Size-only ROMs are useful
// for deterministic replay, but they are not sufficient for a physical-exact
// C64 claim.  KnownStock is now derived from a known CRC32 identity table;
// callers cannot promote arbitrary bytes to stock by passing a trust flag.
enum class C64RomTrust : uint8_t {
    Missing = 0,
    SizeOnlyUnverified = 1,
    KnownStock = 2,
    KnownPatchedNonStock = 3,
};

enum class C64RomSlot : uint8_t {
    Basic = 0,
    Kernal = 1,
    Character = 2,
};

enum class C64RomIdentity : uint8_t {
    Missing = 0,
    UnknownSizeOnly = 1,
    Basic90122601 = 2,
    Kernal90122701 = 3,
    Kernal90122702 = 4,
    Kernal90122703 = 5,
    KernalJapanese90614502 = 6,
    KernalSwedish32501702 = 7,
    KernalSwedishVip64 = 8,
    KernalSx6425110404 = 9,
    Character90122501 = 10,
    CharacterJapanese = 11,
    CharacterSwedish32501802 = 12,
    KnownPatchedNonStock = 13,
};

inline bool c64RomIdentityIsKnownStock(C64RomIdentity id) noexcept {
    switch (id) {
        case C64RomIdentity::Basic90122601:
        case C64RomIdentity::Kernal90122701:
        case C64RomIdentity::Kernal90122702:
        case C64RomIdentity::Kernal90122703:
        case C64RomIdentity::KernalJapanese90614502:
        case C64RomIdentity::KernalSwedish32501702:
        case C64RomIdentity::KernalSwedishVip64:
        case C64RomIdentity::KernalSx6425110404:
        case C64RomIdentity::Character90122501:
        case C64RomIdentity::CharacterJapanese:
        case C64RomIdentity::CharacterSwedish32501802:
            return true;
        default:
            return false;
    }
}

inline bool c64RomTrustIsPhysicalStock(C64RomTrust t) noexcept {
    return t == C64RomTrust::KnownStock;
}

inline const char* c64RomIdentityName(C64RomIdentity id) noexcept {
    switch (id) {
        case C64RomIdentity::Missing: return "missing";
        case C64RomIdentity::UnknownSizeOnly: return "size-only-unverified";
        case C64RomIdentity::Basic90122601: return "C64 BASIC 901226-01";
        case C64RomIdentity::Kernal90122701: return "C64 KERNAL 901227-01";
        case C64RomIdentity::Kernal90122702: return "C64 KERNAL 901227-02";
        case C64RomIdentity::Kernal90122703: return "C64 KERNAL 901227-03";
        case C64RomIdentity::KernalJapanese90614502: return "C64 KERNAL JP 906145-02";
        case C64RomIdentity::KernalSwedish32501702: return "C64 KERNAL SE 325017-02";
        case C64RomIdentity::KernalSwedishVip64: return "C64 KERNAL SE/VIP64";
        case C64RomIdentity::KernalSx6425110404: return "SX-64 KERNAL 251104-04";
        case C64RomIdentity::Character90122501: return "C64 CHARACTER 901225-01";
        case C64RomIdentity::CharacterJapanese: return "C64 CHARACTER JP";
        case C64RomIdentity::CharacterSwedish32501802: return "C64 CHARACTER SE 325018-02";
        case C64RomIdentity::KnownPatchedNonStock: return "known-patched-non-stock";
    }
    return "unknown";
}

inline uint32_t c64Crc32(const uint8_t* data, size_t size) noexcept {
    if (!data && size != 0u) return 0u;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

inline C64RomIdentity c64IdentifyRomByCrc(C64RomSlot slot, size_t size, uint32_t crc32) noexcept {
    switch (slot) {
        case C64RomSlot::Basic:
            if (size == 0x2000u && crc32 == 0xF833D117u) return C64RomIdentity::Basic90122601;
            break;
        case C64RomSlot::Kernal:
            if (size == 0x2000u) {
                if (crc32 == 0xDCE782FAu) return C64RomIdentity::Kernal90122701;
                if (crc32 == 0xA5C687B3u) return C64RomIdentity::Kernal90122702;
                if (crc32 == 0xDBE3E7C7u) return C64RomIdentity::Kernal90122703;
                if (crc32 == 0x3A9EF6F1u) return C64RomIdentity::KernalJapanese90614502;
                if (crc32 == 0x8F294C51u) return C64RomIdentity::KernalSwedish32501702;
                if (crc32 == 0xF10C2C25u) return C64RomIdentity::KernalSwedishVip64;
                if (crc32 == 0x2C5965D4u) return C64RomIdentity::KernalSx6425110404;
            }
            break;
        case C64RomSlot::Character:
            if (size == 0x1000u) {
                if (crc32 == 0xEC4272EEu) return C64RomIdentity::Character90122501;
                if (crc32 == 0x1604F6C1u) return C64RomIdentity::CharacterJapanese;
                if (crc32 == 0xBEE9B3FDu) return C64RomIdentity::CharacterSwedish32501802;
            }
            break;
    }
    return size == 0u ? C64RomIdentity::Missing : C64RomIdentity::UnknownSizeOnly;
}

inline C64RomIdentity c64IdentifyRom(C64RomSlot slot, const uint8_t* data, size_t size) noexcept {
    if (!data || size == 0u) return C64RomIdentity::Missing;
    return c64IdentifyRomByCrc(slot, size, c64Crc32(data, size));
}

inline C64PlaLines c64DecodeProcessorPort(uint8_t portData) noexcept {
    return C64PlaLines{
        static_cast<bool>(portData & 0x01u),
        static_cast<bool>(portData & 0x02u),
        static_cast<bool>(portData & 0x04u)
    };
}

inline bool c64PlaRomIoRegionEnabled(const C64PlaLines& lines) noexcept {
    // C64 ROM/IO decode is active when either LORAM or HIRAM is asserted. With
    // both low, $A000-$BFFF/$D000-$FFFF expose RAM regardless of CHAREN.
    return lines.loram || lines.hiram;
}

inline C64VisibleDevice c64PlaVisibleDevice(uint16_t address, uint8_t portData) noexcept {
    const C64PlaLines lines = c64DecodeProcessorPort(portData);
    const bool romIo = c64PlaRomIoRegionEnabled(lines);

    if (address >= 0xA000u && address <= 0xBFFFu) {
        if (lines.loram && lines.hiram) return C64VisibleDevice::BasicRom;
        return C64VisibleDevice::Ram;
    }

    if (address >= 0xD000u && address <= 0xDFFFu) {
        if (!romIo) return C64VisibleDevice::Ram;
        return lines.charen ? C64VisibleDevice::Io : C64VisibleDevice::CharacterRom;
    }

    if (address >= 0xE000u) {
        if (lines.hiram) return C64VisibleDevice::KernalRom;
        return C64VisibleDevice::Ram;
    }

    return C64VisibleDevice::Ram;
}


inline C64VisibleDevice c64PlaVisibleDeviceWithCartridge(uint16_t address,
                                                         uint8_t portData,
                                                         bool exromHigh,
                                                         bool gameHigh) noexcept {
    // Deterministic PLA/cart surface. Lines are active-low on real C64.
    // This captures normal 8K/16K and Ultimax visibility for PSID/test replay.
    const bool exrom = !exromHigh;
    const bool game = !gameHigh;
    if (exrom && game) { // 16K cartridge
        if (address >= 0x8000u && address <= 0x9FFFu) return C64VisibleDevice::CartridgeLo;
        if (address >= 0xA000u && address <= 0xBFFFu) return C64VisibleDevice::CartridgeHi;
    } else if (exrom && !game) { // 8K cartridge
        if (address >= 0x8000u && address <= 0x9FFFu) return C64VisibleDevice::CartridgeLo;
    } else if (!exrom && game) { // Ultimax
        if (address >= 0x8000u && address <= 0x9FFFu) return C64VisibleDevice::CartridgeLo;
        if (address >= 0xE000u) return C64VisibleDevice::CartridgeHi;
        if (address >= 0xA000u && address <= 0xBFFFu) return C64VisibleDevice::Ram;
    }
    return c64PlaVisibleDevice(address, portData);
}

class C64RomSet {
public:
    static constexpr size_t kBasicSize = 0x2000u;
    static constexpr size_t kKernalSize = 0x2000u;
    static constexpr size_t kCharacterSize = 0x1000u;

    // audit P0-8: ROM helpers below index with `address - base` masked to the array
    // size. Masking requires power-of-two sizes; assert that here so the masks below
    // stay range-safe if a size ever changes.
    static_assert((kBasicSize & (kBasicSize - 1u)) == 0u, "kBasicSize must be power of two");
    static_assert((kKernalSize & (kKernalSize - 1u)) == 0u, "kKernalSize must be power of two");
    static_assert((kCharacterSize & (kCharacterSize - 1u)) == 0u, "kCharacterSize must be power of two");
    static constexpr uint16_t kBasicBase = 0xA000u;
    static constexpr uint16_t kKernalBase = 0xE000u;
    static constexpr uint16_t kCharacterBase = 0xD000u;

    void resetDeterministic() noexcept {
        basic_.fill(0xEAu);     // benign NOP-like deterministic fill
        kernal_.fill(0xEAu);
        character_.fill(0x00u);
        basicExternal_ = false;
        kernalExternal_ = false;
        characterExternal_ = false;
        basicTrust_ = C64RomTrust::Missing;
        kernalTrust_ = C64RomTrust::Missing;
        characterTrust_ = C64RomTrust::Missing;
        basicIdentity_ = C64RomIdentity::Missing;
        kernalIdentity_ = C64RomIdentity::Missing;
        characterIdentity_ = C64RomIdentity::Missing;
        refreshDeterministicVectors_();
    }

    void resetDeterministicIfNoExternalRoms() noexcept {
        if (!hasAnyExternalRom()) resetDeterministic();
    }

    // audit P0-8: range-safe ROM indexing. The previous `address - 0xA000u` index
    // underflowed to a huge value (and tripped -Warray-bounds / UB) for any address
    // below the device base. Mask the offset to the (power-of-two) array size so the
    // index is provably in [0, size) for every uint16_t address. Callers only invoke
    // these for in-range device addresses; the mask is a hard safety net.
    uint8_t readBasic(uint16_t address) const noexcept {
        return basic_[(static_cast<size_t>(address) - kBasicBase) & (kBasicSize - 1u)];
    }
    uint8_t readKernal(uint16_t address) const noexcept {
        return kernal_[(static_cast<size_t>(address) - kKernalBase) & (kKernalSize - 1u)];
    }
    uint8_t readCharacter(uint16_t address) const noexcept {
        return character_[(static_cast<size_t>(address) - kCharacterBase) & (kCharacterSize - 1u)];
    }

    void pokeBasic(uint16_t address, uint8_t value) noexcept {
        basic_[(static_cast<size_t>(address) - kBasicBase) & (kBasicSize - 1u)] = value;
    }
    void pokeKernal(uint16_t address, uint8_t value) noexcept {
        kernal_[(static_cast<size_t>(address) - kKernalBase) & (kKernalSize - 1u)] = value;
    }
    void pokeCharacter(uint16_t address, uint8_t value) noexcept {
        character_[(static_cast<size_t>(address) - kCharacterBase) & (kCharacterSize - 1u)] = value;
    }

    bool loadBasic(const uint8_t* data, size_t size) noexcept {
        return loadBasic(data, size, C64RomTrust::SizeOnlyUnverified);
    }
    bool loadKernal(const uint8_t* data, size_t size) noexcept {
        return loadKernal(data, size, C64RomTrust::SizeOnlyUnverified);
    }
    bool loadCharacter(const uint8_t* data, size_t size) noexcept {
        return loadCharacter(data, size, C64RomTrust::SizeOnlyUnverified);
    }
    bool loadBasic(const uint8_t* data, size_t size, C64RomTrust trust) noexcept {
        if (!data || size != basic_.size()) return false;
        for (size_t i = 0; i < basic_.size(); ++i) basic_[i] = data[i];
        basicExternal_ = true;
        basicIdentity_ = c64IdentifyRom(C64RomSlot::Basic, data, size);
        basicTrust_ = trustForLoadedRom_(basicIdentity_, trust);
        return true;
    }
    bool loadKernal(const uint8_t* data, size_t size, C64RomTrust trust) noexcept {
        if (!data || size != kernal_.size()) return false;
        for (size_t i = 0; i < kernal_.size(); ++i) kernal_[i] = data[i];
        kernalExternal_ = true;
        kernalIdentity_ = c64IdentifyRom(C64RomSlot::Kernal, data, size);
        kernalTrust_ = trustForLoadedRom_(kernalIdentity_, trust);
        return true;
    }
    bool loadCharacter(const uint8_t* data, size_t size, C64RomTrust trust) noexcept {
        if (!data || size != character_.size()) return false;
        for (size_t i = 0; i < character_.size(); ++i) character_[i] = data[i];
        characterExternal_ = true;
        characterIdentity_ = c64IdentifyRom(C64RomSlot::Character, data, size);
        characterTrust_ = trustForLoadedRom_(characterIdentity_, trust);
        return true;
    }
    uint32_t checksumBasic() const noexcept { return checksum_(basic_.data(), basic_.size()); }
    uint32_t checksumKernal() const noexcept { return checksum_(kernal_.data(), kernal_.size()); }
    uint32_t checksumCharacter() const noexcept { return checksum_(character_.data(), character_.size()); }
    uint32_t crc32Basic() const noexcept { return c64Crc32(basic_.data(), basic_.size()); }
    uint32_t crc32Kernal() const noexcept { return c64Crc32(kernal_.data(), kernal_.size()); }
    uint32_t crc32Character() const noexcept { return c64Crc32(character_.data(), character_.size()); }

    bool hasExternalBasic() const noexcept { return basicExternal_; }
    bool hasExternalKernal() const noexcept { return kernalExternal_; }
    bool hasExternalCharacter() const noexcept { return characterExternal_; }
    bool hasCompleteExternalRomSet() const noexcept { return basicExternal_ && kernalExternal_ && characterExternal_; }
    C64RomTrust basicRomTrust() const noexcept { return basicTrust_; }
    C64RomTrust kernalRomTrust() const noexcept { return kernalTrust_; }
    C64RomTrust characterRomTrust() const noexcept { return characterTrust_; }
    C64RomIdentity basicRomIdentity() const noexcept { return basicIdentity_; }
    C64RomIdentity kernalRomIdentity() const noexcept { return kernalIdentity_; }
    C64RomIdentity characterRomIdentity() const noexcept { return characterIdentity_; }
    bool hasVerifiedStockBasicRom() const noexcept { return basicExternal_ && c64RomTrustIsPhysicalStock(basicTrust_); }
    bool hasVerifiedStockKernalRom() const noexcept { return kernalExternal_ && c64RomTrustIsPhysicalStock(kernalTrust_); }
    bool hasVerifiedStockCharacterRom() const noexcept { return characterExternal_ && c64RomTrustIsPhysicalStock(characterTrust_); }
    bool hasVerifiedStockRomSet() const noexcept { return hasVerifiedStockBasicRom() && hasVerifiedStockKernalRom() && hasVerifiedStockCharacterRom(); }
    bool hasUnverifiedCompleteExternalRomSet() const noexcept { return hasCompleteExternalRomSet() && !hasVerifiedStockRomSet(); }
    bool hasAnyExternalRom() const noexcept { return basicExternal_ || kernalExternal_ || characterExternal_; }

    bool canPatchKernalRom() const noexcept { return !kernalExternal_; }

    // Explicit trust promotion/demotion hook for a future verified ROM identity
    // table or host UI.  Missing cannot be assigned to an already-loaded ROM; a
    // caller that wants Missing must reset the ROM set.
    bool setBasicRomTrust(C64RomTrust trust) noexcept {
        if (!basicExternal_) return false;
        const C64RomTrust normalized = trustForLoadedRom_(basicIdentity_, trust);
        if (trust == C64RomTrust::KnownStock && normalized != C64RomTrust::KnownStock) return false;
        basicTrust_ = normalized;
        if (normalized == C64RomTrust::KnownPatchedNonStock) basicIdentity_ = C64RomIdentity::KnownPatchedNonStock;
        return true;
    }
    bool setKernalRomTrust(C64RomTrust trust) noexcept {
        if (!kernalExternal_) return false;
        const C64RomTrust normalized = trustForLoadedRom_(kernalIdentity_, trust);
        if (trust == C64RomTrust::KnownStock && normalized != C64RomTrust::KnownStock) return false;
        kernalTrust_ = normalized;
        if (normalized == C64RomTrust::KnownPatchedNonStock) kernalIdentity_ = C64RomIdentity::KnownPatchedNonStock;
        return true;
    }
    bool setCharacterRomTrust(C64RomTrust trust) noexcept {
        if (!characterExternal_) return false;
        const C64RomTrust normalized = trustForLoadedRom_(characterIdentity_, trust);
        if (trust == C64RomTrust::KnownStock && normalized != C64RomTrust::KnownStock) return false;
        characterTrust_ = normalized;
        if (normalized == C64RomTrust::KnownPatchedNonStock) characterIdentity_ = C64RomIdentity::KnownPatchedNonStock;
        return true;
    }

private:
    static C64RomTrust trustForLoadedRom_(C64RomIdentity identity, C64RomTrust requested) noexcept {
        if (c64RomIdentityIsKnownStock(identity)) return C64RomTrust::KnownStock;
        if (requested == C64RomTrust::KnownPatchedNonStock) return C64RomTrust::KnownPatchedNonStock;
        return C64RomTrust::SizeOnlyUnverified;
    }
    void refreshDeterministicVectors_() noexcept {
        // Deterministic built-in ROM mini-vector surface. External ROMs can
        // replace it; without external ROMs we provide stable reset and IRQ/BRK
        // vector targets so strict RSID/BRK handling reads defined ROM bytes.
        //
        // Minimal PSID-safe HLE KERNAL vector surface. External ROMs replace
        // this; without them we expose the canonical CINV/NMINV indirection and
        // CIA ACK handlers instead of a bare RTI-only stub.
        const size_t reset = 0x0000u; // $E000
        const uint8_t resetStub[] = {0x78u,0xD8u,0xA2u,0xFFu,0x9Au,0xA9u,0x2Fu,0x8Du,0x00u,0x00u,
                                     0xA9u,0x37u,0x8Du,0x01u,0x00u,0x20u,0x8Du,0xFFu,0x58u,0x60u};
        for (size_t i = 0; i < sizeof(resetStub); ++i) kernal_[reset + i] = resetStub[i];
        const size_t irqEntry = 0x1F48u; // $FF48: PHA/TXA/PHA/TYA/PHA/JMP ($0314)
        const uint8_t irq[] = {0x48u,0x8Au,0x48u,0x98u,0x48u,0x6Cu,0x14u,0x03u};
        for (size_t i = 0; i < sizeof(irq); ++i) kernal_[irqEntry + i] = irq[i];
        const size_t nmiEntry = 0x1E43u; // $FE43: JMP ($0318); $FE47: LDA $DD0D; RTI
        kernal_[nmiEntry+0]=0x6Cu; kernal_[nmiEntry+1]=0x18u; kernal_[nmiEntry+2]=0x03u;
        kernal_[0x1E47u]=0xADu; kernal_[0x1E48u]=0x0Du; kernal_[0x1E49u]=0xDDu; kernal_[0x1E4Au]=0x40u;
        const size_t irqAck = 0x0A31u; // $EA31: LDA $DC0D; PLA/TAY; PLA/TAX; PLA; RTI
        const uint8_t ack[] = {0xADu,0x0Du,0xDCu,0x68u,0xA8u,0x68u,0xAAu,0x68u,0x40u};
        for (size_t i = 0; i < sizeof(ack); ++i) kernal_[irqAck + i] = ack[i];
        // Vectors
        kernal_[0x1FFAu] = 0x43u; kernal_[0x1FFBu] = 0xFEu; // $FFFA -> $FE43
        kernal_[0x1FFCu] = 0x00u; kernal_[0x1FFDu] = 0xE0u; // $FFFC -> $E000
        kernal_[0x1FFEu] = 0x48u; kernal_[0x1FFFu] = 0xFFu; // $FFFE -> $FF48
    }
    static uint32_t checksum_(const uint8_t* data, size_t size) noexcept {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < size; ++i) { h ^= data[i]; h *= 16777619u; }
        return h;
    }
    std::array<uint8_t, kBasicSize> basic_{};
    std::array<uint8_t, kKernalSize> kernal_{};
    std::array<uint8_t, kCharacterSize> character_{};
    bool basicExternal_ = false;
    bool kernalExternal_ = false;
    bool characterExternal_ = false;
    C64RomTrust basicTrust_ = C64RomTrust::Missing;
    C64RomTrust kernalTrust_ = C64RomTrust::Missing;
    C64RomTrust characterTrust_ = C64RomTrust::Missing;
    C64RomIdentity basicIdentity_ = C64RomIdentity::Missing;
    C64RomIdentity kernalIdentity_ = C64RomIdentity::Missing;
    C64RomIdentity characterIdentity_ = C64RomIdentity::Missing;
};

} // namespace ArpSID::C64
