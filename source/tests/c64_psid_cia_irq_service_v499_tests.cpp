#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_telemetry.h"
#include "arpsid/core/psid_header.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = static_cast<uint8_t>(v >> 8u);
    b[off + 1u] = static_cast<uint8_t>(v);
}

static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = static_cast<uint8_t>(v >> 24u);
    b[off + 1u] = static_cast<uint8_t>(v >> 16u);
    b[off + 2u] = static_cast<uint8_t>(v >> 8u);
    b[off + 3u] = static_cast<uint8_t>(v);
}

static std::vector<uint8_t> buildCiaTimedPsid() {
    constexpr uint16_t dataOff = 0x7Cu;
    std::vector<uint8_t> b(dataOff, 0);
    b[0] = 'P'; b[1] = 'S'; b[2] = 'I'; b[3] = 'D';
    be16(b, 0x04u, 2u);
    be16(b, 0x06u, dataOff);
    be16(b, 0x08u, 0u);
    be16(b, 0x0Au, 0x0800u);
    be16(b, 0x0Cu, 0x0830u);
    be16(b, 0x0Eu, 1u);
    be16(b, 0x10u, 1u);
    be32(b, 0x12u, 0x00000001u); // song 1 uses CIA timing
    b.push_back(0x00u);
    b.push_back(0x08u);

    const uint8_t init[] = {
        0x8Du, 0x02u, 0xD4u,       // STA $D402 = selected song index
        0xA9u, 0x40u,              // LDA #$40
        0x8Du, 0x00u, 0x20u,       // STA $2000
        0x60u                      // RTS
    };
    b.insert(b.end(), init, init + sizeof(init));
    while (b.size() < dataOff + 2u + 0x30u) b.push_back(0xEAu);

    const uint8_t play[] = {
        0xEEu, 0x00u, 0x20u,       // INC $2000
        0xADu, 0x00u, 0x20u,       // LDA $2000
        0x8Du, 0x00u, 0xD4u,       // STA $D400
        0x60u                      // RTS
    };
    b.insert(b.end(), play, play + sizeof(play));
    return b;
}

class TinyBus final : public ArpSID::C64::Mos6510Bus {
public:
    uint8_t cpuRead(uint16_t address) noexcept override { return mem[address]; }
    void cpuWrite(uint16_t address, uint8_t value) noexcept override { mem[address] = value; }
    std::array<uint8_t, 65536> mem{};
};

