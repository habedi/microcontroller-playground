# Host unit tests for path resolution and editor logic.

CC       ?= cc
PYTHON   ?= python3
UV       ?= uv
CFLAGS   ?= -O1 -g -std=c11 -Wall -Wextra -Werror -I../main
LDFLAGS  ?=
SANFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD    = build
SRC      = ../main

.PHONY: test test-retawq-tls clean

test: $(BUILD)/test_path_util $(BUILD)/test_editor $(BUILD)/test_wget
	$(BUILD)/test_path_util
	$(BUILD)/test_editor
	CC="$(CC)" CFLAGS="$(CFLAGS)" SANFLAGS="$(SANFLAGS)" LDFLAGS="$(LDFLAGS)" $(PYTHON) -m unittest -v test_wifi.py test_autocomplete.py test_wget.py test_filesystem.py

test-retawq-tls:
	CC="$(CC)" CFLAGS="$(CFLAGS)" SANFLAGS="$(SANFLAGS)" LDFLAGS="$(LDFLAGS)" $(UV) run --group test python -m unittest -v test_retawq_tls.py

$(BUILD)/test_path_util: test_path_util.c $(SRC)/path_util.c $(SRC)/path_util.h | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_path_util.c $(SRC)/path_util.c $(LDFLAGS)

$(BUILD)/test_editor: test_editor.c $(SRC)/editor.c $(SRC)/editor.h | $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-result $(SANFLAGS) -o $@ test_editor.c $(LDFLAGS)

$(BUILD)/test_wget: test_wget.c $(SRC)/wget.c $(SRC)/wget.h \
                    wget_stubs/esp_http_client.h wget_stubs/esp_crt_bundle.h \
                    wget_stubs/esp_random.h | $(BUILD)
	$(CC) $(CFLAGS) -Iwget_stubs $(SANFLAGS) -o $@ test_wget.c $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
