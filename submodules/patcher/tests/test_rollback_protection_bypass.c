#include "../patchlib.h"

static void put32(CHAR8* buf, INT32 off, UINT32 raw) {
    buf[off] = (CHAR8)(raw & 0xff);
    buf[off + 1] = (CHAR8)((raw >> 8) & 0xff);
    buf[off + 2] = (CHAR8)((raw >> 16) & 0xff);
    buf[off + 3] = (CHAR8)((raw >> 24) & 0xff);
}

static void put_cstr(CHAR8* buf, INT32 off, const CHAR8* s) {
    while (*s) buf[off++] = *s++;
    buf[off] = 0;
}

static void make_prefixed_pe(CHAR8* buf, INT32 delta) {
    buf[delta] = 'M';
    buf[delta + 1] = 'Z';
    put32(buf, delta + 0x3c, 0x40);
    buf[delta + 0x40] = 'P';
    buf[delta + 0x41] = 'E';
}

static UINT32 encode_add_x_imm(UINT8 rd, UINT16 imm) {
    return 0x91000000u | ((UINT32)imm << 10) | ((UINT32)rd << 5) | rd;
}

static UINT32 encode_b_cond(INT32 from, INT32 to, UINT8 cond) {
    INT32 imm19 = (to - from) >> 2;
    return 0x54000000u | (((UINT32)imm19 & 0x7ffffu) << 5) | cond;
}

static UINT32 encode_b(INT32 from, INT32 to) {
    INT32 imm26 = (to - from) >> 2;
    return 0x14000000u | ((UINT32)imm26 & 0x03ffffffu);
}

static void make_fixture(CHAR8* buf, INT32 delta, BOOLEAN include_legacy_scm) {
    const INT32 compare_off = delta + 0x1100;
    const INT32 compare_branch_off = compare_off + 4;
    const INT32 compare_ok_off = compare_off + 0x40;
    const INT32 compare_xref = compare_off + 0x24;
    const INT32 compare_string = delta + 0x1800;
    const INT32 write_fn = delta + 0x1200;
    const INT32 write_xref = write_fn + 0x1c;
    const INT32 write_string = delta + 0x1880;
    const INT32 scm_ab = delta + 0x1400;
    const INT32 scm_legacy = delta + 0x1440;

    make_prefixed_pe(buf, delta);

    put_cstr(buf, compare_string,
             ": Image rollback index is less than the stored rollback index.");
    put32(buf, compare_off, 0xeb0c011f); /* CMP X8, X12 */
    put32(buf, compare_branch_off,
          encode_b_cond(compare_branch_off, compare_ok_off, 2)); /* B.CS */
    put32(buf, compare_xref, 0x90000006); /* ADRP X6, same page */
    put32(buf, compare_xref + 4, encode_add_x_imm(6, 0x800));

    put_cstr(buf, write_string,
             "WriteRollbackIndex Location %d, RollbackIndex %d");
    put32(buf, write_fn, 0xd503233f);     /* PACIASP */
    put32(buf, write_fn + 4, 0xd100c3ff); /* SUB SP,SP,#0x30 */
    put32(buf, write_xref, 0x90000001);   /* ADRP X1, same page */
    put32(buf, write_xref + 4, encode_add_x_imm(1, 0x880));

    put32(buf, scm_ab, 0x52802201);      /* MOV W1,#0x110 */
    put32(buf, scm_ab + 4, 0x910003e3);  /* ADD X3,SP,#0 */
    put32(buf, scm_ab + 8, 0x910003e4);  /* ADD X4,SP,#0 */
    put32(buf, scm_ab + 12, 0x72a64001); /* MOVK W1,#0x3200,LSL#16 */
    put32(buf, scm_ab + 16, 0x2a1f03e2); /* MOV W2,WZR */
    put32(buf, scm_ab + 20, 0xf9401c08); /* LDR X8,[X0,#0x38] */
    put32(buf, scm_ab + 24, 0xd63f0100); /* BLR X8 */
    put32(buf, scm_ab + 28, 0xb4000100); /* CBZ X0,ok */

    if (include_legacy_scm) {
        put32(buf, scm_legacy, 0x528023c1);      /* MOV W1,#0x11e */
        put32(buf, scm_legacy + 4, 0x910003e3);  /* ADD X3,SP,#0 */
        put32(buf, scm_legacy + 8, 0x910003e4);  /* ADD X4,SP,#0 */
        put32(buf, scm_legacy + 12, 0x72a04001); /* MOVK W1,#0x200,LSL#16 */
        put32(buf, scm_legacy + 16, 0x2a1f03e2); /* MOV W2,WZR */
        put32(buf, scm_legacy + 20, 0xf9401c08); /* LDR X8,[X0,#0x38] */
        put32(buf, scm_legacy + 24, 0xd63f0100); /* BLR X8 */
        put32(buf, scm_legacy + 28, 0xb4000100); /* CBZ X0,ok */
    }
}

int main(void) {
    const INT32 delta = 0x80;
    const INT32 compare_branch_off = delta + 0x1104;
    const INT32 compare_ok_off = delta + 0x1140;
    const INT32 write_fn = delta + 0x1200;
    const INT32 scm_ab_blr = delta + 0x1418;
    const INT32 scm_legacy_blr = delta + 0x1458;
    CHAR8 complete[0x3000] = {0};
    CHAR8 incomplete[0x3000] = {0};

    make_fixture(complete, delta, TRUE);
    if (patch_rollback_protection_bypass(complete, sizeof(complete)) != 4)
        return 1;
    if (read_instr(complete, compare_branch_off)
        != encode_b(compare_branch_off, compare_ok_off))
        return 2;
    if (read_instr(complete, write_fn) != 0x2a1f03e0
        || read_instr(complete, write_fn + 4) != 0xd65f03c0)
        return 3;
    if (read_instr(complete, scm_ab_blr) != 0xaa1f03e0
        || read_instr(complete, scm_legacy_blr) != 0xaa1f03e0)
        return 4;

    make_fixture(incomplete, delta, FALSE);
    if (patch_rollback_protection_bypass(incomplete, sizeof(incomplete)) != 0)
        return 5;
    if (read_instr(incomplete, compare_branch_off)
        != encode_b_cond(compare_branch_off, compare_ok_off, 2))
        return 6;

    return 0;
}