int main() {
    using namespace ArpSID::C64;

    static_assert(c64Mos6510AllKilOpcodesHavePromotedMicroSequence(), "KIL/JAM coverage must be compile-time closed");
    static_assert(c64Mos6510AllOfficialOpcodesHavePromotedMicroSequence(), "Official opcode coverage must be compile-time closed");
    static_assert(c64Mos6510NoOfficialOpcodeNeedsSemanticFallback(), "Official opcodes must not need fallback");

    TinyBus bus;
    Mos6510 cpu;
    cpu.reset(0x0200u);
    bus.mem[0x0200u] = 0x02u;
    (void)cpu.tickWithOpcodeHint(bus, 0x02u);
    require(cpu.state().jammed, "KIL/JAM opcode deterministically jams");
    require(cpu.state().semanticFallbackCount == 0u, "KIL/JAM opcode is promoted and not semantic fallback");
    require(cpu.state().officialSemanticFallbackCount == 0u, "KIL/JAM does not pollute official fallback evidence");

    C64Platform p;
    p.reset(true);
    const uint16_t originalBases[] = {0xD400u, 0xD420u};
    require(p.configurePsidSidBases(originalBases, 2), "valid multi-SID base config succeeds");
    const uint16_t duplicateBases[] = {0xD400u, 0xD400u};
    require(!p.configurePsidSidBases(duplicateBases, 2), "duplicate SID base config fails");
    require(p.sidChipCount() == 2 && p.sidBase(1) == 0xD420u, "failed SID base config leaves prior state atomic");

    auto psid = buildCiaTimedPsid();
    ArpSID::PsidHeader core{};
    require(ArpSID::psidParse(psid.data(), static_cast<uint32_t>(psid.size()), core) == ArpSID::PsidParseResult::OK,
            "CIA timed PSID parses");
    require(ArpSID::psidUsesCiaTimingForSong(core, 1), "PSID speed bit marks song 1 as CIA timed");

    C64Runtime rt;
    rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "runtime loads CIA timed PSID");
    require(rt.runInit(1, 512), "CIA timed PSID init reaches deterministic idle loop");
    require(rt.platform().bootState().psidCiaIdleLoopAddress != 0u, "PSID CIA idle-loop address is recorded");

    auto bootstrap = rt.validateInstalledInterruptBootstrap();
    require(bootstrap.valid, "installed interrupt bootstrap validates before IRQ drive");
    require(bootstrap.irqAckCia1, "IRQ trampoline ledger records CIA1 acknowledge policy");
    require(bootstrap.irqVectorRam == bootstrap.irqTrampolineAddress, "RAM IRQ vector points at trampoline");
    // v893 refresh: the v891 CIA bootstrap carries the LDA #iomap / STA $01
    // banking prefix — canonical length is C64Platform::kPsidCiaBootstrapCodeLength.
    require(bootstrap.irqTrampolineLength == C64Platform::kPsidCiaBootstrapCodeLength,
            "IRQ trampoline length is canonical");

    auto drive = rt.runPsidCiaPlaybackIrqTicks(65536u);
    require(drive.irqObserved, "CIA Timer A IRQ is observed");
    require(drive.cpuIrqLineObserved, "CIA IRQ reaches CPU IRQ line");
    require(drive.vectorEntered, "CPU enters IRQ vector/trampoline path");
    require(drive.ciaAckObserved, "IRQ trampoline acknowledges CIA ICR");
    require(drive.playAddressEntered, "IRQ trampoline enters play address through JSR");
    require(drive.trampolineShapeCanonical, "IRQ trampoline byte shape remains canonical");
    require(drive.vectorStillInstalled, "IRQ vector ledger still matches RAM and CPU vectors");
    require(drive.ciaLatchCorrect, "PAL CIA Timer A latch remains canonical");
    require(drive.systemIntegrityClean, "system integrity remains clean after IRQ drive");
    require(drive.ticksToIrq > 0u && drive.ticksToVector >= drive.ticksToIrq, "ticks to IRQ/vector are monotonic");
    require(drive.ticksToPlay >= drive.ticksToVector, "ticks to play follows vector entry");

    require(rt.platform().peekMemory(0x2000u) == 0x41u, "play body executes through CPU IRQ service path");
    require(rt.sidSink().regsByChip[0][0] == 0x41u, "play writes SID register through IRQ service path");

    auto integrity = rt.validateC64SystemIntegrity();
    require(!c64IntegrityFailClosed(integrity), "integrity fail-closed gate stays open for validated runtime");
    require(integrity.allKilOpcodesPromoted, "integrity reports KIL/JAM promotion");
    require(integrity.noOfficialSemanticFallbackObserved, "integrity reports no official semantic fallback");
    require(integrity.officialOpcodeMicrosequenceComplete, "integrity reports official opcode microsequence coverage");
    require(integrity.tickHasNoSpeculativeVisibleFetch, "integrity reports no speculative visible opcode fetch");

    auto debug = rt.rsidDebugSnapshot();
    require(debug.interruptBootstrapValid, "RSID debug snapshot exposes live valid bootstrap");
    require(debug.irqTrampolineChecksum == bootstrap.irqTrampolineChecksum, "debug snapshot exposes live trampoline checksum");
    require(debug.cia1LatchA == Cia6526::kTodPal50HzPhi2Period, "debug snapshot exposes PAL CIA latch");

    C64ChipSnapshot tele = c64BuildTelemetrySnapshot(rt.platform(), true, 7u, 1u, 50.0f, "T", "A", "R", 1u, 1u);
    require(tele.psidCiaIrqObserved, "telemetry mirrors PSID CIA IRQ evidence");
    require(tele.psidCiaCpuIrqLineObserved, "telemetry mirrors CPU IRQ line evidence");
    require(tele.psidCiaVectorEntered, "telemetry mirrors vector evidence");
    require(tele.psidCiaPlayAddressEntered, "telemetry mirrors play-entry evidence");
    require(tele.psidCiaAckObserved, "telemetry mirrors CIA ack evidence");
    require(tele.psidCiaRunGeneration == drive.runGeneration, "telemetry mirrors run generation");
    require(tele.psidCiaServiceGeneration == drive.serviceGeneration, "telemetry mirrors service generation");

    std::puts("C64PsidCiaIrqServiceV499 tests passed");
    return 0;
}
