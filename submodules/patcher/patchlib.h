#include "types.h"

#include "arm64_inst_decoder.h"

typedef enum { LOC_REG, LOC_STK64, LOC_STK8 } DataLocType;

typedef struct {
    DataLocType type;
    INT32 val;
} DataLoc;

typedef struct {
    DataLoc locs[256];
    INT32 count;
} LocSet;

static BOOLEAN locset_has(const LocSet* s, DataLoc l) {
    for (INT32 i = 0; i < s->count; ++i)
        if (s->locs[i].type == l.type && s->locs[i].val == l.val) return TRUE;
    return FALSE;
}
static BOOLEAN locset_has_reg  (const LocSet* s, INT8 r)   { return locset_has(s, (DataLoc){LOC_REG, r}); }
static BOOLEAN locset_has_stk64(const LocSet* s, UINT32 v) { return locset_has(s, (DataLoc){LOC_STK64, (INT32)v}); }
static BOOLEAN locset_has_stk8 (const LocSet* s, UINT32 v) { return locset_has(s, (DataLoc){LOC_STK8, (INT32)v}); }

static void locset_add(LocSet* s, DataLoc l) {
    if (!locset_has(s, l) && s->count < 256) s->locs[s->count++] = l;
}
static void locset_add_reg  (LocSet* s, INT8 r)   { locset_add(s, (DataLoc){LOC_REG, r}); }
static void locset_add_stk64(LocSet* s, UINT32 v) { locset_add(s, (DataLoc){LOC_STK64, (INT32)v}); }
static void locset_add_stk8 (LocSet* s, UINT32 v) { locset_add(s, (DataLoc){LOC_STK8, (INT32)v}); }

static void locset_del(LocSet* s, DataLoc l) {
    for (INT32 i = 0; i < s->count; ++i) {
        if (s->locs[i].type == l.type && s->locs[i].val == l.val) {
            s->locs[i] = s->locs[s->count - 1];
            s->count--;
            return;
        }
    }
}
static void locset_del_reg  (LocSet* s, INT8 r)   { locset_del(s, (DataLoc){LOC_REG, r}); }
static void locset_del_stk64(LocSet* s, UINT32 v) { locset_del(s, (DataLoc){LOC_STK64, (INT32)v}); }
static void locset_del_stk8 (LocSet* s, UINT32 v) { locset_del(s, (DataLoc){LOC_STK8, (INT32)v}); }

static BOOLEAN locset_empty(const LocSet* s) { return s->count == 0; }

static void locset_print(const LocSet* s) {
    if (s->count == 0) {
        Print_patcher("WARRNING: LocSet empty, using fallback. Will Match any LDRB\n");
        return;
    }
    Print_patcher("  LocSet{");
    for (INT32 i = 0; i < s->count; ++i) {
        if (i) Print_patcher(", ");
        switch (s->locs[i].type) {
            case LOC_REG:   Print_patcher("W%d", s->locs[i].val); break;
            case LOC_STK64: Print_patcher("[SP+0x%X]/64", s->locs[i].val); break;
            case LOC_STK8:  Print_patcher("[SP+0x%X]/8",  s->locs[i].val); break;
        }
    }
    Print_patcher("}\n");
}

/* 判断一条指令是否为 STRB 或 32-bit STR W，并提取字段 */
typedef struct {
    BOOLEAN valid;
    UINT8   rt;
    UINT8   rn;
    UINT32  imm;
    UINT8   size;   /* 1 = STRB, 4 = STR W */
} StoreInfo;

/* legacy alias */
typedef StoreInfo StrbInfo;

static StoreInfo decode_any_store(UINT32 raw) {
    StoreInfo info = { FALSE, 0, 0, 0, 0 };
    DecodedInst d;
    memset(&d, 0, sizeof(d));

    if (decode_inst_strb_imm(raw, &d)) {
        info.valid = TRUE; info.rt = d.rt; info.rn = d.rn; info.imm = d.imm; info.size = 1;
    } else if (decode_inst_strb_post(raw, &d)) {
        info.valid = TRUE; info.rt = d.rt; info.rn = d.rn; info.imm = (UINT32)d.simm & 0x1FF; info.size = 1;
    } else if (decode_inst_strb_pre(raw, &d)) {
        info.valid = TRUE; info.rt = d.rt; info.rn = d.rn; info.imm = (UINT32)d.simm & 0x1FF; info.size = 1;
    } else if (decode_inst_str_w_imm(raw, &d)) {
        info.valid = TRUE; info.rt = d.rt; info.rn = d.rn; info.imm = d.imm; info.size = 4;
    }
    return info;
}

/* Fallback (locset empty) sink acceptance check.
 *
 * Without bounds, the first store after anchor may land on unrelated
 * code (loop scratch on stack, lookup table iteration counters, etc.).
 * Reject obvious non-sinks:
 *   1. distance: within FALLBACK_MAX_BYTES of anchor
 *   2. base register: not SP (real sink writes to a context struct field)
 *   3. offset range: typical device-state field sits at 0x100..0x800
 */
#define FALLBACK_MAX_BYTES 0x40
#define FALLBACK_MIN_FIELD_OFF 0x100
#define FALLBACK_MAX_FIELD_OFF 0x800

static BOOLEAN fallback_sink_acceptable(StoreInfo si, INT32 off, INT32 anchor_off) {
    if (off - anchor_off > FALLBACK_MAX_BYTES) return FALSE;
    if (si.rn == 31) return FALSE;
    if (si.imm < FALLBACK_MIN_FIELD_OFF || si.imm > FALLBACK_MAX_FIELD_OFF) return FALSE;
    return TRUE;
}

INT32 patch_abl_gbl(CHAR8* buffer, INT32 size) {
    CHAR8 target[]      = { 'e',0, 'f',0, 'i',0, 's',0, 'p',0 };
    CHAR8 replacement[] = { 'n',0, 'u',0, 'l',0, 'l',0, 's',0 };
    INT32 target_len = sizeof(target);
    for (INT32 i = 0; i < size - target_len; ++i) {
        if (memcmp_patcher(buffer + i, target, target_len) == 0) {
            memcpy_patcher(buffer + i, replacement, target_len);
            return 0;
        }
    }
    return -1;
}

INT16 Original[] = {
    -1, 0x00, 0x00, 0x34, 0x28, 0x00, 0x80, 0x52,
    0x06, 0x00, 0x00, 0x14, 0xE8, -1, 0x40, 0xF9,
    0x08, 0x01, 0x40, 0x39, 0x1F, 0x01, 0x00, 0x71,
    0xE8, 0x07, 0x9F, 0x1A, 0x08, 0x79, 0x1F, 0x53
};
INT16 Patched[] = {
    -1, -1, -1, -1, 0x08, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};

