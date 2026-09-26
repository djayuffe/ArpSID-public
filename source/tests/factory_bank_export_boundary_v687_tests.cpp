// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
static void req(bool ok,const char*msg){ if(!ok){ std::cerr<<"FAIL: "<<msg<<"\n"; std::exit(1);} }
static std::string readFile(const std::string&p){std::ifstream f(p,std::ios::binary); std::ostringstream ss; ss<<f.rdbuf(); return ss.str();}
int main(){
    const std::string h=readFile(std::string(ARPSID_SOURCE_DIR)+"/include/arpsid/patchbank/sid_patchbank_io.h");
    const std::string c=readFile(std::string(ARPSID_SOURCE_DIR)+"/source/patchbank/sid_patchbank_io.cpp");
    req(h.find("v1 user-bank-compatible bank")!=std::string::npos,".arpbank docs say v1 user bank compatible");
    req(h.find("128-slot factory patch bank") == std::string::npos,".arpbank docs no longer claim full 128-slot factory bank");
    req(h.find("Canonical factory slots 128..179 require")!=std::string::npos,".arpbank boundary documents extended slots");
    req(c.find("Factory Bank v1 subset")!=std::string::npos,"export name says subset");
    std::cout << "FactoryBankExportBoundaryV687Tests PASS\n"; return 0;
}
