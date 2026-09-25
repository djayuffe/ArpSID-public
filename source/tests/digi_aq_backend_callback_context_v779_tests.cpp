#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!in) { std::cerr << "missing file: " << rel << "\n"; std::exit(1); }
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

static std::string stripComments(const std::string& s) {
    std::string out; out.reserve(s.size());
    bool line=false, block=false, str=false, chr=false, esc=false;
    for (size_t i=0;i<s.size();++i) {
        const char c=s[i]; const char n=(i+1<s.size())?s[i+1]:'\0';
        if (line) { if (c=='\n') { line=false; out.push_back(c); } continue; }
        if (block) { if (c=='*' && n=='/') { block=false; ++i; } continue; }
        if (!str && !chr && c=='/' && n=='/') { line=true; ++i; continue; }
        if (!str && !chr && c=='/' && n=='*') { block=true; ++i; continue; }
        out.push_back(c);
        if (esc) { esc=false; continue; }
        if (c=='\\' && (str||chr)) { esc=true; continue; }
        if (!chr && c=='"') str=!str; else if (!str && c=='\'') chr=!chr;
    }
    return out;
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    require(hay.find(needle) != std::string::npos, msg);
}

int main() {
    const std::string hdr = stripComments(readFile("source/au3/ArpSIDDigiAudioQueueCapture.h"));
    const std::string src = stripComments(readFile("source/au3/ArpSIDDigiAudioQueueCapture.mm"));
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");

    requireContains(hdr, "struct AQCallbackContext", "backend callback context type exists");
    requireContains(hdr, "std::atomic<AQCapture*> owner{nullptr};", "context owns atomic owner pointer");
    requireContains(hdr, "std::atomic<uintptr_t> activeQueuePtr{0};", "context mirrors active queue pointer");
    requireContains(hdr, "std::atomic<bool> stopping{true};", "context exposes stopping gate");
    requireContains(hdr, "std::atomic<uint32_t> callbacksInFlight_{0};", "context owns in-flight count before owner resolution");
    requireContains(hdr, "AQCallbackContext callbackContext_{};", "AQCapture owns one stable callback context");

    requireContains(src, "AudioQueueNewInput(&asbd_, &AQCapture::inputCallback, &callbackContext_", "AudioQueue input receives callback context, not raw this");
    requireContains(src, "AudioQueueAddPropertyListener(queue_, kAudioQueueProperty_IsRunning, &AQCapture::propertyListener, &callbackContext_)", "property listener receives callback context, not raw this");
    requireContains(src, "AudioQueueRemovePropertyListener(queue_, kAudioQueueProperty_IsRunning, &AQCapture::propertyListener, &callbackContext_)", "property listener removal uses the same context pointer");
    requireContains(src, "auto* ctx = static_cast<AQCallbackContext*>(userData);", "callbacks cast userData to context");
    requireContains(src, "ctx->callbacksInFlight_.fetch_add(1u", "callbacks increment in-flight before resolving owner");
    requireContains(src, "AQCapture* self = ctx->owner.load(std::memory_order_acquire);", "callbacks acquire-load owner from context");
    requireContains(src, "if (!self) return;", "callbacks nil-guard owner");
    requireContains(src, "callbackContext_.owner.store(nullptr, std::memory_order_release);", "stop invalidates owner before disposal");
    requireContains(src, "callbackContext_.callbacksInFlight_.load", "stop waits on context-owned in-flight counter");

    require(src.find("AudioQueueNewInput(&asbd_, &AQCapture::inputCallback, this") == std::string::npos,
            "AudioQueueNewInput must not receive raw this");
    require(src.find("AudioQueueAddPropertyListener(queue_, kAudioQueueProperty_IsRunning, &AQCapture::propertyListener, this") == std::string::npos,
            "AudioQueueAddPropertyListener must not receive raw this");
    require(src.find("AudioQueueRemovePropertyListener(queue_, kAudioQueueProperty_IsRunning, &AQCapture::propertyListener, this") == std::string::npos,
            "AudioQueueRemovePropertyListener must not receive raw this");
    require(src.find("auto* self = static_cast<AQCapture*>(userData);") == std::string::npos,
            "static callbacks must not cast userData directly to AQCapture");

    requireContains(cmake, "DigiAQBackendCallbackContextV779Tests", "v779 guard registered in CMake");
    requireContains(audit, "Fix-order #33", "audit records fix-order #33");
    requireContains(audit, "AQCapture backend", "audit documents backend context lifetime fix");

    std::cout << "DigiAQBackendCallbackContextV779Tests PASS\n";
    return 0;
}
