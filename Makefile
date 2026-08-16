# ArmyMen II reconstruction harness.
#
# Everything here is 32-bit PE, built with mingw. It is NOT winelib: Wine 11 on
# this host is new-WoW64 (i386-windows + x86_64-unix, no i386-unix), so 32-bit
# winelib ELF objects cannot be produced at all. The harness has to live inside
# the game's own 32-bit address space, which means PE.

CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -Wall -Wextra -std=gnu11 -fno-strict-aliasing
LDFLAGS := -static-libgcc

BUILD   := build
WINE    ?= wine
PREFIX  := $(CURDIR)/.wine
GAMEDIR := $(PREFIX)/drive_c/GOG Games/Army Men II
GAMEEXE := C:\\GOG Games\\Army Men II\\ArmyMen2.exe

HOOK_SRC := src/inject/dllmain.c \
            src/inject/patch.c \
            src/inject/trace.c \
            src/inject/hooklog.c \
            src/inject/gamelog.c \
            src/game/savetag.c

LAUNCH_SRC := src/inject/launcher.c

.PHONY: all clean run run-log install-hook

all: $(BUILD)/am2hook.dll $(BUILD)/launcher.exe

$(BUILD):
	@mkdir -p $(BUILD)

# The DLL is loaded into the game by launcher.exe before its entry point runs.
$(BUILD)/am2hook.dll: $(HOOK_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -shared -o $@ $(HOOK_SRC) $(LDFLAGS)

$(BUILD)/launcher.exe: $(LAUNCH_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(LAUNCH_SRC) $(LDFLAGS)

# The launcher looks for am2hook.dll beside itself, so both land in the game
# folder. Neither replaces a shipped file; the install stays stock.
install-hook: all
	@cp $(BUILD)/am2hook.dll $(BUILD)/launcher.exe "$(GAMEDIR)/"
	@echo "installed am2hook.dll + launcher.exe into the game folder"

# Windowed so it cannot mode-switch the desktop out from under you.
run: install-hook
	WINEPREFIX="$(PREFIX)" WINEDEBUG=-all $(WINE) explorer /desktop=amii,800x600 \
	    "C:\\GOG Games\\Army Men II\\launcher.exe"

# Same, with the game's own 1999 debug logger un-stubbed.
run-log: install-hook
	WINEPREFIX="$(PREFIX)" WINEDEBUG=-all AM2_GAMELOG=1 \
	    $(WINE) explorer /desktop=amii,800x600 \
	    "C:\\GOG Games\\Army Men II\\launcher.exe"

clean:
	rm -rf $(BUILD)
