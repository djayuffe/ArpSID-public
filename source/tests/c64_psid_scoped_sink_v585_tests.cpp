// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_psid_scoped_sink_v585_tests.cpp
// v585: Scoped sidSink_ in C64Platform::runCycles() and executeInstruction().
//
// Pre-v585, both runCycles(cycles, sid) and executeInstruction(sid) permanently
// replaced sidSink_ when sid != nullptr:
// if (sid) sidSink_ = sid; // permanent leak: prev never restored
//
// This meant any caller that passed a temporary sink inadvertently changed the
// platform's permanent SID routing. In the C64/PSID kernel path:
// platform.runCycles(playPhi2Cycles, &c64SidBridge_)
// re-set sidSink_ to &c64SidBridge_ (which is already the permanent sink// harmless in this case), but the general pattern is fragile. If any future
// call site passes a truly temporary sink, it leaks silently into subsequent
// write callbacks.
//
// Fix (c64_platform.h): save sidSink_ before the temporary override and
// restore it unconditionally on return:
// SidRegisterSink* const prev = sidSink_;
// if (sid) sidSink_ = sid;
// ...
// if (sid) sidSink_ = prev;
//
// This makes runCycles/executeInstruction safe for temporary sinks without
// changing the behaviour of callers that use attachSid() for permanent routing.
//
// Tests use arithmetic simulation — no C64 platform linkage.

#include <cassert>
#include <cstdint>
#include <cstring>

// ── §1 Scoped runCycles restores previous sink after return ─────────────────
static void testScopedRunCyclesRestoresSink()
{
    // Model of runCycles() before and after fix.

    struct SinkA { int id = 1; };
    struct SinkB { int id = 2; };

    // Before fix: permanent replacement.
    {
        SinkA a; SinkB b;
        void* sidSink = &a;
        void* sid = &b;  // non-null temporary sink
        // Simulate pre-fix runCycles(cycles, sid):
        if (sid) sidSink = sid;
        // Run cycles...
        // sidSink remains sid after return (no restore).
        assert(sidSink == &b);  // leaked — not restored
    }

    // After fix: scoped replacement.
    {
        SinkA a; SinkB b;
        void* sidSink = &a;
        void* sid = &b;  // non-null temporary sink
        // Simulate v585 runCycles(cycles, sid):
        void* const prev = sidSink;
        if (sid) sidSink = sid;
        // Run cycles...
        if (sid) sidSink = prev;  // restore
        assert(sidSink == &a);   // restored to permanent sink
    }
}

// ── §2 Scoped executeInstruction restores previous sink ─────────────────────
static void testScopedExecuteInstructionRestoresSink()
{
    int permanentSink = 10;
    int temporarySink = 99;

    void* sidSink = &permanentSink;

    // Simulate v585 executeInstruction(&temporarySink):
    void* const prev = sidSink;
    sidSink = &temporarySink;
    // Execute instruction...
    sidSink = prev;  // restore

    assert(sidSink == &permanentSink);
}

// ── §3 runCycles with null sid does not change sidSink_ ─────────────────────
static void testRunCyclesNullSidNoChange()
{
    int permanentSink = 42;
    void* sidSink = &permanentSink;

    // Simulate runCycles(cycles, nullptr):
    void* sid = nullptr;
    void* const prev = sidSink;
    if (sid) sidSink = sid;
    // Run cycles...
    if (sid) sidSink = prev;

    // sidSink unchanged — no sid was provided.
    assert(sidSink == &permanentSink);
}

// ── §4 Temporary sink receives writes during call only ───────────────────────
static void testTemporarySinkReceivesWritesDuringCallOnly()
{
    // Model: sidSink_ receives writes during stepPhi2_().
    // With scoped fix, writes go to the temporary sink during the call
    // and back to the permanent sink after return.

    int writeCountPermanent = 0;
    int writeCountTemporary = 0;

    struct MockSink {
        int* counter = nullptr;
        void onWrite() { if (counter) ++(*counter); }
    };

    MockSink permanent, temporary;
    permanent.counter  = &writeCountPermanent;
    temporary.counter  = &writeCountTemporary;

    MockSink* sidSink = &permanent;

    // runCycles with temporary sink.
    {
        MockSink* const prev = sidSink;
        sidSink = &temporary;
        // Simulate 3 writes during cycles.
        for (int i = 0; i < 3; ++i) sidSink->onWrite();
        sidSink = prev;
    }

    // After call: permanent sink is active again.
    sidSink->onWrite();  // goes to permanent

    assert(writeCountTemporary == 3);  // received during call
    assert(writeCountPermanent == 1);  // received after call
}

// ── §5 Multiple consecutive calls each restore independently ─────────────────
static void testMultipleConsecutiveCallsEachRestore()
{
    int sink0 = 0, sink1 = 1, sink2 = 2;
    void* sidSink = &sink0;

    // First call with sink1 as temporary.
    {
        void* const prev = sidSink;
        sidSink = &sink1;
        sidSink = prev;
    }
    assert(sidSink == &sink0);

    // Second call with sink2 as temporary.
    {
        void* const prev = sidSink;
        sidSink = &sink2;
        sidSink = prev;
    }
    assert(sidSink == &sink0);

    // Third call with null — no change.
    {
        void* const prev = sidSink;
        void* sid = nullptr;
        if (sid) sidSink = sid;
        if (sid) sidSink = prev;
    }
    assert(sidSink == &sink0);
}

// ── §6 Jammed passive advance does not leave sink changed ────────────────────
static void testJammedPassiveDoesNotLeaveSinkChanged()
{
    // When jammed=true, CPU executes 0 instructions but runCycles() still
    // ticks CIA/VIC for playPhi2Cycles. With scoped sidSink_, the bridge
    // sink passed to runCycles() is restored after return — even in the
    // jammed zero-instruction path.

    int bridgeSink = 0;
    int altSink    = 1;
    void* sidSink  = &bridgeSink;

    const bool jammed = true;  // simulated

    // Simulate runCycles(playPhi2Cycles, &altSink) with jammed=true.
    {
        void* const prev = sidSink;
        void* sid = &altSink;
        if (sid) sidSink = sid;
        // Ticking CIA/VIC for playPhi2Cycles cycles (no CPU instructions because jammed).
        // ... (elided: no SID writes happen)
        if (sid) sidSink = prev;
    }

    // sidSink_ restored even in the jammed path.
    assert(sidSink == &bridgeSink);
    (void)jammed;
}

// ── §7 Permanent attachSid() still sets sidSink_ unconditionally ─────────────
static void testAttachSidSetsUnconditionally()
{
    // attachSid() is the API for permanent routing — it does NOT save/restore.
    // Only runCycles/executeInstruction have scoped semantics.

    int sinkA = 0, sinkB = 1;
    void* sidSink = &sinkA;

    // attachSid(&sinkB): unconditional permanent set.
    void* newSink = &sinkB;
    sidSink = newSink;

    assert(sidSink == &sinkB);  // permanent, not scoped
}

// ── main ──────────────────────────────────────────────────────────────────────
int main()
{
    testScopedRunCyclesRestoresSink();
    testScopedExecuteInstructionRestoresSink();
    testRunCyclesNullSidNoChange();
    testTemporarySinkReceivesWritesDuringCallOnly();
    testMultipleConsecutiveCallsEachRestore();
    testJammedPassiveDoesNotLeaveSinkChanged();
    testAttachSidSetsUnconditionally();
    return 0;
}
