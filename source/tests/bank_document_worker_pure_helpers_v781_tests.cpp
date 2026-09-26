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

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    require(hay.find(needle) != std::string::npos, msg);
}

static std::string sectionFrom(const std::string& hay, const std::string& start, const std::string& end) {
    const auto a = hay.find(start);
    require(a != std::string::npos, "section start exists");
    const auto b = hay.find(end, a + start.size());
    require(b != std::string::npos, "section end exists");
    return hay.substr(a, b - a);
}

int main() {
    const std::string code = stripComments(readFile("source/au3/ArpSIDViewController.mm"));
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");

    requireContains(code, "ArpSIDSaveBankDocumentToURL_v781(", "pure bank save helper exists");
    requireContains(code, "ArpSIDLoadBankDocumentFromURL_v781(", "pure bank load helper exists");
    requireContains(code, "return ArpSIDSaveBankDocumentToURL_v781(url, patches, metas, displayName);",
                    "ObjC save method delegates to pure helper for compatibility");
    requireContains(code, "return ArpSIDLoadBankDocumentFromURL_v781(url, patches, metas);",
                    "ObjC load method delegates to pure helper for compatibility");

    const std::string importJson = sectionFrom(code, "-(void)_bankImportJSON:(id)sender", "-(void)_bankLoadBank:(id)sender");
    const std::string loadBank = sectionFrom(code, "-(void)_bankLoadBank:(id)sender", "-(void)_bankExportAll:(id)sender");
    const std::string drumExport = sectionFrom(code, "-(void)_drumExportUserKitBank:(id)sender", "-(void)_drumImportUserKitBank:(id)sender");
    const std::string drumImport = sectionFrom(code, "-(void)_drumImportUserKitBank:(id)sender", "-(void)_drumOpenBank:(id)sender");

    requireContains(importJson, "const auto err=ArpSIDLoadBankDocumentFromURL_v781(bankURL, patches, metas);",
                    "JSON bank import worker uses pure load helper");
    require(importJson.find("[s _loadBankDocumentFromURL:op.URL") == std::string::npos,
            "JSON bank import worker must not call controller load helper off-main");
    requireContains(loadBank, "? ArpSIDLoadBankDocumentFromURL_v781(bankURL, patches, metas)",
                    "generic bank load JSON path uses pure load helper");
    require(loadBank.find("[s _loadBankDocumentFromURL:op.URL") == std::string::npos,
            "generic bank load worker must not call controller load helper off-main");
    requireContains(drumExport, "ArpSIDSaveBankDocumentToURL_v781(exportURL, patches, metas, @\"DrSID User Kits\")",
                    "DrSID user-kit export worker uses pure save helper");
    requireContains(drumImport, "const auto err=ArpSIDLoadBankDocumentFromURL_v781(importURL, patches, metas);",
                    "DrSID user-kit import worker uses pure load helper");

    requireContains(cmake, "BankDocumentWorkerPureHelpersV781Tests", "v781 guard registered in CMake");
    requireContains(audit, "Fix-order #35", "audit records fix-order #35");
    requireContains(audit, "bank document worker", "audit documents bank worker pure-helper fix");

    std::cout << "BankDocumentWorkerPureHelpersV781Tests PASS\n";
    return 0;
}
