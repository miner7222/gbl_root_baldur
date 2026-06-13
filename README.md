# GBL Root Baldur

`gbl_root_baldur` is a focused fork of
[`superturtlee/gbl_root_canoe`](https://github.com/superturtlee/gbl_root_canoe)
for the `baldur` platform.

For the baseline design, boot flow, and original fake-locked bootloader
principles, read the upstream
[`superturtlee/gbl_root_canoe`](https://github.com/superturtlee/gbl_root_canoe)
repository first.

This fork is not intended to target every Snapdragon 8 Gen 5 / 8 Elite Gen 5
device. It keeps the upstream fake-locked boot flow as a base, then adds,
retargets, or removes ABL patches according to what is needed for `baldur`.

The generated EFI loads through the Qualcomm `efisp` UEFI path, reads the
active-slot ABL image, applies the local patch set in memory, and chainloads the
patched ABL.

---

## Builder Guide

Builds are expected to run on Linux or WSL with the project Docker image.

### Prerequisites

- `make`
- Docker image `gbl_builder:latest`
- WSL users: export `DOCKER_HOST=unix:///run/docker.sock` before invoking
  Docker directly

### Fork Build Targets

The targets below are added and maintained by this fork. Other inherited
upstream targets may still exist in the Makefiles, but they are not the release
focus of this repository.

- `make target_generic_efi_prc`
  Builds `targets/generic_efi/build/generic_superfastboot_prc.efi`.

- `make target_generic_efi_row`
  Builds `targets/generic_efi/build/generic_superfastboot_row.efi`.

- `make target_generic_efi_prc_arb`
  Builds `targets/generic_efi/build/generic_superfastboot_prc_arb.efi`.

- `make target_generic_efi_row_arb`
  Builds `targets/generic_efi/build/generic_superfastboot_row_arb.efi`.

- `make target_generic_efi_all`
  Builds all four EFI variants above.

The `generic_superfastboot` filename is kept for compatibility with the
upstream build layout.

---

## User Guide

### 1. Using Generic EFIs

Use the EFI variant that matches the desired region behavior:

- `generic_superfastboot_prc.efi`
- `generic_superfastboot_row.efi`
- `generic_superfastboot_prc_arb.efi`
- `generic_superfastboot_row_arb.efi`

Flash the selected EFI to `efisp` through EDL mode (`9008`).

The `_arb` variants are only for builds that intentionally include the ARB
variant behavior. Prefer the non-`_arb` variants unless that path is explicitly
needed.

### 2. OTA Upgrade

Before an OTA or firmware change, verify whether the active-slot ABL and the
target firmware are both covered by the current patch set. This fork patches the
ABL loaded through `efisp`; boot-chain components that execute before that point
are outside the EFI patcher's reach.

### 3. Variant Notes

- PRC / ROW variants force the corresponding region-facing cmdline values.
- `_arb` variants are separate build outputs for ARB-specific testing.
- The EFI patches ABL in memory at boot time; it does not permanently rewrite
  the `abl` partition by itself.
