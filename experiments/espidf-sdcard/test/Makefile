# Host unit tests for path resolution and editor logic.

CC       ?= cc
CFLAGS   ?= -O1 -g -std=c11 -Wall -Wextra -Werror -I../main
LDFLAGS  ?=
SANFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD    = build
SRC      = ../main

.PHONY: test clean

test: $(BUILD)/test_path_util
	$(BUILD)/test_path_util

$(BUILD)/test_path_util: test_path_util.c $(SRC)/path_util.c $(SRC)/path_util.h | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_path_util.c $(SRC)/path_util.c $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
