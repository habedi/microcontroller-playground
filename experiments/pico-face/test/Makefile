# Host tests for board-agnostic state machine, renderers, and input.

CC      ?= cc
CFLAGS  ?= -O1 -g -std=c11 -Wall -Wextra -Werror -I../src
LDFLAGS ?=

# Sanitizers enabled to catch out-of-bounds writes in rendering.

SANFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD = build
SRC   = ../src

.PHONY: test clean

test: $(BUILD)/test_face $(BUILD)/test_render $(BUILD)/test_sprite \
      $(BUILD)/test_input $(BUILD)/test_dirty
	$(BUILD)/test_face
	$(BUILD)/test_render
	$(BUILD)/test_sprite
	$(BUILD)/test_input
	$(BUILD)/test_dirty

$(BUILD)/test_face: test_face.c $(SRC)/face.c $(SRC)/face.h | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_face.c $(SRC)/face.c $(LDFLAGS)

$(BUILD)/test_render: test_render.c $(SRC)/face.c $(SRC)/face_render.c \
                     $(SRC)/face.h $(SRC)/face_render.h | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_render.c $(SRC)/face.c \
	  $(SRC)/face_render.c $(LDFLAGS)

# Art and preset assets tested together.

PRESET_SRCS = $(SRC)/face.c $(SRC)/face_render.c $(SRC)/face_sprite.c \
              $(SRC)/face_pixel.c $(SRC)/face_crab.c $(SRC)/face_penguin.c \
              $(SRC)/face_bot.c $(SRC)/face_graph.c $(SRC)/face_overlay.c \
              $(SRC)/face_font.c $(SRC)/face_preset.c

$(BUILD)/test_sprite: test_sprite.c $(PRESET_SRCS) | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_sprite.c $(PRESET_SRCS) $(LDFLAGS)

$(BUILD)/test_input: test_input.c $(SRC)/face_input.c $(SRC)/face_input.h \
                     | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_input.c $(SRC)/face_input.c \
	  $(LDFLAGS)

$(BUILD)/test_dirty: test_dirty.c $(SRC)/face_dirty.c $(SRC)/face_dirty.h \
                     | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_dirty.c $(SRC)/face_dirty.c \
	  $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
