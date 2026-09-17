#include "patchs/lenovo/cmdline_region_override.h"
#include "arm64_inst/utils.h"
#include <string.h>

/* ABL emits the regional cmdline arguments from a runtime value (TZ region
 * tag or device buffer).  Force the emitted value by redirecting the value
 * pointer at the emission site to a fixed rodata literal.
 *
 * androidboot.pcbaidinfo: after the adrp+add pair that loads the key string
 * the value pointer is built with `ADD X2, Xn, #imm`; rewrite that to
 * `ADR X2, <literal>`.
 *
 * hqsysfs.pcba_config: the formatter is called twice (key, then value) and
 * the value pointer is the `LDR X2` between the two BLs; rewrite that load
 * to `ADR X2, <literal>`.  Suppressing either emission instead leaves a
 * stray token glued into /proc/cmdline, so the redirect is the only shape
 * that yields exactly `key=value`.
 */

#define PCBAIDINFO_SCAN_WINDOW 0x20
#define HQSYSFS_SCAN_WINDOW    0x34

static int32_t find_bytes(char* buffer, int32_t size, const char* needle) {
    int32_t len = (int32_t)strlen(needle);
    for (int32_t i = 0; i + len < size; ++i) {
        if (memcmp(buffer + i, needle, len) == 0) return i;
    }
    return -1;
}

static int32_t find_region_literal(char* buffer, int32_t size,
                                   const char* region) {
    for (int32_t i = 0; i + 4 < size; ++i) {
        if (buffer[i] == 0
            && buffer[i+1] == region[0]
            && buffer[i+2] == region[1]
            && buffer[i+3] == region[2]
            && buffer[i+4] == 0)
            return i + 1;
    }
    return -1;
}

static int32_t encode_adr_x2(int32_t from, int32_t to, uint32_t* out) {
    int32_t off = to - from;
    if (off < -(1 << 20) || off >= (1 << 20)) return -1;

    uint32_t imm21 = (uint32_t)off & 0x1FFFFFu;
    *out = 0x10000000u
         | ((imm21 & 3u) << 29)
         | (0x10u << 24)
         | (((imm21 >> 2) & 0x7FFFFu) << 5)
         | 2u;
    return 0;
}

int32_t patch_pcbaidinfo_override(char* buffer, int32_t size,
                                  const char* region) {
    static const char key[] = " androidboot.pcbaidinfo=";

    if (!region || !region[0] || !region[1] || !region[2]) return 0;

    int32_t key_off = find_bytes(buffer, size, key);
    if (key_off < 0) {
        printf("cmdline pcbaidinfo: key string not present, skipping\n");
        return 0;
    }

    int32_t lit_off = find_region_literal(buffer, size, region);
    if (lit_off < 0) {
        printf("cmdline pcbaidinfo: region literal not present, skipping\n");
        return 0;
    }

    for (int32_t off = 0x1000; off + 8 <= size; off += 4) {
        DecodedInst d0 = decode_at(buffer, off);
        if (d0.type != INST_ADRP) continue;
        DecodedInst d1 = decode_at(buffer, off + 4);
        if (d1.type != INST_ADD_X_IMM) continue;
        if (d1.rt != d0.rt || d1.rn != d0.rt) continue;
        if (calc_adrl_file_offset(buffer, off, 0) != (int64_t)key_off) continue;

        int32_t sink_off = -1;
        for (int32_t j = off + 8;
             j + 4 <= size && j < off + PCBAIDINFO_SCAN_WINDOW; j += 4) {
            if ((read_instr(buffer, j) & 0xFF80001Fu) == 0x91000002u) {
                sink_off = j;
                break;
            }
        }
        if (sink_off < 0) continue;

        uint32_t adr;
        if (encode_adr_x2(sink_off, lit_off, &adr) != 0) {
            printf("cmdline pcbaidinfo: ADR range exceeded, skipping site\n");
            continue;
        }

        printf("cmdline pcbaidinfo: ADR X2 @ 0x%X -> literal 0x%X\n",
               sink_off, lit_off);
        write_instr(buffer, sink_off, adr);
        return 1;
    }

    printf("cmdline pcbaidinfo: emission site not located\n");
    return 0;
}

int32_t patch_hqsysfs_pcba_config_override(char* buffer, int32_t size,
                                           const char* region) {
    static const char key[] = " hqsysfs.pcba_config=";

    if (!region || !region[0] || !region[1] || !region[2]) return 0;

    int32_t key_off = find_bytes(buffer, size, key);
    if (key_off < 0) {
        printf("cmdline hqsysfs: key string not present, skipping\n");
        return 0;
    }

    int32_t lit_off = find_region_literal(buffer, size, region);
    if (lit_off < 0) {
        printf("cmdline hqsysfs: region literal not present, skipping\n");
        return 0;
    }

    for (int32_t off = 0x1000; off + 8 <= size; off += 4) {
        DecodedInst d0 = decode_at(buffer, off);
        if (d0.type != INST_ADRP) continue;
        DecodedInst d1 = decode_at(buffer, off + 4);
        if (d1.type != INST_ADD_X_IMM) continue;
        if (d1.rt != d0.rt || d1.rn != d0.rt) continue;
        if (calc_adrl_file_offset(buffer, off, 0) != (int64_t)key_off) continue;

        int32_t key_bl = -1, value_bl = -1, value_load = -1;
        for (int32_t j = off + 8;
             j + 4 <= size && j < off + HQSYSFS_SCAN_WINDOW; j += 4) {
            uint32_t raw = read_instr(buffer, j);
            if ((raw & 0xFC000000u) == 0x94000000u) {
                if (key_bl < 0) {
                    key_bl = j;
                } else {
                    value_bl = j;
                    break;
                }
                continue;
            }

            DecodedInst dj = decode_at(buffer, j);
            if (key_bl >= 0 && dj.type == INST_LDR_X_IMM && dj.rt == 2)
                value_load = j;
        }
        if (key_bl < 0 || value_bl < 0 || value_load < 0 || value_load >= value_bl)
            continue;

        uint32_t adr;
        if (encode_adr_x2(value_load, lit_off, &adr) != 0) {
            printf("cmdline hqsysfs: ADR range exceeded, skipping site\n");
            continue;
        }

        printf("cmdline hqsysfs: ADR X2 @ 0x%X -> literal 0x%X\n",
               value_load, lit_off);
        write_instr(buffer, value_load, adr);
        return 1;
    }

    printf("cmdline hqsysfs: emission site not located\n");
    return 0;
}
