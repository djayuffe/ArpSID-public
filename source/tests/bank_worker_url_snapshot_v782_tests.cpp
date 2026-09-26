// Copyright (C) 2024-2026 Ulf Bertilsson
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
static void require(bool ok, const char* msg) { if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); } }
static void requireContains(const std::string& h, const std::string& n, const char* msg) { require(h.find(n)!=std::string::npos, msg); }
static std::string sectionFrom(const std::string& hay, const std::string& start, const std::string& end) {
    const auto a=hay.find(start); require(a!=std::string::npos, "section start exists");
    const auto b=hay.find(end, a+start.size()); require(b!=std::string::npos, "section end exists");
    return hay.substr(a, b-a);
}
static void forbidPanelURLInAsyncBody(const std::string& section, const char* name) {
    const auto d=section.find("dispatch_async(dispatch_get_global_queue");
    require(d!=std::string::npos, name);
    const std::string worker=section.substr(d);
    require(worker.find("op.URL") == std::string::npos, "worker/main continuation must not dereference NSOpenPanel op.URL");
    require(worker.find("sp.URL") == std::string::npos, "worker/main continuation must not dereference NSSavePanel sp.URL");
}
int main() {
    const std::string code=stripComments(readFile("source/au3/ArpSIDViewController.mm"));
    const std::string cmake=readFile("CMakeLists.txt");

    const std::string importJson=sectionFrom(code, "-(void)_bankImportJSON:(id)sender", "-(void)_bankLoadBank:(id)sender");
    requireContains(importJson, "NSURL* bankURL = [op.URL copy];", "JSON bank import snapshots URL before worker dispatch");
    requireContains(importJson, "NSString* bankFileName = [bankURL.lastPathComponent copy];", "JSON bank import snapshots display name");
    requireContains(importJson, "ArpSIDLoadBankDocumentFromURL_v781(bankURL, patches, metas)", "JSON worker uses copied URL");
    forbidPanelURLInAsyncBody(importJson, "JSON import has worker dispatch");

    const std::string loadBank=sectionFrom(code, "-(void)_bankLoadBank:(id)sender", "-(void)_bankExportAll:(id)sender");
    requireContains(loadBank, "NSURL* bankURL = [op.URL copy];", "generic bank load snapshots URL before worker dispatch");
    requireContains(loadBank, "NSString* bankPath = [bankURL.path copy];", "generic bank load snapshots path");
    requireContains(loadBank, "NSString* bankFileName = [bankURL.lastPathComponent copy];", "generic bank load snapshots display name");
    requireContains(loadBank, "ArpSIDLoadBankDocumentFromURL_v781(bankURL, patches, metas)", "generic JSON load uses copied URL");
    forbidPanelURLInAsyncBody(loadBank, "generic bank load has worker dispatch");

    const std::string drumExport=sectionFrom(code, "-(void)_drumExportUserKitBank:(id)sender", "-(void)_drumImportUserKitBank:(id)sender");
    requireContains(drumExport, "NSURL* exportURL = [sp.URL copy];", "DrSID kit export snapshots save URL");
    requireContains(drumExport, "NSString* exportFileName = [exportURL.lastPathComponent copy];", "DrSID kit export snapshots display name");
    requireContains(drumExport, "ArpSIDSaveBankDocumentToURL_v781(exportURL, patches, metas, @\"DrSID User Kits\")", "DrSID kit export worker uses copied URL");
    forbidPanelURLInAsyncBody(drumExport, "DrSID kit export has worker dispatch");

    const std::string drumImport=sectionFrom(code, "-(void)_drumImportUserKitBank:(id)sender", "-(void)_drumOpenBank:(id)sender");
    requireContains(drumImport, "NSURL* importURL = [op.URL copy];", "DrSID kit import snapshots URL");
    requireContains(drumImport, "NSString* importFileName = [importURL.lastPathComponent copy];", "DrSID kit import snapshots display name");
    requireContains(drumImport, "ArpSIDLoadBankDocumentFromURL_v781(importURL, patches, metas)", "DrSID kit import worker uses copied URL");
    forbidPanelURLInAsyncBody(drumImport, "DrSID kit import has worker dispatch");

    requireContains(cmake, "BankWorkerURLSnapshotV782Tests", "v782 guard registered");
    std::cout << "BankWorkerURLSnapshotV782Tests PASS\n";
    return 0;
}
