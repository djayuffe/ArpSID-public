// bank_status_mainqueue_weak_continuation_v789_tests.cpp
//
// fix-order #43: synchronous bank/preset save/export handlers should not queue
// main-thread status continuations that retain the editor/controller via a strong
// local `s`. The handler already owns a zeroing weak controller token (`ws`); any
// delayed main-queue status publish must weak-load from `ws` on main and bail if
// the editor has been torn down.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(static_cast<bool>(f), rel);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string mm = readFile("source/au3/ArpSIDViewController.mm");

    require(mm.find("ArpSIDViewController* __weak ws=self;") != std::string::npos,
            "bank handlers use a zeroing weak controller token");

    require(mm.find("dispatch_async(dispatch_get_main_queue(),^{ [s _bankSetStatus:msg panel:s->_pBank]; });") == std::string::npos,
            "bank status continuations must not retain the handler strong local `s`");

    const std::string weakStatus =
        "dispatch_async(dispatch_get_main_queue(),^{\n"
        "            ArpSIDViewController* ss = ws; if(!ss) return;\n"
        "            [ss _bankSetStatus:msg panel:ss->_pBank];\n"
        "        });";
    size_t count = 0;
    for (size_t pos = mm.find(weakStatus); pos != std::string::npos; pos = mm.find(weakStatus, pos + 1)) {
        ++count;
    }
    require(count >= 6, "all synchronous bank save/export status continuations weak-load on main");

    require(mm.find("[ss _bankSetStatus:msg panel:ss->_pBank]") != std::string::npos,
            "status publish still targets the bank panel when the controller survives");

    std::cout << "BankStatusMainQueueWeakContinuationV789Tests PASS\n";
    return 0;
}
