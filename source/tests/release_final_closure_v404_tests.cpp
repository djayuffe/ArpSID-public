#include "arpsid/core/psid_header.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/sid_event_queue.h"
#include "source/au3/ArpSIDCanonicalEvents.h"

#include <cstdio>
#include <cstdint>
#include <vector>

static int g_failures = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}
template<typename T>
static void checkEq(T got, T expected, const char* msg) {
    if (got != expected) {
        std::fprintf(stderr, "FAIL: %s got=%lld expected=%lld\n", msg,
                     static_cast<long long>(got), static_cast<long long>(expected));
        ++g_failures;
    }
}

static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off]=uint8_t(v>>8); b[off+1]=uint8_t(v); }
static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) { b[off]=uint8_t(v>>24); b[off+1]=uint8_t(v>>16); b[off+2]=uint8_t(v>>8); b[off+3]=uint8_t(v); }

static std::vector<uint8_t> buildPsid(bool rsid=false,
                                      uint16_t version=2,
                                      uint16_t songs=1,
                                      uint16_t startSong=1,
                                      uint16_t loadAddr=0,
                                      uint16_t initAddr=0x0800,
                                      uint16_t playAddr=0x0806,
                                      uint8_t secondSid=0,
                                      uint8_t thirdSid=0) {
    const uint16_t dataOff = version >= 2 ? 0x7C : 0x76;
    std::vector<uint8_t> b(dataOff, 0);
    b[0] = rsid ? 'R' : 'P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b, 0x04, version); be16(b, 0x06, dataOff); be16(b, 0x08, loadAddr);
    be16(b, 0x0A, initAddr); be16(b, 0x0C, playAddr);
    be16(b, 0x0E, songs); be16(b, 0x10, startSong); be32(b, 0x12, 0);
    // v896 refresh: RSID flag bit 1 ($0002) marks a C64 BASIC tune, which the
    // parser now honestly refuses (UnsupportedRsidBasic). This fixture means a
    // plain PAL RSID — use the video-standard bit ($0004) instead.
    if (version >= 2) { be16(b, 0x76, rsid ? 4 : 0); b[0x7A] = secondSid; b[0x7B] = thirdSid; }
    if (loadAddr == 0) { b.push_back(0x00); b.push_back(0x08); }
    const uint8_t code[] = {0xA9,0x55,0x8D,0x00,0xD4,0x60,0xA9,0x66,0x8D,0x01,0xD4,0x60};
    b.insert(b.end(), code, code + sizeof(code));
    return b;
}

static void testPsidValidationRuntime() {
    ArpSID::PsidHeader hdr{};
    auto good = buildPsid(false);
    check(ArpSID::psidParse(good.data(), uint32_t(good.size()), hdr) == ArpSID::PsidParseResult::OK, "valid PSID accepted");
    check(!hdr.isRsid && hdr.effectiveLoadAddr == 0x0800 && hdr.payloadLen >= 12, "valid PSID payload/effective load decoded");

    auto zeroSongs = buildPsid(false, 2, 0, 1);
    check(ArpSID::psidParse(zeroSongs.data(), uint32_t(zeroSongs.size()), hdr) == ArpSID::PsidParseResult::BadSongCount, "songs=0 rejected");

    auto badStart = buildPsid(false, 2, 2, 3);
    check(ArpSID::psidParse(badStart.data(), uint32_t(badStart.size()), hdr) == ArpSID::PsidParseResult::BadStartSong, "startSong > songs rejected");

    // v873: the extra-SID selector ($7A) is a PSID v3 field; validate it on a v3 header
    // (a v2 header leaves $7A reserved, so an odd byte there is simply ignored).
    auto badExtraSidAddress = buildPsid(false, 3, 1, 1, 0, 0x0800, 0x0806, 0x43, 0);
    check(ArpSID::psidParse(badExtraSidAddress.data(), uint32_t(badExtraSidAddress.size()), hdr) == ArpSID::PsidParseResult::BadSidAddress, "odd extra SID address selector rejected");

    auto multiSid = buildPsid(false, 3, 1, 1, 0, 0x0800, 0x0806, 0x42, 0);
    check(ArpSID::psidParse(multiSid.data(), uint32_t(multiSid.size()), hdr) == ArpSID::PsidParseResult::OK, "multi-SID parses after routed chip metadata landed");
    check(ArpSID::psidSidChipCount(hdr) == 2 && ArpSID::psidSidBaseForChip(hdr, 1) == 0xD420, "multi-SID metadata exposes the second SID base");

    auto rsidOk = buildPsid(true, 2, 1, 1, 0, 0x0800, 0x0000);
    check(ArpSID::psidParse(rsidOk.data(), uint32_t(rsidOk.size()), hdr) == ArpSID::PsidParseResult::OK && hdr.isRsid, "RSID accepted only with machine-scheduled play vector");

    auto rsidBad = buildPsid(true, 2, 1, 1, 0, 0x0800, 0x0806);
    check(ArpSID::psidParse(rsidBad.data(), uint32_t(rsidBad.size()), hdr) == ArpSID::PsidParseResult::BadRsidHeader, "RSID direct play vector rejected for fast projection runtime");

    ArpSID::C64::C64Runtime rt; rt.reset(true);
    check(rt.loadPsid(good.data(), good.size()), "C64 runtime loads validated single-SID PSID");
    check(rt.loadPsid(multiSid.data(), multiSid.size()), "C64 runtime accepts multi-SID PSID metadata instead of silently dropping extra chips");
}

