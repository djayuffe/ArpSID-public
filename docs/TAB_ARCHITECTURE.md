# ArpSID GUI tab architecture

The tab inventory lives in `include/arpsid/gui/tab_architecture.h`. The macOS
Cocoa editor (AU, AUv3, Standalone and macOS VST3) and the Windows/Linux VST3
editor both build their tab strip from `kProductionVisibleTabs`. Compile-time
`static_assert`s in that header pin the order, the labels and the
implementation status.

For each tab's contents see [VST3_EDITOR.md](VST3_EDITOR.md#tabs), which has a
screenshot of every tab. For the parameters on each tab see
[PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md).

## Production tab ring

`kProductionVisibleTabs` has 17 tabs, in segmented-control order:

| # | Tab | HUD | Enum (persisted ID) | Primary drum context | Description (`TabSpec`) |
|---:|---|---|---|---|---|
| 0 | MAIN | MAIN | `Main` (0) | — | Primary SID instrument, VCO, filter, envelope and output surface. |
| 1 | LFO / ARP | LFO | `LfoArp` (1) | — | Four realtime LFOs and the host/internal arpeggiator. |
| 2 | SID REG | SIDREG | `SidRegisters` (3) | — | Direct SID register editor with OSC3, ENV3, POT and voice scopes. |
| 3 | SEQ | SEQ | `Sequencer` (4) | SID-808 analog projection | Global step sequencer and SID-808/DrSID performance surface. |
| 4 | DRSID | DRSID | `DrSid` (13) | DrSID C64 wavetable | Dedicated C64 register-microprogram drum surface. |
| 5 | FILTER | FILTER | `Filter` (5) | — | Expanded filter controls and chronological input/output scopes. |
| 6 | MACRO | MACRO | `Macro` (6) | — | Eight live macro controls and modulation routing. |
| 7 | FORENSIC | FOREN | `Forensic` (7) | — | Analog-forensic controls, live VCO scopes and bus stress telemetry. |
| 8 | SIDCORE | SCORE | `SidCore` (8) | — | SID bus matrix, register timeline, chip state and VCO scopes. |
| 9 | C64 | C64 | `C64` (11) | — | Realtime C64 CPU, VIC-II, CIA, SID, memory and debug cockpit, with the SID player. |
| 10 | HI-FI | HIFI | `HiFi` (12) | — | Post-SID quality, safety and audible-delta monitoring. |
| 11 | BANK | BANK | `Bank` (9) | — | Factory and user patch/bank browser covering the canonical slot space. |
| 12 | OPTIONS | OPT | `Options` (10) | — | Compact runtime options and C64 control hub. |
| 13 | SETTINGS | SET | `Settings` (14) | — | Topology, theme, language, diagnostics and canonical-policy status. |
| 14 | MIX | MIX | `Mix` (16) | — | Per-instrument mixer, FX, sends, limiter and monitor controls. |
| 15 | KIT | KIT | `Kit` (17) | — | DrSID, SID-808 and DIGI kit assignment and voice editor. |
| 16 | DIGI | DIGI | `Digi` (18) | DIGI 4-bit | `$D418` sample import, capture, pads, sequencing and bus telemetry. |

The enum values are persisted: saved GUI state stores the tab by its ID, not
its position in the ring. Values are never reused or renumbered.

## Hidden IDs

| ID | Status | Why it exists |
|---|---|---|
| `LegacySidProjection` (2) | Compatibility only; never visible | An old persisted value. It migrates to a visible tab. |
| `C64State` (15) | Has a `TabSpec`, not in the ring | The former C64 STATE inspector. Its diagnostics now live on C64 and SIDCORE. The spec stays so projects that persisted ID 15 migrate deterministically. |

Older source spellings (`DRSID`, `SID808`, `DIGI`, `SEQ`, `KIT`, `MIX`,
`SIDCORE`, `C64STATE`) remain as enum aliases. `SID808` names the Sequencer
surface; SID-808 was never a separate persisted tab.

## Invariants

These are enforced by `static_assert` in `tab_architecture.h` and by the tab
tests (`gui_tab_architecture_v543`, `tab_architecture_promotion_v566`,
`tab_no_scaffold_v631`, `c64_state_tab_promotion_v581` and others):

1. The ring has exactly 17 unique tabs, and every one has a `TabSpec`.
2. MAIN is first and DIGI is last. The legacy projection ID is never visible.
3. All 17 visible tabs are implemented, and no visible tab is a scaffold.
4. Every tab has a non-empty name and description, and a HUD label of at most
   6 glyphs (it must fit the HUD status cell).
5. There are 18 specs: the 17 visible tabs plus the migration-only C64 STATE.
6. The primary drum context per tab matches the engine split (SEQ: SID-808,
   DRSID: DrSID, DIGI: 4-bit). The GUI never sources engine identity; the
   engine's `DrumKitIdentity` wins and the GUI reflects it.
7. The Windows/Linux editor uses the same order: `EditorLayoutCoverageTests`
   checks its tab table against `kProductionVisibleTabs`.

## Tab data that is not parameters

Most tabs edit host parameters. Four tabs also edit models that are saved with
the plug-in state, but are not host parameters:

| Tab | Model | Header |
|---|---|---|
| SETTINGS | `SettingsPanelModel`: topology, theme, language, diagnostics | `settings_panel_model.h` |
| MIX | `MixPanelModel`: 16 channels, sends, master, FX chains | `mix_panel_model.h` |
| KIT | `KitStateBlob`: the panel model, step grid, SID-808 voice configs, DIGI assignments | `kit_state_blob.h` |
| DIGI | `DigiPanelModel` + `DigiSampleBankBlob`: slots, steps, user samples | `digi_panel_model.h`, `digi_sample_bank_v596.h` |

SIDCORE reads a live data model instead, `SidCorePanelModel`
(`sidcore_panel_model.h`). It is a fixed-size, RT-safe ring of the last SID
register writes, fed by the render thread and read by the GUI; it is not saved.

`tab_architecture.h` also carries implementation contracts for DIGI and KIT
(`ImplementationContract::kDigiTabContract`, `kKitTabContract`) that describe
what each surface must provide.
