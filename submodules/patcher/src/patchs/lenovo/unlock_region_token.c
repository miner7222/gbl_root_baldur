#include "patchs/lenovo/unlock_region_token.h"
#include "arm64_inst/utils.h"
#include <string.h>

/* `oem unlock-region` verifies a challenge token before accepting a region
 * change.  Keep the locked-device guard and the exact region input check;
 * only force the token verification result to success by replacing the
 * verify call with `MOV X0, XZR` (the caller branches on X0 == 0).
 *
 * Locator: the command string is referenced by an 8-byte-aligned
 * { command, handler } entry; inside the handler, the token gate is the
 * first `TST W0, #0xff` followed by a call whose result is tested with
 * `CBZ X0`.
 */

#define UNLOCK_REGION_MAX_BYTES 0x180
#define MOV_X0_XZR 0xAA1F03E0u
#define TST_W0_FF  0x72001C1Fu
#define PACIASP    0xD503233Fu

static bool is_cbz_x0(uint32_t raw) {
    return (raw & 0xFF00001Fu) == 0xB4000000u;
}

static bool is_standalone_cstr(char* buffer, int32_t size, int32_t off,
                               int32_t len) {
    if (off < 0 || off + len >= size) return false;
    if (buffer[off + len] != 0) return false;
    if (off > 0 && buffer[off - 1] != 0) return false;
    return true;
}

static uint64_t read_u64(const char* p) {
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static int32_t patch_handler_token_call(char* buffer, int32_t size,
                                        int32_t handler_off) {
    if (handler_off < 0 || handler_off + 4 > size) return 0;
    if (read_instr(buffer, handler_off) != PACIASP) return 0;

    int32_t end = handler_off + UNLOCK_REGION_MAX_BYTES;
    if (end > size - 8) end = size - 8;

    for (int32_t off = handler_off + 4; off <= end; off += 4) {
        uint32_t raw = read_instr(buffer, off);
        if (raw == PACIASP) break;
        if (raw != TST_W0_FF) continue;

        for (int32_t bl_off = off + 4; bl_off <= end; bl_off += 4) {
            uint32_t bl = read_instr(buffer, bl_off);
            if (bl == PACIASP) break;
            if ((bl & 0xFC000000u) != 0x94000000u) continue;
            if (!is_cbz_x0(read_instr(buffer, bl_off + 4))) continue;

            printf("unlock-region token: handler 0x%X, verify call @ 0x%X "
                   "-> MOV X0, XZR\n", handler_off, bl_off);
            write_instr(buffer, bl_off, MOV_X0_XZR);
            return 1;
        }
    }
    return 0;
}

int32_t patch_unlock_region_token_bypass(char* buffer, int32_t size) {
    static const char cmd[] = "oem unlock-region";
    const int32_t cmd_len = (int32_t)(sizeof(cmd) - 1);

    if (size < cmd_len + 16) return 0;

    for (int32_t cmd_off = 0; cmd_off + cmd_len < size; ++cmd_off) {
        if (memcmp(buffer + cmd_off, cmd, cmd_len) != 0) continue;
        if (!is_standalone_cstr(buffer, size, cmd_off, cmd_len)) continue;

        for (int32_t ent = 0; ent + 16 <= size; ent += 8) {
            if (read_u64(buffer + ent) != (uint64_t)cmd_off) continue;

            uint64_t handler = read_u64(buffer + ent + 8);
            if (handler + 4 > (uint64_t)size) continue;

            if (patch_handler_token_call(buffer, size, (int32_t)handler) == 0)
                continue;

            return 1;
        }
    }

    printf("unlock-region token: command handler not located, skipping\n");
    return 0;
}
