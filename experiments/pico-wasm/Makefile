PORT     ?= /dev/ttyACM0
BAUD     ?= 115200
WASM_BIN  = main.wasm

.PHONY: all
all: $(WASM_BIN) ## Build the WebAssembly binary

$(WASM_BIN): main.c
	zig cc -target wasm32-freestanding -Wl,--no-entry \
		-Wl,--export=main -Wl,--export=fib -Wl,--export=add \
		-Os $< -o $@
	@echo "Built $@ ($$(stat -c%s $@) bytes)"

.PHONY: transfer
transfer: $(WASM_BIN) ## Transfer main.wasm to /data on the board via Zmodem
	uv run python send.py $(WASM_BIN) --port $(PORT) --baud $(BAUD)

.PHONY: clean
clean: ## Remove build artifacts
	rm -f $(WASM_BIN)
