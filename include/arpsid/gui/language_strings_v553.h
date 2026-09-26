// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// language_strings_v553.h — Localized UI string table for SETTINGS language support (v553).
//
// PURPOSE
// ------// The SETTINGS tab's Language selector (v544) exposes five languages:
// English / Norwegian / German / French / Japanese
// This header defines a compile-time string table covering all localizable
// string keys used by `_settingsPanel_v544_:` and `_c64StatePanel_v544_:`.
//
// Keeping the table in a pure C++ header makes it:
// * Independently testable without Cocoa (settings_language_v553_tests.cpp)
// * Constexpr-validated at compile time
// * Bundle-independent — no .lproj files required; works regardless of the
// host application's NSBundle path (critical for AU components embedded in
// Logic/GarageBand/AUM whose main bundle is the host, not the plug-in)
//
// CONTRACT
// -------// * UIStringKey_v553 enumerates every localizable string key. kKeyCount
// must equal the number of keys; verified by static_assert.
// * `localizedString_v553(key, lang)` never returns null.
// * English strings are the fallback for any untranslated key.
// * All strings are UTF-8 (source file is UTF-8; C string literals are UTF-8).
// * Adding a new key requires updating all 5 language functions +
// incrementing the compile-time count.
//
// SCOPE (v553)
// -----------// Covers the SETTINGS panel:
// • 6 section headers
// • 5 sub-labels
// • 3 audio engine items
// • 4 tempo sync source items
// • 4 MIDI mapping items
// • 4 theme items
// • 1 "Reapply Theme" button
// And the C64 STATE panel:
// • 1 panel title

#ifndef ARPSID_GUI_LANGUAGE_STRINGS_V553_H
#define ARPSID_GUI_LANGUAGE_STRINGS_V553_H

#include "arpsid/gui/settings_panel_model.h"
#include <type_traits>
#include <cstdint>

namespace ArpSID {
namespace GUI {

// ─── String key inventory ────────────────────────────────────────────────────
enum class UIStringKey_v553 : std::uint8_t {
    // Section headers (◈ prefix is included in the string)
    kSectionAudioEngine  = 0,
    kSectionHostSync,
    kSectionMidiMapping,
    kSectionTheme,
    kSectionLanguage,
    kSectionAdvanced,

    // Sub-labels (shown beneath section header, above popup)
    kLabelTopologyMode,
    kLabelTempoSource,
    kLabelMidiPreset,
    kLabelColorScheme,
    kLabelPluginLanguage,

    // Audio engine mode items (popup order: BitPerfect / Single / Dual)
    kEngineItemBitPerfect,
    kEngineItemSingleSid,
    kEngineItemDualSid,

    // Tempo sync source items (popup order: None / HostTempo / MidiClock / Internal)
    kSyncItemNone,
    kSyncItemHostTempo,
    kSyncItemMidiClock,
    kSyncItemInternal,

    // MIDI mapping preset items (popup order: GM / MSSIAH / C64Keyboard / Custom)
    kMidiItemGM,
    kMidiItemMSSIAH,
    kMidiItemC64Keyboard,
    kMidiItemCustom,

    // Theme items (popup order: Dark / Light / C64Classic / HighContrast)
    kThemeItemDark,
    kThemeItemLight,
    kThemeItemC64Classic,
    kThemeItemHighContrast,

    // Button and panel title
    kButtonReapplyTheme,
    kPanelC64StateTitle,

