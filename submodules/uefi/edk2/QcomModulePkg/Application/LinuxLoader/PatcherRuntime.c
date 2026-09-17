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
#include "../../../../../patcher/src/patchs/lenovo/lock_flash_cmd.c"
#include "../../../../../patcher/src/patchs/lenovo/keymaster_unlock_sink.c"
#include "../../../../../patcher/src/patchs/lenovo/region_lockout_bypass.c"
#include "../../../../../patcher/src/patchs/lenovo/cmdline_region_override.c"
#include "../../../../../patcher/src/patchs/lenovo/avb_key_swap.c"
#include "../../../../../patcher/src/patchs/lenovo/unlock_region_token.c"
#endif
