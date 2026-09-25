#include "arpsid/core/sid_runtime_voice_policy.h"
#include <iostream>
static bool require(bool ok, const char* msg){ if(!ok) std::cerr << "FAIL: " << msg << "\n"; return ok; }
static bool sustain_case(){
    ArpSID::VoiceAllocator va; va.setPlayMode(ArpSID::PlayMode::Poly); ArpSID::VoiceEventBuffer out;
    va.noteOn(60,1.0f,0,1001,out); out.reset();
    va.setSustainPedal(0,true); va.noteOff(60,0,1001,out);
    if(!require(out.count==0,"poly sustained note-off must not emit GateOff")) return false;
    const auto st=va.voiceManager().getVoiceState(0);
    if(!require(st.isActive && !st.keyDown && st.isSustained,"voice must stay active and sustained")) return false;
    int gate=-1; va.setSustainGateOffCallback([](void* c,int v){*static_cast<int*>(c)=v;},&gate);
    va.setSustainPedal(0,false);
    if(!require(gate==0,"sustain pedal-up must emit deferred gate-off")) return false;
    const auto r=va.voiceManager().getVoiceState(0);
    return require(!r.isActive && !r.keyDown && !r.isSustained,"voice must reset after sustain release");
}
static bool sostenuto_case(){
    ArpSID::VoiceAllocator va; va.setPlayMode(ArpSID::PlayMode::Poly); ArpSID::VoiceEventBuffer out;
    va.noteOn(64,1.0f,1,2002,out); out.reset();
    va.setSostenutoPedal(1,true); va.noteOff(64,1,2002,out);
    if(!require(out.count==0,"poly sostenuto note-off must not emit GateOff")) return false;
    const auto st=va.voiceManager().getVoiceState(0);
    if(!require(st.isActive && !st.keyDown && st.isSostenuto,"voice must stay active and sostenuto")) return false;
    int gate=-1; va.setSustainGateOffCallback([](void* c,int v){*static_cast<int*>(c)=v;},&gate);
    va.setSostenutoPedal(1,false);
    return require(gate==0,"sostenuto pedal-up must emit deferred gate-off");
}
int main(){ return (sustain_case() && sostenuto_case()) ? 0 : 1; }
