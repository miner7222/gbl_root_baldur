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
build: submodule_uefi
	# do nothing, just build the submodules
build_prc: submodule_uefi_prc
	# do nothing, just build the submodules
build_row: submodule_uefi_row
	# do nothing, just build the submodules
build_all:
	$(MAKE) build_prc
	cp ./build/generic_superfastboot_prc.efi ./build/.gsf_prc.tmp
	$(MAKE) build_row
	mv ./build/.gsf_prc.tmp ./build/generic_superfastboot_prc.efi
	@echo "Built build/generic_superfastboot_prc.efi and build/generic_superfastboot_row.efi"
	ls -l ./build
