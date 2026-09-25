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

static void requireAbsent(const std::string& hay, const std::string& needle, const char* msg) {
    require(hay.find(needle) == std::string::npos, msg);
}

int main() {
    const std::string adapter = stripComments(readFile("source/au3/ArpSIDDSPKernelAdapter.mm"));
    const std::string cmake = readFile("CMakeLists.txt");

    requireContains(adapter, "std::unique_ptr<ArpSID::ArpSIDDSPKernel> _kernel;",
                    "DSP kernel adapter must use unique_ptr ownership");
    requireContains(adapter, "_kernel = std::make_unique<ArpSID::ArpSIDDSPKernel>();",
                    "DSP kernel adapter must construct with make_unique");
    requireContains(adapter, "return _kernel->loadPsidData(bytes,",
                    "PSID loading must use the synchronous load path");
    requireContains(adapter, "static_cast<size_t>(length),",
                    "PSID loading must preserve the input byte length");
    requireContains(adapter, "static_cast<uint16_t>(subtune)) ? YES : NO;",
                    "PSID loading must preserve the selected subtune");

    requireAbsent(adapter, "std::shared_ptr<ArpSID::ArpSIDDSPKernel> _kernel",
                  "DSP kernel adapter must not use shared_ptr kernel ownership");
    requireAbsent(adapter, "std::make_shared<ArpSID::ArpSIDDSPKernel>",
                  "DSP kernel adapter must not construct the kernel with make_shared");
    requireAbsent(adapter, "std::allocate_shared<ArpSID::ArpSIDDSPKernel>",
                  "DSP kernel adapter must not construct the kernel with allocate_shared");
    requireAbsent(adapter, "std::shared_ptr<ArpSID::ArpSIDDSPKernel> kernel = _kernel",
                  "PSID loader must not create a local shared kernel capture");
    requireAbsent(adapter, "loadPsidDataForRequest(payload->data(), payload->size(), requestedSubtune, requestTicket)",
                  "PSID loader must not use async request-ticket loading");
    requireAbsent(adapter, "dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{",
                  "PSID loader must not dispatch an async kernel-capturing block");

    requireContains(cmake, "DspKernelAllocationShapeV842Tests",
                    "v842 allocation-shape guard must be registered in CMake");

    std::cout << "DspKernelAllocationShapeV842Tests PASS\n";
    return 0;
}
