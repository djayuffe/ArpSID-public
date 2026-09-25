# ArpSIDViewController split boundaries

`ArpSIDViewController.mm` is still the Cocoa coordinator, but new GUI code must land in these section files instead of expanding the coordinator:

- Chrome/palette helpers
- Parameter binding helpers
- Patch extraction/import UI
- SIDCORE telemetry GUI
- Scope rendering GUI
- HI-FI / forensic panel GUI
- C64 SID player GUI
- Import/export helpers

Invariant: these GUI sections are UI-only. They must not call host callbacks or mutate DSP state from the render thread.
