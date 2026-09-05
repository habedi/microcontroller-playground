# Host preview of every preset against every expression, and a bus estimate
# for partial redraw.  Neither needs a board or a cross compiler.

CC     ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror -I../src

BUILD = build
SRC   = ../src

.PHONY: preview stats clean

# Renders the contact sheet, then converts it to PNG so any viewer opens it.

preview: $(BUILD)/preview
	$(BUILD)/preview $(WHEN) $(PALETTE) > $(BUILD)/faces.ppm
	python3 ppm2png.py $(BUILD)/faces.ppm $(BUILD)/faces.png
	@echo "wrote $(BUILD)/faces.png"

SRCS = $(SRC)/face.c $(SRC)/face_render.c $(SRC)/face_sprite.c \
       $(SRC)/face_pixel.c $(SRC)/face_crab.c \
       $(SRC)/face_preset.c

$(BUILD)/preview: preview.c $(SRCS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ preview.c $(SRCS)

# Prints the share of the panel sent per frame, per preset and state.

stats: $(BUILD)/stats
	$(BUILD)/stats

$(BUILD)/stats: stats.c $(SRCS) $(SRC)/face_dirty.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ stats.c $(SRCS) $(SRC)/face_dirty.c

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
