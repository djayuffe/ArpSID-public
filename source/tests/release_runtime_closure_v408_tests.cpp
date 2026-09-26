// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/psid_header.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/sid_event_queue.h"
#include "arpsid/core/host_transport_snapshot.h"
#include "source/au3/ArpSIDCanonicalEvents.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>
#include <type_traits>

static int g_failures = 0;
static void check(bool cond, const char* msg) { if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; } }
template<typename T> static void checkEq(T got, T expected, const char* msg) {
    if (got != expected) { std::fprintf(stderr, "FAIL: %s got=%lld expected=%lld\n", msg, (long long)got, (long long)expected); ++g_failures; }
}
static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off]=uint8_t(v>>8); b[off+1]=uint8_t(v); }
static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) { b[off]=uint8_t(v>>24); b[off+1]=uint8_t(v>>16); b[off+2]=uint8_t(v>>8); b[off+3]=uint8_t(v); }

static std::vector<uint8_t> buildPsid(bool rsid=false, uint16_t version=2, uint16_t songs=1, uint16_t startSong=1,
                                      uint16_t loadAddr=0, uint16_t initAddr=0x0800, uint16_t playAddr=0x0809,
                                      uint8_t secondSid=0, uint8_t thirdSid=0) {
    const uint16_t dataOff = version >= 2 ? 0x7C : 0x76;
    std::vector<uint8_t> b(dataOff, 0);
    b[0] = rsid ? 'R' : 'P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b,0x04,version); be16(b,0x06,dataOff); be16(b,0x08,loadAddr); be16(b,0x0A,initAddr); be16(b,0x0C,playAddr);
    be16(b,0x0E,songs); be16(b,0x10,startSong); be32(b,0x12,0);
    // v896 refresh: RSID flag bit 1 ($0002) marks a C64 BASIC tune, which the
    // parser now honestly refuses (UnsupportedRsidBasic). This fixture means a
    // plain PAL RSID — use the video-standard bit ($0004) instead.
    if (version >= 2) { be16(b,0x76,rsid ? 4 : 0); b[0x7A]=secondSid; b[0x7B]=thirdSid; }
    if (loadAddr == 0) { b.push_back(0x00); b.push_back(0x08); }
    const uint8_t code[] = {
        0x8D,0x02,0xD4,0xA9,0x55,0x8D,0x00,0xD4,0x60, // init: STA $D402 proves A=song-1; LDA #$55; STA $D400; RTS
        0xA9,0x66,0x8D,0x01,0xD4,0x60                  // play: LDA #$66; STA $D401; RTS
    };
    b.insert(b.end(), code, code + sizeof(code));
    return b;
}

static void testPsidParserAndRuntimeActuallyExecute() {
    auto psid = buildPsid(false, 2, 3, 2);
    ArpSID::PsidHeader hdr{};
    auto r = ArpSID::psidParse(psid.data(), (uint32_t)psid.size(), hdr);
    check(r == ArpSID::PsidParseResult::OK, ArpSID::psidParseResultName(r));
    checkEq(hdr.effectiveLoadAddr, (uint16_t)0x0800, "little-endian payload load address decoded");

    ArpSID::C64::C64Runtime rt; rt.reset(true);
    check(rt.loadPsid(psid.data(), psid.size()), "runtime accepts validated single-SID PSID");
    check(rt.runInit(0, 64), "runtime init trampoline returns using startSong default");
    checkEq(rt.sidSink().regs[2], (uint8_t)0x01, "init A register receives startSong-1");
    checkEq(rt.sidSink().regs[0], (uint8_t)0x55, "init routine wrote SID register $D400");
    check(!rt.runPlay(0), "zero play instruction budget fails deterministically");
    check(rt.runPlay(64), "runtime play trampoline returns");
    checkEq(rt.sidSink().regs[1], (uint8_t)0x66, "play routine wrote SID register $D401");
}

