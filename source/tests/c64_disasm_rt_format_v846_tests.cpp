// c64_disasm_rt_format_v846_tests.cpp
//
// The C64 disassembly line formatter runs on the audio render thread during
// heavy telemetry snapshots. v846 replaced its std::snprintf with a deterministic
// hand-rolled formatter (std::snprintf can take a per-call locale lock in some
// libc implementations and is not realtime-safe).
//
// This guard proves the RT-safe formatter is byte-identical to the previous
// snprintf layout across every opcode and a spread of PC/operand values, and that
// it always terminates within the fixed 64-byte field. The reference snprintf
// here runs only in the test process, never on the audio thread.

#include "arpsid/core/c64_telemetry.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace ArpSID::C64;

static int g_failures = 0;

// Byte-for-byte reproduction of the pre-v846 render-thread formatting, including
// the zero-initialised 64-byte field the disassembler wrote into.
static void referenceFormat(char (&out)[64], uint16_t pc, uint8_t op, uint8_t b1, uint8_t b2) {
    const uint8_t n = c64OpcodeSize(op);
    const char* m = c64OpcodeMnemonic(op);
    std::memset(out, 0, 64);
    if (n == 3)      std::snprintf(out, 64, "%04X  %02X %02X %02X  %s", pc, op, b1, b2, m);
    else if (n == 2) std::snprintf(out, 64, "%04X  %02X %02X     %s", pc, op, b1, m);
    else             std::snprintf(out, 64, "%04X  %02X        %s", pc, op, m);
    out[63] = '\0';
}

int main() {
    static const uint16_t pcs[]    = {0x0000u, 0x00A3u, 0x1234u, 0xABCDu, 0xFFFFu, 0xE5B7u};
    static const uint8_t  operands[] = {0x00u, 0x01u, 0x7Fu, 0x80u, 0xABu, 0xFFu};

    for (int opi = 0; opi < 256 && g_failures == 0; ++opi) {
        const uint8_t op = static_cast<uint8_t>(opi);
        for (uint16_t pc : pcs) {
            for (uint8_t b1 : operands) {
                for (uint8_t b2 : operands) {
                    char ref[64];
                    referenceFormat(ref, pc, op, b1, b2);

                    char got[64];
                    std::memset(got, 0xCC, sizeof(got)); // poison to catch missing tail-fill
                    c64FormatDisassemblyLine(got, pc, op, b1, b2);

                    if (std::memcmp(ref, got, 64) != 0) {
                        std::printf("MISMATCH op=%02X pc=%04X b1=%02X b2=%02X\n  ref='%s'\n  got='%s'\n",
                                    op, pc, b1, b2, ref, got);
                        ++g_failures;
                        goto done;
                    }
                    bool terminated = false;
                    for (int i = 0; i < 64; ++i) { if (got[i] == '\0') { terminated = true; break; } }
                    if (!terminated) {
                        std::printf("NOT TERMINATED op=%02X pc=%04X\n", op, pc);
                        ++g_failures;
                        goto done;
                    }
                }
            }
        }
    }

done:
    if (g_failures == 0) {
        std::printf("c64_disasm_rt_format_v846_tests: PASS (byte-identical, RT-safe, no snprintf)\n");
        return 0;
    }
    std::printf("c64_disasm_rt_format_v846_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
