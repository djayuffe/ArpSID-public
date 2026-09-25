// v874 audit P0-1/P0-2/P1-8: a legally configured extra SID at $DE00-$DFE0 must be
// routed by the runtime bus (writes reach the SID sink as the correct chip/reg), and
// the runtime SID-base validator must share the parser's legal-range rule.
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/psid_header.h"

#include <cstdio>
#include <cstdlib>

using namespace ArpSID::C64;

static int failures = 0;
static void require(bool v, const char* msg) {
    if (!v) { std::fprintf(stderr, "c64_extra_sid_de00_routing_v874_tests FAIL: %s\n", msg); ++failures; }
}

int main() {
    // ---- P0-2: runtime validator shares the parser's legal-range rule ----
    {
        C64Platform p{}; p.reset(true);
        const uint16_t good1[2] = {0xD400u, 0xD420u};
        const uint16_t good2[2] = {0xD400u, 0xDE00u};
        const uint16_t good3[2] = {0xD400u, 0xDFE0u};
        const uint16_t bad_color[2] = {0xD400u, 0xD800u}; // color RAM
        const uint16_t bad_cia1[2]  = {0xD400u, 0xDC00u}; // CIA1
        const uint16_t bad_cia2[2]  = {0xD400u, 0xDD00u}; // CIA2
        const uint16_t bad_chip0[1] = {0xD420u};          // primary must be $D400
        require(p.configurePsidSidBases(good1, 2u), "$D420 extra SID accepted");
        require(p.configurePsidSidBases(good2, 2u), "$DE00 extra SID accepted");
        require(p.configurePsidSidBases(good3, 2u), "$DFE0 extra SID accepted");
        require(!p.configurePsidSidBases(bad_color, 2u), "$D800 (color RAM) rejected as SID base");
        require(!p.configurePsidSidBases(bad_cia1, 2u),  "$DC00 (CIA1) rejected as SID base");
        require(!p.configurePsidSidBases(bad_cia2, 2u),  "$DD00 (CIA2) rejected as SID base");
        require(!p.configurePsidSidBases(bad_chip0, 1u), "primary SID must be $D400");
        // Shared validator agrees at the source-of-truth level.
        require(ArpSID::psidValidConfiguredSidBase(0xD400u, 0u), "validator: $D400 chip0 ok");
        require(!ArpSID::psidValidConfiguredSidBase(0xD420u, 0u), "validator: $D420 not valid for chip0");
        require(ArpSID::psidValidConfiguredSidBase(0xDE00u, 1u), "validator: $DE00 chip1 ok");
        require(!ArpSID::psidValidConfiguredSidBase(0xDC00u, 1u), "validator: $DC00 not valid extra base");
    }

    // ---- P0-1: $DE00 secondary SID is not routed until configured ----
    {
        C64Platform p{}; p.reset(true);
        C64SidBridgeState bridge{}; p.attachSid(&bridge);
        p.cpuWrite(0xDE18u, 0x0Fu);
        require(bridge.writeCount == 0u, "$DE18 is unmapped IO before SID2 is configured");
    }

    // ---- P0-1: configured $DE00 secondary SID routes writes to chip 1 ----
    {
        C64Platform p{}; p.reset(true);
        C64SidBridgeState bridge{}; p.attachSid(&bridge);
        const uint16_t bases[2] = {0xD400u, 0xDE00u};
        require(p.configurePsidSidBases(bases, 2u), "configure SID2 at $DE00");

        p.cpuWrite(0xDE18u, 0x0Fu); // $DE00 + $18 -> chip1 volume/filter-mode reg
        require(bridge.writeCount == 1u, "$DE18 reaches SID bridge");
        require(bridge.lastChip == 1u,  "$DE18 routes to chip 1");
        require(bridge.lastReg == 0x18u, "$DE18 maps to SID register $18");

        // A frequency-lo write into the same secondary window routes too.
        p.cpuWrite(0xDE00u, 0x42u);     // chip1 reg $00
        require(bridge.lastChip == 1u && bridge.lastReg == 0x00u, "$DE00 routes to chip1 reg $00");

        // The broadened guard must NOT swallow neighbouring CIA writes.
        const uint32_t beforeCia = bridge.writeCount;
        p.cpuWrite(0xDC18u, 0x55u);     // CIA1 area, not a configured SID window
        require(bridge.writeCount == beforeCia, "$DC18 (CIA1) does not leak into SID routing");
    }

    // ---- P0-1: top-of-range $DFE0 secondary SID routes ----
    {
        C64Platform p{}; p.reset(true);
        C64SidBridgeState bridge{}; p.attachSid(&bridge);
        const uint16_t bases[2] = {0xD400u, 0xDFE0u};
        require(p.configurePsidSidBases(bases, 2u), "configure SID2 at $DFE0");
        p.cpuWrite(0xDFF8u, 0x0Fu);     // $DFE0 + $18
        require(bridge.writeCount == 1u && bridge.lastChip == 1u && bridge.lastReg == 0x18u,
                "$DFF8 routes to chip1 reg $18 for a $DFE0 secondary SID");
    }

    if (failures == 0) std::puts("c64_extra_sid_de00_routing_v874_tests: PASS");
    return failures == 0 ? 0 : 1;
}
