// param_observer_no_bridge_reentrancy_v757_tests.cpp
//
// audit P0-12 / fix-order #10: the AUParameter observer block must not call back
// into the AU bridge. Observer blocks (tokenByAddingParameterObserver:) can fire on
// the render/automation thread; re-entering the AU there (e.g. getParameterValue)
// risks re-entrancy, priority inversion or blocking. Program/BankSlot must be
// re-read on the deferred MAIN-thread sync (_primeParameterCacheFromBridge), not in
// the callback.
//
// Structural regression guard (a runtime test would need a live AU host firing
// observers off-main): the observer block caches the observed value and defers, and
// never reads through the bridge inside the callback.

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

// Return the brace-balanced block that begins at the first '{' after `needle`.
static std::string blockAfter(const std::string& src, const std::string& needle) {
    const size_t at = src.find(needle);
    require(at != std::string::npos, needle.c_str());
    size_t brace = src.find('{', at);
    require(brace != std::string::npos, "opening brace after needle");
    int depth = 0; size_t i = brace;
    for (; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') { if (--depth == 0) { ++i; break; } }
    }
    return src.substr(brace, i - brace);
}

// Strip // line comments so guards check actual code, not prose that may legitimately
// name a forbidden API.
static std::string stripLineComments(const std::string& s) {
    std::string out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        const size_t c = line.find("//");
        out += (c == std::string::npos ? line : line.substr(0, c));
        out += '\n';
    }
    return out;
}

int main() {
    const std::string mm = readFile("source/au3/ArpSIDViewController.mm");

    // The observer registration and its block (code only — comments may name the
    // forbidden API for documentation).
    const std::string observer =
        stripLineComments(blockAfter(mm, "_tok=[_observedParameterTree tokenByAddingParameterObserver:"));

    // The block MUST NOT call back into the AU bridge.
    require(observer.find("getParameterValue") == std::string::npos,
            "observer block must not call getParameterValue (AU bridge re-entrancy)");
    require(observer.find("[au getParameter") == std::string::npos &&
            observer.find("->_au getParameter") == std::string::npos,
            "observer block must not read parameters through the bridge");

    // It must cache the observed value and defer presentation work.
    require(observer.find("_storeCachedParamValue:pid value:val") != std::string::npos,
            "observer block caches the observed value directly");
    require(observer.find("_needsDeferredPresetUISync") != std::string::npos,
            "observer block defers preset/UI sync to the main thread");

    // Program/BankSlot must remain presentation params (so the deferred main sync
    // re-reads them authoritatively).
    require(observer.find("kParamProgram") != std::string::npos &&
            observer.find("kParamBankSlot") != std::string::npos,
            "Program/BankSlot are still handled (as presentation params) in the observer");

    // The deferred main-thread path is where the authoritative bridge re-read lives.
    const std::string poll = blockAfter(mm, "_needsDeferredPresetUISync.exchange(false");
    require(poll.find("_primeParameterCacheFromBridge") != std::string::npos,
            "deferred main-thread sync re-primes the cache from the bridge");

    std::cout << "ParamObserverNoBridgeReentrancyV757Tests PASS\n";
    return 0;
}
