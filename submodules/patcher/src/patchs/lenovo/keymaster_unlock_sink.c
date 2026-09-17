#include "patchs/lenovo/keymaster_unlock_sink.h"
#include "arm64_inst/utils.h"

/* The TEE call argument block keeps the unlock byte and the color word next
 * to each other on the stack: STRB <unlock>, [SP,#0x60] followed by an
 * STP <pair>, [SP,#0x64].  Zeroing the byte keeps KeyMaster from seeing an
 * impossible (unlocked, green) state that would trip the tamper flag.
 */
#define KEYMASTER_SINK_MAX_BYTES 0x400
#define KEYMASTER_UNLOCK_OFF     0x60
#define KEYMASTER_COLOR_OFF      0x64
#define KEYMASTER_COMPANION_SPAN 0x40

static bool companion_stp_at(char* buffer, int32_t size, int32_t off) {
    int32_t end = off + KEYMASTER_COMPANION_SPAN;
    if (end > size - 4) end = size - 4;

    for (int32_t i = off + 4; i <= end; i += 4) {
        uint32_t raw = read_instr(buffer, i);
        if ((raw & 0xFFC00000u) != 0x29000000u) continue;
        if (((raw >> 5) & 0x1Fu) != 31) continue;

        int32_t imm7 = (int32_t)((raw >> 15) & 0x7F);
        if (imm7 & 0x40) imm7 |= ~0x7F;
        if ((uint32_t)(imm7 << 2) == KEYMASTER_COLOR_OFF) return true;
    }
    return false;
}

bool patch_keymaster_unlock_sink(char* buffer, int32_t size, int32_t anchor_off) {
    if (anchor_off < 0 || anchor_off >= size - 4) return false;

    int32_t patched = 0;
    int32_t end = anchor_off + KEYMASTER_SINK_MAX_BYTES;
    if (end > size - 4) end = size - 4;

    for (int32_t off = anchor_off + 4; off <= end; off += 4) {
        DecodedInst d = decode_at(buffer, off);
        if (d.type != INST_STRB_IMM || d.rn != 31
            || d.imm != KEYMASTER_UNLOCK_OFF)
            continue;
        if (!companion_stp_at(buffer, size, off)) continue;

        printf("KeyMaster unlock sink @ 0x%X: STRB W%d,[SP,#0x%X]\n",
               off, d.rt, d.imm);
        printf("  Before: %02X %02X %02X %02X\n",
               (uint8_t)buffer[off], (uint8_t)buffer[off+1],
               (uint8_t)buffer[off+2], (uint8_t)buffer[off+3]);

        write_instr(buffer, off, strb_with_reg(d.raw, 31));

        printf("  After : %02X %02X %02X %02X (Rt -> WZR)\n",
               (uint8_t)buffer[off], (uint8_t)buffer[off+1],
               (uint8_t)buffer[off+2], (uint8_t)buffer[off+3]);
        patched++;
    }

    if (patched == 0)
        printf("keymaster sink: not found\n");
    else if (patched > 1)
        printf("keymaster sink: warning, %d sites patched (expected 1)\n",
               patched);
    return patched > 0;
}
