// ─── ArpSIDModMatrix.h ────────────────────────────────────────────────────────
// Phase 3: Real modulation routing graph with explicit route objects.
// Sources: LFO1-4, env, velocity, note, key-follow, macro 1-8, mod wheel,
// pitch bend, channel pressure, poly pressure, random.
// Destinations: filter cutoff/res, VCO1-3 detune/PW, master volume.
// Both global and per-voice evaluation paths.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "ArpSIDCanonicalEvents.h"
#include "arpsid/core/math_utils.h"
#include "arpsid/core/sid_mod_matrix_types.h"
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <functional>

namespace ArpSID {

// ── Canonical modulation aliases ─────────────────────────────────────────────
using ModSource = SidModSource;
static constexpr int kModSourceCount = kSidModSourceCount;
using ModDest = SidModTarget;
static constexpr int kModDestCount = kSidModTargetCount;
using ModTransform = SidModTransform;
using ModRoute = SidModRoute;
static constexpr int kMaxModRoutes = 32;

// ── Context passed to evaluation ──────────────────────────────────────────────
struct ModContext {
    float lfoValues[4]   = {};   // LFO 1-4 bipolar [-1..1]
    float velocity       = 0.5f; // last note velocity [0..1]
    float noteNumber     = 0.5f; // normalized note [0..1]
    float keyFollow      = 0.f;  // semitones from root / 48
    float modWheel       = 0.f;  // CC1 [0..1]
    float pitchBend      = 0.5f; // [0..1], 0.5=centre
    float afterTouch     = 0.f;  // channel pressure [0..1]
    float polyPressure   = 0.f;  // per-voice [0..1]
    float macros[8]      = {};   // macro 1-8 [0..1]
    float envLevel       = 0.f;  // envelope follower [0..1]
    uint32_t rngState    = 0x6D2B79F5u; // per-note random state
};

// ── Result: per-destination accumulated modulation delta ─────────────────────
struct ModResult {
    float deltas[(int)ModDest::_Count] = {};
    void reset() noexcept { for (auto& d : deltas) d = 0.f; }
    float get(ModDest d) const noexcept {
        return (int)d < kModDestCount ? deltas[(int)d] : 0.f;
    }
    float apply(ModDest d, float base) const noexcept {
        return std::clamp(base + get(d), 0.f, 1.f);
    }
};

// ── Modulation Matrix ─────────────────────────────────────────────────────────
class ModMatrix {
public:
    ModMatrix() { reset(); }

    void reset() noexcept {
        routeCount_ = 0;
        for (auto& r : routes_) r = {};
    }

    int  routeCount() const noexcept { return routeCount_; }
    void setRouteCount(int n) noexcept { routeCount_ = std::clamp(n, 0, kMaxModRoutes); }

    ModRoute& route(int i) noexcept {
        if (i >= 0 && i < kMaxModRoutes) return routes_[i];
        invalidRoute_ = {};
        return invalidRoute_;
    }
    const ModRoute& route(int i) const noexcept {
        return (i >= 0 && i < kMaxModRoutes) ? routes_[i] : invalidRoute_;
    }

    // Add a route — returns index or -1 if full
    int addRoute(ModSource src, ModDest dst, float depth, ModTransform xform = ModTransform::Linear) noexcept {
        if (routeCount_ >= kMaxModRoutes) return -1;
        int idx = routeCount_++;
        routes_[idx] = { src, dst, depth, xform, true, true };
        routes_[idx].sanitize();
        return idx;
    }

    void removeRoute(int idx) noexcept {
        if (idx < 0 || idx >= routeCount_) return;
        for (int i = idx; i < routeCount_ - 1; ++i) routes_[i] = routes_[i+1];
        routes_[--routeCount_] = {};
    }

    // ── Evaluate all active routes given a context → ModResult ─────────────
    // Accumulates deltas by destination; caller applies to base param values.
    ModResult evaluate(const ModContext& ctx) const noexcept {
        ModResult result{};
        for (int i = 0; i < routeCount_; ++i) {
            const ModRoute& r = routes_[i];
            if (!r.active()) continue;

            float src = getSourceValue_(r.source, r.bipolar, ctx);
            src = applyTransform_(src, r.transform);
            if (!std::isfinite(src)) continue;

            const float delta = std::clamp(src * r.depth, -1.f, 1.f);
            if (!std::isfinite(delta)) continue;
            if ((int)r.target < kModDestCount)
                result.deltas[(int)r.target] = std::clamp(
                    result.deltas[(int)r.target] + delta, -1.f, 1.f);
        }
        return result;
    }

private:
    ModRoute routes_[kMaxModRoutes];
    ModRoute invalidRoute_{};
    int      routeCount_ = 0;

    float getSourceValue_(ModSource src, bool bipolar, const ModContext& ctx) const noexcept {
        float v = 0.f;
        switch (src) {
        case ModSource::LFO1:         v = ctx.lfoValues[0]; break;
        case ModSource::LFO2:         v = ctx.lfoValues[1]; break;
        case ModSource::LFO3:         v = ctx.lfoValues[2]; break;
        case ModSource::LFO4:         v = ctx.lfoValues[3]; break;
        case ModSource::Velocity:     v = ctx.velocity;     break;
        case ModSource::NoteNumber:   v = ctx.noteNumber;   break;
        case ModSource::KeyFollow:    v = ctx.keyFollow;    break;
        case ModSource::ModWheel:     v = ctx.modWheel;     break;
        case ModSource::PitchBend:    v = ctx.pitchBend * 2.f - 1.f;  break;
        case ModSource::AfterTouch:   v = ctx.afterTouch;  break;
        case ModSource::PolyPressure: v = ctx.polyPressure; break;
        case ModSource::Random:       {
            uint32_t rng = ctx.rngState;
            v = ArpSID_rand_bipolar(rng);
        } break;
        case ModSource::Macro1:  v = ctx.macros[0]; break;
        case ModSource::Macro2:  v = ctx.macros[1]; break;
        case ModSource::Macro3:  v = ctx.macros[2]; break;
        case ModSource::Macro4:  v = ctx.macros[3]; break;
        case ModSource::Macro5:  v = ctx.macros[4]; break;
        case ModSource::Macro6:  v = ctx.macros[5]; break;
        case ModSource::Macro7:  v = ctx.macros[6]; break;
        case ModSource::Macro8:  v = ctx.macros[7]; break;
        case ModSource::Env1:    v = ctx.envLevel;  break;
        default: v = 0.f; break;
        }
        // Normalize to bipolar [-1..1] if caller wants unipolar [0..1]
        return bipolar ? std::clamp(v, -1.f, 1.f)
                       : std::clamp(v * 0.5f + 0.5f, 0.f, 1.f);
    }

    float applyTransform_(float v, ModTransform t) const noexcept {
        switch (t) {
        case ModTransform::Linear:  return v;
        case ModTransform::Squared: return (v >= 0.f) ?  v*v : -(v*v);
        case ModTransform::Abs:     return std::fabs(v);
        case ModTransform::Invert:  return -v;
        default: return v;
        }
    }
};

} // namespace ArpSID