static void testAuEventOverflowTelemetryRuntime() {
    ArpSID::EventBuffer buf{};
    for (int i = 0; i < ArpSID::kMaxTimedEvents; ++i) {
        ArpSID::TimedEvent ev{};
        ev.kind = ArpSID::EventKind::PitchBend;
        ev.sampleOffset = i;
        ev.rawOrder = static_cast<uint32_t>(i);
        ev.data14 = 8192;
        check(buf.push(ev), "initial AU event fill accepted");
    }
    ArpSID::TimedEvent panic{};
    panic.kind = ArpSID::EventKind::Panic;
    panic.sampleOffset = 0;
    panic.rawOrder = 999999;
    check(buf.push(panic), "panic replaces lower-priority event on full AU event buffer");
    const auto& tel = buf.overflow();
    check(tel.overflowed, "AU event overflow telemetry marks overflow");
    checkEq(tel.replacedLowerPriority, uint64_t{1}, "AU event overflow replacement counted");
    checkEq(tel.droppedController, uint64_t{1}, "AU event overflow records dropped controller/pitch event");
    checkEq(tel.droppedPanic, uint64_t{0}, "AU event overflow never records panic as dropped in this scenario");

    buf.reset();
    checkEq(buf.overflow().droppedTotal, uint64_t{0}, "AU event overflow telemetry resets with buffer");
}

static void testCoreEventOverflowTelemetryRuntime() {
    // v965 PIPE-013: the core event queue reserves headroom (kReleaseReserve) for
    // release/stop/panic traffic. Ordinary low-priority events (pitch bend, CC,
    // note-on) are admitted only up to (capacity - reserve); the reserve is
    // available exclusively to release-critical events so a stuck voice can
    // always be released. At true capacity a release-critical event replaces the
    // worst lower-priority event rather than being dropped.
    ArpSID::SidTimedEventQueue q{ArpSID::SidTimedEventQueue::AllocateStorage{}};
    const int reserve  = ArpSID::SidTimedEventQueue::kReleaseReserve;
    const int lowAdmit = ArpSID::kMaxSidTimedEvents - reserve;
    for (int i = 0; i < lowAdmit; ++i) {
        ArpSID::SidTimedEvent ev{};
        ev.type = ArpSID::SidTimedEventType::PitchBend;
        ev.arrival_order = static_cast<uint32_t>(i);
        check(q.push(ev), "low-priority core events admitted up to the release-reserve boundary");
    }
    ArpSID::SidTimedEvent refused{};
    refused.type = ArpSID::SidTimedEventType::PitchBend;
    refused.arrival_order = 1000000u;
    check(!q.push(refused), "low-priority event is refused once only the release reserve remains");
    for (int i = 0; i < reserve; ++i) {
        ArpSID::SidTimedEvent ev{};
        ev.type = ArpSID::SidTimedEventType::MidiNoteOff;
        ev.arrival_order = static_cast<uint32_t>(200000 + i);
        check(q.push(ev), "release-critical events may consume the reserved headroom to capacity");
    }
    ArpSID::SidTimedEvent off{};
    off.type = ArpSID::SidTimedEventType::MidiNoteOff;
    off.arrival_order = 500000;
    check(q.push(off), "note-off replaces lower-priority event on full core event queue");
    const auto& tel = q.overflow();
    check(tel.overflowed, "core event overflow telemetry marks overflow");
    checkEq(tel.replacedLowerPriority, uint64_t{1}, "core event replacement counted");
    checkEq(tel.droppedNoteOff, uint64_t{0}, "core event note-off was preserved");
    q.reset();
    checkEq(q.overflow().droppedTotal, uint64_t{0}, "core event overflow telemetry resets with queue");
}

int main() {
    testPsidValidationRuntime();
    testAuEventOverflowTelemetryRuntime();
    testCoreEventOverflowTelemetryRuntime();
    if (g_failures == 0) {
        std::puts("ReleaseFinalClosureV404 runtime checks passed");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) FAILED\n", g_failures);
    return 1;
}
