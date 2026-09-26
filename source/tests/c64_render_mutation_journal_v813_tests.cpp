// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

int main() {
    using namespace ArpSID::C64;

    C64Platform p{};
    p.reset(true);
    p.pokeMemory(0x1000u, 0x12u);
    p.cpu().state().pc = 0x3456u;
    const uint64_t phi2Before = p.phi2Cycle();
    const uint16_t pcBefore = p.cpu().state().pc;

    require(p.beginRenderMutationJournal(), "journal must begin when inactive");
    require(p.renderMutationJournalActive(), "journal must report active after begin");
    require(!p.beginRenderMutationJournal(), "nested journal begin must be refused");

    p.pokeMemory(0x1000u, 0x34u);
    p.pokeMemory(0x1000u, 0x56u); // duplicate dirty cell must still restore original
    p.cpu().state().pc = 0x7777u;
    p.cpuWrite(0x0001u, 0x00u);
    p.runCycles(5u);

    require(p.peekMemory(0x1000u) == 0x56u, "mutation must be visible before rollback");
    require(p.rollbackRenderMutationJournal(), "rollback should succeed without overflow");
    require(!p.renderMutationJournalActive(), "journal must deactivate after rollback");
    require(p.peekMemory(0x1000u) == 0x12u, "rollback must restore first RAM value");
    require(p.cpu().state().pc == pcBefore, "rollback must restore CPU state");
    require(p.phi2Cycle() == phi2Before, "rollback must restore PHI2 cycle");

    require(p.beginRenderMutationJournal(), "journal must begin again after rollback");
    p.pokeMemory(0x1000u, 0x9Au);
    p.cpu().state().pc = 0x9999u;
    p.commitRenderMutationJournal();
    require(!p.renderMutationJournalActive(), "journal must deactivate after commit");
    require(p.peekMemory(0x1000u) == 0x9Au, "commit must preserve RAM mutation");
    require(p.cpu().state().pc == 0x9999u, "commit must preserve CPU mutation");

    std::cout << "C64RenderMutationJournalV813Tests PASS\n";
    return 0;
}