INT32 patch_abl_bootstate(CHAR8* buffer, INT32 size,
                          INT8* lock_register_num, INT32* offset) {
    INT32 pattern_len = sizeof(Original) / sizeof(INT16);
    INT32 patched_count = 0;
    if (size < pattern_len) return 0;
    for (INT32 i = 0; i <= size - pattern_len; ++i) {
        BOOLEAN match = TRUE;
        for (INT32 j = 0; j < pattern_len; ++j) {
            if (Original[j] != -1 && (UINT8)buffer[i + j] != (UINT8)Original[j]) {
                match = FALSE; break;
            }
        }
        if (match) {
            *lock_register_num = (INT8)((UINT8)buffer[i] & 0x1F);
            *offset = i;
            #ifndef DISABLE_PATCH_3
            for (INT32 j = 0; j < pattern_len; ++j)
                if (Patched[j] != -1) buffer[i + j] = (char)Patched[j];
            #endif
            patched_count++;
            i += pattern_len - 1;
        }
    }
    return patched_count;
}

static INT32 track_forward_patch_strb(CHAR8* buffer, INT32 size, INT32 ldrb_off,
                                      INT8 src_reg, INT32 anchor_off) {
    LocSet set;
    set.count = 0;
    locset_add_reg(&set, src_reg);

    Print_patcher("\n=== Forward tracking from LDRB@0x%X (W%d), anchor=0x%X ===\n",
           ldrb_off, (int)src_reg, anchor_off);
    locset_print(&set);

    for (INT32 off = ldrb_off + 4; off < size - 4; off += 4) {
        DecodedInst d = decode_at(buffer, off);

        if (d.type == INST_PACIASP) {
            Print_patcher("0x%X: function boundary, stop\n", off);
            break;
        }

        switch (d.type) {

        /* ---- STR Xt, [SP, #imm] 64-bit spill ---- */
        case INST_STR_X_IMM:
            if (d.rn == 31) {
                if (locset_has_reg(&set, (INT8)d.rt)) {
                    Print_patcher("  0x%X: STR X%d,[SP,#0x%X] spill64\n", off, d.rt, d.imm);
                    locset_add_stk64(&set, d.imm);
                    locset_print(&set);
                } else if (locset_has_stk64(&set, d.imm)) {
                    Print_patcher("  0x%X: STR X%d,[SP,#0x%X] overwrite stk64 -> del\n", off, d.rt, d.imm);
                    locset_del_stk64(&set, d.imm);
                    locset_print(&set);
                }
            }
            break;

        /* ---- LDR Xt, [SP, #imm] 64-bit reload ---- */
        case INST_LDR_X_IMM:
            if (d.rn == 31) {
                if (locset_has_stk64(&set, d.imm)) {
                    Print_patcher("  0x%X: LDR X%d,[SP,#0x%X] reload64\n", off, d.rt, d.imm);
                    locset_add_reg(&set, (INT8)d.rt);
                    locset_print(&set);
                } else if (locset_has_reg(&set, (INT8)d.rt)) {
                    Print_patcher("  0x%X: LDR X%d,[SP,#0x%X] overwrite reg -> del\n", off, d.rt, d.imm);
                    locset_del_reg(&set, (INT8)d.rt);
                    locset_print(&set);
                }
            }
            break;

        /* INST_STR_W_IMM SP-spill handling moved into the unified case below. */

        /* ---- LDR Wt, [SP, #imm] 32-bit reload ---- */
        case INST_LDR_W_IMM:
            if (d.rn == 31) {
                if (locset_has_stk64(&set, d.imm)) {
                    Print_patcher("  0x%X: LDR W%d,[SP,#0x%X] reload32\n", off, d.rt, d.imm);
                    locset_add_reg(&set, (INT8)d.rt);
                    locset_print(&set);
                } else if (locset_has_reg(&set, (INT8)d.rt)) {
                    Print_patcher("  0x%X: LDR W%d,[SP,#0x%X] overwrite reg -> del\n", off, d.rt, d.imm);
                    locset_del_reg(&set, (INT8)d.rt);
                    locset_print(&set);
                }
            }
            break;

        /* ---- LDRB Wt, [Xn, #imm] — 外部内存覆写寄存器 ---- */
        case INST_LDRB_IMM:
            if (locset_has_reg(&set, (INT8)d.rt)) {
                Print_patcher("  0x%X: LDRB W%d,[X%d,#0x%X] overwrite reg -> del\n",
                       off, d.rt, d.rn, d.imm);
                locset_del_reg(&set, (INT8)d.rt);
                locset_print(&set);
            }
            break;

        /* ---- MOV Xd, Xm ---- */
        case INST_MOV_X:
            if (locset_has_reg(&set, (INT8)d.rm) && d.rt != 31) {
                Print_patcher("  0x%X: MOV X%d,X%d propagate\n", off, d.rt, d.rm);
                locset_add_reg(&set, (INT8)d.rt);
                locset_print(&set);
            } else if (locset_has_reg(&set, (INT8)d.rt)) {
                Print_patcher("  0x%X: MOV X%d,X%d overwrite -> del\n", off, d.rt, d.rm);
                locset_del_reg(&set, (INT8)d.rt);
                locset_print(&set);
            }
            break;

        /* ---- MOV Wd, Wm ---- */
        case INST_MOV_W:
            if (locset_has_reg(&set, (INT8)d.rm) && d.rt != 31) {
                Print_patcher("  0x%X: MOV W%d,W%d propagate\n", off, d.rt, d.rm);
                locset_add_reg(&set, (INT8)d.rt);
                locset_print(&set);
            } else if (locset_has_reg(&set, (INT8)d.rt)) {
                Print_patcher("  0x%X: MOV W%d,W%d overwrite -> del\n", off, d.rt, d.rm);
                locset_del_reg(&set, (INT8)d.rt);
                locset_print(&set);
            }
            break;

        /* ---- STRB / STR W (所有形式) ----
         * Boot-state field may be 1 byte (STRB) or 4 bytes (STR W).
         * Fallback path is gated by fallback_sink_acceptable to avoid
         * patching unrelated stores after the anchor.
         */
        case INST_STRB_IMM:
        case INST_STRB_POST:
        case INST_STRB_PRE:
        case INST_STR_W_IMM: {
            StoreInfo si = decode_any_store(d.raw);
            BOOLEAN tracked = si.valid && locset_has_reg(&set, (INT8)si.rt);
            BOOLEAN fallback = si.valid && locset_empty(&set) && off > anchor_off
                               && fallback_sink_acceptable(si, off, anchor_off);
            if (tracked || fallback) {
                if (off > anchor_off) {
                    #ifndef DISABLE_PRINT
                    const CHAR8* mnem = (si.size == 1) ? "STRB" : "STR ";
                    if (si.rn == 31) {
                        Print_patcher("  0x%X: %s W%d,[SP,#0x%X] ** SINK (after anchor0x%X)%s **\n",
                        off, mnem, si.rt, si.imm, anchor_off, fallback ? " [fallback]" : "");
                    } else {
                        Print_patcher("  0x%X: %s W%d,[X%d,#0x%X] ** SINK (after anchor0x%X)%s **\n",
                        off, mnem, si.rt, si.rn, si.imm, anchor_off, fallback ? " [fallback]" : "");
                    }
                    #endif
                    Print_patcher("  Before: %02X %02X %02X %02X\n",
                           (UINT8)buffer[off], (UINT8)buffer[off+1],
                           (UINT8)buffer[off+2], (UINT8)buffer[off+3]);

                    /* Rt is bits[0:4] for both STRB and STR W, so the
                     * STRB rewrite helper applies unchanged. */
                    write_instr(buffer, off, strb_with_reg(d.raw, 31));

                    Print_patcher("  After : %02X %02X %02X %02X (Rt -> WZR)\n",
                           (UINT8)buffer[off], (UINT8)buffer[off+1],
                           (UINT8)buffer[off+2], (UINT8)buffer[off+3]);
                    return 1;
                } else if (si.size == 1) {
                    Print_patcher("  0x%X: STRB W%d,[X%d,#0x%X] before anchor -> spill8\n",
                           off, si.rt, si.rn, si.imm);
                    if (si.rn == 31) locset_add_stk8(&set, si.imm);
                    locset_print(&set);
                } else if (si.size == 4 && si.rn == 31) {
                    /* 32-bit spill tracked via stk64 slot (no stk32 category) */
                    Print_patcher("  0x%X: STR W%d,[SP,#0x%X] before anchor -> spill32\n",
                           off, si.rt, si.imm);
                    locset_add_stk64(&set, si.imm);
                    locset_print(&set);
                }
            } else if (si.valid && si.size == 1 && si.rn == 31
                       && locset_has_stk8(&set, si.imm)) {
                Print_patcher("  0x%X: STRB W%d,[SP,#0x%X] overwrite stk8 -> del\n",
                       off, si.rt, si.imm);
                locset_del_stk8(&set, si.imm);
            } else if (si.valid && si.size == 4 && si.rn == 31
                       && locset_has_stk64(&set, si.imm)) {
                Print_patcher("  0x%X: STR W%d,[SP,#0x%X] overwrite stk -> del\n",
                       off, si.rt, si.imm);
                locset_del_stk64(&set, si.imm);
            }
            break;
        }

        default:
            break;
        }
    }

    Print_patcher("Forward tracking: no sink STRB found after anchor 0x%X\n", anchor_off);
    return -1;
}
//
INT32 source_callback(CHAR8* buffer, INT32 size, INT32 now_offset, INT8 current_target, INT32 anchor_offset) {
    Print_patcher("  Before: %02X %02X %02X %02X\n",
        (UINT8)buffer[now_offset], (UINT8)buffer[now_offset+1],
        (UINT8)buffer[now_offset+2], (UINT8)buffer[now_offset+3]);
    #ifndef DISABLE_PATCH_4
    write_instr(buffer, now_offset, encode_movz_w((UINT8)current_target, 1));
    Print_patcher("  After : %02X %02X %02X %02X (MOV W%d, #1)\n",
        (UINT8)buffer[now_offset], (UINT8)buffer[now_offset+1],
        (UINT8)buffer[now_offset+2], (UINT8)buffer[now_offset+3],
        (int)current_target);
    #endif
    #ifndef DISABLE_PATCH_5
    INT32 fwd = track_forward_patch_strb(buffer, size, now_offset, current_target, anchor_offset);
    if (fwd <= 0) {
        Print_patcher("Warning: sink STRB not found after anchor 0x%X\n", anchor_offset);
        return -1;
    }
    Print_patcher("Sink patched successfully.\n");
    #endif
    return 0;
}
INT32 empty_callback(CHAR8* buffer, INT32 size, INT32 now_offset, INT8 current_target, INT32 anchor_offset) {
    return 0;
}
//定义 callback 函数类型
typedef INT32 (*SourceCallback)(CHAR8* buffer, INT32 size, INT32 now_offset, INT8 current_target, INT32 anchor_offset);
/* ============================================================
 *  第十二部分：反向找 LDRB 源头
 * ============================================================ */
