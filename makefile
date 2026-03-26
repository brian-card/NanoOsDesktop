# ============================================================================
#  NanoOS Desktop - LVGL v9 Simulator Build (Ubuntu / SDL2)
# ============================================================================
#
#  Project layout:
#    NanoOsDesktop/
#    |
#    +-- src
#    |   |
#    |   +-- main.c          <- Platform entry point (pure LVGL API)
#    |   +-- desktop.c       <- Desktop UI (pure LVGL API)
#    |
#    +-- include
#    |   |
#    |   +-- desktop.h
#    |   +-- lv_conf.h       <- LVGL configuration (enables SDL driver)
#    |
#    +-- makefile
#    +-- lib/lvgl/           <- git clone https://github.com/lvgl/lvgl.git
#
#  No lv_drivers repo needed — LVGL v9 has SDL built in.
#
#  Usage:
#    make setup    # pull LVGL
#    make          # build
#    make clean    # remove build artifacts
#    make run      # build and run
#

# ---- Toolchain ---------------------------------------------------------

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g \
           -DLV_CONF_INCLUDE_SIMPLE \

MKDIR   := mkdir -p
RM      := rm -rf
LDFLAGS :=

# SDL2 flags
SDL_CFLAGS  := $(shell sdl2-config --cflags)
SDL_LDFLAGS := $(shell sdl2-config --libs)

CFLAGS  += $(SDL_CFLAGS)
LDFLAGS += $(SDL_LDFLAGS) -lm -lpthread

# ---- Sources -----------------------------------------------------------

SRC_DIR := src

INCLUDES := \
    -Iinclude \
    -Ilib/lvgl \

# LVGL core + widgets + drivers (SDL driver lives in lib/lvgl/src/drivers/sdl/)
LVGL_SRCS := $(shell find lib/lvgl/src -name '*.c')

# Application
APP_SRCS  := $(SRC_DIR)/main.c $(SRC_DIR)/desktop.c

ALL_SRCS  := $(LVGL_SRCS) $(APP_SRCS)

# ---- Objects -----------------------------------------------------------

BUILD_DIR := build

LVGL_OBJS := $(patsubst %.c,$(BUILD_DIR)/lvgl/%.o,$(LVGL_SRCS))

APP_OBJS  := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(APP_SRCS))

ALL_OBJS := $(LVGL_OBJS) $(APP_OBJS)

# ---- Target ------------------------------------------------------------

TARGET := $(BUILD_DIR)/bin/nanoos-desktop

.PHONY: all clean run setup

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	@$(MKDIR) $(dir $@)
	$(CC) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	@echo "==> Built: $(TARGET)"

$(BUILD_DIR)/lvgl/%.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/obj/%.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	$(RM) $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

setup:
	git submodule update --init --recursive
	@echo "==> Setup complete. Now run 'make'"
