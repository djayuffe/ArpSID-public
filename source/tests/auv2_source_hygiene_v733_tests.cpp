// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) {
        std::cerr << "missing file: " << rel << "\n";
        std::abort();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

static std::string sliceBetween(const std::string& s,
                                const std::string& begin,
                                const std::string& end) {
    const std::size_t b = s.find(begin);
    require(b != std::string::npos, "slice begin marker missing");
    const std::size_t e = s.find(end, b + begin.size());
    require(e != std::string::npos, "slice end marker missing");
    return s.substr(b, e - b);
}

int main() {
    const std::string auv2 = readFile("source/au2/ArpSIDAUv2Component.mm");
    const std::string gui = readFile("source/au3/ArpSIDViewController.mm");

    require(gui.find("ArpSIDDrsidTabPanel.h//") == std::string::npos,
            "DrSID panel documentation must not contain comment-merge artifact h//");
    require(gui.find("// The constexpr binding tables in that header are guarded by static_assert") != std::string::npos,
            "DrSID static_assert documentation should be on its own comment line");
    require(gui.find("v311  rsidStrictStatusCode") != std::string::npos,
            "C64 STATE strict-status GUI label should use current v311 wording");
    require(gui.find("v307  rsidStrictStatusCode") == std::string::npos,
            "C64 STATE strict-status GUI label must not keep stale v307 wording");

    const std::string addRenderNotify = sliceBetween(
        auv2,
        "static OSStatus componentAddRenderNotify",
        "static OSStatus componentRemoveRenderNotify");
    require(addRenderNotify.find("impl->renderNotifyListeners.push_back({proc, userData});") != std::string::npos,
            "AUv2 AddRenderNotify must accept host callbacks instead of rejecting auval/Logic");
    require(addRenderNotify.find("publishRenderNotifyTableLocked(impl);") != std::string::npos,
            "AUv2 AddRenderNotify must publish the callback table after registration");
    require(addRenderNotify.find("return noErr;") != std::string::npos,
            "AUv2 AddRenderNotify must return noErr on successful registration");
    require(addRenderNotify.find("return kAudioUnitErr_CannotDoInCurrentContext") == std::string::npos,
            "AUv2 AddRenderNotify table pressure must not trip Logic recovery");
    require(addRenderNotify.find("renderNotifySkippedUnstablePublicationCount.fetch_add") == std::string::npos,
            "AUv2 AddRenderNotify must not count registration as skipped render-notify dispatch");

    const std::string callRenderNotify = sliceBetween(
        auv2,
        "static OSStatus callAuv2RenderNotifyTable",
        "static inline void waitForRenderUsersToDrainBounded_");
    require(callRenderNotify.find("const OSStatus status = proc(") != std::string::npos,
            "AUv2 render-notify table must dispatch accepted callbacks");
    require(callRenderNotify.find("notifyCallbackViolationCount.fetch_add") != std::string::npos,
            "AUv2 render-notify dispatch must attribute realtime-guard callback violations");
    require(auv2.find("render-notify callbacks are unsupported") == std::string::npos,
            "AUv2 source must not document render-notify as unsupported after auval/Logic fix");
    require(auv2.find("callbacks are always skipped") == std::string::npos,
            "AUv2 source must not silently skip every render-notify callback");

    const std::string failClosed = sliceBetween(
        auv2,
        "static inline OSStatus auv2FailClosedSilentNoErr",
        "class Auv2RealtimeGuardSliceAudit");
    require(failClosed.find("lastRenderError.store(noErr") != std::string::npos,
            "AUv2 silent fail-closed renders must keep host-visible LastRenderError clear");
    require(failClosed.find("lastRenderError.store(diagnosticStatus") == std::string::npos,
            "AUv2 silent diagnostics must not leak through LastRenderError and trip Logic recovery");

    const std::string rtAudit = sliceBetween(
        auv2,
        "class Auv2RealtimeGuardSliceAudit final",
        "Auv2RealtimeGuardSliceAudit(const Auv2RealtimeGuardSliceAudit&)");
    require(rtAudit.find("lastRenderError.store(noErr") != std::string::npos,
            "AUv2 post-render realtime guard telemetry must not poison LastRenderError");
    require(rtAudit.find("lastRenderError.store(kAudioUnitErr_CannotDoInCurrentContext") == std::string::npos,
            "AUv2 post-render realtime guard telemetry must not report CannotDoInCurrentContext to Logic");

    const std::string setParameter = sliceBetween(
        auv2,
        "static OSStatus componentSetParameter",
        "static OSStatus componentScheduleParameters");
    require(setParameter.find("kernel->enqueueParameterIntent") != std::string::npos,
            "AUv2 SetParameter still routes host values through kernel ingress");
    require(setParameter.find("cacheParameterValueForInstance(impl, inID, value);") != std::string::npos,
            "AUv2 SetParameter caches host values even when timed ingress is saturated");
    require(setParameter.find("return kAudioUnitErr_CannotDoInCurrentContext") == std::string::npos,
            "AUv2 SetParameter queue pressure must not surface as a Logic plug-in problem");

    const std::string scheduleParameters = sliceBetween(
        auv2,
        "static OSStatus componentScheduleParameters",
        "static OSStatus componentRender");
    require(scheduleParameters.find("auv2RampAnchorDroppedCount.fetch_add") != std::string::npos,
            "AUv2 scheduled ramp drops remain observable as telemetry");
    require(scheduleParameters.find("return kAudioUnitErr_CannotDoInCurrentContext") == std::string::npos,
            "AUv2 ScheduleParameters queue pressure must not surface as a Logic plug-in problem");

    const std::string render = sliceBetween(
        auv2,
        "static OSStatus componentRender",
        "static OSStatus componentReset");
    require(render.find("!impl->initialized.load(std::memory_order_acquire)") != std::string::npos &&
            render.find("auv2FailClosedSilentNoErr(impl,\n                                         ioActionFlags,\n                                         output,\n                                         inNumberFrames,\n                                         renderedChannels,\n                                         kAudioUnitErr_Uninitialized,\n                                         &impl->renderReadinessDiagnosticCount)") != std::string::npos,
            "AUv2 transient startup render-readiness gaps must be silence/noErr diagnostics for Logic");

    const std::string midiEvent = sliceBetween(
        auv2,
        "static OSStatus componentMIDIEvent",
        "static AudioComponentMethod lookupSelector");
    require(midiEvent.find("kernel->enqueueMidiIntent(bytes, length") != std::string::npos,
            "AUv2 MIDI entrypoint must route valid host MIDI through the canonical kernel ingress queue");
    require(midiEvent.find("return queued ? noErr : kAudioUnitErr_CannotDoInCurrentContext;") == std::string::npos,
            "AUv2 MIDI overflow must not surface as -10863 to Logic/hosts");
    require(midiEvent.find("A saturated MIDI ring is not a host-call failure") != std::string::npos &&
            midiEvent.find("return noErr;") != std::string::npos,
            "AUv2 MIDI overflow policy must accept saturated valid MusicDeviceMIDIEvent bursts");

    std::cout << "Auv2SourceHygieneV733Tests PASS\n";
    return 0;
}