static void testPsidRsidStrictHeaderPolicy() {
    ArpSID::PsidHeader hdr{};
    auto badMagic = buildPsid(false); badMagic[0] = 'X';
    check(ArpSID::psidParse(badMagic.data(), (uint32_t)badMagic.size(), hdr) == ArpSID::PsidParseResult::BadMagic, "bad magic rejected");
    auto badOffset = buildPsid(false); be16(badOffset, 0x06, 0x70);
    check(ArpSID::psidParse(badOffset.data(), (uint32_t)badOffset.size(), hdr) == ArpSID::PsidParseResult::BadOffset, "too-small data offset rejected");
    auto badVersion = buildPsid(false, 5);
    check(ArpSID::psidParse(badVersion.data(), (uint32_t)badVersion.size(), hdr) == ArpSID::PsidParseResult::BadVersion, "unsupported PSID version rejected");
    auto zeroSongs = buildPsid(false, 2, 0, 1);
    check(ArpSID::psidParse(zeroSongs.data(), (uint32_t)zeroSongs.size(), hdr) == ArpSID::PsidParseResult::BadSongCount, "songs=0 rejected");
    auto badStart = buildPsid(false, 2, 2, 3);
    check(ArpSID::psidParse(badStart.data(), (uint32_t)badStart.size(), hdr) == ArpSID::PsidParseResult::BadStartSong, "startSong beyond songs rejected");
    // v873: extra-SID selector bytes ($7A/$7B) are PSID v3+ fields, so exercise the
    // extra-SID validation on v3 headers (a v2 header leaves $7A/$7B reserved).
    auto oddExtraSid = buildPsid(false, 3, 1, 1, 0, 0x0800, 0x0809, 0x43, 0);
    check(ArpSID::psidParse(oddExtraSid.data(), (uint32_t)oddExtraSid.size(), hdr) == ArpSID::PsidParseResult::BadSidAddress, "odd extra SID selector rejected");
    auto ioAreaSid = buildPsid(false, 3, 1, 1, 0, 0x0800, 0x0809, 0xD8, 0);
    check(ArpSID::psidParse(ioAreaSid.data(), (uint32_t)ioAreaSid.size(), hdr) == ArpSID::PsidParseResult::BadSidAddress, "forbidden $D800-$DFFF extra SID selector rejected");
    auto multiSid = buildPsid(false, 3, 1, 1, 0, 0x0800, 0x0809, 0x42, 0);
    check(ArpSID::psidParse(multiSid.data(), (uint32_t)multiSid.size(), hdr) == ArpSID::PsidParseResult::OK, "multi-SID PSID now parses after routed multi-chip bus landed");
    check(ArpSID::psidSidChipCount(hdr) == 2 && ArpSID::psidSidBaseForChip(hdr, 1) == 0xD420, "multi-SID metadata exposes second SID base");
    ArpSID::C64::C64Runtime rt; rt.reset(true);
    check(rt.loadPsid(multiSid.data(), multiSid.size()), "runtime accepts multi-SID instead of silently dropping extra chips");
    check(rt.platform().sidChipCount() == 2 && rt.platform().sidBase(1) == 0xD420, "runtime configures routed second SID bus");
    auto rsidFastPlay = buildPsid(true, 2, 1, 1, 0, 0x0800, 0x0809);
    check(ArpSID::psidParse(rsidFastPlay.data(), (uint32_t)rsidFastPlay.size(), hdr) == ArpSID::PsidParseResult::BadRsidHeader, "RSID with direct play vector rejected by fast projection runtime");
}

static void testPayloadLoadDoesNotWrapMemory() {
    std::vector<uint8_t> img(0x76, 0);
    img[0]='P'; img[1]='S'; img[2]='I'; img[3]='D';
    be16(img,0x04,1); be16(img,0x06,0x76); be16(img,0x08,0xFFFE); be16(img,0x0A,0xFFFE); be16(img,0x0C,0); be16(img,0x0E,1); be16(img,0x10,1);
    img.push_back(0xEA); img.push_back(0xEA); img.push_back(0xEA); img.push_back(0xEA);
    ArpSID::PsidHeader hdr{};
    check(ArpSID::psidParse(img.data(), (uint32_t)img.size(), hdr) == ArpSID::PsidParseResult::OK, "near-end load PSID parses");
    uint8_t ram[65536]{}; ram[0] = 0xA5; ram[1] = 0x5A;
    ArpSID::psidLoadIntoRam(hdr, ram);
    checkEq(ram[0], (uint8_t)0xA5, "load helper does not wrap to $0000");
    checkEq(ram[1], (uint8_t)0x5A, "load helper does not wrap to $0001");
    checkEq(ram[0xFFFE], (uint8_t)0xEA, "load helper writes first in-range byte");
    checkEq(ram[0xFFFF], (uint8_t)0xEA, "load helper writes last in-range byte");
}

