# ArpSID v911 remaining ingress-authority closure

This package is a source-level closure pass over v910. It fixes the remaining audit items without changing the public 0.0.690 project version:

- Async MIDI held-note mirror is now accepted-only. A NoteOn is mirrored only after `midiQueue_.push()` succeeds. Queue-full NoteOn drops can no longer create phantom held notes for factory-root restore or synth-mode reconcile. Accepted/latching NoteOff and CC safety fallbacks still update the relevant mirrors.
- Oversized stereo and mono parent-block chunking now disables child raw async ring drains through `parentChunkAsyncDrainActive_`. Rings are drained once at parent scope; events arriving during recursive chunk rendering are held for the next host block instead of being resolved with child chunk timing.
- Merged async/host chunk events are sorted before recursive dispatch so sample-ordered side effects such as GM DrSID promotion and DIGI triggers cannot be invoked out of order.
- Raw MIDI ring translation now rejects malformed short messages by status length instead of zero-filling missing bytes into note/CC/pressure events.
- CC11 master-volume projection now guards `runtimeExecutionOwner_` before dereference.
- The v910 ingress-parity closure test includes v911 source-contract guards for these fixes.
