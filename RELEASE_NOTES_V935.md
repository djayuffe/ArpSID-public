# v935 SynthMode ARP/SEQ authority complete closure

Status: COMPLETE.

- SynthMode structural reset now rejects stale `ArpEnable=1` and `SeqEnable=1` alongside stale DrSID authority.
- Entering SynthMode in AU3 and Phase2 immediately clears ARP/SEQ raw state and ARP active telemetry.
- AU3 and Phase2 SynthMode orphan/stuck-note cleanup now keys only off top-level SidRegister/SynthMode authority, not raw ARP/SEQ flags.
- Shared `SidRuntimeModel::isArpEnabled()` now reports effective ARP authority only in BitPerfect/classic mode.
- Sequencer engine/telemetry is suppressed while SynthMode owns note authority, preventing stale SeqEnable from poisoning SynthMode runtime.
- Adds a small behavioral guard for shared runtime effective ARP authority plus source guards for reset, transition and cleanup contracts.
- Preserves v927-v934 SynthMode/ARP/Instrument/GUI/backend authority closures.
