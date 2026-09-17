#include "patchs/lenovo/lock_flash_cmd.h"
#include <stdio.h>
#include <string.h>

/* `oem lock-flash` is a ROW-only fastboot command; the command table
 * holds a pointer to this literal.  Mangling the literal removes the
 * command from the dispatcher without touching the handler.
 *
 * Only the ROW variant is built with this patch (see PatchBuffer).
 */
bool patch_disable_lock_flash_cmd(char* buffer, int32_t size) {
    static const char needle[] = "oem lock-flash";
    const int32_t needle_len = (int32_t)(sizeof(needle) - 1);
    int32_t patched = 0;

    for (int32_t off = 0; off + needle_len < size; ++off) {
        if (memcmp(buffer + off, needle, needle_len) != 0) continue;
        if (buffer[off + needle_len] != 0) continue;
        if (off > 0 && buffer[off - 1] != 0) continue;

        printf("lock-flash cmd: hit @ 0x%X, mangling literal\n", off);
        buffer[off + 4] = '_';
        patched++;
        off += needle_len - 1;
    }

    if (patched == 0)
        printf("lock-flash cmd: string not present, skipping\n");
    else if (patched > 1)
        printf("lock-flash cmd: warning, %d sites patched (expected 1)\n", patched);
    return patched > 0;
}
