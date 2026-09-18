#include "patchs/lenovo/flashing_unlock_verify_bypass.h"
#include "arm64_inst/utils.h"
#include <string.h>

/* Skip the unlock-image (sn.img) verification inside the
 * `fastboot flashing unlock` handler while leaving the OEM-unlock gate
 * intact.  The gate lives in a separate function that checks the
 * IsAllowUnlock flag and prints "Flashing Unlock is not allowed"; the
 * verification lives in the unlock handler that ends by calling that gate.
 *
 * Locator:
 *   1. resolve the gate function from the "Flashing Unlock is not allowed"
 *      string xref, and the unlock handler from the "verify_oem_unlock:
 *      Sha256Hash failed" string xref;
 *   2. find the handler's branch (call or tail) into the gate function;
 *   3. jump target = the nearest preceding write to w0 (the gate argument);
 *   4. rewrite the handler's first conditional branch (the verification
 *      result check right after the prologue) as an unconditional branch
 *      to that target.
 *
 * One site per ABL.  ABLs without both strings are left untouched.
 */

#define UNLOCK_GATE_STR    "Flashing Unlock is not allowed"
#define UNLOCK_VERIFY_STR  "verify_oem_unlock: Sha256Hash failed"
#define UNLOCK_BACK_SCAN   0x1000
#define UNLOCK_FWD_SCAN    0x1000
#define UNLOCK_ARG_SCAN    0x40

static int32_t find_unlock_bytes(char* buffer, int32_t size, const char* needle) {
    int32_t len = (int32_t)strlen(needle);
    for (int32_t i = 0; i + len < size; ++i) {
        if (memcmp(buffer + i, needle, len) == 0) return i;
    }
    return -1;
}

static int32_t unlock_func_start(char* buffer, int32_t off) {
    for (int32_t i = off; i >= 0 && off - i <= UNLOCK_BACK_SCAN; i -= 4) {
        if (read_instr(buffer, i) == 0xD503233FU) return i;
    }
    return -1;
}

static int32_t find_unlock_xref(char* buffer, int32_t size, int32_t str_off) {
    for (int32_t off = 0; off + 8 <= size; off += 4) {
        DecodedInst d0 = decode_at(buffer, off);
        if (d0.type != INST_ADRP) continue;
        DecodedInst d1 = decode_at(buffer, off + 4);
        if (d1.type != INST_ADD_X_IMM) continue;
        if (d1.rt != d0.rt || d1.rn != d0.rt) continue;
        if (calc_adrl_file_offset(buffer, off, 0) == (int64_t)str_off) return off;
    }
    return -1;
}

static int32_t branch_target(uint32_t raw, int32_t off) {
    if ((raw & 0xFC000000u) == 0x94000000u || (raw & 0xFC000000u) == 0x14000000u) {
        int32_t imm26 = (int32_t)(raw & 0x03FFFFFFu);
        if (imm26 & 0x02000000) imm26 -= 0x04000000;
        return off + imm26 * 4;
    }
    return -1;
}

static bool is_cond_branch(uint32_t raw) {
    if ((raw & 0xFF000010u) == 0x54000000u) return true;   /* B.cond */
    switch (raw & 0x7F000000u) {
    case 0x34000000u: case 0x35000000u:                    /* CBZ/CBNZ W */
    case 0xB4000000u: case 0xB5000000u:                    /* CBZ/CBNZ X */
    case 0x36000000u: case 0x37000000u:                    /* TBZ/TBNZ */
        return true;
    default:
        return false;
    }
}

int32_t patch_flashing_unlock_verify_bypass(char* buffer, int32_t size) {
    int32_t gate_str = find_unlock_bytes(buffer, size, UNLOCK_GATE_STR);
    int32_t verify_str = find_unlock_bytes(buffer, size, UNLOCK_VERIFY_STR);
    if (gate_str < 0 || verify_str < 0) {
        printf("flashing-unlock verify: anchor strings not present, skipping\n");
        return 0;
    }

    int32_t gate_xref = find_unlock_xref(buffer, size, gate_str);
    int32_t verify_xref = find_unlock_xref(buffer, size, verify_str);
    if (gate_xref < 0 || verify_xref < 0) {
        printf("flashing-unlock verify: string xrefs not found, skipping\n");
        return 0;
    }

    int32_t gate_entry = unlock_func_start(buffer, gate_xref);
    int32_t verify_entry = unlock_func_start(buffer, verify_xref);
    if (gate_entry < 0 || verify_entry < 0) {
        printf("flashing-unlock verify: function boundaries not found, skipping\n");
        return 0;
    }

    /* Handler branch into the gate: a call on some ABLs, a tail branch on
     * others.  The gate argument write sits immediately before it. */
    int32_t gate_branch = -1;
    for (int32_t off = verify_entry; off < size - 4; off += 4) {
        uint32_t raw;
        if (off > verify_entry + UNLOCK_FWD_SCAN) break;
        raw = read_instr(buffer, off);
        if (off != verify_entry && raw == 0xD503233FU) break;
        if (branch_target(raw, off) == gate_entry) {
            /* Only accept a jump that targets the gate function. */
            if ((raw & 0xFC000000u) == 0x94000000u ||
                (raw & 0xFC000000u) == 0x14000000u) {
                gate_branch = off;
                break;
            }
        }
    }
    if (gate_branch < 0) {
        printf("flashing-unlock verify: gate call not found, skipping\n");
        return 0;
    }

    int32_t jump_target = -1;
    for (int32_t off = gate_branch - 4;
         off >= verify_entry && gate_branch - off <= UNLOCK_ARG_SCAN; off -= 4) {
        uint32_t raw = read_instr(buffer, off);
        if ((raw & 0xFFE0001Fu) == 0x52800000u && (raw & 0x1Fu) == 0) {
            jump_target = off;          /* mov w0, #imm */
            break;
        }
        if (raw == 0x2A1F03E0u) {       /* mov w0, wzr */
            jump_target = off;
            break;
        }
    }
    if (jump_target < 0) {
        printf("flashing-unlock verify: gate argument setup not found, skipping\n");
        return 0;
    }

    int32_t bail_site = -1;
    for (int32_t off = verify_entry + 4; off < gate_branch; off += 4) {
        uint32_t raw = read_instr(buffer, off);
        if (raw == 0xD503233FU) break;
        if (is_cond_branch(raw)) {
            bail_site = off;
            break;
        }
    }
    if (bail_site < 0) {
        printf("flashing-unlock verify: verification check not found, skipping\n");
        return 0;
    }

    int32_t delta = (jump_target - bail_site) >> 2;
    if (delta <= 0 || delta >= 0x02000000) {
        printf("flashing-unlock verify: target out of branch range, skipping\n");
        return 0;
    }

    printf("flashing-unlock verify: skip verification at 0x%X -> gate 0x%X"
           " (gate entry 0x%X)\n", bail_site, jump_target, gate_entry);
    printf("  Before: %02X %02X %02X %02X\n",
           (uint8_t)buffer[bail_site], (uint8_t)buffer[bail_site+1],
           (uint8_t)buffer[bail_site+2], (uint8_t)buffer[bail_site+3]);
    write_instr(buffer, bail_site, 0x14000000u | ((uint32_t)delta & 0x03FFFFFFu));
    printf("  After : %02X %02X %02X %02X (B)\n",
           (uint8_t)buffer[bail_site], (uint8_t)buffer[bail_site+1],
           (uint8_t)buffer[bail_site+2], (uint8_t)buffer[bail_site+3]);

    return 1;
}
