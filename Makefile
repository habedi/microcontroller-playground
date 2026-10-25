NUTTX_DIR ?= external/nuttx
BOARD     ?= esp32p4-function-ev-board:nsh
# Nix dev shell profile kept as a GC root for the RISC-V toolchain.
DEVSHELL_PROFILE ?= .nix-devshell-profile
PORT      ?= /dev/ttyUSB0
TTY       ?= /dev/ttyACM0
BAUD      ?= 115200
UF2       ?= $(NUTTX_DIR)/nuttx.uf2

# Pico mass storage mount in BOOTSEL mode (RP2350 or RPI-RP2).
PICO_MOUNT ?= $(shell findmnt -rn -o TARGET -S LABEL=RP2350 2>/dev/null || \
  findmnt -rn -o TARGET -S LABEL=RPI-RP2 2>/dev/null)

# Saved NuttX defconfig path under configs/nuttx/<board>/<name>.
SAVED_CONFIG   ?=
SAVED_BOARD     = $(notdir $(patsubst %/,%,$(dir $(SAVED_CONFIG))))
NUTTX_APPS_DIR ?= ../nuttx-apps

# Submodule patch mappings: <submodule directory>:<patch directory>.
PATCH_SETS = $(NUTTX_DIR):patches/nuttx external/nuttx-apps:patches/nuttx-apps

# Pico SDK project configuration.
PICOSDK_DIR    ?= external/pico-sdk
PICOSDK_BOARD  ?= pico2_w
PICOSDK_SRC    ?= experiments/picosdk-hello
PICOSDK_BUILD  ?= build/picosdk/$(notdir $(PICOSDK_SRC))

# ESP-IDF toolchain and build directories.
ESPIDF_DIR    ?= external/esp-idf
ESPIDF_TOOLS  ?= $(CURDIR)/build/espressif
ESPIDF_TARGET ?= esp32p4
ESPIDF_SRC    ?= experiments/espidf-hello
ESPIDF_BUILD  ?= $(CURDIR)/build/espidf/$(notdir $(ESPIDF_SRC))
ESPIDF_PORT   ?= /dev/ttyACM0

# Bare metal Rust (esp-rs) and Zig (MicroZig) experiments.
RUST_PACKAGE  ?= serial-heartbeat
RUST_TARGET   ?= riscv32imac-unknown-none-elf
RUST_BUILD    ?= $(CURDIR)/build/rust
ZIG_BUILD     ?= $(CURDIR)/build/zig

# Rust ESP-IDF (idf-rs) experiments.
IDFRS_SRC     ?= experiments/rgb-cycle
IDFRS_PORT    ?= /dev/ttyACM0

# Serial port and build directory for the onboard ESP32-C6.
C6_PORT       ?= /dev/ttyUSB0
C6_BUILD      ?= $(CURDIR)/build/espidf/espidf-c6-slave

# Store resolved sdkconfig in the build directory.
ESPIDF_ARGS   ?= -C $(ESPIDF_SRC) -B $(ESPIDF_BUILD) \
                 -D SDKCONFIG=$(ESPIDF_BUILD)/sdkconfig

# CYW43439 wireless firmware blob checked during wireless NuttX builds.
CYW43_FIRMWARE ?= \
  external/pico-sdk/lib/cyw43-driver/firmware/43439A0-7.95.49.00.combined

# The architecture of the current NuttX configuration, empty when the tree is not configured yet.
NUTTX_ARCH = $(shell sed -n 's/^CONFIG_ARCH="\(.*\)"$$/\1/p' $(NUTTX_DIR)/.config 2>/dev/null)

# Select available 32-bit RISC-V cross-compiler prefix from PATH.
ifeq ($(NUTTX_ARCH),risc-v)
  ifeq ($(origin CROSSDEV),undefined)
    CROSSDEV := $(shell \
      if command -v riscv32-none-elf-gcc >/dev/null 2>&1; then \
        echo riscv32-none-elf-; \
      elif command -v riscv32-esp-elf-gcc >/dev/null 2>&1; then \
        echo riscv32-esp-elf-; \
      fi)
  endif
