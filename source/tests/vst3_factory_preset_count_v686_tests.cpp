// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
static void req(bool ok,const char*msg){ if(!ok){ std::cerr<<"FAIL: "<<msg<<"\n"; std::exit(1);} }
static std::string readFile(const std::string&p){std::ifstream f(p,std::ios::binary); std::ostringstream ss; ss<<f.rdbuf(); return ss.str();}
int main(){
    const std::string src=readFile(std::string(ARPSID_SOURCE_DIR)+"/source/arpsid_controller.cpp");
    req(src.find("kMaxPresets_     = ArpSID::kCanonicalFactoryPatchSlotCount")!=std::string::npos,"VST3 uses canonical factory count");
    req(src.find("kMaxPresets_     = 128") == std::string::npos,"VST3 no longer hard-codes 128 presets");
    req(src.find("factoryPatchNameForSlot((int)programIndex)")!=std::string::npos,"VST3 names still come from factory table");
    std::cout << "Vst3FactoryPresetCountV686Tests PASS\n"; return 0;
}
