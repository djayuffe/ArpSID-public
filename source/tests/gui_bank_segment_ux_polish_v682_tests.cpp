// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
static std::string slice(const std::string& s, const std::string& a, const std::string& b) {
    const auto i=s.find(a); if(i==std::string::npos) return {};
    const auto j=s.find(b,i+a.size()); if(j==std::string::npos) return s.substr(i);
    return s.substr(i,j-i);
}
int main() {
    const std::string gui = readFile(std::string(ARPSID_SOURCE_DIR) + "/source/au3/ArpSIDViewController.mm");
    const std::string priority = slice(gui, "static inline BOOL ArpSIDFactorySlotIsSid808PriorityUI", "static NSString* ArpSIDSid808PriorityBadgeUI");
    const std::string bankChg = slice(gui, "-(void)_factoryBankChg:", "-(void)_bankSlotTap:");

    require(priority.find("slot >= 120 && slot <= 149") != std::string::npos,
            "all canonical SID808 slots 120..149 get priority styling");
    require(priority.find("case 118:") != std::string::npos &&
            priority.find("case 119:") != std::string::npos &&
            priority.find("case 127:") != std::string::npos,
            "legacy SID808 compatibility slots keep priority styling");
    require(bankChg.find("display slots %ld..%ld") != std::string::npos,
            "bank group status explicitly labels 1-based display slot range");
    require(bankChg.find("•  slots %ld..%ld") == std::string::npos,
            "bank group status no longer mixes unlabeled 1-based range with 0-based tooltip text");

    std::cout << "GuiBankSegmentUxPolishV682Tests PASS\n";
    return 0;
}
