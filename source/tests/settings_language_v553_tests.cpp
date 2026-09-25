// SPDX-License-Identifier: BSD-3-Clause
// settings_language_v553_tests.cpp — Contract tests for v553 language string table.
//
// Tests:
// Section I — UIStringKey_v553::kKeyCount == 28 (static_assert)
// Section II — Compile-time non-empty checks for English keys (static_assert)
// Section III — All keys × all languages return non-null, non-empty strings
// Section IV — English section headers match expected prefix
// Section V — Norwegian section headers differ from English
// Section VI — German section headers differ from English
// Section VII — French section headers differ from English
// Section VIII— Japanese strings non-empty (first byte != 0)
// Section IX — No language/key combo returns the same pointer for a translated key
// (i.e. Norwegian kSectionAudioEngine ≠ English kSectionAudioEngine)

#include "arpsid/gui/language_strings_v553.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace ArpSID::GUI;

// ─── Section I: kKeyCount pinned (static_assert) ─────────────────────────────

static_assert(static_cast<unsigned>(UIStringKey_v553::kKeyCount) == 28u,
              "kKeyCount must be 28");

// ─── Section II: compile-time non-empty checks ───────────────────────────────

static_assert(localizedString_v553(UIStringKey_v553::kSectionAudioEngine, Language::English)[0]   != '\0', "en kSectionAudioEngine");
static_assert(localizedString_v553(UIStringKey_v553::kSectionHostSync, Language::English)[0]      != '\0', "en kSectionHostSync");
static_assert(localizedString_v553(UIStringKey_v553::kSectionMidiMapping, Language::English)[0]   != '\0', "en kSectionMidiMapping");
static_assert(localizedString_v553(UIStringKey_v553::kSectionTheme, Language::English)[0]         != '\0', "en kSectionTheme");
static_assert(localizedString_v553(UIStringKey_v553::kSectionLanguage, Language::English)[0]      != '\0', "en kSectionLanguage");
static_assert(localizedString_v553(UIStringKey_v553::kSectionAdvanced, Language::English)[0]      != '\0', "en kSectionAdvanced");
static_assert(localizedString_v553(UIStringKey_v553::kLabelTopologyMode, Language::English)[0]    != '\0', "en kLabelTopologyMode");
static_assert(localizedString_v553(UIStringKey_v553::kEngineItemBitPerfect, Language::English)[0] != '\0', "en kEngineBitPerfect");
static_assert(localizedString_v553(UIStringKey_v553::kButtonReapplyTheme, Language::English)[0]   != '\0', "en kButtonReapplyTheme");
static_assert(localizedString_v553(UIStringKey_v553::kPanelC64StateTitle, Language::English)[0]   != '\0', "en kPanelC64StateTitle");

// ─── helpers ─────────────────────────────────────────────────────────────────

// Iterate every valid key in order
static constexpr UIStringKey_v553 kAllKeys[] = {
    UIStringKey_v553::kSectionAudioEngine,
    UIStringKey_v553::kSectionHostSync,
    UIStringKey_v553::kSectionMidiMapping,
    UIStringKey_v553::kSectionTheme,
    UIStringKey_v553::kSectionLanguage,
    UIStringKey_v553::kSectionAdvanced,
    UIStringKey_v553::kLabelTopologyMode,
    UIStringKey_v553::kLabelTempoSource,
    UIStringKey_v553::kLabelMidiPreset,
    UIStringKey_v553::kLabelColorScheme,
    UIStringKey_v553::kLabelPluginLanguage,
    UIStringKey_v553::kEngineItemBitPerfect,
    UIStringKey_v553::kEngineItemSingleSid,
    UIStringKey_v553::kEngineItemDualSid,
    UIStringKey_v553::kSyncItemNone,
    UIStringKey_v553::kSyncItemHostTempo,
    UIStringKey_v553::kSyncItemMidiClock,
    UIStringKey_v553::kSyncItemInternal,
    UIStringKey_v553::kMidiItemGM,
    UIStringKey_v553::kMidiItemMSSIAH,
    UIStringKey_v553::kMidiItemC64Keyboard,
    UIStringKey_v553::kMidiItemCustom,
    UIStringKey_v553::kThemeItemDark,
    UIStringKey_v553::kThemeItemLight,
    UIStringKey_v553::kThemeItemC64Classic,
    UIStringKey_v553::kThemeItemHighContrast,
    UIStringKey_v553::kButtonReapplyTheme,
    UIStringKey_v553::kPanelC64StateTitle,
};
static_assert(sizeof(kAllKeys)/sizeof(kAllKeys[0]) == 28u, "kAllKeys must have 28 entries");

static constexpr Language kAllLangs[] = {
    Language::English, Language::Norwegian, Language::German,
    Language::French,  Language::Japanese
};

// ─── Section III: all keys × all languages non-empty ─────────────────────────

static void testAllStringsNonEmpty() {
    for (Language lang : kAllLangs) {
        for (UIStringKey_v553 k : kAllKeys) {
            const char* s = localizedString_v553(k, lang);
            assert(s != nullptr);
            assert(s[0] != '\0');
            (void)s;
        }
    }
    std::puts("  III:  all keys x languages non-null and non-empty — OK");
}

// ─── Section IV: English section headers start with the ◈ prefix ─────────────
// ◈ = UTF-8 \xe2\x97\x88

