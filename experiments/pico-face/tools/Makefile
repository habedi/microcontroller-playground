# Host preview of every preset against every expression.  Needs no board and
# no cross compiler.

CC     ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror -I../src

BUILD = build
SRC   = ../src

.PHONY: preview clean

# Renders the contact sheet, then converts it to PNG so any viewer opens it.

preview: $(BUILD)/preview
	$(BUILD)/preview $(WHEN) $(PALETTE) > $(BUILD)/faces.ppm
	python3 ppm2png.py $(BUILD)/faces.ppm $(BUILD)/faces.png
	@echo "wrote $(BUILD)/faces.png"

SRCS = $(SRC)/face.c $(SRC)/face_render.c $(SRC)/face_sprite.c \
       $(SRC)/face_pixel.c $(SRC)/face_hero.c $(SRC)/face_hero_art.c \
       $(SRC)/face_preset.c

$(BUILD)/preview: preview.c $(SRCS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ preview.c $(SRCS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