endif
NUTTX_MAKE_ARGS = $(if $(CROSSDEV),CROSSDEV=$(CROSSDEV))

# The compiler the build will reach for, used to fail early with a readable
# message instead of a screen of missing command errors.
NUTTX_CC = $(strip $(if $(CROSSDEV),$(CROSSDEV)gcc,\
  $(if $(filter risc-v,$(NUTTX_ARCH)),riscv64-unknown-elf-gcc,\
  $(if $(filter arm,$(NUTTX_ARCH)),arm-none-eabi-gcc))))

.DEFAULT_GOAL := help

.PHONY: help
help: ## Show help messages for all available targets
	@grep -E '^[a-zA-Z0-9_-]+:.*## .*$$' Makefile | \
	awk 'BEGIN {FS = ":.*## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

# Setup (OS-agnostic)
.PHONY: shell
shell: ## Enter the Nix dev shell (primary development environment)
	nix develop

.PHONY: shell-pin
shell-pin: ## Build the dev shell and keep it from being garbage collected
	nix develop --profile $(DEVSHELL_PROFILE) --command true

.PHONY: setup-deps
setup-deps: ## Install system dependencies (on Debian-based systems)
	sudo apt-get update
	sudo apt-get install -y \
		build-essential cmake ninja-build pkg-config \
		kconfig-frontends gperf flex bison genromfs libncurses-dev \
		gcc-arm-none-eabi gcc-riscv64-unknown-elf \
		picocom openocd

.PHONY: setup-udev
setup-udev: ## Install the picotool udev rules, so flash-pico runs without sudo
	@p=$$(command -v picotool) || { \
		echo "picotool is not on PATH. Enter the dev shell with make shell."; \
		exit 1; }; \
	rules="$$(dirname $$(readlink -f $$p))/../etc/udev/rules.d/60-picotool.rules"; \
	test -f "$$rules" || { \
		echo "The picotool package carries no udev rules at $$rules."; \
		exit 1; }; \
	echo "Installing $$rules"; \
	sudo install -m 0644 "$$rules" /etc/udev/rules.d/60-picotool.rules && \
	sudo udevadm control --reload-rules && \
	sudo udevadm trigger

.PHONY: submodules
submodules: ## Fetch the git submodules under external/
	git submodule update --init --depth 1
	git -C external/pico-sdk submodule update --init --depth 1 lib/cyw43-driver
	git -C external/esp-idf submodule update --init --recursive --depth 1

.PHONY: install
install: ## Install the uv-managed Python tools (like esptool and pre-commit)
	uv sync

.PHONY: setup-hooks
setup-hooks: ## Install Git hooks (pre-commit and pre-push)
	uv run pre-commit install --hook-type pre-commit
	uv run pre-commit install --hook-type pre-push
	uv run pre-commit install-hooks

.PHONY: test-hooks
test-hooks: ## Run the Git hooks on all files
	uv run pre-commit run --all-files --hook-stage pre-push

