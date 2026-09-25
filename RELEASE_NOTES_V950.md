# ArpSID v950 — DrSID/SID808 SEQ reset preservation closure

> Audit note (v952): v950's guard was primarily a source-contract test. V952
> adds the missing end-to-end kernel render/reset/Stop→Play SID808 KIT test.

v950 completes the v949 DrSID/SID808 SEQ authority split across reset, UI, state-root and persistence paths.

- AU3 transport/AU reset DrSID structural authority now clears ARP but preserves SeqEnable when DrSID/SID808 sequencer transport was active before reset or an explicit factory/root owns sequencer transport.
- AU3 reset preservation captures/replays the full sequencer transport/pattern parameter range from `kParamSeqEnable` through `kParamSeqStep32Gate` when DrSID/SID808 SEQ authority is owned.
- GUI mode selection no longer forces SeqEnable to zero when selecting/re-selecting DrSID/SID808 mode.
- AU2/AU3 DrumMachine and SID808 state-root flavor policies no longer destructively clear restored SeqEnable.
- File-bank drum-kit persistence preserves saved SeqEnable instead of forcing drum sequencer transport off.
- Phase2/VST SEQ disable now releases DrSID when the active render authority is DrSID instead of sending a misleading BitPerfect note-off.
- Added `SeqDrSidResetPreservationV950Tests`.
