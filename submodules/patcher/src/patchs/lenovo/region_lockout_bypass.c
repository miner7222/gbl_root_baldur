#include "patchs/lenovo/region_lockout_bypass.h"
#include "arm64_inst/utils.h"
#include <string.h>

/* Region-mismatch lockout: ABL prints a diagnostic and powers down when the
 * image region tag does not match the device.  Two layouts exist.
 *
 * Direct layout: the diagnostic string has a direct adrp+add xref and the
 * gate is the CBZ immediately before it.  Rewrite the gate as an
 * unconditional B to its own target (the compatible path).  A secondary
 * conditional branch nearby still targets the shared shutdown stub, so
 * redirect the stub's first instruction to the compatible path too.
 *
 * Message-page layout: newer vintages move the diagnostic into a message
 * table, so the string has no direct xref.  The stub still ends with
 * `BL <reset>; B <continue>` after a print cascade; NOP the reset BL so
 * execution falls through to the continue branch.
 */

#define REGION_ANCHOR_DIRECT "region info is not invalid"
#define REGION_ANCHOR_TABLE  "incompatible with hardware"
#define REGION_STUB_SCAN     0x80
#define REGION_MIN_ADRP_REFS 2
#define REGION_MIN_PRINT_BLS 3

static int32_t find_bytes(char* buffer, int32_t size, const char* needle) {
    int32_t len = (int32_t)strlen(needle);
    for (int32_t i = 0; i + len < size; ++i) {
        if (memcmp(buffer + i, needle, len) == 0) return i;
    }
    return -1;
}

static int32_t adrp_page(uint32_t raw, int32_t off) {
    int32_t immlo = (int32_t)((raw >> 29) & 3);
    int32_t immhi = (int32_t)((raw >> 5) & 0x7FFFF);
    int32_t imm21 = (immhi << 2) | immlo;
    if (imm21 & 0x100000) imm21 -= 0x200000;
    return (off & ~0xFFF) + (imm21 << 12);
}

static int32_t decode_cbz_target(uint32_t raw, int32_t off) {
    int32_t imm19 = (int32_t)((raw >> 5) & 0x7FFFF);
    if (imm19 & 0x40000) imm19 -= 0x80000;
    return off + imm19 * 4;
}

static uint32_t encode_b(int32_t from, int32_t to) {
    return 0x14000000u | ((uint32_t)((to - from) >> 2) & 0x03FFFFFFu);
}

int32_t patch_region_lockout_bypass(char* buffer, int32_t size) {
    int32_t anchor_off = find_bytes(buffer, size, REGION_ANCHOR_DIRECT);
    if (anchor_off < 0) {
        printf("region lockout: anchor not present, skipping\n");
        return 0;
    }

    int32_t adrl_off = -1;
    for (int32_t off = 0; off + 8 <= size; off += 4) {
        DecodedInst d0 = decode_at(buffer, off);
        if (d0.type != INST_ADRP) continue;
        DecodedInst d1 = decode_at(buffer, off + 4);
        if (d1.type != INST_ADD_X_IMM) continue;
        if (d1.rt != d0.rt || d1.rn != d0.rt) continue;
        if (calc_adrl_file_offset(buffer, off, 0) == (int64_t)anchor_off) {
            adrl_off = off;
            break;
        }
    }
    if (adrl_off < 0) {
        printf("region lockout: adrp+add ref not found, skipping\n");
        return 0;
    }

    int32_t gate_off = adrl_off - 4;
    if (gate_off < 0) return 0;
    uint32_t gate = read_instr(buffer, gate_off);
    uint8_t hi = (uint8_t)(gate >> 24);
    if (hi != 0xB4 && hi != 0x34) {
        printf("region lockout: gate 0x%08X is not a CBZ, skipping\n", gate);
        return 0;
    }

    int32_t compatible = decode_cbz_target(gate, gate_off);
    if (compatible < 0 || compatible >= size) return 0;

    write_instr(buffer, gate_off, encode_b(gate_off, compatible));
    printf("region lockout: gate 0x%X -> B 0x%X\n", gate_off, compatible);

    int32_t stub_off = -1;
    for (int32_t off = gate_off + 4;
         off + 4 <= size && off < gate_off + REGION_STUB_SCAN; off += 4) {
        uint32_t ins = read_instr(buffer, off);
        int32_t t = -1;

        if ((ins & 0xFF000010u) == 0x54000000u) {
            int32_t imm19 = (int32_t)((ins >> 5) & 0x7FFFF);
            if (imm19 & 0x40000) imm19 -= 0x80000;
            t = off + imm19 * 4;
        } else {
            uint8_t h = (uint8_t)(ins >> 24);
            if (h == 0x34 || h == 0x35 || h == 0xB4 || h == 0xB5)
                t = decode_cbz_target(ins, off);
        }
        if (t < 0 || t >= size || t == compatible) continue;

        stub_off = t;
        break;
    }
    if (stub_off < 0) {
        printf("region lockout: shutdown stub not seen, primary only\n");
        return 1;
    }

    int32_t delta = (compatible - stub_off) >> 2;
    if (delta < -0x2000000 || delta >= 0x2000000) {
        printf("region lockout: stub 0x%X out of B range to 0x%X\n",
               stub_off, compatible);
        return 1;
    }

    write_instr(buffer, stub_off, encode_b(stub_off, compatible));
    printf("region lockout: stub 0x%X -> B 0x%X\n", stub_off, compatible);
    return 2;
}

int32_t patch_region_message_page_bypass(char* buffer, int32_t size) {
    int32_t msg_off = find_bytes(buffer, size, REGION_ANCHOR_TABLE);
    if (msg_off < 0) {
        printf("region message-page: message text not present, skipping\n");
        return 0;
    }
    int32_t msg_page = msg_off & ~0xFFF;

    for (int32_t off = 0x1000; off + 4 <= size; off += 4) {
        uint32_t inst = read_instr(buffer, off);
        if ((inst & 0x9F000000u) != 0x90000000u) continue;
        if (adrp_page(inst, off) != msg_page) continue;

        int32_t limit = off + 0x80;
        if (limit > size - 8) limit = size - 8;

        int32_t adrp_refs = 1;
        int32_t print_bls = 0;

        for (int32_t nx = off + 4; nx + 8 <= limit; nx += 4) {
            uint32_t ins = read_instr(buffer, nx);

            if ((ins & 0x9F000000u) == 0x90000000u) {
                if (adrp_page(ins, nx) == msg_page) adrp_refs++;
                continue;
            }
            if ((ins & 0xFC000000u) != 0x94000000u) continue;
            print_bls++;

            uint32_t br = read_instr(buffer, nx + 4);
            if ((br & 0xFC000000u) != 0x14000000u) continue;
            int32_t b_imm26 = (int32_t)(br & 0x03FFFFFFu);
            if (b_imm26 & 0x2000000) b_imm26 -= 0x4000000;
            if (b_imm26 <= 0) continue;

            int32_t b_target = nx + 4 + b_imm26 * 4;
            if (b_target < 0 || b_target >= size) continue;
            if (adrp_refs < REGION_MIN_ADRP_REFS || print_bls < REGION_MIN_PRINT_BLS)
                continue;

            printf("region message-page: NOP reset call @ 0x%X, continue -> 0x%X\n",
                   nx, b_target);
            write_instr(buffer, nx, NOP);
            return 1;
        }
    }

    printf("region message-page: shutdown pattern not located\n");
    return 0;
}
