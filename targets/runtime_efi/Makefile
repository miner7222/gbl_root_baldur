# Runtime EFI variants: upstream superfastboot BDS with the in-memory ABL
# patch path enabled (AUTO_PATCH_ABL=1) and the target defines baked in.

build_prc:
	$(MAKE) -C ../../submodules/uefi clean
	$(MAKE) -C ../../submodules/uefi build AUTO_PATCH_ABL=1 FORCE_PCBAIDINFO_PRC=1
	mkdir -p build
	cp ../../submodules/uefi/build/BDS.efi ./build/generic_superfastboot_prc.efi

build_row:
	$(MAKE) -C ../../submodules/uefi clean
	$(MAKE) -C ../../submodules/uefi build AUTO_PATCH_ABL=1 FORCE_PCBAIDINFO_ROW=1
	mkdir -p build
	cp ../../submodules/uefi/build/BDS.efi ./build/generic_superfastboot_row.efi

build_prc_arb:
	$(MAKE) -C ../../submodules/uefi clean
	$(MAKE) -C ../../submodules/uefi build AUTO_PATCH_ABL=1 FORCE_PCBAIDINFO_PRC=1 FORCE_AVB_KEY_ARB=1
	mkdir -p build
	cp ../../submodules/uefi/build/BDS.efi ./build/generic_superfastboot_prc_arb.efi

build_row_arb:
	$(MAKE) -C ../../submodules/uefi clean
	$(MAKE) -C ../../submodules/uefi build AUTO_PATCH_ABL=1 FORCE_PCBAIDINFO_ROW=1 FORCE_AVB_KEY_ARB=1
	mkdir -p build
	cp ../../submodules/uefi/build/BDS.efi ./build/generic_superfastboot_row_arb.efi

build_all:
	$(MAKE) build_prc
	$(MAKE) build_row
	$(MAKE) build_prc_arb
	$(MAKE) build_row_arb
	@echo "Built build/generic_superfastboot_{prc,row,prc_arb,row_arb}.efi"
	ls -l ./build

clean:
	rm -rf build || true
