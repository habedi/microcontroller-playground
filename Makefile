NUTTX_DIR ?= external/nuttx
BOARD     ?= esp32p4-function-ev-board:nsh
# The RISC-V toolchain in the dev shell is built from source, so a garbage
# collection would cost an hour to undo. This profile is a GC root.
DEVSHELL_PROFILE ?= .nix-devshell-profile
PORT      ?= /dev/ttyUSB0
TTY       ?= /dev/ttyACM0
BAUD      ?= 115200
UF2       ?= $(NUTTX_DIR)/nuttx.uf2

# A Pico in BOOTSEL mode mounts as a mass storage device. The Pico 2 labels the
# volume RP2350, and the original Pico labels it RPI-RP2. Copying a UF2 there
# flashes the board without the udev rules that picotool needs.
PICO_MOUNT ?= $(shell findmnt -rn -o TARGET -S LABEL=RP2350 2>/dev/null || \
  findmnt -rn -o TARGET -S LABEL=RPI-RP2 2>/dev/null)

# A configuration saved under configs/nuttx/<board>/<name>. The board name
# comes from the path, and any in-tree configuration of that board serves as
# the scaffold, because the saved defconfig then replaces the generated one.
# NuttX strips CONFIG_APPS_DIR when it writes a defconfig, so it has to go
# back in before olddefconfig fills in the remaining defaults.
SAVED_CONFIG   ?=
SAVED_BOARD     = $(notdir $(patsubst %/,%,$(dir $(SAVED_CONFIG))))
NUTTX_APPS_DIR ?= ../nuttx-apps

# The architecture of the current NuttX configuration, empty when the tree is not configured yet.
NUTTX_ARCH = $(shell sed -n 's/^CONFIG_ARCH="\(.*\)"$$/\1/p' $(NUTTX_DIR)/.config 2>/dev/null)

# NuttX takes its command prefix from the board configuration, and the
# Espressif boards ask for riscv64-unknown-elf-. The Nix dev shell carries the
# same compiler under the riscv32-none-elf- name, so a RISC-V build points
# NuttX there when that name is the one on PATH. A machine with the Debian
# riscv64-unknown-elf toolchain, CI included, keeps the NuttX default and
# needs no prefix at all.
ifeq ($(NUTTX_ARCH),risc-v)
  ifeq ($(origin CROSSDEV),undefined)
    CROSSDEV := $(shell command -v riscv32-none-elf-gcc >/dev/null 2>&1 && \
      echo riscv32-none-elf-)
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

# NuttX. A second OS gets its own prefixed group (zephyr-build, and so on).
.PHONY: nuttx-list-boards
nuttx-list-boards: ## List the available NuttX boards and configurations
	cd $(NUTTX_DIR) && ./tools/configure.sh -L

.PHONY: nuttx-configure
nuttx-configure: ## Configure NuttX for BOARD (default: esp32p4-function-ev-board:nsh)
	cd $(NUTTX_DIR) && ./tools/configure.sh $(BOARD)

.PHONY: nuttx-configure-saved
nuttx-configure-saved: ## Configure NuttX from SAVED_CONFIG (a directory under configs/nuttx)
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
	@test -z "$(NUTTX_CC)" || command -v $(NUTTX_CC) >/dev/null 2>&1 || { \
		echo "$(NUTTX_CC) is not on PATH, and the configured board needs it."; \
		echo "Enter the Nix dev shell with make shell, or install the system"; \
		echo "packages with make setup-deps."; \
		exit 1; }

.PHONY: nuttx-build
nuttx-build: nuttx-check-toolchain ## Build the configured NuttX image
	$(MAKE) -C $(NUTTX_DIR) $(NUTTX_MAKE_ARGS)

.PHONY: nuttx-distclean
nuttx-distclean: ## Reset the NuttX tree; run it before switching boards
	$(MAKE) -C $(NUTTX_DIR) distclean

.PHONY: nuttx-flash-esp
nuttx-flash-esp: ## Flash the NuttX image to an Espressif board over PORT
	$(MAKE) -C $(NUTTX_DIR) flash ESPTOOL_PORT=$(PORT) $(NUTTX_MAKE_ARGS)

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
