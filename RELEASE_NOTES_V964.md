# ArpSID v964 C64 SIDPLAY telemetry closure

- Publishes `c64PsidLastParseResult` and `c64PsidLastLoadFailure` through the AU adapter.
- Adds human-readable PSID load-failure names and exact failure reporting in the C64 player UI.
- Surfaces INIT/PLAY addresses, clock model, observed timing model and live play-call count.
- Regression coverage: `C64SidplayTelemetryV964Tests`.
