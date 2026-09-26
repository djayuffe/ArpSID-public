// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drsid_engine.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    ArpSID::DrSidEngine drsid;
    drsid.setSampleRate(48000.0);
    drsid.allocateDrumVoice(36, 100);
    require(drsid.drumVoiceAllocator().activeCount() > 0, "allocator active before panic");
    drsid.allNotesOff();
    require(drsid.drumVoiceAllocator().activeCount() == 0, "allNotesOff resets diagnostic allocator");
    std::cout << "DrSidAllNotesOffAllocatorResetV656Tests PASS\n";
    return 0;
}
