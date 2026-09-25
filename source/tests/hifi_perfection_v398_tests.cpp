#include "arpsid/core/sid_hifi_transcendence.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

static bool finiteBuf(const std::vector<float>& v){ for(float x:v) if(!std::isfinite(x) || x < -1.0001f || x > 1.0001f) return false; return true; }

static float arpsidTestFloatFromBits(uint32_t bits) noexcept {
    float v = 0.0f;
    static_assert(sizeof(v) == sizeof(bits), "float must be 32-bit for hostile FP test patterns");
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

static float arpsidQuietNaNForTest() noexcept { return arpsidTestFloatFromBits(UINT32_C(0x7fc00000)); }
static float arpsidPositiveInfinityForTest() noexcept { return arpsidTestFloatFromBits(UINT32_C(0x7f800000)); }

int main(){
    using namespace ArpSID;
    ArpSIDForensicConfig forensic{};
    SidHiFiConfig pure{}; pure.quality = HiFiQuality::PureEmulation;
    SidHiFiTranscendence h;
    std::vector<float> l(256), r(256), dryL(256), dryR(256);
    for(int i=0;i<256;++i){ dryL[i]=l[i]=0.25f*std::sin(i*0.07f); dryR[i]=r[i]=0.20f*std::cos(i*0.05f); }
    h.configure(48000.0, forensic, pure);
    h.processStereo(l.data(), r.data(), (int)l.size());
    for(int i=0;i<256;++i){ if(l[i]!=dryL[i] || r[i]!=dryR[i]){ std::fprintf(stderr,"pure bypass changed sample %d\n",i); return 1; }}

    SidHiFiConfig cfg{}; cfg.quality = HiFiQuality::Transcendence; cfg.oversampling=16; cfg.masterWidth=1.50f; cfg.tapeSaturation=0.75f; cfg.analogWarmth=0.85f; cfg.psychoExciter=0.90f; cfg.stereoDepth=0.90f; cfg.voiceDiffuserAmount=0.65f;
    h.reset(); h.configure(48000.0, forensic, cfg);
    l=dryL; r=dryR; h.processStereo(l.data(), r.data(), (int)l.size());
    if(!(h.lastDeltaPeak() > 1e-4f)){ std::fprintf(stderr,"transcendence inaudible delta=%g\n", h.lastDeltaPeak()); return 2; }
    if(!finiteBuf(l) || !finiteBuf(r)){ std::fprintf(stderr,"non-finite/out-of-bound stereo output\n"); return 3; }
    if(!(h.lastWetPeak() > 0.0f && h.lastDryPeak() > 0.0f)){ std::fprintf(stderr,"telemetry peaks not populated\n"); return 4; }

    // Silence must remain silence: no free-running air/noise allowed.
    std::fill(l.begin(), l.end(), 0.0f); std::fill(r.begin(), r.end(), 0.0f);
    h.processStereo(l.data(), r.data(), (int)l.size());
    for(float x:l) if(x != 0.0f){ std::fprintf(stderr,"silence leak L=%g\n",x); return 5; }
    for(float x:r) if(x != 0.0f){ std::fprintf(stderr,"silence leak R=%g\n",x); return 6; }
    if(h.lastDeltaPeak()!=0.0f){ std::fprintf(stderr,"silence delta not zero=%g\n",h.lastDeltaPeak()); return 7; }

    // Aliased mono path must be safe and audible through warmth/body, without double writes.
    for(int i=0;i<256;++i) l[i]=0.20f*std::sin(i*0.11f);
    h.reset(); h.configure(44100.0, forensic, cfg);
    h.processStereo(l.data(), l.data(), (int)l.size());
    if(!(h.lastDeltaPeak() > 1e-5f)){ std::fprintf(stderr,"mono aliased path inaudible delta=%g\n",h.lastDeltaPeak()); return 8; }
    if(!finiteBuf(l)){ std::fprintf(stderr,"mono non-finite/out-of-bound\n"); return 9; }

    // NaN input must sanitize instead of propagating.
    l.assign(64, 0.0f); r.assign(64, 0.0f);
    // Do not use NAN/INFINITY macros here: this target is compiled with FP options
    // that make those macros warn as undefined behavior. Generate the same hostile
    // IEEE-754 payloads by bit pattern so the sanitizer test remains warning-clean.
    l[3]=arpsidQuietNaNForTest(); r[5]=arpsidPositiveInfinityForTest(); l[8]=0.3f; r[8]=-0.2f;
    h.processStereo(l.data(), r.data(), 64);
    if(!finiteBuf(l) || !finiteBuf(r)){ std::fprintf(stderr,"NaN/Inf propagated\n"); return 10; }
    return 0;
}
