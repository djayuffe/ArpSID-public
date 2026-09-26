// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/gui/kit_sequencer.h"
#include <cstdlib>
#include <iostream>
static void req(bool ok,const char*msg){ if(!ok){ std::cerr<<"FAIL: "<<msg<<"\n"; std::exit(1);} }
int main(){
    auto grid=ArpSID::GUI::makeDefaultKitStepGrid();
    auto model=ArpSID::GUI::makeDefaultKitPanelModel();
    auto voice=ArpSID::GUI::makeDefaultKitVoiceConfigGrid();
    ArpSID::GUI::CompiledKitSequencer out{};
    req(ArpSID::GUI::compileKitSequencer(out,grid,model,voice),"valid model compiles");
    auto badModel=model; badModel.schemaVersion=0;
    req(!ArpSID::GUI::compileKitSequencer(out,grid,badModel,voice),"bad kit panel rejected");
    auto badVoice=voice; badVoice.schemaVersion=0;
    req(!ArpSID::GUI::compileKitSequencer(out,grid,model,badVoice),"bad voice grid rejected");
    std::cout << "KitSequencerModelValidationV688Tests PASS\n"; return 0;
}
