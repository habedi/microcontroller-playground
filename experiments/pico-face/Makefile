############################################################################
# experiments/pico-face/Makefile
#
# Builds the face as a NuttX builtin application.
#
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_PICO_FACE_PROGNAME)
PRIORITY  = $(CONFIG_PICO_FACE_PRIORITY)
STACKSIZE = $(CONFIG_PICO_FACE_STACKSIZE)
MODULE    = $(CONFIG_PICO_FACE)

# Core state machine and renderers shared with host tests.

CSRCS   = src/face.c src/face_render.c src/face_sprite.c \
          src/face_pixel.c src/face_crab.c src/face_penguin.c \
          src/face_bot.c src/face_graph.c \
          src/face_preset.c src/face_input.c src/face_font.c \
          src/face_overlay.c src/face_dirty.c
MAINSRC = src/face_main.c

CFLAGS += -I$(APPDIR)/external/src

include $(APPDIR)/Application.mk