INT32 find_ldrB_instructio_reverse(CHAR8* buffer, INT32 size,
                                   INT32 anchor_offset, INT8 target_register,INT32* global_var_offset, SourceCallback callback) {
    INT32 now_offset = anchor_offset - 4;
    INT8 current_target = target_register;
    INT32 bounce_count = 0;
    const INT32 MAX_BOUNCES = 8;

    while (now_offset >= 0) {
        DecodedInst d = decode_at(buffer, now_offset);

        if (d.type == INST_PACIASP) {
            Print_patcher("Reached function start at 0x%X\n", now_offset);
            break;
        }

        /* ---- 64-bit 栈 reload 弹跳 ---- */
        if (d.type == INST_LDR_X_IMM && d.rn == 31 && (INT8)d.rt == current_target) {
            UINT32 spill_imm = d.imm;
            Print_patcher("Bounce at 0x%X: LDR X%d,[SP,#0x%X]\n",
                   now_offset, (int)current_target, spill_imm);
            INT32 search = now_offset - 4;
            BOOLEAN found = FALSE;
            while (search >= 0) {
                DecodedInst ds = decode_at(buffer, search);
                if (ds.type == INST_PACIASP) break;
                if (ds.type == INST_STR_X_IMM && ds.rn == 31 && ds.imm == spill_imm) {
                    Print_patcher("  -> STR X%d,[SP,#0x%X] at 0x%X\n",
                           (INT32)ds.rt, spill_imm, search);
                    current_target = (INT8)ds.rt;
                    now_offset = search - 4;
                    found = TRUE;
                    bounce_count++;
                    break;
                }
                search -= 4;
            }
            if (!found) { Print_patcher("  -> No matching STR, abort\n"); return -1; }
            if (bounce_count > MAX_BOUNCES) { Print_patcher("Too many bounces\n"); return -1; }
            continue;
        }

        /* ---- byte 级栈 reload 弹跳 ---- */
        if (d.type == INST_LDRB_IMM && d.rn == 31 && (INT8)d.rt == current_target) {
            UINT32 byte_imm = d.imm;
            Print_patcher("Byte bounce at 0x%X: LDRB W%d,[SP,#0x%X]\n",
                   now_offset, (int)current_target, byte_imm);
            INT32 search = now_offset - 4;
            BOOLEAN found = FALSE;
            while (search >= 0) {
                DecodedInst ds = decode_at(buffer, search);
                if (ds.type == INST_PACIASP) break;
                if (ds.type == INST_STRB_IMM && ds.rn == 31 && ds.imm == byte_imm) {
                    Print_patcher("  -> STRB W%d,[SP,#0x%X] at 0x%X\n",
                           (INT32)ds.rt, byte_imm, search);
                    current_target = (INT8)ds.rt;
                    now_offset = search - 4;
                    found = TRUE;
                    bounce_count++;
                    break;
                }
                search -= 4;
            }
            if (!found) { Print_patcher("  -> No matching STRB, abort\n"); return -1; }
            if (bounce_count > MAX_BOUNCES) { Print_patcher("Too many bounces\n"); return -1; }
            continue;
        }

        /* ---- 真正源头: LDRB W{current_target}, [Xn!=SP, #imm] ---- */
        if (d.type == INST_LDRB_IMM && (INT8)d.rt == current_target && d.rn != 31) {
            Print_patcher("Found source LDRB at 0x%X: LDRB W%d,[X%d,#0x%X](%d bounces)\n",
                   now_offset, d.rt, d.rn, d.imm, bounce_count);
            *global_var_offset = (INT32)d.imm;
            return callback(buffer, size, now_offset, current_target, anchor_offset);
        }

        now_offset -= 4;
    }
    return -1;
}

