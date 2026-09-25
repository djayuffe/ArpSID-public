# ArpSID v949 — SEQ DrSID/SID808 authority closure

v949 separates ARP and SEQ authority laws.

- ARP remains BitPerfect/Classic-only.
- SEQ is valid in Classic/BitPerfect and DrSID/SID808 drum sequencer mode.
- SEQ remains blocked under SynthMode/SID-register.
- AU3 and Phase2 post-batch canonicalization now clear ARP outside BitPerfect, but only clear SEQ where the current mode blocks SEQ.
- DrSID/SID808 mode activation preserves SeqEnable for drum pattern transport.
- Dedicated drum/SID808 flavor enforcement no longer clears SeqEnable; factory/root defaults still load SEQ off.
- GUI SEQ presentation now mirrors DSP law for Classic and DrSID/SID808.
- Added SeqDrSidAuthorityV949Tests.
