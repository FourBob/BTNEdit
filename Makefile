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
#   make BTN_SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
# Bewusst NICHT "SDKROOT": das ist eine Standard-Umgebungsvariable von
# Xcode/xcrun - ein "?=" darauf wuerde eine zufaellig gesetzte, womoeglich
# unpassende SDK aus der Umgebung stillschweigend als -isysroot uebernehmen.
BTN_SDK  ?=
SDKFLAG  := $(if $(BTN_SDK),-isysroot $(BTN_SDK),)
# Zusaetzliche Compiler-Flags, z.B. "make EXTRA_CFLAGS=-Werror" (so baut die CI).
EXTRA_CFLAGS ?=
CFLAGS   := -Wall -Wextra -std=c11 -O2 -Isrc $(SDKFLAG) $(EXTRA_CFLAGS)
OBJCFLAGS:= -Wall -Wextra -fno-objc-arc -O2 -Isrc $(SDKFLAG) $(EXTRA_CFLAGS)
FRAMEWORKS := -framework Cocoa -framework CoreText -framework CoreGraphics $(SDKFLAG)

SRC_C := src/main.c src/render.c src/editor.c src/gapbuffer.c src/highlight.c src/strings.c src/eol.c src/textinput.c
SRC_M := src/shim.m

OBJ := $(SRC_C:.c=.o) $(SRC_M:.m=.o)

BINARY := $(MACOS_DIR)/$(APP_NAME)

.PHONY: all run clean test bench test-objc

# Compiler fuer Tests/Benchmark. Unter Linux: "make test TEST_CC=gcc" (clang
# bringt dort oft keine ASan-Laufzeit mit). Die Tests brauchen kein macOS.
TEST_CC ?= $(CC)

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

# Alle Tests unter tests/ mit AddressSanitizer/UBSan - siehe tests/run_tests.sh.
# SDKFLAG (BTN_SDK) auch hier: die Tests linken gegen libSystem, und ein
# kaputtes Standard-SDK scheitert daran genauso wie die App.
test:
	CC=$(TEST_CC) TEST_EXTRA_CFLAGS="$(SDKFLAG)" tests/run_tests.sh

# Nur macOS: die echte View aus shim.m mit Eingabemethoden-Aufrufen und
# kuenstlichen Tasten-Events (tests/objc/test_shim_input.m).
test-objc:
	mkdir -p $(BUILD_DIR)/tests
	$(CC) $(OBJCFLAGS) -o $(BUILD_DIR)/tests/test_shim_input tests/objc/test_shim_input.m src/shim.m src/strings.c \
		$(FRAMEWORKS)
	$(BUILD_DIR)/tests/test_shim_input

# Laufzeit pro Tastendruck bei grossen Dokumenten (kein Test, nur Messwerte).
bench:
	python3 tests/gen_headers.py src $(BUILD_DIR)/tests/gen
	$(TEST_CC) -std=gnu11 -O2 -Isrc -I$(BUILD_DIR)/tests/gen $(SDKFLAG) -o $(BUILD_DIR)/tests/bench_layout \
		tests/bench_layout.c src/editor.c src/eol.c src/gapbuffer.c src/highlight.c -lm
	$(BUILD_DIR)/tests/bench_layout

clean:
	rm -rf $(BUILD_DIR) src/*.o