static BOOLEAN str_at(const CHAR8* buffer, INT32 size, INT64 file_off, const CHAR8* needle) {
    if (file_off < 0) return FALSE;
    INT32 len = strlen(needle);
    if ((INT32)file_off + len >= size) return FALSE;
    return memcmp_patcher(buffer + file_off, needle, len) == 0;
}

static INT64 calc_adrl_file_offset(const CHAR8* buffer, INT32 adrp_off, UINT64 load_base) {
    DecodedInst d0 = decode_at(buffer, adrp_off);
    DecodedInst d1 = decode_at(buffer, adrp_off + 4);

    if (d0.type != INST_ADRP) return -1;
    if (d1.type != INST_ADD_X_IMM) return -1;
    if (d1.rt != d0.rt || d1.rn != d0.rt) return -1;

    UINT64 pc      = load_base + (UINT64)adrp_off;
    UINT64 page_pc = pc & ~0xFFFull;
    UINT64 target_va = (UINT64)((INT64)page_pc + d0.simm) + d1.imm;
    return (INT64)(target_va - load_base);
}

INT32 patch_adrl_unlocked_to_locked(CHAR8* buffer, INT32 size, UINT64 load_base) {
    if (size < 24) return 0;
    INT32 patched = 0;

    for (INT32 i = 0; i <= size - 24; i += 4) {
        DecodedInst a0 = decode_at(buffer, i);
        DecodedInst a1 = decode_at(buffer, i + 4);
        DecodedInst b0 = decode_at(buffer, i + 8);
        DecodedInst b1 = decode_at(buffer, i + 12);

        if (a0.type != INST_ADRP || a1.type != INST_ADD_X_IMM) continue;
        if (a1.rt != a0.rt || a1.rn != a0.rt) continue;

        if (b0.type != INST_ADRP || b1.type != INST_ADD_X_IMM) continue;
        if (b1.rt != b0.rt || b1.rn != b0.rt) continue;


        UINT8 xa = a0.rt, xb = b0.rt;
        if (xa == xb) continue;

        INT64 off0 = calc_adrl_file_offset(buffer, i,      load_base);
        INT64 off1 = calc_adrl_file_offset(buffer, i + 8,  load_base);

        if (!str_at(buffer, size, off0, "unlocked")) continue;
        if (!str_at(buffer, size, off1, "locked"))   continue;
        BOOLEAN match = FALSE;
        for(int j=i+16; j<=i+40;j+=4){
            DecodedInst c0 = decode_at(buffer, j);
            DecodedInst c1 = decode_at(buffer, j + 4);
            if(c0.type == INST_ADRP && c1.type == INST_ADD_X_IMM){
                INT64 offc = calc_adrl_file_offset(buffer, j, load_base);
                if(str_at(buffer, size, offc, "androidboot.vbmeta.device_state")){
                    match = TRUE;
                    break;
                }
            }
        }
        if (!match) continue;
        Print_patcher("Found ADRL triple at 0x%X:\n", i);
        Print_patcher("  [0x%X] ADRP+ADD X%d -> file:0x%llX \"unlocked\"\n",
               i, xa, (unsigned long long)off0);
        Print_patcher("  [0x%X] ADRP+ADD X%d -> file:0x%llX \"locked\"\n",
               i+8, xb, (unsigned long long)off1);

        UINT32 new_adrp = adrp_with_rd(b0.raw, xa);
        UINT32 new_add  = add_with_reg(b1.raw, xa);

        Print_patcher("  Patch pair-0: ADRP %08X->%08X, ADD %08X->%08X\n",
               a0.raw, new_adrp, a1.raw, new_add);

        write_instr(buffer, i,     new_adrp);
        write_instr(buffer, i + 4, new_add);

        patched++;
        i += 20;
    }

    if (patched == 0)
        Print_patcher("ADRL triple not found\n");
    else
        Print_patcher("ADRL patch applied: %d location(s)\n", patched);

    return patched;
}

