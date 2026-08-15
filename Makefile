NUTTX_DIR ?= external/nuttx
BOARD     ?= esp32p4-function-ev-board:nsh
PORT      ?= /dev/ttyUSB0
TTY       ?= /dev/ttyACM0
BAUD      ?= 115200
UF2       ?= $(NUTTX_DIR)/nuttx.uf2

.DEFAULT_GOAL := help

.PHONY: help
help: ## Show help messages for all available targets
	@grep -E '^[a-zA-Z_-]+:.*## .*$$' Makefile | \
	awk 'BEGIN {FS = ":.*## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

# Setup (OS-agnostic)
.PHONY: shell
shell: ## Enter the Nix dev shell (primary development environment)
	nix develop

.PHONY: setup-deps
setup-deps: ## Install system dependencies (on Debian-based OSes)
	sudo apt-get update
	sudo apt-get install -y \
		build-essential cmake ninja-build pkg-config \
		kconfig-frontends gperf flex bison genromfs libncurses-dev \
		gcc-arm-none-eabi gcc-riscv64-unknown-elf \
		picocom openocd

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

.PHONY: nuttx-menuconfig
nuttx-menuconfig: ## Adjust the current NuttX configuration
	$(MAKE) -C $(NUTTX_DIR) menuconfig

.PHONY: nuttx-build
nuttx-build: ## Build the configured NuttX image
	$(MAKE) -C $(NUTTX_DIR)

.PHONY: nuttx-distclean
nuttx-distclean: ## Reset the NuttX tree; run it before switching boards
	$(MAKE) -C $(NUTTX_DIR) distclean

.PHONY: nuttx-flash-esp
nuttx-flash-esp: ## Flash the NuttX image to an Espressif board over PORT
	$(MAKE) -C $(NUTTX_DIR) flash ESPTOOL_PORT=$(PORT)

# Flashing and the serial console (OS-agnostic)
.PHONY: flash-pico
flash-pico: ## Flash UF2 (default: the NuttX image) to the Pico 2 with picotool
	picotool load -fx $(UF2)

.PHONY: console
console: ## Open the serial console on TTY (default: /dev/ttyACM0)
	picocom -b $(BAUD) $(TTY)

# Maintenance
.PHONY: clean
clean: ## Remove Python caches, without touching external/
	find . -path ./external -prune -o -type d -name '__pycache__' -print0 | xargs -0 -r rm -rf
