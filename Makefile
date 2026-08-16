# ArmyMen II reconstruction harness.
#
# Everything here is 32-bit PE, built with mingw. It is NOT winelib: Wine 11 on
# this host is new-WoW64 (i386-windows + x86_64-unix, no i386-unix), so 32-bit
# winelib ELF objects cannot be produced at all. The harness has to live inside
# the game's own 32-bit address space, which means PE.

CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -g -Wall -Wextra -std=gnu11 -fno-strict-aliasing
LDFLAGS := -static-libgcc

BUILD     := build
WINE      ?= wine
# Overridable: point at a separate prefix for full isolation, including the
# game's own Options.cfg and save/ directory, which are otherwise shared.
PREFIX    ?= $(CURDIR)/.wine
# Deferred, not `:=` -- ISOLATE=1 rewrites PREFIX further down, and an
# immediate assignment here would have baked in the shared prefix.
GAMEDIR    = $(PREFIX)/drive_c/GOG Games/Army Men II
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
            src/game/rect.c \
            src/game/dist.c \
            src/game/objtable.c \
            src/game/objtype.c \
            src/game/packkey.c \
            src/game/text.c \
            src/game/blit.c \
            src/game/sprite.c \
            src/game/surface.c

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
#
# Installed by rename, never by overwrite. `cp` truncates and rewrites in
# place, and Wine maps am2hook.dll straight from this file -- so installing
# while another instance is running corrupted that instance's mapping, taking
# its control-socket thread down with it. Isolated prefixes symlink to this
# same directory, so starting a second instance did exactly that to the first.
# rename(2) swaps the directory entry and leaves the old inode alive for
# anyone who still has it mapped.
install-hook: all
	@for f in am2hook.dll launcher.exe; do \
	    cp $(BUILD)/$$f "$(GAMEDIR)/.$$f.tmp" && \
	    mv -f "$(GAMEDIR)/.$$f.tmp" "$(GAMEDIR)/$$f"; \
	done
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
WINEDBG ?= -all
GAMELOG ?= 1
TRACE   ?= 0
OBSERVE ?= 0
CONTROL ?= 1
DESKTOP ?= 800x600

# --- instance identity ---------------------------------------------------
#
# Concurrent runs collide on four things: the Wine desktop name, the control
# port, the log file and the screenshot names. All four are derived from ID, so
# two runs with different IDs cannot interfere.
#
# The default comes from $DISPLAY (`:99` -> 99, `:0` -> 0), which makes a
# headless run and a desktop run independent without anyone having to think
# about it. Override explicitly to run several on one display:
#
#     make run           # ID from DISPLAY
#     make run ID=7      # a second instance on the same display
#
# `explorer /desktop=NAME` reuses any existing desktop of the same name, and
# that desktop belongs to the display that created it -- two runs sharing a
# name across displays made the second fail with BadWindow.
ID       ?= $(shell d="$${DISPLAY:-0}"; n=$$(echo "$$d" | tr -cd '0-9'); echo "$${n:-0}")
CTLPORT  ?= $(shell echo $$((31337 + ($(ID) % 1000))))
DESKNAME ?= amii$(ID)
LOGFILE  ?= am2-$(ID).log
SHOTS    ?= $(BUILD)/shots/$(ID)

# --- running two games at once -------------------------------------------
#
# The game refuses to start twice: it guards itself with a named mutex,
# "ArmyMenMutex", via OpenMutexA at 0x0040B603. Named kernel objects belong to
# the wineserver, and there is one wineserver per WINEPREFIX -- so a second
# instance in the same prefix always loses, no matter how the ports, desktops
# and logs are separated.
#
# ISOLATE=1 gives the instance its own prefix, hence its own wineserver, hence
# its own mutex namespace. The 579MB game install is not copied: it is
# symlinked in, so an isolated prefix costs a few MB.
#
#     make run ID=7 ISOLATE=1
#
# Caveat: because the install is shared through the symlink, the files the game
# *writes* -- Options.cfg and save/ -- are still shared between isolated
# instances. For independent game state, copy the install instead of linking.
ISOLATE     ?= 0
SHARED_GAME := $(CURDIR)/.wine/drive_c/GOG Games/Army Men II
ifeq ($(ISOLATE),1)
PREFIX := $(CURDIR)/.wine-$(ID)
endif

# Machine-readable, so tools/drive.sh can source these rather than
# re-deriving them and drifting out of step.
.PHONY: config
config:
	@echo "ID='$(ID)'"
	@echo "CTLPORT='$(CTLPORT)'"
	@echo "DESKNAME='$(DESKNAME)'"
	@echo "LOGFILE='$(LOGFILE)'"
	@echo "SHOTS='$(SHOTS)'"
	@echo "GAMEDIR='$(GAMEDIR)'"
	@echo "LOGPATH='$(GAMEDIR)/$(LOGFILE)'"

# Create an isolated prefix on demand. mscoree/mshtml are disabled so wineboot
# does not stop to install Mono and Gecko into a throwaway prefix.
.PHONY: isolate-prefix
isolate-prefix:
ifeq ($(ISOLATE),1)
	@if [ ! -d "$(PREFIX)" ]; then \
	    echo "creating isolated prefix $(PREFIX)"; \
	    WINEPREFIX="$(PREFIX)" WINEDLLOVERRIDES="mscoree=d;mshtml=d" \
	        wineboot -u >/dev/null 2>&1; \
	    mkdir -p "$(PREFIX)/drive_c/GOG Games"; \
	    ln -sfn "$(SHARED_GAME)" "$(PREFIX)/drive_c/GOG Games/Army Men II"; \
	    echo "linked game install into $(PREFIX)"; \
	fi
endif

run: isolate-prefix install-hook
	@echo "run: ID=$(ID) port=$(CTLPORT) desktop=$(DESKNAME) log=$(LOGFILE) prefix=$(PREFIX)"
	WINEPREFIX="$(PREFIX)" WINEDEBUG=$(WINEDBG) \
	    AM2_GAMELOG=$(GAMELOG) AM2_TRACE=$(TRACE) AM2_OBSERVE=$(OBSERVE) \
	    AM2_CONTROL=$(CONTROL) AM2_CTL_PORT=$(CTLPORT) AM2_LOG=$(LOGFILE) \
	    $(WINE) explorer /desktop=$(DESKNAME),$(DESKTOP) \
	    "$(LAUNCHEXE)" "$(GAMEEXE)" $(ARGS)

# Unpatched, straight from the GOG install -- the A/B reference.
run-stock:
	WINEPREFIX="$(PREFIX)" WINEDEBUG=$(WINEDBG) \
	    $(WINE) explorer /desktop=$(DESKNAME),$(DESKTOP) "$(GAMEEXE)"

clean:
	rm -rf $(BUILD)
