#include "arpsid/core/sid_hifi_transcendence.h"
#include <cmath>
#include <cstdio>

int main(){
    using namespace ArpSID;
    SidHiFiTranscendence hifi;
    auto off = sidHiFiConfigFromNormalized(0.f,1.f,.5f,.6f,.45f,.68f,.82f,.65f,.35f);
    auto on  = sidHiFiConfigFromNormalized(1.f,1.f,.5f,1.0f,.85f,.90f,.95f,.90f,.60f);
    float monoDry[256];
    float monoWet[256];
    float l[256], r[256];
    for(int i=0;i<256;++i){
        const float x = 0.18f * std::sin(0.07f * i) + 0.06f * std::sin(0.31f * i);
        monoDry[i]=x; monoWet[i]=x; l[i]=x; r[i]=x*0.8f;
    }
    hifi.configure(48000.0, ArpSIDForensicConfig{}, off);
    hifi.processStereo(monoDry, nullptr, 256);
    for(int i=0;i<256;++i){
        const float x = 0.18f * std::sin(0.07f * i) + 0.06f * std::sin(0.31f * i);
        if(std::fabs(monoDry[i]-x)>1e-7f){ std::fprintf(stderr,"Pure bypass changed mono at %d\n",i); return 1; }
    }
    hifi.configure(48000.0, ArpSIDForensicConfig{}, on);
    hifi.processStereo(monoWet, nullptr, 256);
    if(!(hifi.lastDeltaPeak() > 1e-4f)){ std::fprintf(stderr,"HI-FI mono path inaudible delta=%g\n", hifi.lastDeltaPeak()); return 2; }
    hifi.reset();
    hifi.configure(48000.0, ArpSIDForensicConfig{}, on);
    hifi.processStereo(l, r, 256);
    if(!(hifi.lastDeltaPeak() > 1e-4f)){ std::fprintf(stderr,"HI-FI stereo path inaudible delta=%g\n", hifi.lastDeltaPeak()); return 3; }
    return 0;
}