CHAR8 keyword []="is not allowed in Lock State";
BOOLEAN check_sub_string(CHAR8* str,CHAR8* keyword){
    INT32 len = 0;
    INT32 str_len = 0;
    while(str[str_len]) str_len++;
    while(keyword[len]) len++;
    for (INT32 i = 0; i <= str_len - len; ++i) {
        if (memcmp_patcher(str + i, keyword, len) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOLEAN patch_string_jump(CHAR8* buffer, INT32 size) {
    BOOLEAN patched = FALSE;
    for(int i = 0; i < size - 4; i += 4) {
        DecodedInst d = decode_at(buffer, i);
        INT64 jmp=0;
        if(get_JUMP_target(&d,i,&jmp)){
            if (jmp < 0 || jmp + 8 > size) continue;
            DecodedInst a0 = decode_at(buffer, jmp);
            DecodedInst a1 = decode_at(buffer, jmp + 4);
            if (a0.type != INST_ADRP || a1.type != INST_ADD_X_IMM) continue;
            if (a1.rt != a0.rt || a1.rn != a0.rt) continue;
            INT64 off0 = calc_adrl_file_offset(buffer, jmp, 0);
            if (off0 < 0 || off0 >= size) continue;
            CHAR8* str = buffer + off0;
            if(check_sub_string(str, keyword)){
                Print_patcher("  String: %s\n", str);
                //nop the jump instruction
                write_instr(buffer, i, NOP); // NOP
                patched = TRUE;
            }
        }
    }
    return patched;
}

INT32 find_warning_offset(CHAR8* buffer, INT32 size, UINT64 load_base) {
    if (size < 24) return 0;

    for (INT32 i = 0; i <= size - 24; i += 4) {
        DecodedInst a0 = decode_at(buffer, i);
        DecodedInst a1 = decode_at(buffer, i + 4);
        DecodedInst b0 = decode_at(buffer, i + 8);
        DecodedInst b1 = decode_at(buffer, i + 12);

        if (a0.type != INST_ADRP || a1.type != INST_ADD_X_IMM) continue;
        if (a1.rt != a0.rt || a1.rn != a0.rt) continue;

        if (b0.type != INST_ADRP || b1.type != INST_ADD_X_IMM) continue;
        if (b1.rt != b0.rt || b1.rn != b0.rt) continue;


        UINT8 xa = a0.rt, xb = b0.rt;
        if (xa == xb) continue;

        INT64 off0 = calc_adrl_file_offset(buffer, i,      load_base);
        INT64 off1 = calc_adrl_file_offset(buffer, i + 8,  load_base);

        if (!str_at(buffer, size, off0, "Orange State\n")) continue;
        if (!str_at(buffer, size, off1, "Your device has been unlocked and can't be trusted\n"))   continue;
        return i;
    }
    return -1;
}

BOOLEAN patch_warning(CHAR8* buffer, INT32 size, INT32 global_var_offset) {
    INT32 warn_off = find_warning_offset(buffer, size, 0);
    if (warn_off < 0) {
        Print_patcher("Warning not found\n");
        return FALSE;
    }
    Print_patcher("Warning message ADRL offset: 0x%X\n", warn_off);
    //逆向找到CBZ
    INT32 max_search_range = warn_off - 64 > 0 ? warn_off - 64 : 0;//限制搜索范围，避免误伤其他CBZ
    for(INT32 i = warn_off - 4; i >= max_search_range; i -= 4) {
        DecodedInst d = decode_at(buffer, i);
        if (d.type == INST_PACIASP) {
            Print_patcher("Reached function start at 0x%X, stop\n", i);
            return FALSE;
        }
        if (d.type == INST_CBZ_W) {
            Print_patcher("Register W%d is used for warning jump at 0x%X\n", d.rt, i);
            //track back
            INT32 offset = -1;
            find_ldrB_instructio_reverse(buffer, size, i, (INT8)d.rt, &offset, empty_callback);
            if (offset < 0 || offset != global_var_offset) continue; // not from lock state var, skip
            Print_patcher("Warning jump source var: 0x%X Matched\n", offset);
            write_instr(buffer,i,change_rt(&d, 31)); // change to CBZ WZR
            Print_patcher("Patched CBZ at 0x%X to use WZR, warning disabled\n", i);
            break;
        }
    }
    return TRUE;
}

/* Region-bind compatibility bypass.
 *
 * Anchor: diagnostic string printed when image / device region tags
 * mismatch (typo "is not invalid" intact in known vintages).
 *
 * Stage A: Rewrite the cbz Xt sitting one instruction before the
 * adrp+add to that string into an unconditional B with the same
 * compatible-path target. The primary mismatch branch can no longer
 * fall into the shutdown sequence.
 *
 * Stage B: Two secondary shutdown branches (B.EQ and CBNZ Xt) in the
 * same block remain reachable from alternative entries that bypass
 * the cbz. They both target a single shutdown stub. Overwrite that
 * stub's first instruction with an unconditional B to the same
 * compatible-path target so every incoming branch becomes a no-op
 * redirect.
 *
 * 8-byte runtime patch (4 + 4). Stage A alone is enough for the
 * common direct-mismatch case; Stage B closes the rarer secondary
 * paths. No-op on images without the anchor.
 */
INT32 patch_region_lockout_bypass(CHAR8* buffer, INT32 size) {
    static const CHAR8 anchor[] = "region info is not invalid";
    INT32 anchor_len = (INT32)(sizeof(anchor) - 1);

    /* Locate the anchor string. */
    INT32 str_off = -1;
    for (INT32 i = 0; i + anchor_len < size; ++i) {
        if (memcmp_patcher(buffer + i, anchor, anchor_len) == 0) {
            str_off = i;
            break;
        }
    }
    if (str_off < 0) {
        Print_patcher("region bypass: anchor not found, skipping\n");
        return 0;
    }
    Print_patcher("region bypass: anchor @ 0x%X\n", str_off);

    /* Locate the adrp+add pair that materialises the anchor address. */
    INT32 adrl_off = -1;
    for (INT32 i = 0; i + 8 <= size; i += 4) {
        DecodedInst d0 = decode_at(buffer, i);
        if (d0.type != INST_ADRP) continue;
        DecodedInst d1 = decode_at(buffer, i + 4);
        if (d1.type != INST_ADD_X_IMM) continue;
        if (d1.rt != d0.rt || d1.rn != d0.rt) continue;
        INT64 t = calc_adrl_file_offset(buffer, i, 0);
        if (t == (INT64)str_off) {
            adrl_off = i;
            break;
        }
    }
    if (adrl_off < 0) {
        Print_patcher("region bypass: adrp+add ref not found, skipping\n");
        return 0;
    }
    Print_patcher("region bypass: adrp+add ref @ 0x%X\n", adrl_off);

    /* The gating branch sits exactly one instruction before the adrp. */
    INT32 cbz_off = adrl_off - 4;
    if (cbz_off < 0 || cbz_off + 4 > size) {
        Print_patcher("region bypass: pre-anchor offset out of range\n");
        return 0;
    }
    UINT32 v = *(UINT32*)(buffer + cbz_off);

    /* Accept either CBZ Xt (0xb4) or CBZ Wt (0x34). */
    UINT8 hi = (UINT8)((v >> 24) & 0xff);
    if (hi != 0xb4 && hi != 0x34) {
        Print_patcher("region bypass: pre-anchor instr is 0x%08X (hi=0x%02x),"
                      " expected CBZ; skipping\n", v, hi);
        return 0;
    }

    /* Decode CBZ target. imm19 is signed and scaled by 4. */
    INT32 imm19 = (INT32)((v >> 5) & 0x7ffff);
    if (imm19 & 0x40000) imm19 -= 0x80000;
    INT32 target = cbz_off + imm19 * 4;

    if (target < 0 || target >= size) {
        Print_patcher("region bypass: cbz target 0x%X out of range\n", target);
        return 0;
    }
    Print_patcher("region bypass: cbz @ 0x%X -> compatible target 0x%X\n",
                  cbz_off, target);

    /* Stage A: rewrite cbz as unconditional B to the compatible target.
     * imm26 is signed, scaled by 4, range +/- 128 MB which always
     * covers a same-function branch. */
    INT32 imm26 = (target - cbz_off) >> 2;
    UINT32 new_v = 0x14000000U | ((UINT32)imm26 & 0x3ffffffU);

    Print_patcher("region bypass: patching 0x%08X -> 0x%08X\n", v, new_v);
    *(UINT32*)(buffer + cbz_off) = new_v;

    /* Stage B: find the secondary shutdown stub and redirect it to the
     * compatible target. Scan a short window past the primary site for
     * B.cond / CBZ Xt / CBNZ Xt / CBZ Wt / CBNZ Wt whose target differs
     * from the compatible target -- those land on the shutdown stub. */
    INT32 shutdown_tgt = -1;
    for (INT32 i = cbz_off + 4; i + 4 <= size && i < cbz_off + 0x80; i += 4) {
        UINT32 ins = read_instr(buffer, i);
        INT32 t = -1;

        /* B.cond: 0x54xxxxxC where C in [0,15] */
        if ((ins & 0xFF000010) == 0x54000000) {
            INT32 b_imm19 = (INT32)((ins >> 5) & 0x7ffff);
            if (b_imm19 & 0x40000) b_imm19 -= 0x80000;
            t = i + b_imm19 * 4;
        } else {
            UINT8 hi2 = (UINT8)((ins >> 24) & 0xff);
            if (hi2 == 0x34 || hi2 == 0x35 || hi2 == 0xb4 || hi2 == 0xb5) {
                INT32 c_imm19 = (INT32)((ins >> 5) & 0x7ffff);
                if (c_imm19 & 0x40000) c_imm19 -= 0x80000;
                t = i + c_imm19 * 4;
            }
        }
        if (t < 0 || t >= size) continue;
        if (t == target) continue; /* compatible-path branch, leave alone */

        shutdown_tgt = t;
        break;
    }
    if (shutdown_tgt < 0) {
        Print_patcher("region bypass: secondary shutdown stub not seen,"
                      " stage A only\n");
        return 1;
    }

    /* Sanity: stub must be backward (earlier in the section) and within
     * reach of an unconditional B from itself. */
    INT32 redirect_imm26 = (target - shutdown_tgt) >> 2;
    if (redirect_imm26 < -0x2000000 || redirect_imm26 >= 0x2000000) {
        Print_patcher("region bypass: stub 0x%X out of B range to 0x%X\n",
                      shutdown_tgt, target);
        return 1;
    }

    UINT32 stub_new = 0x14000000U | ((UINT32)redirect_imm26 & 0x3ffffffU);
    Print_patcher("region bypass: stub @ 0x%X 0x%08X -> 0x%08X (B 0x%X)\n",
                  shutdown_tgt, read_instr(buffer, shutdown_tgt),
                  stub_new, target);
    write_instr(buffer, shutdown_tgt, stub_new);

    return 2;
}

/* Stage C — message-page universal bypass.
 *
 * Newer vintages move the diagnostic strings into a structured message
 * table indexed by record id. The cbz X0 anchor used by Stage A no
 * longer has a direct adrp+add xref (string-payload lookup is
 * indirected through the table), so Stage A/B silently no-op. The
 * shutdown stub still exists and still ends with `BL <reset>; B
 * <continue>` after one or more `BL <print>` calls preceded by
 * adrp+add references to the message page.
 *
 * Anchor: ASCII substring "incompatible with hardware" anywhere in the
 * binary. Compute its page; walk .text for the first adrp+add pair
 * targeting that page, then scan forward up to 0x80 bytes for the
 * trailing `BL X; B Y` pair with Y a forward branch. Overwrite the BL
 * with NOP. Execution falls through to the unconditional B which
 * continues boot instead of calling ResetSystem.
 *
 * Selectivity: requires the candidate window to contain >= 2 adrp
 * pairs aimed at the same page and >= 3 BL instructions before the
 * trailing BL+B (printf cascade plus reset). These constraints uniquely
 * identify the region-mismatch shutdown stub across known vintages and
 * keep unrelated `BL; B` sequences from being touched.
 *
 * 4-byte runtime patch (single NOP). No-op on images without the
 * message text.
 */
INT32 patch_region_bypass_stage_c(CHAR8* buffer, INT32 size) {
    static const CHAR8 needle[] = "incompatible with hardware";
    INT32 needle_len = (INT32)(sizeof(needle) - 1);

    INT32 msg_off = -1;
    for (INT32 i = 0; i + needle_len < size; ++i) {
        if (memcmp_patcher(buffer + i, needle, needle_len) == 0) {
            msg_off = i;
            break;
        }
    }
    if (msg_off < 0) {
        Print_patcher("region bypass C: message text not present, skipping\n");
        return 0;
    }
    INT32 msg_page = msg_off & ~0xfff;
    Print_patcher("region bypass C: msg @ 0x%X (page 0x%X)\n", msg_off, msg_page);

    for (INT32 i = 0x1000; i + 4 <= size; i += 4) {
        UINT32 inst = read_instr(buffer, i);
        if ((inst & 0x9F000000u) != 0x90000000u) continue;
        /* decode adrp imm21 -> page target */
        INT32 immlo = (INT32)((inst >> 29) & 3);
        INT32 immhi = (INT32)((inst >> 5) & 0x7ffff);
        INT32 imm21 = (immhi << 2) | immlo;
        if (imm21 & 0x100000) imm21 -= 0x200000;
        INT32 page = (i & ~0xfff) + (imm21 << 12);
        if (page != msg_page) continue;

        /* Scan a short forward window for BL+B trail plus selectivity
         * counters. Counters seed with this adrp (1 adrp, 0 BL). */
        INT32 limit = i + 0x80;
        if (limit > size - 8) limit = size - 8;

        INT32 adrp_to_page = 1;
        INT32 bl_count = 0;

        for (INT32 nx = i + 4; nx + 8 <= limit; nx += 4) {
            UINT32 ins = read_instr(buffer, nx);

            if ((ins & 0x9F000000u) == 0x90000000u) {
                INT32 lo = (INT32)((ins >> 29) & 3);
                INT32 hi = (INT32)((ins >> 5) & 0x7ffff);
                INT32 im = (hi << 2) | lo;
                if (im & 0x100000) im -= 0x200000;
                INT32 pg = (nx & ~0xfff) + (im << 12);
                if (pg == msg_page) adrp_to_page++;
                continue;
            }

            if ((ins & 0xFC000000u) != 0x94000000u) continue;
            bl_count++;

            UINT32 br = read_instr(buffer, nx + 4);
            if ((br & 0xFC000000u) != 0x14000000u) continue;

            INT32 b_imm26 = (INT32)(br & 0x3ffffffu);
            if (b_imm26 & 0x2000000) b_imm26 -= 0x4000000;
            if (b_imm26 <= 0) continue;
            INT32 b_target = (nx + 4) + b_imm26 * 4;
            if (b_target < 0 || b_target >= size) continue;

            /* Selectivity: shutdown stub has multiple adrp refs and a
             * print cascade before the reset call. */
            if (adrp_to_page < 2 || bl_count < 3) continue;

            Print_patcher("region bypass C: NOP BL @ 0x%X (was 0x%08X), "
                          "post-BL B -> 0x%X\n",
                          nx, read_instr(buffer, nx), b_target);
            write_instr(buffer, nx, NOP);
            return 1;
        }
    }

    Print_patcher("region bypass C: shutdown BL+B pattern not located\n");
    return 0;
}

/* Stage D — kernel cmdline `androidboot.pcbaidinfo=` value override.
 *
 * ABL builds the kernel cmdline by appending `androidboot.pcbaidinfo=<value>`
 * where the value is sourced at runtime (TZ region tag or device buffer).
 * To force a fixed value, redirect the value-buffer pointer at the
 * emission call site so the formatter reads from a known rodata literal
 * ("PRC" or "ROW") instead of the runtime buffer.
 *
 * Pattern (OLD PRC / ROW vintages):
 *   ADRP Xa, <page-of-pcbaidinfo>
 *   ADD  Xa, Xa, #<imm12-of-pcbaidinfo>      ; key string
 *   ADD  X2, SP, #imm                        ; value buffer ptr   <-- target
 *   ...
 *   BL   <formatter>                          ; emits "key=value"
 *
 * Rewrite the `ADD X2, SP, #imm` (or `ADD X2, X_any, #imm`) into
 * `ADR X2, <region_literal>`. The literal is a 3-char NUL-terminated
 * string already present in rodata. ADR has +/- 1MB range which is
 * sufficient within a single ABL image.
 *
 * region must be 3 chars ("PRC" or "ROW"). Locator skips if the
 * direct adrp+add key xref is absent (e.g., newer record-table style —
 * needs separate handling).
 */
INT32 patch_pcbaidinfo_override(CHAR8* buffer, INT32 size,
                                const CHAR8* region) {
    static const CHAR8 key[] = " androidboot.pcbaidinfo=";
    INT32 key_len = (INT32)(sizeof(key) - 1);

    if (region == 0 || region[0] == 0 || region[1] == 0 || region[2] == 0) {
        Print_patcher("pcbaidinfo: invalid region argument, skipping\n");
        return 0;
    }

    /* Locate key string */
    INT32 key_off = -1;
    for (INT32 i = 0; i + key_len < size; ++i) {
        if (memcmp_patcher(buffer + i, key, key_len) == 0) {
            key_off = i; break;
        }
    }
    if (key_off < 0) {
        Print_patcher("pcbaidinfo: key string not present, skipping\n");
        return 0;
    }
    Print_patcher("pcbaidinfo: key @ 0x%X (forcing %a)\n", key_off, region);

    /* Locate region literal: \0<region>\0 */
    INT32 lit_off = -1;
    for (INT32 i = 0; i + 4 < size; ++i) {
        if (buffer[i] == 0
            && buffer[i+1] == region[0]
            && buffer[i+2] == region[1]
            && buffer[i+3] == region[2]
            && buffer[i+4] == 0) {
            lit_off = i + 1; break;
        }
    }
    if (lit_off < 0) {
        Print_patcher("pcbaidinfo: region literal not present, skipping\n");
        return 0;
    }
    Print_patcher("pcbaidinfo: region literal @ 0x%X\n", lit_off);

    /* Walk .text for ADRP+ADD pair targeting the key string. */
    INT32 patched = 0;
    for (INT32 i = 0x1000; i + 8 <= size; i += 4) {
        DecodedInst d0 = decode_at(buffer, i);
        if (d0.type != INST_ADRP) continue;
        DecodedInst d1 = decode_at(buffer, i + 4);
        if (d1.type != INST_ADD_X_IMM) continue;
        if (d1.rt != d0.rt || d1.rn != d0.rt) continue;
        INT64 t = calc_adrl_file_offset(buffer, i, 0);
        if (t != (INT64)key_off) continue;

        /* Scan next 0x20 bytes for ADD X2, X_any, #imm (sf=1, Rd=2). */
        INT32 sink_off = -1;
        for (INT32 j = i + 8; j + 4 <= size && j < i + 0x20; j += 4) {
            UINT32 ins = read_instr(buffer, j);
            if ((ins & 0xFF80001Fu) == 0x91000002u) {
                sink_off = j; break;
            }
        }
        if (sink_off < 0) continue;

        /* Encode ADR X2, lit_off. ADR range is +/- 1MB. */
        INT32 off = lit_off - sink_off;
        if (off < -(1 << 20) || off >= (1 << 20)) {
            Print_patcher("pcbaidinfo: ADR range exceeded, skipping site\n");
            continue;
        }
        UINT32 imm21 = (UINT32)off & 0x1FFFFFu;
        UINT32 immlo = imm21 & 3u;
        UINT32 immhi = (imm21 >> 2) & 0x7FFFFu;
        UINT32 adr = 0x10000000u
                   | (immlo << 29)
                   | (0x10u << 24)
                   | (immhi << 5)
                   | 2u;

        Print_patcher("pcbaidinfo: ADR X2 @ 0x%X 0x%08X -> 0x%08X "
                      "(literal 0x%X)\n",
                      sink_off, read_instr(buffer, sink_off), adr, lit_off);
        write_instr(buffer, sink_off, adr);
        patched++;
        break; /* Only patch the first emission site found */
    }

    if (patched == 0)
        Print_patcher("pcbaidinfo: emission site not located (newer record-table style?)\n");
    return patched;
}

/* Stage E — `hqsysfs.pcba_config=` kernel cmdline value override.
 *
 * The hqsysfs kernel module exposes /sys/module/hqsysfs/parameters/
 * pcba_config which carries the region tag (PRC vs ROW) from the
 * kernel cmdline argument `hqsysfs.pcba_config=PRC` emitted by ABL.
 *
 * Earlier revisions of this stage tried two approaches:
 *  v1) Redirect the key-string adrp+add to an embedded override
 *      " hqsysfs.pcba_config=ROW " in .data NUL padding. That flipped
 *      /sys/module/hqsysfs/parameters/pcba_config to "ROW", but the
 *      formatter still appended the runtime value behind our embed,
 *      producing a stray "PRC" token after the space separator:
 *          hqsysfs.pcba_config=ROW PRC
 *      Userspace consumers searching /proc/cmdline for the substring
 *      "PRC" still matched the stray, defeating the override.
 *  v2) NOP only the first BL after the ADRL pair, hoping to suppress
 *      the whole emission. That suppressed the key emission but not
 *      the value emission (the formatter uses two BLs per cmdline
 *      argument: first BL emits the key, second BL emits the value).
 *      Result on /proc/cmdline:
 *          hqsysfs.pcba_stage=MPPRC hqsysfs.pcba_hwid=...
 *      The skipped key left "MP" (preceding emission's value) glued
 *      directly to "PRC" (our emission's value) with no separator.
 *
 * v3 (current): keep both formatter calls, but redirect only the value
 * pointer load to a fixed rodata literal. Pattern around ROW ABL:
 *
 *   ADRP X2, " hqsysfs.pcba_config="
 *   ADD  X2, X2, #imm
 *   BL   formatter                  ; emits key
 *   LDR  X2, [SP, #imm]             ; runtime region ptr  <-- target
 *   BL   formatter                  ; emits value
 *
 * Rewriting the LDR to `ADR X2, "ROW"` yields exactly
 * `hqsysfs.pcba_config=ROW` with no trailing runtime PRC token.
 *
 * Builds without a direct adrp+add key xref (record-table vintages)
 * silently no-op.
 */
INT32 patch_hqsysfs_pcba_config_override(CHAR8* buffer, INT32 size,
                                          const CHAR8* region) {
    static const CHAR8 key[] = " hqsysfs.pcba_config=";
    INT32 key_len = (INT32)(sizeof(key) - 1);

    if (region == 0 || region[0] == 0 || region[1] == 0 || region[2] == 0) {
        Print_patcher("hqsysfs override: invalid region argument, skipping\n");
        return 0;
    }

    /* Find key string in rodata */
    INT32 key_off = -1;
    for (INT32 i = 0; i + key_len < size; ++i) {
        if (memcmp_patcher(buffer + i, key, key_len) == 0) {
            key_off = i; break;
        }
    }
    if (key_off < 0) {
        Print_patcher("hqsysfs override: key string absent, skipping\n");
        return 0;
    }
    Print_patcher("hqsysfs override: key @ 0x%X (forcing %a)\n", key_off, region);

    INT32 lit_off = -1;
    for (INT32 i = 0; i + 4 < size; ++i) {
        if (buffer[i] == 0
            && buffer[i+1] == region[0]
            && buffer[i+2] == region[1]
            && buffer[i+3] == region[2]
            && buffer[i+4] == 0) {
            lit_off = i + 1; break;
        }
    }
    if (lit_off < 0) {
        Print_patcher("hqsysfs override: region literal not present, skipping\n");
        return 0;
    }
    Print_patcher("hqsysfs override: region literal @ 0x%X\n", lit_off);

    /* Find the adrp+add pair targeting key_off, then locate the runtime
     * value load between the key-emit BL and value-emit BL. */
    INT32 patched = 0;
    for (INT32 i = 0x1000; i + 8 <= size; i += 4) {
        DecodedInst d0 = decode_at(buffer, i);
        if (d0.type != INST_ADRP) continue;
        DecodedInst d1 = decode_at(buffer, i + 4);
        if (d1.type != INST_ADD_X_IMM) continue;
        if (d1.rt != d0.rt || d1.rn != d0.rt) continue;
        INT64 t = calc_adrl_file_offset(buffer, i, 0);
        if (t != (INT64)key_off) continue;

        INT32 bl1 = -1, bl2 = -1, value_load = -1;
        for (INT32 j = i + 8; j + 4 <= size && j < i + 0x34; j += 4) {
            UINT32 raw = read_instr(buffer, j);
            /* arm64_inst_decoder has an INST_BL enum but no active BL decoder;
             * match BL by raw opcode like the rest of this file. */
            if ((raw & 0xFC000000u) == 0x94000000u) {
                if (bl1 < 0) {
                    bl1 = j;
                } else {
                    bl2 = j;
                    break;
                }
                continue;
            }

            DecodedInst dj = decode_at(buffer, j);
            if (bl1 >= 0 && dj.type == INST_LDR_X_IMM && dj.rt == 2) {
                value_load = j;
            }
        }
        if (bl1 < 0 || bl2 < 0 || value_load < 0 || value_load >= bl2) continue;

        /* Encode ADR X2, lit_off. ADR range is +/- 1MB. */
        INT32 off = lit_off - value_load;
        if (off < -(1 << 20) || off >= (1 << 20)) {
            Print_patcher("hqsysfs override: ADR range exceeded, skipping site\n");
            continue;
        }
        UINT32 imm21 = (UINT32)off & 0x1FFFFFu;
        UINT32 immlo = imm21 & 3u;
        UINT32 immhi = (imm21 >> 2) & 0x7FFFFu;
        UINT32 adr = 0x10000000u
                   | (immlo << 29)
                   | (0x10u << 24)
                   | (immhi << 5)
                   | 2u;

        Print_patcher("hqsysfs override: ADR X2 @ 0x%X 0x%08X -> 0x%08X "
                      "(literal 0x%X), key BL @ 0x%X, value BL @ 0x%X\n",
                      value_load, read_instr(buffer, value_load), adr,
                      lit_off, bl1, bl2);
        write_instr(buffer, value_load, adr);
        patched++;
        break;
    }

    if (patched == 0)
        Print_patcher("hqsysfs override: ADRL/value-load pair not found (newer record-table style?)\n");
    return patched;
}

BOOLEAN PatchBuffer(CHAR8* data, INT32 size) {
    #ifndef DISABLE_PATCH_1
    if (patch_abl_gbl(data, size) != 0)
        Print_patcher("Warning: Failed to patch ABL GBL\n");
    #endif

    #ifndef DISABLE_PATCH_2
    INT32 patched_adrl = patch_adrl_unlocked_to_locked(data, size, 0);
    if (patched_adrl == 0){
        Print_patcher("Warning: ADRL triple not found, skipping\n");
        // not critical, continue with other patches
    }

    if(patched_adrl > 1){
        Print_patcher("Warning: Multiple ADRL triples patched (%d), verify if all are correct\n", patched_adrl);
        return FALSE; //cr
    }
    #endif
    #ifndef DISABLE_PATCH_6
    if (!patch_string_jump(data, size))
        Print_patcher("Warning: Failed to patch string jump\n");
    #endif
    INT32 offset = -1;
    INT8 lock_register_num = -1;
    INT32 num_patches = patch_abl_bootstate(data, size, &lock_register_num, &offset);
    if (num_patches == 0) {
        Print_patcher("Error: Failed to find/patch ABL Boot State\n");
        free(data);
        return 0;
    }
    Print_patcher("Anchor offset : 0x%X\n", offset);
    Print_patcher("Lock register : W%d\n", (int)lock_register_num);
    Print_patcher("Boot patches: %d\n", num_patches);

    INT32 global_var_offset = -1;
    if (find_ldrB_instructio_reverse(data, size, offset, lock_register_num, &global_var_offset, source_callback) != 0) {
        Print_patcher("Warning: Failed to patch LDRB->STRB chain for W%d\n",
               (int)lock_register_num);
    }
    Print_patcher("Global variable offset (for warning patch): 0x%X\n", global_var_offset);
    // ===================== 启用去黄字补丁 =====================
    if (!patch_warning(data, size, global_var_offset)) {
        Print_patcher("Warning: patch_warning failed\n");
    }
    // ==========================================================

    #ifndef DISABLE_PATCH_REGION
    if (patch_region_lockout_bypass(data, size) == 0)
        Print_patcher("Info: region lockout bypass (A/B) not applied\n");
    if (patch_region_bypass_stage_c(data, size) == 0)
        Print_patcher("Info: region lockout bypass (C) not applied\n");
    #endif

    #if defined(FORCE_PCBAIDINFO_PRC)
    if (patch_pcbaidinfo_override(data, size, "PRC") == 0)
        Print_patcher("Info: pcbaidinfo override (PRC) not applied\n");
    if (patch_hqsysfs_pcba_config_override(data, size, "PRC") == 0)
        Print_patcher("Info: hqsysfs.pcba_config override (PRC) not applied\n");
    #elif defined(FORCE_PCBAIDINFO_ROW)
    if (patch_pcbaidinfo_override(data, size, "ROW") == 0)
        Print_patcher("Info: pcbaidinfo override (ROW) not applied\n");
    if (patch_hqsysfs_pcba_config_override(data, size, "ROW") == 0)
        Print_patcher("Info: hqsysfs.pcba_config override (ROW) not applied\n");
    #endif

    return 1;
}
