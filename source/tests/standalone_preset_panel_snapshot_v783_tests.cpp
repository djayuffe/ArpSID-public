#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(in.good(), "could not open source file");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string sliceBetween(const std::string& s, const std::string& begin, const std::string& end) {
    const auto b = s.find(begin);
    require(b != std::string::npos, "begin marker missing");
    const auto e = s.find(end, b + begin.size());
    require(e != std::string::npos, "end marker missing");
    return s.substr(b, e - b);
}

int main() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string src = readFile(std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDHostAppDelegate.mm");

    const std::string exportFn = sliceBetween(src, "- (void)_menuExportPreset:(id)sender", "- (void)_menuImportPreset:(id)sender");
    require(exportFn.find("__weak ArpSIDHostAppDelegate* weakSelf = self;") != std::string::npos,
            "export panel completion must weak-capture standalone delegate");
    require(exportFn.find("NSURL* exportURL = (r == NSModalResponseOK) ? [sp.URL copy] : nil;") != std::string::npos,
            "export panel must snapshot sp.URL exactly once in the completion handler");
    require(exportFn.find("NSString* exportExtension = [exportURL.pathExtension.lowercaseString copy];") != std::string::npos,
            "export panel must snapshot extension from immutable URL snapshot");
    require(exportFn.find("ArpSIDHostAppDelegate* strongSelf = weakSelf;") != std::string::npos,
            "export path must weak-load delegate before reading AU state");
    require(exportFn.find("[self->_audioUnit") == std::string::npos,
            "export path must not retain/dereference self inside completion block");
    require(exportFn.find("sp.URL.pathExtension") == std::string::npos,
            "export path must not dereference panel URL extension after snapshot");
    require(exportFn.find("writeToURL:sp.URL") == std::string::npos,
            "export path must write to the URL snapshot, not the panel URL");

    const std::string importFn = sliceBetween(src, "- (void)_menuImportPreset:(id)sender", "- (void)_menuReset:(id)sender");
    require(importFn.find("__weak ArpSIDHostAppDelegate* weakSelf = self;") != std::string::npos,
            "import panel completion must weak-capture standalone delegate");
    require(importFn.find("NSURL* importURL = (r == NSModalResponseOK) ? [op.URL copy] : nil;") != std::string::npos,
            "import panel must snapshot op.URL exactly once in the completion handler");
    require(importFn.find("NSData* data = [NSData dataWithContentsOfURL:importURL];") != std::string::npos,
            "import path must read from immutable URL snapshot");
    require(importFn.find("ArpSIDHostAppDelegate* strongSelf = weakSelf;") != std::string::npos,
            "import path must weak-load delegate before publishing AU state");
    require(importFn.find("[self->_audioUnit") == std::string::npos,
            "import path must not retain/dereference self inside completion block");
    require(importFn.find("dataWithContentsOfURL:op.URL") == std::string::npos,
            "import path must not read directly from panel URL");

    std::cout << "StandalonePresetPanelSnapshotV783Tests PASS\n";
    return 0;
}
