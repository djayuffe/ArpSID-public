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

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string sliceBetween(const std::string& s, const std::string& begin, const std::string& end) {
    const auto b = s.find(begin);
    require(b != std::string::npos, "begin marker missing");
    const auto e = s.find(end, b + begin.size());
    require(e != std::string::npos, "end marker missing");
    return s.substr(b, e - b);
}

static void requireContains(const std::string& s, const std::string& needle, const char* msg) {
    require(s.find(needle) != std::string::npos, msg);
}

static void requireAbsent(const std::string& s, const std::string& needle, const char* msg) {
    require(s.find(needle) == std::string::npos, msg);
}

int main() {
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");

    const std::string bankPatch = sliceBetween(vc, "-(void)_bankSavePatch:(id)sender", "-(void)_bankLoadPatch:(id)sender");
    requireContains(bankPatch, "NSURL* patchURL = [sp.URL copy];", "patch save snapshots save-panel URL");
    requireContains(bankPatch, "NSString* patchFileName = [patchURL.lastPathComponent copy];", "patch save snapshots filename");
    requireContains(bankPatch, "NSString* patchStem = [patchURL.URLByDeletingPathExtension.lastPathComponent copy];", "patch save snapshots stem");
    requireContains(bankPatch, "name:patchStem", "patch save uses stem snapshot for metadata");
    requireContains(bankPatch, "_savePatchDocumentToURL:patchURL", "patch save uses URL snapshot for IO");
    requireAbsent(bankPatch, "sp.URL.lastPathComponent", "patch save must not use panel URL for status filename after snapshot");
    requireAbsent(bankPatch, "sp.URL.URLByDeletingPathExtension", "patch save must not use panel URL for metadata stem after snapshot");

    const std::string bankLoadPatch = sliceBetween(vc, "-(void)_bankLoadPatch:(id)sender", "-(void)_bankSaveBank:(id)sender");
    requireContains(bankLoadPatch, "NSURL* patchURL = [op.URL copy];", "patch load snapshots open-panel URL");
    requireContains(bankLoadPatch, "NSString* patchFileName = [patchURL.lastPathComponent copy];", "patch load snapshots filename");
    requireContains(bankLoadPatch, "_loadPatchDocumentFromURL:patchURL", "patch load uses URL snapshot for IO");
    requireAbsent(bankLoadPatch, "op.URL.lastPathComponent", "patch load must not use panel URL for status filename after snapshot");

    const std::string bankSaveBank = sliceBetween(vc, "-(void)_bankSaveBank:(id)sender", "-(void)_bankImportJSON:(id)sender");
    requireContains(bankSaveBank, "NSURL* bankURL = [sp.URL copy];", "bank save snapshots save-panel URL");
    requireContains(bankSaveBank, "NSString* bankFileName = [bankURL.lastPathComponent copy];", "bank save snapshots filename");
    requireContains(bankSaveBank, "ArpSIDSaveBankDocumentToURL_v781(bankURL", "bank save uses URL snapshot for IO");
    requireAbsent(bankSaveBank, "sp.URL.lastPathComponent", "bank save must not use panel URL for status filename after snapshot");

    const std::string bankExportAll = sliceBetween(vc, "-(void)_bankExportAll:(id)sender", "-(void)_bankLoadFactory:(id)sender");
    requireContains(bankExportAll, "NSURL* exportURL = [op.URL copy];", "export-all snapshots directory URL");
    requireContains(bankExportAll, "NSString* exportPath = [exportURL.path copy];", "export-all snapshots path");
    requireContains(bankExportAll, "NSString* exportDirName = [exportURL.lastPathComponent copy];", "export-all snapshots directory name");
    requireContains(bankExportAll, "exportAllToDirectory(exportPath.UTF8String", "export-all uses path snapshot for IO");
    requireAbsent(bankExportAll, "op.URL.lastPathComponent", "export-all must not use panel URL for status filename after snapshot");
    requireAbsent(bankExportAll, "op.URL.path.UTF8String", "export-all must not use panel URL path directly after snapshot");

    const std::string drumSave = sliceBetween(vc, "-(void)_drumSaveUserKit:(id)sender", "-(void)_drumRefreshUserKitLibrary:(id)sender");
    requireContains(drumSave, "NSURL* kitURL = [sp.URL copy];", "DrSID kit save snapshots save-panel URL");
    requireContains(drumSave, "NSString* kitFileName = [kitURL.lastPathComponent copy];", "DrSID kit save snapshots filename");
    requireContains(drumSave, "NSString* patchStem = [kitURL.URLByDeletingPathExtension.lastPathComponent copy];", "DrSID kit save snapshots stem");
    // v968: the drum save routes through the drum-normalized _saveDrumKitDocumentToURL
    // (forces DrSID render mode into the saved root). It still uses the snapshotted
    // kitURL for IO, which is the contract this guard protects.
    requireContains(drumSave, "_saveDrumKitDocumentToURL:kitURL", "DrSID kit save uses URL snapshot for IO");
    requireContains(drumSave, "s->_loadedDrumUserKitURL=kitURL;", "DrSID kit save publishes URL snapshot");
    requireAbsent(drumSave, "sp.URL.lastPathComponent", "DrSID kit save must not use panel URL filename after snapshot");
    requireAbsent(drumSave, "sp.URL.path", "DrSID kit save must not use panel URL path after snapshot");

    requireContains(cmake, "BankPanelCompletionURLSnapshotV785Tests", "v785 guard registered in CMake");
    require(audit.find("fix-order #39") != std::string::npos || audit.find("Fix-order #39") != std::string::npos, "audit records fix-order #39");
    requireContains(audit, "bank/preset panel completion URL snapshots", "audit describes panel URL snapshot hardening");

    std::cout << "BankPanelCompletionURLSnapshotV785Tests PASS\n";
    return 0;
}
