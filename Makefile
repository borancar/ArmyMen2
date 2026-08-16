# ArmyMen II reconstruction harness.
#
# Everything here is 32-bit PE, built with mingw. It is NOT winelib: Wine 11 on
# this host is new-WoW64 (i386-windows + x86_64-unix, no i386-unix), so 32-bit
# winelib ELF objects cannot be produced at all. The harness has to live inside
# the game's own 32-bit address space, which means PE.

CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -Wall -Wextra -std=gnu11 -fno-strict-aliasing
LDFLAGS := -static-libgcc

BUILD     := build
WINE      ?= wine
PREFIX    := $(CURDIR)/.wine
GAMEDIR   := $(PREFIX)/drive_c/GOG Games/Army Men II
GAMEEXE   := C:\\GOG Games\\Army Men II\\ArmyMen2.exe
LAUNCHEXE := C:\\GOG Games\\Army Men II\\launcher.exe

HOOK_SRC := src/inject/dllmain.c \
            src/inject/patch.c \
            src/inject/trace.c \
            src/inject/observe.c \
            src/inject/hooklog.c \
            src/inject/gamelog.c \
            src/inject/input.c \
            src/inject/dinput_hook.c \
            src/inject/control.c \
            src/game/savetag.c \
            src/game/rect.c

# ws2_32 for the control socket.
HOOK_LIBS := -lws2_32

LAUNCH_SRC := src/inject/launcher.c

.PHONY: all clean run run-stock install-hook

all: $(BUILD)/am2hook.dll $(BUILD)/launcher.exe

$(BUILD):
	@mkdir -p $(BUILD)

# The DLL is loaded into the game by launcher.exe before its entry point runs.
$(BUILD)/am2hook.dll: $(HOOK_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -shared -o $@ $(HOOK_SRC) $(LDFLAGS) $(HOOK_LIBS)

$(BUILD)/launcher.exe: $(LAUNCH_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(LAUNCH_SRC) $(LDFLAGS)

# The launcher looks for am2hook.dll beside itself, so both land in the game
# folder. Neither replaces a shipped file; the install stays stock.
install-hook: all
	@cp $(BUILD)/am2hook.dll $(BUILD)/launcher.exe "$(GAMEDIR)/"
	@echo "installed am2hook.dll + launcher.exe into the game folder"

# --- running the game ----------------------------------------------------
#
# `make run` is the single entry point -- there is deliberately no second
# launch target for variations. Everything is a knob:
#
#     make run                     harness + the 1999 debug logger
#     make run GAMELOG=0           leave the logger stubbed
#     make run TRACE=1             argument-trace every patched function
#     make run OBSERVE=1           log the observed functions' call sites
#     make run ARGS=               drop the developer switches
#     make run DESKTOP=1024x768    bigger virtual desktop
#
# Watch the recovered debug commentary from another terminal with:
#     tail -f ".wine/drive_c/GOG Games/Army Men II/am2.log"
#
# Always windowed, so the game cannot mode-switch the desktop out from under
# you. Runs on $DISPLAY, so set DISPLAY=:99 for a headless Xvfb run.

ARGS    ?= -nointro -dbg
GAMELOG ?= 1
TRACE   ?= 0
OBSERVE ?= 0
CONTROL ?= 1
CTLPORT ?= 31337
DESKTOP ?= 800x600

# `explorer /desktop=NAME` reuses any existing desktop of the same name, and
# that desktop belongs to the X display that created it. A headless run on :99
# and a run on :0 sharing one name makes the second attach to a window on the
# wrong display, which fails with BadWindow. So the name carries the display.
DESKTAG  := $(shell echo "$${DISPLAY:-none}" | tr -cd 'A-Za-z0-9')
DESKNAME ?= amii$(DESKTAG)

run: install-hook
	WINEPREFIX="$(PREFIX)" WINEDEBUG=-all \
	    AM2_GAMELOG=$(GAMELOG) AM2_TRACE=$(TRACE) AM2_OBSERVE=$(OBSERVE) \
	    AM2_CONTROL=$(CONTROL) AM2_CTL_PORT=$(CTLPORT) \
	    $(WINE) explorer /desktop=$(DESKNAME),$(DESKTOP) \
	    "$(LAUNCHEXE)" "$(GAMEEXE)" $(ARGS)

# Unpatched, straight from the GOG install -- the A/B reference.
run-stock:
	WINEPREFIX="$(PREFIX)" WINEDEBUG=-all \
	    $(WINE) explorer /desktop=$(DESKNAME),$(DESKTOP) "$(GAMEEXE)"

clean:
	rm -rf $(BUILD)
