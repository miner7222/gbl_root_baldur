# GBL Root Baldur

Focused fork of [superturtlee/gbl_root_canoe](https://github.com/superturtlee/gbl_root_canoe)
(upstream `main`, 74ed59f) for the target platform family.

Unlike upstream, this fork keeps the **runtime in-memory ABL patch path**:
one EFI is flashed raw to the `efisp` partition.  On boot it reads the
active-slot ABL, unwraps the LinuxLoader PE from the firmware volume,
applies the patch set in memory and chainloads the patched image.  No
`persist` files are required and ABL updates are picked up automatically.

The host-side patcher and the upstream offline flow are kept for
pre-flash validation; the runtime EFI compiles the same patch sources.

---

## Build

Linux or WSL with the project docker image (`gbl_builder:latest`); export
`DOCKER_HOST=unix:///run/docker.sock` before invoking docker.

Runtime EFI variants (release artifacts):

```bash
make target_runtime_efi_all     # or target_runtime_efi_{prc,row,prc_arb,row_arb}
```

Outputs `targets/runtime_efi/build/generic_superfastboot_{prc,row,prc_arb,row_arb}.efi`.

Offline host patcher (validation and toolkits):

```bash
cd submodules/patcher && make build build_prc build_row build_prc_arb build_row_arb
```

Build-order trap: `submodules/uefi/Makefile` has a target named `build`
that collides with the `build/` output directory.  After `make tools`, a
later `make build` silently no-ops and `BDS.efi` is never produced.  Run
`make clean` (or remove `submodules/uefi/build`) before `make build`.

---

## Install

Runtime variant (single artifact):

- Flash `generic_superfastboot_<variant>.efi` raw to the `efisp` partition
  (EDL or `dd`).
- Optional tools: copy the toolkit `efisp/` directory (`BOOTENTRIES`,
  `tools/*.efi`) to `/mnt/vendor/persist/efisp/` so the Volume Up menu can
  offer them.

Upstream offline model (for reference or validation):

- `BDS.efi` raw to `efisp`; cracked `boot.efi`, `BOOTENTRIES` and `tools/`
  on `persist` under `efisp/`.

---

## Credit

Upstream project: [superturtlee/gbl_root_canoe](https://github.com/superturtlee/gbl_root_canoe).
