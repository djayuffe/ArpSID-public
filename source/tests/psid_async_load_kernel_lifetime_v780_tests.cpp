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
    const std::string src = stripComments(readFile("source/au3/ArpSIDDSPKernelAdapter.mm"));
    const std::string cmake = readFile("CMakeLists.txt");

    requireContains(src, "std::unique_ptr<ArpSID::ArpSIDDSPKernel> _kernel;",
                    "adapter kernel must keep the known-good single-owner lifetime");
    requireContains(src, "_kernel = std::make_unique<ArpSID::ArpSIDDSPKernel>();",
                    "adapter must construct the DSP kernel with make_unique");
    requireContains(src, "return _kernel->loadPsidData(bytes,",
                    "PSID load must use the synchronous known-good load path");
    requireContains(src, "static_cast<size_t>(length),",
                    "synchronous PSID load must pass the byte length");
    requireContains(src, "static_cast<uint16_t>(subtune)) ? YES : NO;",
                    "synchronous PSID load must pass the selected subtune");

    require(src.find("std::shared_ptr<ArpSID::ArpSIDDSPKernel> _kernel") == std::string::npos,
            "adapter must not store the DSP kernel with shared ownership");
    require(src.find("std::make_shared<ArpSID::ArpSIDDSPKernel>") == std::string::npos,
            "adapter must not construct the DSP kernel with make_shared");
    require(src.find("std::allocate_shared<ArpSID::ArpSIDDSPKernel>") == std::string::npos,
            "adapter must not construct the DSP kernel with allocate_shared");
    require(src.find("std::shared_ptr<ArpSID::ArpSIDDSPKernel> kernel = _kernel") == std::string::npos,
            "PSID loader must not create a local shared kernel capture");
    require(src.find("loadPsidDataForRequest(payload->data(), payload->size(), requestedSubtune, requestTicket)") == std::string::npos,
            "PSID loader must not use the async request-ticket path");
    require(src.find("dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{") == std::string::npos,
            "PSID loader must not dispatch an async block that captures kernel lifetime");

    requireContains(cmake, "PsidAsyncLoadKernelLifetimeV780Tests", "v780 guard registered in CMake");

    std::cout << "PsidAsyncLoadKernelLifetimeV780Tests PASS\n";
    return 0;
}
