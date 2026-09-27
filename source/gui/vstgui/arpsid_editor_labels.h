// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — value text for the cross-platform editor.
//
// Choice names and the stepped decode law live in the shared presentation
// authority (sid_parameter_presentation.h). This header adds editor-only
// text: SID registers as $XX (hosts show them in decimal).
#pragma once

#include "arpsid/core/sid_parameter_presentation.h"
#include "parameter_ids.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ArpSID::Editor {

// Stepped values: the index the engine plays, the on-grid value for an
// index, and the choice names all come from the shared presentation
// authority (arpsid/core/sid_parameter_presentation.h), so the editor, host
// text and the parameter reference agree with the engine's decoders.
inline int stepIndexForParam(int id, float norm) {
    return sidParameterChoiceIndex(id, std::isfinite(norm) ? norm : 0.f);
}

inline float stepNormForIndex(int id, int index) { return sidParameterChoiceNormalized(id, index); }

inline const char* choiceLabel(int id, int index) { return sidParameterChoiceName(id, index); }

// Editor text for a parameter value, or empty to use the host text.
inline std::string editorValueText(int id, float norm) {
    if (normalizedParamStepCount(id) > 0)
        if (const char* l = choiceLabel(id, stepIndexForParam(id, norm))) return l;
    char buf[32];
    if (id >= static_cast<int>(kParamSidRegD400) && id <= static_cast<int>(kParamSidRegD41D)) {
        std::snprintf(buf, sizeof buf, "$%02X", static_cast<unsigned>(std::lround(std::clamp(norm, 0.f, 1.f) * 255.f)));
        return buf;
    }
    return {};
}

} // namespace ArpSID::Editor
