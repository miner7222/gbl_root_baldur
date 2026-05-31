clean:
	rm -rf build || true
submodule_uefi:
	cd ../../submodules/uefi && make clean build_generic
	mkdir -p ./build
	cp ../../submodules/uefi/build/generic_superfastboot.efi ./build/generic_superfastboot.efi
submodule_uefi_prc:
	cd ../../submodules/uefi && make clean build_generic FORCE_PCBAIDINFO_PRC=1
	mkdir -p ./build
	cp ../../submodules/uefi/build/generic_superfastboot.efi ./build/generic_superfastboot_prc.efi
submodule_uefi_row:
	cd ../../submodules/uefi && make clean build_generic FORCE_PCBAIDINFO_ROW=1
	mkdir -p ./build
	cp ../../submodules/uefi/build/generic_superfastboot.efi ./build/generic_superfastboot_row.efi
submodule_uefi_prc_arb:
	cd ../../submodules/uefi && make clean build_generic FORCE_PCBAIDINFO_PRC=1 FORCE_AVB_KEY_ARB=1
	mkdir -p ./build
	cp ../../submodules/uefi/build/generic_superfastboot.efi ./build/generic_superfastboot_prc_arb.efi
submodule_uefi_row_arb:
	cd ../../submodules/uefi && make clean build_generic FORCE_PCBAIDINFO_ROW=1 FORCE_AVB_KEY_ARB=1
	mkdir -p ./build
	cp ../../submodules/uefi/build/generic_superfastboot.efi ./build/generic_superfastboot_row_arb.efi
build: submodule_uefi
	# do nothing, just build the submodules
build_prc: submodule_uefi_prc
	# do nothing, just build the submodules
build_row: submodule_uefi_row
	# do nothing, just build the submodules
build_prc_arb: submodule_uefi_prc_arb
	# do nothing, just build the submodules
build_row_arb: submodule_uefi_row_arb
	# do nothing, just build the submodules
build_all:
	$(MAKE) build_prc
	cp ./build/generic_superfastboot_prc.efi ./build/.gsf_prc.tmp
	$(MAKE) build_row
	cp ./build/generic_superfastboot_row.efi ./build/.gsf_row.tmp
	$(MAKE) build_prc_arb
	cp ./build/generic_superfastboot_prc_arb.efi ./build/.gsf_prc_arb.tmp
	$(MAKE) build_row_arb
	mv ./build/.gsf_prc.tmp     ./build/generic_superfastboot_prc.efi
	mv ./build/.gsf_row.tmp     ./build/generic_superfastboot_row.efi
	mv ./build/.gsf_prc_arb.tmp ./build/generic_superfastboot_prc_arb.efi
	@echo "Built build/generic_superfastboot_{prc,row,prc_arb,row_arb}.efi"
	ls -l ./build
