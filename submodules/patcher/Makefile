SRCS := $(shell find src -name '*.c')
CFLAGS := -O2 -Iinclude

clean:
	rm -rf build || true

build:
	mkdir -p build
	gcc $(CFLAGS) -o build/patch_abl $(SRCS)

build_prc:
	mkdir -p build
	gcc $(CFLAGS) -DFORCE_PCBAIDINFO_PRC -o build/patch_abl_prc $(SRCS)

build_row:
	mkdir -p build
	gcc $(CFLAGS) -DFORCE_PCBAIDINFO_ROW -o build/patch_abl_row $(SRCS)

build_prc_arb:
	mkdir -p build
	gcc $(CFLAGS) -DFORCE_PCBAIDINFO_PRC -DFORCE_AVB_KEY_ARB -o build/patch_abl_prc_arb $(SRCS)

build_row_arb:
	mkdir -p build
	gcc $(CFLAGS) -DFORCE_PCBAIDINFO_ROW -DFORCE_AVB_KEY_ARB -o build/patch_abl_row_arb $(SRCS)

build_android:
	mkdir -p build
	$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang $(CFLAGS) -o build/patch_abl_android $(SRCS)

build_windows:
	mkdir -p build
	x86_64-w64-mingw32-gcc $(CFLAGS) -o build/patch_abl.exe $(SRCS)
