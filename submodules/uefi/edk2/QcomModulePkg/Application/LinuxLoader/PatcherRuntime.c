/** @file PatcherRuntime.c

  Compiles the host-side ABL patcher sources into the runtime EFI build.
  The UEFI shims under submodules/patcher/uefi_include supply printf and
  the memory/string routines; enabled with RUNTIME_PATCH_ABL=1.
**/

#ifdef AUTO_PATCH_ABL
#include "../../../../../patcher/src/arm64_inst/arm64_inst_decoder.c"
#include "../../../../../patcher/src/arm64_inst/utils.c"
#include "../../../../../patcher/src/patchs/core.c"
#include "../../../../../patcher/src/patchs/oplus/warning.c"
#include "../../../../../patcher/src/patchs/oplus/forceenablefastboot.c"
#endif
