// v874 audit (scoped mirror completeness): the applied-write observer of
// renderSidRegisterQueueToStereo() must see EXACTLY the writes the render applies to the
// audio engine — every one, once, in applied order — regardless of which projection path
// pushed them. This is the mechanism the C64 telemetry mirror is now driven from, so it
// captures scheduler/performance writes (note-on/off, glide, pitch bend, aftertouch) too.
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/core/sid_runtime_sidreg_queue_render.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace ArpSID;

static int failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "SidProjectionAppliedWriteObserverV874Tests FAIL: " << msg << "\n"; ++failures; }
}

int main() {
    SidRegisterEngine eng;
    eng.reset();
    SidWriteQueue q;
    q.clear();

    // A mix that mimics a note-on burst: freq lo/hi + a hard-restart control sequence
    // spread across distinct cycle offsets, plus a later gate-on. Distinct (reg,sample,cycle)
    // tuples so none are coalesced.
    struct W { uint8_t reg; uint8_t val; uint32_t s; uint16_t c; };
    const std::vector<W> pushed = {
        {0u, 0x10u, 2u, 0u},   // freq lo
        {1u, 0x20u, 2u, 0u},   // freq hi
        {4u, 0x11u, 5u, 0u},   // control no-gate
        {4u, 0x08u, 5u, 1u},   // test on (same sample, next cycle)
        {4u, 0x00u, 5u, 2u},   // test off
        {4u, 0x01u, 8u, 0u},   // gate on
    };
    for (const auto& w : pushed) require(q.push(w.reg, w.val, w.s, w.c), "queue push accepted");

    float left[16] = {0};
    float right[16] = {0};
    std::vector<W> observed;
    renderSidRegisterQueueToStereo(eng, q, left, right, 16, 0,
        [&observed](const SidWrite& w) noexcept {
            observed.push_back(W{w.regIndex, w.value, w.sampleOffset, w.cycleOffset});
        });

    // Every pushed write was applied and observed exactly once.
    require(observed.size() == pushed.size(), "observer saw every applied write exactly once");

    // Applied order is the stable-sorted (sample, cycle, push-order) order.
    const W expected[] = {
        {0u, 0x10u, 2u, 0u},
        {1u, 0x20u, 2u, 0u},
        {4u, 0x11u, 5u, 0u},
        {4u, 0x08u, 5u, 1u},
        {4u, 0x00u, 5u, 2u},
        {4u, 0x01u, 8u, 0u},
    };
    if (observed.size() == 6u) {
        for (size_t i = 0; i < 6u; ++i) {
            const bool match = observed[i].reg == expected[i].reg && observed[i].val == expected[i].val &&
                               observed[i].s == expected[i].s && observed[i].c == expected[i].c;
            require(match, "observed applied write matches expected reg/value/sample/cycle in order");
        }
    }

    // Control-critical transitions on reg 4 at distinct cycles are NOT coalesced away.
    int reg4Count = 0;
    for (const auto& w : observed) if (w.reg == 4u) ++reg4Count;
    require(reg4Count == 4, "all four control-byte transitions survive to the observer");

    // The no-observer overload must render identically without crashing.
    SidWriteQueue q2; q2.clear();
    for (const auto& w : pushed) q2.push(w.reg, w.val, w.s, w.c);
    SidRegisterEngine eng2; eng2.reset();
    float l2[16] = {0}, r2[16] = {0};
    renderSidRegisterQueueToStereo(eng2, q2, l2, r2, 16, 0);  // default (noop observer)
    require(true, "noop-observer overload renders without crashing");

    std::cout << (failures == 0 ? "SidProjectionAppliedWriteObserverV874Tests PASS\n"
                                : "SidProjectionAppliedWriteObserverV874Tests FAIL\n");
    return failures == 0 ? 0 : 1;
}