# Apply stacked patches under patches/ to submodules.
.PHONY: nuttx-patch
nuttx-patch: ## Apply the patches under patches/ to the NuttX submodules
	@for set in $(PATCH_SETS); do \
		dir=$${set%%:*}; pdir=$${set#*:}; \
		test -d "$$pdir" || continue; \
		for p in $$pdir/*.patch; do \
			test -f "$$p" || continue; \
			if git -C $$dir apply --check "$(CURDIR)/$$p" 2>/dev/null; then \
				git -C $$dir apply "$(CURDIR)/$$p" && echo "applied $$p"; \
			elif git -C $$dir apply --reverse --check "$(CURDIR)/$$p" \
			     2>/dev/null; then \
				echo "already applied $$p"; \
			else \
				touched=1; \
				for f in $$(sed -n 's|^+++ b/||p' "$$p"); do \
					if git -C $$dir ls-files --error-unmatch "$$f" \
					     >/dev/null 2>&1; then \
						git -C $$dir diff --quiet -- "$$f" && touched=0; \
					else \
						test -f $$dir/$$f || touched=0; \
					fi; \
				done; \
				if [ $$touched = 1 ]; then \
					echo "already applied $$p (under a later patch)"; \
				else \
					echo "ERROR: $$p does not apply cleanly to $$dir"; \
					echo "Run make nuttx-repatch to reset the patch state."; \
					exit 1; \
				fi; \
			fi; \
		done; \
	done

# Revert patches in reverse order.
.PHONY: nuttx-unpatch
nuttx-unpatch: ## Remove the patches under patches/ from the NuttX submodules
	@for set in $(PATCH_SETS); do \
		dir=$${set%%:*}; pdir=$${set#*:}; \
		test -d "$$pdir" || continue; \
		for p in $$(ls -r $$pdir/*.patch 2>/dev/null); do \
			test -f "$$p" || continue; \
			git -C $$dir apply --reverse "$(CURDIR)/$$p" 2>/dev/null && \
				echo "reverted $$p" || echo "not applied $$p"; \
			for f in $$(sed -n 's|^+++ b/||p' "$$p"); do \
				git -C $$dir ls-files --error-unmatch "$$f" \
					>/dev/null 2>&1 \
					|| { test -f $$dir/$$f && rm -f $$dir/$$f && \
						echo "  removed untracked $$f"; }; \
			done; \
		done; \
	done

# Reset patches and clear untracked files left by reverted patches.
.PHONY: nuttx-repatch
nuttx-repatch: nuttx-unpatch nuttx-patch ## Reset the patches to a consistent state

.PHONY: cyw43-firmware
cyw43-firmware: ## Generate the CYW43439 firmware blob the wireless build needs
	uv run python tools/cyw43-firmware.py

# Symlink out-of-tree application into external/nuttx-apps/external.
EXTERNAL_APP ?= experiments/pico-face

.PHONY: nuttx-link-app
nuttx-link-app: ## Link EXTERNAL_APP into the NuttX apps tree as apps/external
	@test -f "$(EXTERNAL_APP)/Kconfig" || { \
		echo "EXTERNAL_APP must name a directory holding Kconfig, Make.defs,"; \
		echo "and Makefile, for example EXTERNAL_APP=experiments/pico-face"; \
		exit 1; }
	ln -sfn "$(CURDIR)/$(EXTERNAL_APP)" external/nuttx-apps/external
	@echo "apps/external -> $(EXTERNAL_APP)"

.PHONY: nuttx-unlink-app
nuttx-unlink-app: ## Remove the apps/external symlink
	rm -f external/nuttx-apps/external

.PHONY: nuttx-list-boards
nuttx-list-boards: ## List the available NuttX boards and configurations
	cd $(NUTTX_DIR) && ./tools/configure.sh -L

.PHONY: nuttx-configure
nuttx-configure: nuttx-patch ## Configure NuttX for BOARD (default: esp32p4-function-ev-board:nsh)
	cd $(NUTTX_DIR) && ./tools/configure.sh $(BOARD)

.PHONY: nuttx-configure-saved
nuttx-configure-saved: nuttx-patch ## Configure NuttX from SAVED_CONFIG (a directory under configs/nuttx)
	@test -f "$(SAVED_CONFIG)/defconfig" || { \
		echo "Set SAVED_CONFIG to a directory that holds a defconfig, for example"; \
		echo "make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/esp32p4-function-ev-board/usbconsole-rev1"; \
		exit 1; }
	cd $(NUTTX_DIR) && ./tools/configure.sh -E $(SAVED_BOARD):nsh
	cp $(SAVED_CONFIG)/defconfig $(NUTTX_DIR)/.config
	kconfig-tweak --file $(NUTTX_DIR)/.config \
		--set-str CONFIG_APPS_DIR "$(NUTTX_APPS_DIR)"
	$(MAKE) -C $(NUTTX_DIR) olddefconfig

.PHONY: nuttx-save-config
nuttx-save-config: ## Save the current NuttX configuration into SAVED_CONFIG
	@test -n "$(SAVED_CONFIG)" || { \
		echo "Set SAVED_CONFIG to the directory to write the defconfig into."; \
		exit 1; }
	$(MAKE) -C $(NUTTX_DIR) savedefconfig
	mkdir -p $(SAVED_CONFIG)
	mv $(NUTTX_DIR)/defconfig $(SAVED_CONFIG)/defconfig

.PHONY: nuttx-menuconfig
nuttx-menuconfig: ## Adjust the current NuttX configuration
	$(MAKE) -C $(NUTTX_DIR) menuconfig

.PHONY: nuttx-check-toolchain
nuttx-check-toolchain:
	@test -n "$(NUTTX_ARCH)" || { \
		echo "No NuttX configuration found in $(NUTTX_DIR)."; \
		echo "Run make nuttx-configure BOARD=<board>:<config> first."; \
		exit 1; }
	@grep -q '^CONFIG_IEEE80211_INFINEON_CYW43439=y' $(NUTTX_DIR)/.config \
	  2>/dev/null || exit 0; \
	grep -q '^CONFIG_RP23XX_INFINEON_CYW43439=y' $(NUTTX_DIR)/.config || { \
		echo "This configuration asks for the CYW43439, but the board symbol"; \
		echo "is absent, so the patches under patches/nuttx are not applied."; \
		echo "Kconfig drops the symbol without a word and the image would"; \
		echo "build without wlan0. Run make nuttx-patch, then configure again."; \
		exit 1; }; \
	fw=$$(sed -n 's/^CONFIG_CYW43439_FIRMWARE_BIN_PATH="\(.*\)"$$/\1/p' \
	  $(NUTTX_DIR)/.config); \
	case "$$fw" in *PLAYGROUND_ROOT*) \
		test -n "$$PLAYGROUND_ROOT" || { \
			echo "PLAYGROUND_ROOT is unset, and the firmware path needs it."; \
			echo "The path would resolve to /build/... and the build would"; \
			echo "substitute a dummy blob. Enter the dev shell with make shell."; \
			exit 1; }; ;; esac; \
	fw=$$(eval echo "$$fw"); \
	case "$$fw" in /*) ;; *) fw="$(NUTTX_DIR)/$$fw" ;; esac; \
	if test ! -s "$$fw" || test "$$(stat -c%s "$$fw")" -lt 100000; then \
		echo "The CYW43439 firmware at $$fw is missing or too small."; \
		echo "A wireless build substitutes a dummy for it and the image then"; \
		echo "runs but never talks to the chip. Run make cyw43-firmware."; \
		exit 1; \
	fi
	@test -z "$(NUTTX_CC)" || command -v $(NUTTX_CC) >/dev/null 2>&1 || { \
		echo "$(NUTTX_CC) is not on PATH, and the configured board needs it."; \
		echo "Enter the Nix dev shell with make shell, or install the system"; \
		echo "packages with make setup-deps."; \
		exit 1; }
	@test "$(NUTTX_ARCH)" = risc-v || exit 0; \
	std=$$($(NUTTX_CC) -dM -E -x c /dev/null 2>/dev/null | \
	  sed -n 's/^#define __STDC_VERSION__ \([0-9]*\)L$$/\1/p'); \
	if test -n "$$std" && test "$$std" -ge 202311; then \
		echo "$(NUTTX_CC) defaults to C23 (__STDC_VERSION__ $$std)."; \
		echo "C23 removed ATOMIC_VAR_INIT, which the vendored Espressif HAL"; \
		echo "still uses, so the build fails inside esp-hal-3rdparty with"; \
		echo "'initializer element is not constant'. The flake pins GCC 14 for"; \
		echo "this reason, so this shell predates that pin. Leave it and enter"; \
		echo "a fresh one with make shell."; \
		exit 1; \
	fi

.PHONY: nuttx-build
nuttx-build: nuttx-check-toolchain ## Build the configured NuttX image
	$(MAKE) -C $(NUTTX_DIR) $(NUTTX_MAKE_ARGS)

.PHONY: nuttx-distclean
nuttx-distclean: ## Reset the NuttX tree; run it before switching boards
	$(MAKE) -C $(NUTTX_DIR) distclean

.PHONY: nuttx-flash-esp
nuttx-flash-esp: ## Flash the NuttX image to an Espressif board over PORT
	$(MAKE) -C $(NUTTX_DIR) flash ESPTOOL_PORT=$(PORT) $(NUTTX_MAKE_ARGS)

# Pico SDK. Bare metal C and C++ on the RP2350, with the wireless, Bluetooth,
# TCP/IP, and USB stacks the SDK bundles.
.PHONY: picosdk-configure
picosdk-configure: ## Configure a Pico SDK project (PICOSDK_SRC=<dir>)
	@test -f "$(PICOSDK_SRC)/CMakeLists.txt" || { \
		echo "$(PICOSDK_SRC) has no CMakeLists.txt."; \
		echo "Set PICOSDK_SRC to a project directory under experiments/."; \
		exit 1; }
	cmake -G Ninja -S $(PICOSDK_SRC) -B $(PICOSDK_BUILD) \
		-DPICO_BOARD=$(PICOSDK_BOARD)

.PHONY: picosdk-build
picosdk-build: ## Build the configured Pico SDK project
	@test -f "$(PICOSDK_BUILD)/build.ninja" || { \
		echo "$(PICOSDK_BUILD) is not configured. Run make picosdk-configure."; \
		exit 1; }
	cmake --build $(PICOSDK_BUILD)

.PHONY: picosdk-distclean
picosdk-distclean: ## Remove the Pico SDK build directory
	rm -rf $(PICOSDK_BUILD)

.PHONY: espidf-install
espidf-install: ## Install the ESP-IDF toolchains for ESPIDF_TARGET
	@test -f "$(ESPIDF_DIR)/install.sh" || { \
		echo "$(ESPIDF_DIR) is empty. Run make submodules first."; \
		exit 1; }
	IDF_TOOLS_PATH=$(ESPIDF_TOOLS) $(ESPIDF_DIR)/install.sh $(ESPIDF_TARGET)

.PHONY: espidf-build
espidf-build: ## Build an ESP-IDF project (ESPIDF_SRC=<dir>)
	@test -f "$(ESPIDF_SRC)/CMakeLists.txt" || { \
		echo "$(ESPIDF_SRC) has no CMakeLists.txt."; \
		echo "Set ESPIDF_SRC to a project directory under experiments/."; \
		exit 1; }
	bash -c 'export IDF_TOOLS_PATH=$(ESPIDF_TOOLS); \
		. $(ESPIDF_DIR)/export.sh >/dev/null && \
		if test -f $(ESPIDF_BUILD)/sdkconfig && \
		   grep -q "CONFIG_IDF_TARGET=\"$(ESPIDF_TARGET)\"" $(ESPIDF_BUILD)/sdkconfig; then \
			idf.py $(ESPIDF_ARGS) build; \
		else \
			idf.py $(ESPIDF_ARGS) set-target $(ESPIDF_TARGET) build; \
		fi'

.PHONY: espidf-flash
espidf-flash: ## Flash the built ESP-IDF project over ESPIDF_PORT
	bash -c 'export IDF_TOOLS_PATH=$(ESPIDF_TOOLS); \
		. $(ESPIDF_DIR)/export.sh >/dev/null && \
		idf.py $(ESPIDF_ARGS) -p $(ESPIDF_PORT) flash'

.PHONY: espidf-monitor
espidf-monitor: ## Open the ESP-IDF serial monitor on ESPIDF_PORT
	bash -c 'export IDF_TOOLS_PATH=$(ESPIDF_TOOLS); \
		. $(ESPIDF_DIR)/export.sh >/dev/null && \
		idf.py $(ESPIDF_ARGS) -p $(ESPIDF_PORT) monitor'

.PHONY: espidf-flash-c6
espidf-flash-c6: ## Flash ESP-Hosted firmware to the C6 through a UART adapter on C6_PORT
	@test -f "$(C6_BUILD)/espidf_c6_slave.bin" || { \
		echo "Build it first:"; \
		echo "  make espidf-build ESPIDF_SRC=experiments/espidf-c6-slave \\"; \
		echo "       ESPIDF_TARGET=esp32c6"; \
		exit 1; }
	@echo "Wire the adapter to the ESP32-C6 UART pads: EN, TXD, RXD, GND."
	@echo "Do not connect VDD. They are pads, so the joints need soldering."
	@echo "Hold the P4 in its bootloader so it does not drive the bus."
	bash -c 'export IDF_TOOLS_PATH=$(ESPIDF_TOOLS); \
		. $(ESPIDF_DIR)/export.sh >/dev/null && \
		python -m esptool --chip esp32c6 -p $(C6_PORT) write_flash \
			0x0 $(C6_BUILD)/bootloader/bootloader.bin \
			0x8000 $(C6_BUILD)/partition_table/partition-table.bin \
			0x10000 $(C6_BUILD)/espidf_c6_slave.bin'

.PHONY: espidf-distclean
espidf-distclean: ## Remove the ESP-IDF build directory
	rm -rf $(ESPIDF_BUILD)

# Bare metal Rust and Zig, built from the repository root.
.PHONY: rust-build
rust-build: ## Build a Rust workspace package (RUST_PACKAGE=<name>)
	cargo build --package $(RUST_PACKAGE) --release --locked --target $(RUST_TARGET) --target-dir $(RUST_BUILD)

.PHONY: zig-build
zig-build: ## Build MicroZig firmware with the root build script
	zig build -Doptimize=ReleaseSmall --prefix $(ZIG_BUILD)

# Rust ESP-IDF (idf-rs) experiments.
.PHONY: idfrs-build
idfrs-build: ## Build an idf-rs Rust experiment (IDFRS_SRC=<dir>)
	bash -c 'test -f $$HOME/export-esp.sh && . $$HOME/export-esp.sh; \
		cd $(IDFRS_SRC) && cargo build --release'

.PHONY: idfrs-flash
idfrs-flash: ## Flash an idf-rs firmware binary over IDFRS_PORT
	espflash flash --port $(IDFRS_PORT) --monitor $(RUST_BUILD)/xtensa-esp32s3-espidf/release/$(notdir $(IDFRS_SRC))

# Flashing and the serial console (OS-agnostic)
.PHONY: flash-pico
flash-pico: ## Flash UF2 (default: the NuttX image) to the Pico 2 with picotool
	picotool load -fx $(UF2)

.PHONY: flash-pico-uf2
flash-pico-uf2: ## Flash UF2 by copying it to a Pico that is in BOOTSEL mode
	@test -f "$(UF2)" || { \
		echo "$(UF2) does not exist. Build it with make nuttx-build first."; \
		exit 1; }
	@test -n "$(PICO_MOUNT)" || { \
		echo "No Pico in BOOTSEL mode is mounted."; \
		echo "Hold BOOTSEL while plugging the board in, then try again."; \
		exit 1; }
	cp $(UF2) $(PICO_MOUNT)/
	sync

.PHONY: console
console: ## Open the serial console on TTY (default: /dev/ttyACM0)
	picocom -b $(BAUD) $(TTY)

# Maintenance
.PHONY: clean
clean: ## Remove Python caches, without touching external/
	find . -path ./external -prune -o -type d -name '__pycache__' -print0 | xargs -0 -r rm -rf
