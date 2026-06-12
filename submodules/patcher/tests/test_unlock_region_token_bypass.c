#include "../patchlib.h"

static void put32(CHAR8* buf, INT32 off, UINT32 raw) {
    buf[off] = (CHAR8)(raw & 0xff);
    buf[off + 1] = (CHAR8)((raw >> 8) & 0xff);
    buf[off + 2] = (CHAR8)((raw >> 16) & 0xff);
    buf[off + 3] = (CHAR8)((raw >> 24) & 0xff);
}

static void put64(CHAR8* buf, INT32 off, UINT64 raw) {
    for (INT32 i = 0; i < 8; ++i)
        buf[off + i] = (CHAR8)((raw >> (i * 8)) & 0xff);
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

int main(void) {
    const INT32 delta = 0x80;
    const INT32 cmd_va = 0x2200;
    const INT32 handler_va = 0x1200;
    const INT32 table_off = delta + 0x3000;
    const INT32 handler_off = delta + handler_va;
    const INT32 token_bl_off = handler_off + 0x68;

    CHAR8 buf[0x4000] = {0};
    make_prefixed_pe(buf, delta);

    put_cstr(buf, delta + cmd_va, "oem unlock-region");
    put64(buf, table_off, cmd_va);
    put64(buf, table_off + 8, handler_va);

    put32(buf, handler_off, 0xd503233f);          /* PACIASP */
    put32(buf, handler_off + 0x5c, 0x940004b1);   /* BL token-state/read */
    put32(buf, handler_off + 0x60, 0x72001c1f);   /* TST W0,#0xff */
    put32(buf, handler_off + 0x64, 0x54000500);   /* B.EQ no-token */
    put32(buf, token_bl_off, 0x9400044d);         /* BL verify-token */
    put32(buf, token_bl_off + 4, 0xb4000100);     /* CBZ X0, ok */
    put32(buf, token_bl_off + 0x90, 0xd503233f);  /* next function */

    if (patch_unlock_region_token_bypass(buf, sizeof(buf)) != 1)
        return 1;

    if (read_instr(buf, token_bl_off) != 0xaa1f03e0)
        return 2;

    return 0;
}
