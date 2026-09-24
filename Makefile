APP_NAME    := BTNEdit
BUILD_DIR   := build
APP_DIR     := $(BUILD_DIR)/$(APP_NAME).app
CONTENTS_DIR:= $(APP_DIR)/Contents
MACOS_DIR   := $(CONTENTS_DIR)/MacOS
RESOURCES_DIR:= $(CONTENTS_DIR)/Resources

CC       := clang
# Optionaler SDK-Sysroot-Override - leer/ungesetzt aendert nichts am
# gewohnten Verhalten. Workaround fuer einen bekannten Toolchain-Bug: die
# von "xcrun --show-sdk-path" aufgeloeste SDK (z.B. eine sehr neue/Beta-
# SDK-Version) kann .tbd-Stub-Dateien enthalten, die der Linker noch nicht
# versteht ("tapi error: malformed file", "unknown architecture") - betrifft
# nicht den Quellcode hier, sondern nur die lokale Xcode/CLT-Installation.
# Abhilfe: eine aeltere, auf der Maschine bereits vorhandene SDK-Version
# erzwingen, z.B.:
#   make clean
#   make SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
SDKROOT  ?=
SDKFLAG  := $(if $(SDKROOT),-isysroot $(SDKROOT),)
CFLAGS   := -Wall -Wextra -std=c11 -O2 -Isrc $(SDKFLAG)
OBJCFLAGS:= -Wall -Wextra -fno-objc-arc -O2 -Isrc $(SDKFLAG)
FRAMEWORKS := -framework Cocoa -framework CoreText -framework CoreGraphics $(SDKFLAG)

SRC_C := src/main.c src/render.c src/editor.c src/gapbuffer.c src/highlight.c src/strings.c
SRC_M := src/shim.m

OBJ := $(SRC_C:.c=.o) $(SRC_M:.m=.o)

BINARY := $(MACOS_DIR)/$(APP_NAME)

.PHONY: all run clean

all: $(BINARY) $(CONTENTS_DIR)/Info.plist $(RESOURCES_DIR)/AppIcon.icns

$(MACOS_DIR):
	mkdir -p $(MACOS_DIR)

$(RESOURCES_DIR):
	mkdir -p $(RESOURCES_DIR)

$(BINARY): $(OBJ) | $(MACOS_DIR)
	$(CC) -o $@ $(OBJ) $(FRAMEWORKS)

$(CONTENTS_DIR)/Info.plist: Info.plist | $(MACOS_DIR)
	cp Info.plist $@

# iconutil ist Teil der Xcode-Kommandozeilenwerkzeuge (macOS-only) - baut aus
# resources/AppIcon.iconset (einzelne PNGs in Standard-Groessen) das fertige
# .icns-Bundle-Icon.
$(RESOURCES_DIR)/AppIcon.icns: resources/AppIcon.iconset | $(RESOURCES_DIR)
	iconutil -c icns resources/AppIcon.iconset -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.m
	$(CC) $(OBJCFLAGS) -c $< -o $@

run: all
	open $(APP_DIR)

clean:
	rm -rf $(BUILD_DIR) src/*.o
