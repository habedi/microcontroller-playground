############################################################################
# experiments/pico-face/Makefile
#
# Builds the face as a NuttX builtin application.  The host tests and the
# preview have their own makefiles under test/ and tools/, and neither needs
# a cross compiler.
#
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_PICO_FACE_PROGNAME)
PRIORITY  = $(CONFIG_PICO_FACE_PRIORITY)
STACKSIZE = $(CONFIG_PICO_FACE_STACKSIZE)
MODULE    = $(CONFIG_PICO_FACE)

# The expression state machine and the renderer carry no board code, so they
# are the same files the host tests build.

CSRCS   = src/face.c src/face_render.c
MAINSRC = src/face_main.c

CFLAGS += -I$(APPDIR)/external/src

include $(APPDIR)/Application.mk