// Fill a core queue to true capacity: low-priority events up to the release
// reserve boundary, then release-critical events consuming the reserve. Leaves
// the queue full with lower-priority events present to be replaced.
static void fillCoreQueueToCapacityWithLowPriority(ArpSID::SidTimedEventQueue& q) {
    const int reserve  = ArpSID::SidTimedEventQueue::kReleaseReserve;
    const int lowAdmit = ArpSID::kMaxSidTimedEvents - reserve;
    for (int i=0;i<lowAdmit;++i) { ArpSID::SidTimedEvent ev{}; ev.type=ArpSID::SidTimedEventType::PitchBend; ev.arrival_order=(uint32_t)i; check(q.push(ev), "low-priority core fill to reserve boundary"); }
    for (int i=0;i<reserve;++i)  { ArpSID::SidTimedEvent ev{}; ev.type=ArpSID::SidTimedEventType::TransportChange; ev.arrival_order=(uint32_t)(100000+i); check(q.push(ev), "release-critical fill consumes reserved headroom"); }
}

static void testEventOverflowTelemetryIsSemantic() {
    // ── AU EventBuffer: no release reserve; fills to capacity, then a higher-
    //    priority event replaces the worst lower-priority one. ──
    ArpSID::EventBuffer au{};
    for (int i=0;i<ArpSID::kMaxTimedEvents;++i) { ArpSID::TimedEvent ev{}; ev.kind=ArpSID::EventKind::PitchBend; ev.rawOrder=(uint32_t)i; check(au.push(ev), "AU queue fill"); }
    ArpSID::TimedEvent panic{}; panic.kind=ArpSID::EventKind::Panic; panic.rawOrder=9999;
    check(au.push(panic), "AU panic survives full queue");
    check(au.overflow().overflowed, "AU overflow flag set");
    checkEq(au.overflow().droppedPanic, (uint64_t)0, "AU panic not dropped");
    checkEq(au.overflow().replacedLowerPriority, (uint64_t)1, "AU lower-priority replacement counted");

    // ── Core queue: v965 PIPE-013 reserves headroom; a release-critical NoteOff
    //    at true capacity survives by replacing the worst lower-priority event. ──
    ArpSID::SidTimedEventQueue core{ArpSID::SidTimedEventQueue::AllocateStorage{}};
    fillCoreQueueToCapacityWithLowPriority(core);
    ArpSID::SidTimedEvent off{}; off.type=ArpSID::SidTimedEventType::MidiNoteOff; off.arrival_order=9999;
    check(core.push(off), "core note-off survives full queue via replacement");
    checkEq(core.overflow().droppedNoteOff, (uint64_t)0, "core note-off not dropped");
    checkEq(core.overflow().replacedLowerPriority, (uint64_t)1, "core replacement counted");

    // ── v965 PIPE-001 sort contract: canonical arrival_order is authoritative;
    //    only emergency sample-boundary kills (Panic/AllSoundOff/AllNotesOff)
    //    preempt, and only among resolved same-sample events. A plain NoteOff no
    //    longer reorders ahead of earlier-arrived traffic by event-type priority. ──
    {
        ArpSID::SidTimedEventQueue s{ArpSID::SidTimedEventQueue::AllocateStorage{}};
        ArpSID::SidTimedEvent pb{};  pb.type=ArpSID::SidTimedEventType::PitchBend; pb.sample_offset=0; pb.arrival_order=0; s.push(pb);
        ArpSID::SidTimedEvent pnc{}; pnc.type=ArpSID::SidTimedEventType::Panic;    pnc.sample_offset=0; pnc.arrival_order=5; s.push(pnc);
        s.sort();
        check(s.begin()->type == ArpSID::SidTimedEventType::Panic, "same-sample emergency Panic preempts a lower-priority event even when it arrived later");
    }
    {
        ArpSID::SidTimedEventQueue s{ArpSID::SidTimedEventQueue::AllocateStorage{}};
        ArpSID::SidTimedEvent pb{};   pb.type=ArpSID::SidTimedEventType::PitchBend;   pb.sample_offset=0; pb.arrival_order=0; s.push(pb);
        ArpSID::SidTimedEvent noff{}; noff.type=ArpSID::SidTimedEventType::MidiNoteOff; noff.sample_offset=0; noff.arrival_order=5; s.push(noff);
        s.sort();
        check(s.begin()->type == ArpSID::SidTimedEventType::PitchBend, "non-emergency NoteOff does not reorder ahead of an earlier same-sample arrival (PIPE-001 arrival authority)");
    }
}