static void testEnglishSectionHeaders() {
    const UIStringKey_v553 sectionKeys[] = {
        UIStringKey_v553::kSectionAudioEngine,
        UIStringKey_v553::kSectionHostSync,
        UIStringKey_v553::kSectionMidiMapping,
        UIStringKey_v553::kSectionTheme,
        UIStringKey_v553::kSectionLanguage,
        UIStringKey_v553::kSectionAdvanced,
    };
    for (UIStringKey_v553 k : sectionKeys) {
        const char* s = localizedString_v553(k, Language::English);
        // First 3 bytes must be the ◈ UTF-8 sequence: E2 97 88
        assert(static_cast<unsigned char>(s[0]) == 0xE2u);
        assert(static_cast<unsigned char>(s[1]) == 0x97u);
        assert(static_cast<unsigned char>(s[2]) == 0x88u);
        (void)s;
    }
    std::puts("  IV:   English section headers start with ◈ prefix — OK");
}

// ─── Section V: Norwegian section headers differ from English ─────────────────

static void testNorwegianDiffersFromEnglish() {
    const UIStringKey_v553 sectionKeys[] = {
        UIStringKey_v553::kSectionAudioEngine,  // EN: AUDIO ENGINE, NO: LYDMOTOR
        UIStringKey_v553::kSectionHostSync,
        UIStringKey_v553::kSectionTheme,
        UIStringKey_v553::kLabelTopologyMode,
        UIStringKey_v553::kEngineItemBitPerfect,
        UIStringKey_v553::kThemeItemDark,
    };
    for (UIStringKey_v553 k : sectionKeys) {
        const char* en = localizedString_v553(k, Language::English);
        const char* no = localizedString_v553(k, Language::Norwegian);
        assert(std::strcmp(en, no) != 0);
        (void)en; (void)no;
    }
    std::puts("  V:    Norwegian strings differ from English — OK");
}

// ─── Section VI: German section headers differ from English ──────────────────

static void testGermanDiffersFromEnglish() {
    const UIStringKey_v553 diffKeys[] = {
        UIStringKey_v553::kSectionAudioEngine,   // DE: AUDIOMODUL
        UIStringKey_v553::kSectionMidiMapping,   // DE: MIDI-BELEGUNG
        UIStringKey_v553::kLabelTempoSource,     // DE: Tempoquelle
        UIStringKey_v553::kEngineItemSingleSid,  // DE: Einzel-SID 3-Stimmen
        UIStringKey_v553::kThemeItemDark,        // DE: Dunkel
    };
    for (UIStringKey_v553 k : diffKeys) {
        const char* en = localizedString_v553(k, Language::English);
        const char* de = localizedString_v553(k, Language::German);
        assert(std::strcmp(en, de) != 0);
        (void)en; (void)de;
    }
    std::puts("  VI:   German strings differ from English — OK");
}

// ─── Section VII: French section headers differ from English ─────────────────

static void testFrenchDiffersFromEnglish() {
    const UIStringKey_v553 diffKeys[] = {
        UIStringKey_v553::kSectionAudioEngine,   // FR: MOTEUR AUDIO
        UIStringKey_v553::kSectionTheme,         // FR: THÈME
        UIStringKey_v553::kLabelMidiPreset,      // FR: Préréglage
        UIStringKey_v553::kEngineItemBitPerfect, // FR: BitParfait...
        UIStringKey_v553::kThemeItemHighContrast,// FR: Contraste élevé
    };
    for (UIStringKey_v553 k : diffKeys) {
        const char* en = localizedString_v553(k, Language::English);
        const char* fr = localizedString_v553(k, Language::French);
        assert(std::strcmp(en, fr) != 0);
        (void)en; (void)fr;
    }
    std::puts("  VII:  French strings differ from English — OK");
}

// ─── Section VIII: Japanese strings non-empty ────────────────────────────────

static void testJapaneseStringsNonEmpty() {
    for (UIStringKey_v553 k : kAllKeys) {
        const char* jp = localizedString_v553(k, Language::Japanese);
        assert(jp != nullptr);
        assert(jp[0] != '\0');
        (void)jp;
    }
    // A few spot checks: Japanese section headers start with ◈ (E2 97 88)
    const char* jpAudioEngine = localizedString_v553(
        UIStringKey_v553::kSectionAudioEngine, Language::Japanese);
    assert(static_cast<unsigned char>(jpAudioEngine[0]) == 0xE2u);
    assert(static_cast<unsigned char>(jpAudioEngine[1]) == 0x97u);
    assert(static_cast<unsigned char>(jpAudioEngine[2]) == 0x88u);
    (void)jpAudioEngine;
    std::puts("  VIII: Japanese strings non-empty and ◈-prefixed — OK");
}

// ─── Section IX: Translated keys return different strings than English ────────

static void testTranslatedKeysDifferFromEnglish() {
    // For each language that truly translates (no, de, fr, jp),
    // at least the first section header must differ from English.
    const Language translatedLangs[] = {
        Language::Norwegian, Language::German, Language::French, Language::Japanese
    };
    for (Language lang : translatedLangs) {
        const char* en  = localizedString_v553(UIStringKey_v553::kSectionAudioEngine, Language::English);
        const char* loc = localizedString_v553(UIStringKey_v553::kSectionAudioEngine, lang);
        assert(std::strcmp(en, loc) != 0);
        (void)en; (void)loc;
    }
    std::puts("  IX:   each non-English language translates kSectionAudioEngine — OK");
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::puts("settings_language_v553_tests");
    std::puts("  I:    static_assert kKeyCount == 28 — OK");
    std::puts("  II:   static_assert English keys non-empty — OK");
    testAllStringsNonEmpty();
    testEnglishSectionHeaders();
    testNorwegianDiffersFromEnglish();
    testGermanDiffersFromEnglish();
    testFrenchDiffersFromEnglish();
    testJapaneseStringsNonEmpty();
    testTranslatedKeysDifferFromEnglish();
    std::puts("ALL PASS");
    return 0;
}
