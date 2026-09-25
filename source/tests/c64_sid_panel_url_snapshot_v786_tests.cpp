#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string src = readFile(root + "/source/au3/ArpSIDViewController.mm");

    require(contains(src, "fix-order #40" ) || contains(src, "sidURL = [op.URL copy]") || contains(src, "NSURL* sidURL = [op.URL copy];"),
            "C64 SID load handler must snapshot op.URL before file IO/status use");
    require(contains(src, "NSString* sidFileName = [sidURL.lastPathComponent copy];"),
            "C64 SID load handler must snapshot display filename from the URL snapshot");
    require(contains(src, "NSData*data=[NSData dataWithContentsOfURL:sidURL options:0 error:nil];"),
            "C64 SID load handler must load from sidURL snapshot, not op.URL");
    require(contains(src, "load accepted %@ subtune %u/%u\", sidFileName"),
            "C64 SID debug/status text must use sidFileName snapshot");
    require(!contains(src, "dataWithContentsOfURL:op.URL"),
            "C64 SID load path must not perform IO against panel URL directly");
    require(!contains(src, "op.URL.lastPathComponent"),
            "C64 SID load path must not read panel URL lastPathComponent directly after completion");

    std::cout << "PASS: C64 SID panel completion snapshots URL/name before load/status\n";
    return 0;
}
