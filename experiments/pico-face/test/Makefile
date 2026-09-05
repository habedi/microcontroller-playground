# Host tests for the parts of the face that need no board: the expression
# state machine, the renderers and their art, and the button mapping.  Run make test from this directory.

CC      ?= cc
CFLAGS  ?= -O1 -g -std=c11 -Wall -Wextra -Werror -I../src
LDFLAGS ?=

# Undefined behaviour and out of bounds writes in the drawing code are the
# failures worth catching here, so the sanitizers are on by default.

SANFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD = build
SRC   = ../src

.PHONY: test clean

test: $(BUILD)/test_face $(BUILD)/test_render $(BUILD)/test_sprite \
      $(BUILD)/test_input
	$(BUILD)/test_face
	$(BUILD)/test_render
	$(BUILD)/test_sprite
	$(BUILD)/test_input

$(BUILD)/test_face: test_face.c $(SRC)/face.c $(SRC)/face.h | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_face.c $(SRC)/face.c $(LDFLAGS)

$(BUILD)/test_render: test_render.c $(SRC)/face.c $(SRC)/face_render.c \
                     $(SRC)/face.h $(SRC)/face_render.h | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_render.c $(SRC)/face.c \
	  $(SRC)/face_render.c $(LDFLAGS)

# The presets and the art, which test_sprite covers as one unit because the
# preset table is what decides which art is reachable.

PRESET_SRCS = $(SRC)/face.c $(SRC)/face_render.c $(SRC)/face_sprite.c \
              $(SRC)/face_pixel.c $(SRC)/face_crab.c \
              $(SRC)/face_preset.c

$(BUILD)/test_sprite: test_sprite.c $(PRESET_SRCS) | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_sprite.c $(PRESET_SRCS) $(LDFLAGS)

$(BUILD)/test_input: test_input.c $(SRC)/face_input.c $(SRC)/face_input.h \
                     | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ test_input.c $(SRC)/face_input.c \
	  $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
