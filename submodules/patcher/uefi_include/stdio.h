#ifndef PATCHER_UEFI_STDIO_H
#define PATCHER_UEFI_STDIO_H
/* UEFI build shim: patcher logging is for the host tool only and is
 * compiled out of the firmware build. */
#define printf(...) ((void)0)
#endif /* PATCHER_UEFI_STDIO_H */
