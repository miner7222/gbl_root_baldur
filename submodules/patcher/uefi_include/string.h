#ifndef PATCHER_UEFI_STRING_H
#define PATCHER_UEFI_STRING_H
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#define memcmp CompareMem
#define memcpy CopyMem
#define memset SetMem
#define strlen AsciiStrLen
#endif /* PATCHER_UEFI_STRING_H */