    kKeyCount  ///< sentinel — not a valid key
};

static_assert(static_cast<std::uint8_t>(UIStringKey_v553::kKeyCount) == 28u,
              "UIStringKey_v553::kKeyCount must be 28; update this assert and all "
              "language functions when adding new keys");

// ─── English strings ──────────────────────────────────────────────────────────
constexpr const char* localStrings_en_v553_(UIStringKey_v553 k) noexcept {
    switch (k) {
    case UIStringKey_v553::kSectionAudioEngine:    return "\xe2\x97\x88 AUDIO ENGINE";
    case UIStringKey_v553::kSectionHostSync:       return "\xe2\x97\x88 HOST SYNC";
    case UIStringKey_v553::kSectionMidiMapping:    return "\xe2\x97\x88 MIDI MAPPING";
    case UIStringKey_v553::kSectionTheme:          return "\xe2\x97\x88 THEME";
    case UIStringKey_v553::kSectionLanguage:       return "\xe2\x97\x88 LANGUAGE";
    case UIStringKey_v553::kSectionAdvanced:       return "\xe2\x97\x88 ADVANCED";
    case UIStringKey_v553::kLabelTopologyMode:     return "Topology mode";
    case UIStringKey_v553::kLabelTempoSource:      return "Tempo source";
    case UIStringKey_v553::kLabelMidiPreset:       return "Preset";
    case UIStringKey_v553::kLabelColorScheme:      return "Color scheme";
    case UIStringKey_v553::kLabelPluginLanguage:   return "Plugin language";
    case UIStringKey_v553::kEngineItemBitPerfect:  return "BitPerfect (legacy 8-chip)";
    case UIStringKey_v553::kEngineItemSingleSid:   return "Single SID 3-voice (audit-correct)";
    case UIStringKey_v553::kEngineItemDualSid:     return "Dual SID 6-voice";
    case UIStringKey_v553::kSyncItemNone:          return "None";
    case UIStringKey_v553::kSyncItemHostTempo:     return "Host tempo";
    case UIStringKey_v553::kSyncItemMidiClock:     return "MIDI clock";
    case UIStringKey_v553::kSyncItemInternal:      return "Internal";
    case UIStringKey_v553::kMidiItemGM:            return "General MIDI";
    case UIStringKey_v553::kMidiItemMSSIAH:        return "MSSIAH";
    case UIStringKey_v553::kMidiItemC64Keyboard:   return "C64 keyboard";
    case UIStringKey_v553::kMidiItemCustom:        return "Custom";
    case UIStringKey_v553::kThemeItemDark:         return "Dark";
    case UIStringKey_v553::kThemeItemLight:        return "Light";
    case UIStringKey_v553::kThemeItemC64Classic:   return "C64 Classic";
    case UIStringKey_v553::kThemeItemHighContrast: return "High Contrast";
    case UIStringKey_v553::kButtonReapplyTheme:    return "\xe2\x86\xba  Reapply Theme";
    case UIStringKey_v553::kPanelC64StateTitle:    return "\xe2\x97\x88 C64 STATE DIAGNOSTIC DASHBOARD (v520+ audit stabilization)";
    case UIStringKey_v553::kKeyCount:              break;
    }
    return "";
}

// ─── Norwegian (Bokmål) strings ───────────────────────────────────────────────
constexpr const char* localStrings_no_v553_(UIStringKey_v553 k) noexcept {
    switch (k) {
    case UIStringKey_v553::kSectionAudioEngine:    return "\xe2\x97\x88 LYDMOTOR";
    case UIStringKey_v553::kSectionHostSync:       return "\xe2\x97\x88 VERTSYNKRON.";
    case UIStringKey_v553::kSectionMidiMapping:    return "\xe2\x97\x88 MIDI-OPPSETT";
    case UIStringKey_v553::kSectionTheme:          return "\xe2\x97\x88 TEMA";
    case UIStringKey_v553::kSectionLanguage:       return "\xe2\x97\x88 SPR\xc3\x85K";
    case UIStringKey_v553::kSectionAdvanced:       return "\xe2\x97\x88 AVANSERT";
    case UIStringKey_v553::kLabelTopologyMode:     return "Topologi-modus";
    case UIStringKey_v553::kLabelTempoSource:      return "Tempokilde";
    case UIStringKey_v553::kLabelMidiPreset:       return "Forh\xc3\xa5ndsinnst.";
    case UIStringKey_v553::kLabelColorScheme:      return "Fargevalg";
    case UIStringKey_v553::kLabelPluginLanguage:   return "Programspr\xc3\xa5k";
    case UIStringKey_v553::kEngineItemBitPerfect:  return "BitPerfekt (klassisk 8-chip)";
    case UIStringKey_v553::kEngineItemSingleSid:   return "Enkel SID 3-stemme";
    case UIStringKey_v553::kEngineItemDualSid:     return "Dobbel SID 6-stemme";
    case UIStringKey_v553::kSyncItemNone:          return "Ingen";
    case UIStringKey_v553::kSyncItemHostTempo:     return "Vertsbpm";
    case UIStringKey_v553::kSyncItemMidiClock:     return "MIDI-klokke";
    case UIStringKey_v553::kSyncItemInternal:      return "Intern";
    case UIStringKey_v553::kMidiItemGM:            return "General MIDI";
    case UIStringKey_v553::kMidiItemMSSIAH:        return "MSSIAH";
    case UIStringKey_v553::kMidiItemC64Keyboard:   return "C64-tastatur";
    case UIStringKey_v553::kMidiItemCustom:        return "Egendefinert";
    case UIStringKey_v553::kThemeItemDark:         return "M\xc3\xb8rk";
    case UIStringKey_v553::kThemeItemLight:        return "Lys";
    case UIStringKey_v553::kThemeItemC64Classic:   return "C64 klassisk";
    case UIStringKey_v553::kThemeItemHighContrast: return "H\xc3\xb8y kontrast";
    case UIStringKey_v553::kButtonReapplyTheme:    return "\xe2\x86\xba  Gjenopprett tema";
    case UIStringKey_v553::kPanelC64StateTitle:    return "\xe2\x97\x88 C64-TILSTAND DIAGNOSTIKK (v520+ revisjonsst.)";
    case UIStringKey_v553::kKeyCount:              break;
    }
    return "";
}

// ─── German strings ───────────────────────────────────────────────────────────
constexpr const char* localStrings_de_v553_(UIStringKey_v553 k) noexcept {
    switch (k) {
    case UIStringKey_v553::kSectionAudioEngine:    return "\xe2\x97\x88 AUDIOMODUL";
    case UIStringKey_v553::kSectionHostSync:       return "\xe2\x97\x88 HOST-SYNC";
    case UIStringKey_v553::kSectionMidiMapping:    return "\xe2\x97\x88 MIDI-BELEGUNG";
    case UIStringKey_v553::kSectionTheme:          return "\xe2\x97\x88 DESIGN";
    case UIStringKey_v553::kSectionLanguage:       return "\xe2\x97\x88 SPRACHE";
    case UIStringKey_v553::kSectionAdvanced:       return "\xe2\x97\x88 ERWEITERT";
    case UIStringKey_v553::kLabelTopologyMode:     return "Topologie-Modus";
    case UIStringKey_v553::kLabelTempoSource:      return "Tempoquelle";
    case UIStringKey_v553::kLabelMidiPreset:       return "Voreinstellung";
    case UIStringKey_v553::kLabelColorScheme:      return "Farbschema";
    case UIStringKey_v553::kLabelPluginLanguage:   return "Plugin-Sprache";
    case UIStringKey_v553::kEngineItemBitPerfect:  return "BitPerfekt (klass. 8-Chip)";
    case UIStringKey_v553::kEngineItemSingleSid:   return "Einzel-SID 3-Stimmen";
    case UIStringKey_v553::kEngineItemDualSid:     return "Doppel-SID 6-Stimmen";
    case UIStringKey_v553::kSyncItemNone:          return "Keiner";
    case UIStringKey_v553::kSyncItemHostTempo:     return "Host-Tempo";
    case UIStringKey_v553::kSyncItemMidiClock:     return "MIDI-Uhr";
    case UIStringKey_v553::kSyncItemInternal:      return "Intern";
    case UIStringKey_v553::kMidiItemGM:            return "General MIDI";
    case UIStringKey_v553::kMidiItemMSSIAH:        return "MSSIAH";
    case UIStringKey_v553::kMidiItemC64Keyboard:   return "C64-Tastatur";
    case UIStringKey_v553::kMidiItemCustom:        return "Benutzerdefiniert";
    case UIStringKey_v553::kThemeItemDark:         return "Dunkel";
    case UIStringKey_v553::kThemeItemLight:        return "Hell";
    case UIStringKey_v553::kThemeItemC64Classic:   return "C64 Klassisch";
    case UIStringKey_v553::kThemeItemHighContrast: return "Hoher Kontrast";
    case UIStringKey_v553::kButtonReapplyTheme:    return "\xe2\x86\xba  Design erneuern";
    case UIStringKey_v553::kPanelC64StateTitle:    return "\xe2\x97\x88 C64-ZUSTAND DIAGNOSE (v520+ Pr\xc3\xbc" "fstabilisierung)";
    case UIStringKey_v553::kKeyCount:              break;
    }
    return "";
}

// ─── French strings ───────────────────────────────────────────────────────────
constexpr const char* localStrings_fr_v553_(UIStringKey_v553 k) noexcept {
    switch (k) {
    case UIStringKey_v553::kSectionAudioEngine:    return "\xe2\x97\x88 MOTEUR AUDIO";
    case UIStringKey_v553::kSectionHostSync:       return "\xe2\x97\x88 SYNC H\xc3\x94TE";
    case UIStringKey_v553::kSectionMidiMapping:    return "\xe2\x97\x88 MAPPAGE MIDI";
    case UIStringKey_v553::kSectionTheme:          return "\xe2\x97\x88 TH\xc3\x88ME";
    case UIStringKey_v553::kSectionLanguage:       return "\xe2\x97\x88 LANGUE";
    case UIStringKey_v553::kSectionAdvanced:       return "\xe2\x97\x88 AVANC\xc3\x89";
    case UIStringKey_v553::kLabelTopologyMode:     return "Mode topologie";
    case UIStringKey_v553::kLabelTempoSource:      return "Source de tempo";
    case UIStringKey_v553::kLabelMidiPreset:       return "Pr\xc3\xa9r\xc3\xa9glage";
    case UIStringKey_v553::kLabelColorScheme:      return "Palette de couleurs";
    case UIStringKey_v553::kLabelPluginLanguage:   return "Langue du greffon";
    case UIStringKey_v553::kEngineItemBitPerfect:  return "BitParfait (classique 8-puce)";
    case UIStringKey_v553::kEngineItemSingleSid:   return "SID unique 3 voix";
    case UIStringKey_v553::kEngineItemDualSid:     return "Double SID 6 voix";
    case UIStringKey_v553::kSyncItemNone:          return "Aucun";
    case UIStringKey_v553::kSyncItemHostTempo:     return "Tempo h\xc3\xb4te";
    case UIStringKey_v553::kSyncItemMidiClock:     return "Horloge MIDI";
    case UIStringKey_v553::kSyncItemInternal:      return "Interne";
    case UIStringKey_v553::kMidiItemGM:            return "MIDI G\xc3\xa9n\xc3\xa9ral";
    case UIStringKey_v553::kMidiItemMSSIAH:        return "MSSIAH";
    case UIStringKey_v553::kMidiItemC64Keyboard:   return "Clavier C64";
    case UIStringKey_v553::kMidiItemCustom:        return "Personnalis\xc3\xa9";
    case UIStringKey_v553::kThemeItemDark:         return "Sombre";
    case UIStringKey_v553::kThemeItemLight:        return "Clair";
    case UIStringKey_v553::kThemeItemC64Classic:   return "C64 Classique";
    case UIStringKey_v553::kThemeItemHighContrast: return "Contraste \xc3\xa9lev\xc3\xa9";
    case UIStringKey_v553::kButtonReapplyTheme:    return "\xe2\x86\xba  R\xc3\xa9" "appliquer th\xc3\xa8me";
    case UIStringKey_v553::kPanelC64StateTitle:    return "\xe2\x97\x88 TABLEAU DE BORD C64 (v520+ audit stabilisation)";
    case UIStringKey_v553::kKeyCount:              break;
    }
    return "";
}

// ─── Japanese (日本語) strings ────────────────────────────────────────────────
// Strings are encoded as UTF-8 hex escapes to keep this header ASCII-safe.
// The escaped sequences decode to: ◈=\xe2\x97\x88 ↺=\xe2\x86\xba
// All other Japanese characters are embedded as UTF-8 escape sequences.
constexpr const char* localStrings_jp_v553_(UIStringKey_v553 k) noexcept {
    switch (k) {
    // ◈ オーディオエンジン
    case UIStringKey_v553::kSectionAudioEngine:
        return "\xe2\x97\x88 \xe3\x82\xaa\xe3\x83\xbc\xe3\x83\x87\xe3\x82\xa3\xe3\x82\xaa\xe3\x82\xa8\xe3\x83\xb3\xe3\x82\xb8\xe3\x83\xb3";
    // ◈ ホスト同期
    case UIStringKey_v553::kSectionHostSync:
        return "\xe2\x97\x88 \xe3\x83\x9b\xe3\x82\xb9\xe3\x83\x88\xe5\x90\x8c\xe6\x9c\x9f";
    // ◈ MIDIマッピング
    case UIStringKey_v553::kSectionMidiMapping:
        return "\xe2\x97\x88 MIDI\xe3\x83\x9e\xe3\x83\x83\xe3\x83\x94\xe3\x83\xb3\xe3\x82\xb0";
    // ◈ テーマ
    case UIStringKey_v553::kSectionTheme:
        return "\xe2\x97\x88 \xe3\x83\x86\xe3\x83\xbc\xe3\x83\x9e";
    // ◈ 言語
    case UIStringKey_v553::kSectionLanguage:
        return "\xe2\x97\x88 \xe8\xa8\x80\xe8\xaa\x9e";
    // ◈ 詳細設定
    case UIStringKey_v553::kSectionAdvanced:
        return "\xe2\x97\x88 \xe8\xa9\xb3\xe7\xb4\xb0\xe8\xa8\xad\xe5\xae\x9a";
    // トポロジーモード
    case UIStringKey_v553::kLabelTopologyMode:
        return "\xe3\x83\x88\xe3\x83\x9d\xe3\x83\xad\xe3\x82\xb8\xe3\x83\xbc\xe3\x83\xa2\xe3\x83\xbc\xe3\x83\x89";
    // テンポソース
    case UIStringKey_v553::kLabelTempoSource:
        return "\xe3\x83\x86\xe3\x83\xb3\xe3\x83\x9d\xe3\x82\xbd\xe3\x83\xbc\xe3\x82\xb9";
    // プリセット
    case UIStringKey_v553::kLabelMidiPreset:
        return "\xe3\x83\x97\xe3\x83\xaa\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88";
    // カラースキーム
    case UIStringKey_v553::kLabelColorScheme:
        return "\xe3\x82\xab\xe3\x83\xa9\xe3\x83\xbc\xe3\x82\xb9\xe3\x82\xad\xe3\x83\xbc\xe3\x83\xa0";
    // プラグイン言語
    case UIStringKey_v553::kLabelPluginLanguage:
        return "\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe8\xa8\x80\xe8\xaa\x9e";
    // ビットパーフェクト (旧8チップ)
    // Note: \xa7 is followed by '8' which is a hex digit — split with "" to prevent
    // the compiler from reading \xa78 as a single (out-of-range) escape sequence.
    case UIStringKey_v553::kEngineItemBitPerfect:
        return "\xe3\x83\x93\xe3\x83\x83\xe3\x83\x88\xe3\x83\x91\xe3\x83\xbc\xe3\x83\x95\xe3\x82\xa7\xe3\x82\xaf\xe3\x83\x88 (\xe6\x97\xa7" "8\xe3\x83\x81\xe3\x83\x83\xe3\x83\x97)";
    // シングルSID 3ボイス
    case UIStringKey_v553::kEngineItemSingleSid:
        return "\xe3\x82\xb7\xe3\x83\xb3\xe3\x82\xb0\xe3\x83\xabSID 3\xe3\x83\x9c\xe3\x82\xa4\xe3\x82\xb9";
    // デュアルSID 6ボイス
    case UIStringKey_v553::kEngineItemDualSid:
        return "\xe3\x83\x87\xe3\x83\xa5\xe3\x82\xa2\xe3\x83\xabSID 6\xe3\x83\x9c\xe3\x82\xa4\xe3\x82\xb9";
    // なし
    case UIStringKey_v553::kSyncItemNone:
        return "\xe3\x81\xaa\xe3\x81\x97";
    // ホストテンポ
    case UIStringKey_v553::kSyncItemHostTempo:
        return "\xe3\x83\x9b\xe3\x82\xb9\xe3\x83\x88\xe3\x83\x86\xe3\x83\xb3\xe3\x83\x9d";
    // MIDIクロック
    case UIStringKey_v553::kSyncItemMidiClock:
        return "MIDI\xe3\x82\xaf\xe3\x83\xad\xe3\x83\x83\xe3\x82\xaf";
    // 内部
    case UIStringKey_v553::kSyncItemInternal:
        return "\xe5\x86\x85\xe9\x83\xa8";
    // ゼネラルMIDI
    case UIStringKey_v553::kMidiItemGM:
        return "\xe3\x82\xbc\xe3\x83\x8d\xe3\x83\xa9\xe3\x83\xabMIDI";
    case UIStringKey_v553::kMidiItemMSSIAH:
        return "MSSIAH";
    // C64キーボード
    case UIStringKey_v553::kMidiItemC64Keyboard:
        return "C64\xe3\x82\xad\xe3\x83\xbc\xe3\x83\x9c\xe3\x83\xbc\xe3\x83\x89";
    // カスタム
    case UIStringKey_v553::kMidiItemCustom:
        return "\xe3\x82\xab\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\xa0";
    // ダーク
    case UIStringKey_v553::kThemeItemDark:
        return "\xe3\x83\x80\xe3\x83\xbc\xe3\x82\xaf";
    // ライト
    case UIStringKey_v553::kThemeItemLight:
        return "\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\x88";
    // C64クラシック
    case UIStringKey_v553::kThemeItemC64Classic:
        return "C64\xe3\x82\xaf\xe3\x83\xa9\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf";
    // ハイコントラスト
    case UIStringKey_v553::kThemeItemHighContrast:
        return "\xe3\x83\x8f\xe3\x82\xa4\xe3\x82\xb3\xe3\x83\xb3\xe3\x83\x88\xe3\x83\xa9\xe3\x82\xb9\xe3\x83\x88";
    // ↺ テーマを再適用
    case UIStringKey_v553::kButtonReapplyTheme:
        return "\xe2\x86\xba  \xe3\x83\x86\xe3\x83\xbc\xe3\x83\x9e\xe3\x82\x92\xe5\x86\x8d\xe9\x81\xa9\xe7\x94\xa8";
    // ◈ C64ステート診断 (v520+)
    case UIStringKey_v553::kPanelC64StateTitle:
        return "\xe2\x97\x88 C64\xe3\x82\xb9\xe3\x83\x86\xe3\x83\xbc\xe3\x83\x88\xe8\xa8\xba\xe6\x96\xad (v520+)";
    case UIStringKey_v553::kKeyCount:              break;
    }
    return "";
}

// ─── Main dispatch ────────────────────────────────────────────────────────────
// Returns the localized string for the given key in the given language.
// Falls back to English if the key is unknown. Never returns null.
constexpr const char* localizedString_v553(UIStringKey_v553 k, Language lang) noexcept {
    switch (lang) {
    case Language::English:   return localStrings_en_v553_(k);
    case Language::Norwegian: return localStrings_no_v553_(k);
    case Language::German:    return localStrings_de_v553_(k);
    case Language::French:    return localStrings_fr_v553_(k);
    case Language::Japanese:  return localStrings_jp_v553_(k);
    }
    return localStrings_en_v553_(k);
}

// ─── Compile-time invariants ──────────────────────────────────────────────────
// Verify English strings are non-empty for a selection of keys.
static_assert(localizedString_v553(UIStringKey_v553::kSectionAudioEngine, Language::English)[0] != '\0',
              "English kSectionAudioEngine must be non-empty");
static_assert(localizedString_v553(UIStringKey_v553::kEngineItemBitPerfect, Language::English)[0] != '\0',
              "English kEngineItemBitPerfect must be non-empty");
static_assert(localizedString_v553(UIStringKey_v553::kPanelC64StateTitle, Language::English)[0] != '\0',
              "English kPanelC64StateTitle must be non-empty");
static_assert(localizedString_v553(UIStringKey_v553::kButtonReapplyTheme, Language::English)[0] != '\0',
              "English kButtonReapplyTheme must be non-empty");
// Norwegian differs from English for section headers.
// Both start with ◈ (E2 97 88) + space, so compare byte[4] which is the first
// letter of the word: English byte[4]='A' (AUDIO), Norwegian byte[4]='L' (LYDMOTOR).
static_assert(localizedString_v553(UIStringKey_v553::kSectionAudioEngine, Language::Norwegian)[4] !=
              localizedString_v553(UIStringKey_v553::kSectionAudioEngine, Language::English)[4],
              "Norwegian kSectionAudioEngine must differ from English at byte[4]");

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_LANGUAGE_STRINGS_V553_H
