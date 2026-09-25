#include "arpsid/gui/kit_runtime_resolver.h"
#include "arpsid/gui/kit_sequencer.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::uint16_t slotOf(const ArpSID::GUI::CompiledKitEvent& ev) noexcept {
    return static_cast<std::uint16_t>(ev.selectedFactorySlotLo |
        (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8));
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    KitStepGrid grid = makeDefaultKitStepGrid();
    for (std::size_t dc = 0; dc < kKitDrumClassCount; ++dc)
        grid.steps[dc][0] = static_cast<std::uint8_t>(80u + dc);

    KitPanelModel model = makeDefaultKitPanelModel();
    KitAssignConfigGrid assign = makeDefaultKitAssignConfigGrid();
    KitVoiceConfigGrid voices = makeDefaultKitVoiceConfigGrid();

    // Force a rotating target map across all 9 drum classes.
    for (std::size_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        const auto target = static_cast<KitEngineTarget>(dc % kKitEngineTargetCount);
        kitAssignSetEngineTargetOverride(assign.assignConfigs[dc], static_cast<std::uint8_t>(target));
        model.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex =
            static_cast<std::uint8_t>(dc % kKitDrSidSlotCount);
        model.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::SID808)].factorySlotIndex =
            static_cast<std::uint8_t>(dc % kKitSid808SlotCount);
        model.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::Digi)].factorySlotIndex =
            static_cast<std::uint8_t>((dc + 5u) % kKitDigiSlotCount);

        assign.assignConfigs[dc].digiSlotIndex = static_cast<std::uint8_t>((dc + 7u) % kKitDigiSlotCount);
        kitAssignSetTuneShift(assign.assignConfigs[dc], static_cast<std::int8_t>((int(dc) % 5) - 2));

        voices.voiceConfigs[dc].waveform = (dc & 1u) ? kKitVoiceWavePul : kKitVoiceWaveNoi;
        voices.voiceConfigs[dc].attackDecay = static_cast<std::uint8_t>(0x20u | (dc & 0x0Fu));
        voices.voiceConfigs[dc].sustainRelease = static_cast<std::uint8_t>(0x80u | (dc & 0x0Fu));
        kitVoiceSetPulseWidth(voices.voiceConfigs[dc], static_cast<std::uint16_t>(0x0400u + dc));
        voices.voiceConfigs[dc].flags = static_cast<std::uint8_t>(dc & 0x07u);
    }

    CompiledKitSequencer cs = makeDefaultCompiledKitSequencer();
    require(compileKitSequencer(cs, grid, model, voices, assign), "full kit compiles");
    require(cs.steps[0].eventCount == kKitDrumClassCount, "all kit classes compile to events");

    bool sawDrsid = false, sawSid808 = false, sawDigi = false;
    for (std::uint8_t i = 0; i < cs.steps[0].eventCount; ++i) {
        const auto& ev = cs.steps[0].events[i];
        const std::size_t dc = ev.drumClass;
        const KitResolvedHit hit = resolveCompiledKitEvent(ev, &assign.assignConfigs[dc]);

        require(hit.drumClass == dc, "resolved hit preserves drum class");
        require(hit.midiNote == kKitDrumClassMidiNote[dc], "resolved hit preserves canonical MIDI note");
        require(hit.velocity == grid.steps[dc][0], "resolved hit preserves velocity");
        require(hit.voiceWaveform == (voices.voiceConfigs[dc].waveform & kKitVoiceWaveMask), "voice waveform resolved");
        require(hit.voiceAttackDecay == voices.voiceConfigs[dc].attackDecay, "voice AD resolved");
        require(hit.voiceSustainRelease == voices.voiceConfigs[dc].sustainRelease, "voice SR resolved");
        require(hit.voicePulseWidth == kitVoicePulseWidth(voices.voiceConfigs[dc]), "voice PW resolved");
        require(hit.voiceFlags == (voices.voiceConfigs[dc].flags & 0x07u), "voice flags resolved");
        require(hit.digiRuntimeSlot == 0u, "Digi runtime projection is dense slot 0");
        require(hit.digiActiveSlotCount == 1u, "Digi dense projection has one active slot");
        require(hit.digiSourceSlotIndex == assign.assignConfigs[dc].digiSlotIndex, "Digi source slot identity preserved");
        require(hit.digiAbsoluteFactorySlot == kitAbsoluteSlotForTarget(KitEngineTarget::Digi, assign.assignConfigs[dc].digiSlotIndex),
                "Digi absolute factory slot resolved from source slot");
        require(hit.digiTuneShift == kitAssignTuneShift(assign.assignConfigs[dc]), "Digi tune shift resolved");

        if (hit.target == KitEngineTarget::DrSID) {
            sawDrsid = true;
            require(isDrSidFactorySlot(static_cast<int>(hit.selectedFactorySlot)), "DrSID selected slot in DrSID range");
        } else if (hit.target == KitEngineTarget::SID808) {
            sawSid808 = true;
            require(isSid808FactorySlot(static_cast<int>(hit.selectedFactorySlot)), "SID808 selected slot in SID808 range");
        } else if (hit.target == KitEngineTarget::Digi) {
            sawDigi = true;
            require(isDigiFactorySlot(static_cast<int>(hit.selectedFactorySlot)), "Digi selected slot in Digi range");
        }
        require(kitResolvedSlotMatchesTarget(hit), "resolved selected slot matches engine target");
        require(slotOf(ev) == hit.selectedFactorySlot, "compiled and resolved selected slot agree");
    }

    require(sawDrsid && sawSid808 && sawDigi, "full kit resolver covered all targets");
    std::cout << "KitRuntimeResolverV657Tests PASS\n";
    return 0;
}