static void testQueueSwapPreservesOverflowTelemetry() {
    ArpSID::SidTimedEventQueue a{ArpSID::SidTimedEventQueue::AllocateStorage{}}; ArpSID::SidTimedEventQueue b{ArpSID::SidTimedEventQueue::AllocateStorage{}};
    fillCoreQueueToCapacityWithLowPriority(a);
    ArpSID::SidTimedEvent panic{}; panic.type=ArpSID::SidTimedEventType::Panic; panic.arrival_order=7777;
    check(a.push(panic), "swap telemetry panic replacement");
    checkEq(a.overflow().replacedLowerPriority, (uint64_t)1, "pre-swap replacement counted");
    a.swap(b);
    checkEq(a.overflow().replacedLowerPriority, (uint64_t)0, "swap moved telemetry out of source");
    checkEq(b.overflow().replacedLowerPriority, (uint64_t)1, "swap preserved telemetry in destination");
    checkEq(b.overflow().droppedPanic, (uint64_t)0, "panic was not counted as dropped after swap");
}

static void testTransportSnapshotSeqlockRuntime() {
    ArpSID::HostTransportSnapshotSeqlock snap; ArpSID::HostTransportPodSnapshot out{};
    check(!snap.read(out), "empty snapshot does not read as valid");
    ArpSID::HostTransportPodSnapshot in{}; in.bpm=173.5; in.beatPosition=42.25; in.sampleRate=96000.0; in.loopStart=16.0; in.loopEnd=4.0; in.frameCount=512; in.flags=7;
    snap.publish(in); check(snap.read(out), "published snapshot reads coherently");
    checkEq((int)out.bpm, 173, "bpm carried through snapshot");
    checkEq((int)out.sampleRate, 96000, "sample rate carried through snapshot");
    checkEq((int)out.loopEnd, 16, "loopEnd clamped to loopStart");
    checkEq(out.frameCount, 512, "frame count carried through snapshot");
    checkEq(out.flags, (uint32_t)7, "flags carried through snapshot");
    ArpSID::HostTransportPodSnapshot bad{}; bad.bpm=std::numeric_limits<double>::infinity(); bad.sampleRate=0.0; bad.frameCount=-7;
    snap.publish(bad); check(snap.read(out), "sanitized snapshot remains readable");
    checkEq((int)out.bpm, 120, "bad BPM sanitized");
    checkEq((int)out.sampleRate, 44100, "bad sample rate sanitized");
    checkEq(out.frameCount, 0, "negative frame count sanitized");
}

static void testCompileTimeReleaseInvariants() {
    check(std::is_nothrow_move_constructible<ArpSID::SidTimedEventQueue>::value, "event queue move is noexcept");
    check(std::is_nothrow_move_assignable<ArpSID::SidTimedEventQueue>::value, "event queue move assignment is noexcept");
    check(sizeof(ArpSID::SidTimedEvent) <= 64, "SidTimedEvent remains compact");
}

int main() {
    testCompileTimeReleaseInvariants();
    testPsidParserAndRuntimeActuallyExecute();
    testPsidRsidStrictHeaderPolicy();
    testPayloadLoadDoesNotWrapMemory();
    testEventOverflowTelemetryIsSemantic();
    testQueueSwapPreservesOverflowTelemetry();
    testTransportSnapshotSeqlockRuntime();
    if (g_failures == 0) { std::puts("ReleaseRuntimeClosureV408 final closure checks passed"); return 0; }
    std::fprintf(stderr, "%d test(s) FAILED\n", g_failures); return 1;
}
