# Host preview of the six expressions.  Needs no board and no cross compiler.

CC     ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror -I../src

BUILD = build
SRC   = ../src

.PHONY: preview clean

# Renders the contact sheet, then converts it to PNG so any viewer opens it.

preview: $(BUILD)/preview
	$(BUILD)/preview $(WHEN) > $(BUILD)/faces.ppm
	python3 ppm2png.py $(BUILD)/faces.ppm $(BUILD)/faces.png
	@echo "wrote $(BUILD)/faces.png"

$(BUILD)/preview: preview.c $(SRC)/face.c $(SRC)/face_render.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ preview.c $(SRC)/face.c $(SRC)/face_render.c

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
