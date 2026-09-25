// v894 unsync closure tests.
//
// SidWrite.sampleOffset is BLOCK-LOCAL. Writes scheduled past the current
// block's end (pushSynthModeWriteDelayedByCycles hard-restart re-gates /
// delayed TEST releases from a block-tail note-on) survive the render's
// eraseIf() but kept their old-block offsets: in the next block they fired a
// full block late, or NEVER at typical block sizes (the local sample index
// never reaches them), leaving a dead gate / TEST-muted voice and a stale
// queue entry. SidWriteQueue::rebaseAfterBlock() shifts survivors into the
// next block's timeline.
//
// v897 UPDATE: the per-block call from processBlockInto() was REVERTED — the
// fractional sub-span render path gives pending writes a longer-than-one-block
// lifetime, and rebasing them every block corrupted live synth/projection
// write timing. The method itself is retained (pinned here) for a future fix
// co-located with the actual queue consumer.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "arpsid/core/sid_runtime_sidreg_queue_render.h"
#include "arpsid/engines/sid_register_engine.h"

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "sid_write_queue_rebase_v894_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

void beyondBlockWritesAreRebasedAndConsumedNextBlock() {
    using namespace ArpSID;
    SIDVoice::initTablesOnce();
    SidRegisterEngine engine;
    engine.prepare(44100.0);

    SidWriteQueue q;
    q.clear();
    q.push(0x00u, 0x11u, 3u);    // in-block
    q.push(0x01u, 0x22u, 511u);  // last sample of a 512 block
    q.push(0x04u, 0x41u, 513u);  // beyond block: e.g. hard-restart re-gate
    q.push(0x00u, 0x33u, 700u);  // beyond block

    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    renderSidRegisterQueueToStereo(engine, q, l.data(), r.data(), 512);
    require(q.size() == 2u, "in-block writes consumed; beyond-block writes survive");
    require(q.data()[0].sampleOffset == 513u && q.data()[1].sampleOffset == 700u,
            "surviving writes still carry old-block offsets before rebase");

    // Without rebase these would NEVER fire in 512-frame blocks. Rebase shifts
    // them into the next block's timeline.
    q.rebaseAfterBlock(512u);
    require(q.data()[0].sampleOffset == 1u && q.data()[1].sampleOffset == 188u,
            "rebase shifts surviving offsets by exactly the block length");

    renderSidRegisterQueueToStereo(engine, q, l.data(), r.data(), 512);
    require(q.size() == 0u,
            "rebased writes are consumed in the following block (they used to be stuck forever)");
}

void rebaseClampsLateLeftoversToBlockStart() {
    using namespace ArpSID;
    SidWriteQueue q;
    q.clear();
    q.push(0x02u, 0x10u, 5u);    // pathological leftover below block length
    q.push(0x03u, 0x20u, 600u);
    q.rebaseAfterBlock(512u);
    q.sortStable();
    require(q.data()[0].sampleOffset == 0u,
            "leftover below the block length clamps to sample 0 (fires immediately, never lost)");
    require(q.data()[1].sampleOffset == 88u, "beyond-block offset shifts by block length");
}

void rebaseIsIdentityForEmptyOrZeroFrames() {
    using namespace ArpSID;
    SidWriteQueue q;
    q.clear();
    q.rebaseAfterBlock(512u); // empty: no-op, no crash
    q.push(0x00u, 0x01u, 10u);
    q.rebaseAfterBlock(0u);   // zero frames: no-op
    require(q.data()[0].sampleOffset == 10u, "zero-frame rebase leaves offsets untouched");
}

} // namespace

int main() {
    beyondBlockWritesAreRebasedAndConsumedNextBlock();
    rebaseClampsLateLeftoversToBlockStart();
    rebaseIsIdentityForEmptyOrZeroFrames();
    std::cout << "sid_write_queue_rebase_v894_tests PASS\n";
    return 0;
}
