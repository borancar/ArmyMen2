# ArmyMen II reconstruction harness.
#
# Everything here is 32-bit PE, built with mingw. It is NOT winelib: Wine 11 on
# this host is new-WoW64 (i386-windows + x86_64-unix, no i386-unix), so 32-bit
# winelib ELF objects cannot be produced at all. The harness has to live inside
# the game's own 32-bit address space, which means PE.

# The harness in src/inject is C; the reconstruction in src/game is C++, because
# the original is C++ -- the savegame anchors name .cpp files and ~100 game
# functions take `this` in ecx. A non-static member function is thiscall on i386
# by default, so C++ is the natural vehicle for those; doing it in C would mean
# fighting the language with __attribute__((thiscall)).
#
# No exceptions, no RTTI, no C++ runtime: this DLL is injected into a 1999 game
# and must not drag a standard library in behind it.
CC       := i686-w64-mingw32-gcc
CXX      := i686-w64-mingw32-g++
# -MMD -MP emit a .d beside each .o listing the headers it used. Without them
# a header edit leaves stale objects behind, which is not a theoretical worry:
# the old build compiled every source in one command, so headers were always
# picked up, and splitting into per-object rules silently lost that.
DEPFLAGS := -MD -MP
CFLAGS   := -O2 -g -Wall -Wextra -std=gnu11 -fno-strict-aliasing $(DEPFLAGS)
CXXFLAGS := -O2 -g -Wall -Wextra -std=gnu++14 -fno-strict-aliasing \
            -fno-exceptions -fno-rtti $(DEPFLAGS)
LDFLAGS  := -static-libgcc -static-libstdc++

BUILD     := build
WINE      ?= wine
# Overridable: point at a separate prefix for full isolation, including the
# game's own Options.cfg and save/ directory, which are otherwise shared.
PREFIX    ?= $(CURDIR)/.wine
# Deferred, not `:=` -- ISOLATE=1 rewrites PREFIX further down, and an
# immediate assignment here would have baked in the shared prefix.
GAMEDIR    = $(PREFIX)/drive_c/GOG Games/Army Men II
GAMEEXE   := C:\\GOG Games\\Army Men II\\ArmyMen2.exe
# PORT=1's two paths. The install is named for AM2_GAMEDIR rather than chdir'd
# into, and the exe is reached through Z: so no copy into the game folder is
# needed -- see STATUS.md.
GAMEDIRWIN := C:\\GOG Games\\Army Men II
PORTEXEWIN := Z:$(subst /,\\,$(CURDIR))\\build\\ArmyMen2.exe
LAUNCHEXE := C:\\GOG Games\\Army Men II\\launcher.exe

HOOK_C   := src/inject/dllmain.c \
            src/inject/selfcheck.c \
            src/inject/patch.c \
            src/inject/restore.c \
            src/inject/trace.c \
            src/inject/observe.c \
            src/inject/hooklog.c \
            src/inject/gamelog.c \
            src/inject/input.c \
            src/inject/dinput_hook.c \
            src/inject/control.c

HOOK_CXX := src/game/savetag.cpp \
            src/game/map.cpp \
            src/game/maprow.cpp \
            src/game/pad.cpp \
            src/game/place.cpp \
            src/game/dirty.cpp \
            src/game/air.cpp \
            src/game/anim.cpp \
            src/game/army.cpp \
            src/game/gameproc.cpp \
            src/game/item.cpp \
            src/game/msgslot.cpp \
            src/game/commmsg.cpp \
            src/game/armymsg.cpp \
            src/game/defparse.cpp \
            src/game/definfo.cpp \
            src/game/region.cpp \
            src/game/objflag.cpp \
            src/game/misc.cpp \
            src/game/gamedir.cpp \
            src/game/event.cpp \
            src/game/trig.cpp \
            src/game/cheat.cpp \
            src/game/script.cpp \
            src/game/objscript.cpp \
            src/game/image.cpp \
            src/game/crt.cpp \
            src/game/win32/startgame.cpp \
            src/game/win32/frame.cpp \
            src/game/rect.cpp \
            src/game/dist.cpp \
            src/game/objtable.cpp \
            src/game/objtype.cpp \
            src/game/packkey.cpp \
            src/game/text.cpp \
            src/game/blit.cpp \
            src/game/win32/sprite.cpp \
            src/game/win32/surface.cpp \
            src/game/win32/font.cpp \
            src/game/win32/mapdraw.cpp \
            src/game/win32/widget.cpp \
            src/game/win32/palette.cpp \
            src/game/win32/winmain.cpp \
            src/game/win32/winproc.cpp \
            src/game/win32/device.cpp \
            src/game/win32/report.cpp \
            src/game/win32/wavefile.cpp \
            src/game/win32/dplay.cpp \
            src/game/win32/movie.cpp \
            src/game/win32/audio.cpp

HOOK_OBJ := $(patsubst %.c,$(BUILD)/obj/%.o,$(HOOK_C)) \
            $(patsubst %.cpp,$(BUILD)/obj/%.o,$(HOOK_CXX))

# ws2_32 for the control socket; gdi32/user32 for the runtime font generator,
# which draws each glyph with GDI before encoding it.
HOOK_LIBS := -lws2_32 -lgdi32 -luser32 -lole32 -lwinmm -ladvapi32

HOOK_DEP := $(HOOK_OBJ:.o=.d)

LAUNCH_SRC := src/inject/launcher.c

.PHONY: all clean run run-stock install-hook

all: $(BUILD)/am2hook.dll $(BUILD)/launcher.exe

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(HOOK_DEP)

# The DLL is loaded into the game by launcher.exe before its entry point runs.
# Linked with the C++ driver so the C and C++ objects come together correctly.
$(BUILD)/am2hook.dll: $(HOOK_OBJ) | $(BUILD)
	$(CXX) -shared -o $@ $(HOOK_OBJ) $(LDFLAGS) $(HOOK_LIBS)

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

# PORT=1 launches the STANDALONE instead of the injected build: the exe under
# build/, straight from the tree, with AM2_GAMEDIR naming the install so it
# needs no copy into the game folder. A variable on this recipe rather than a
# second target, which is the rule this file keeps -- near-duplicate launch
# targets drift apart and it stops being obvious which is canonical.
#
# The harness variables above do not apply to it: it has no DLL to inject, so
# TRACE and OBSERVE do nothing, and its log is written beside the exe as
# build/am2port.log rather than into the game folder.
ARGS    ?= -nointro -dbg
WINEDBG ?= -all
GAMELOG ?= 1
TRACE   ?= 0
OBSERVE ?= 0
CONTROL ?= 1
DESKTOP ?= 800x600
PORT    ?= 0

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

# Differential test: replay vectors recorded from the ORIGINAL function against
# the reconstruction. Needs no display, no mission and no scripted clicks -- the
# binary is the specification and Unicorn executes it.
#
# Only functions that read no global data can be checked this way; one that
# reads a global needs that global mapped, and mapping it means starting the
# game, which is the thing this avoids.
SELFTEST_SRC := tests/selftest.cpp src/game/rect.cpp src/game/dist.cpp \
                src/game/packkey.cpp src/game/item.cpp src/game/msgslot.cpp src/game/armymsg.cpp src/game/defparse.cpp src/game/definfo.cpp src/game/region.cpp src/game/objflag.cpp src/game/misc.cpp src/game/objtype.cpp src/game/objtable.cpp src/game/cheat.cpp src/game/script.cpp src/game/objscript.cpp src/game/image.cpp src/game/crt.cpp src/game/gamedir.cpp src/game/event.cpp src/game/savetag.cpp src/game/army.cpp src/game/maprow.cpp src/game/map.cpp src/game/air.cpp src/game/trig.cpp src/game/gameproc.cpp src/game/pad.cpp src/game/place.cpp src/game/dirty.cpp src/game/anim.cpp src/platform/crt/sort.cpp src/platform/crt/string.cpp src/platform/crt/conv.cpp src/platform/crt/printf.cpp src/platform/crt/fltcvt.cpp src/platform/crt/stdio.cpp

.PHONY: selftest
selftest: $(BUILD)/selftest.exe
	@WINEPREFIX="$(PREFIX)" wine $(BUILD)/selftest.exe

# Makefile is a prerequisite because SELFTEST_SRC lives in it: without that,
# removing a module from the list leaves a stale binary that make happily
# calls up to date, and the link guard in `check` reports ok on a list that
# no longer links. Verified by removing gamedir.cpp and watching it fail.
$(BUILD)/selftest.exe: $(SELFTEST_SRC) tests/vectors.h tests/scriptvec.h tests/placevec.h tests/dirtyvec.h tests/fireposevec.h tests/hitreactvec.h tests/qsortvec.h tests/printfvec.h tests/loadimage.h Makefile
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -static -static-libgcc -static-libstdc++ \
	    -o $@ $(SELFTEST_SRC)

# Re-record from the original. Slow with --angr (symbolic execution per
# function), so it is not part of `make selftest`.
# Re-record the script tokeniser's answers from the original. Separate from
# `check` because it emulates 15,000 words and takes minutes; the recorded
# header is what `selftest` replays.
.PHONY: scriptvec
scriptvec:
	./.venv/bin/python tools/scriptcheck.py

# The same idea one layer up: the ORIGINAL `place` line parser run over every
# line the thirty-six shipped placement files contain, recorded into
# tests/placevec.h for `selftest` to replay.
.PHONY: placevec
placevec:
	./.venv/bin/python tools/placecheck.py

# A STATEFUL oracle, and the only check either dirty-list function has: the
# ORIGINAL run over a sequence of appends under Unicorn, with the whole record
# array recorded rather than a return value. See tools/dirtycheck.py for why
# neither ab.sh configuration can see them.
# The same shape once more: the ORIGINAL SelectFirePose run over every case
# tools/firepose.py enumerates, recorded into tests/fireposevec.h for
# `selftest` to replay against the C. Separate from `check` because `check`
# already runs the tool -- this target only re-records the header.
.PHONY: fireposevec
fireposevec:
	./.venv/bin/python tools/firepose.py --emit tests/fireposevec.h

# tools/hitreactcheck.py enumerates, recorded into tests/hitreactvec.h so the
# C is replayed against the ORIGINAL's answers rather than against a model.
.PHONY: hitreactvec
hitreactvec:
	./.venv/bin/python tools/hitreactcheck.py --emit tests/hitreactvec.h

.PHONY: dirtyvec
dirtyvec:
	./.venv/bin/python tools/dirtycheck.py

.PHONY: vectors
vectors:
	./.venv/bin/python tools/vectors.py --validate --angr --emit

# Everything that can be checked without launching the game. The A/B is the
# real verification and needs a display and about forty minutes -- run
# `tools/ab.sh all` for that -- but these catch a different class of problem
# and take seconds.
#
# The drift check is the point. docs/boundary.md, docs/comcalls.tsv and the
# rest are generated, and a tool changing without its output being regenerated
# leaves the repository asserting something no tool currently produces. That
# has happened: figures quoted in CLAUDE.md were stale by many commits before
# anyone noticed, which is the prose version of the same fault.
#
# What it catches is a TOOL changing its output without the output being
# regenerated and committed -- tested by making coverage.py print a different
# heading, which fails the target. What it does not catch is a hand-edit to a
# generated file: the tools rewrite those before git is consulted, so the edit
# is silently healed rather than reported. That is the right outcome and the
# wrong message, and it is worth knowing before trusting a green run to mean
# nobody has touched docs/ by hand.
#
# src/game/scripttokens.h is covered too. It is the one generated file outside
# docs/ -- 185 keyword constants read from the table the game itself walks, so
# a case label cannot disagree with the binary about what a number means.
.PHONY: check
check:
	@rc=0; \
	for t in coverage comcalls merges checkcom checkhooks binpatches blindspots checkclaims crt scripttokens scriptactions screens checkpatches checkprose checkseams checkinstalled checkcallers checkglobals checkoffsets checksplit checkthis checkgap checkvtables glyphdump qsortcheck printfcheck moviecheck posecheck formationcheck shakecheck roachcheck rlecheck mprowcheck weaponcheck listcheck placementcheck aicheck hitreactcheck savetagcheck numberkeycheck aiignorecheck aitwincheck aiwalkcheck aifollowcheck aikeeprangecheck rowreleasecheck stateleavecheck refreshcheck vehpointcheck roachbitecheck pathplancheck vehexitcheck tilesetcheck damagecheck shotcheck shotdmgcheck rectquerycheck ringcheck boolcheck explcheck collectcheck firepose regioncheck pathcheck tilepathcheck cheats; do \
	    printf '  %-12s ' "$$t"; \
	    if ./.venv/bin/python tools/$$t.py >/dev/null 2>&1; then \
	        echo ok; \
	    else \
	        echo FAILED; rc=1; \
	    fi; \
	done; \
	printf '  %-12s ' "selftest-link"; \
	if $(MAKE) -s $(BUILD)/selftest.exe >/dev/null 2>&1; then \
	    echo ok; \
	else \
	    echo FAILED; \
	    echo "    build/selftest.exe does not link. Closing an orig_ seam turns"; \
	    echo "    a call through an address into a real symbol, so SELFTEST_SRC"; \
	    echo "    needs the module the function now lives in."; \
	    rc=1; \
	fi; \
	if [ -n "$$(git status --porcelain docs/ src/game/scripttokens.h src/platform/glyphs.inc)" ]; then \
	    echo "  generated files DRIFTED from what is committed:"; \
	    git status --short docs/ src/game/scripttokens.h src/platform/glyphs.inc | sed 's/^/    /'; \
	    echo "    regenerate and commit, or find out why a tool changed its mind"; \
	    rc=1; \
	else \
	    echo "  generated    ok (regenerate identically)"; \
	fi; \
	[ $$rc -eq 0 ] && echo "all static checks pass; tools/ab.sh all is the other half"; \
	exit $$rc

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

run: isolate-prefix $(if $(filter 1,$(PORT)),standalone,install-hook)
	@echo "run: ID=$(ID) port=$(CTLPORT) desktop=$(DESKNAME) log=$(LOGFILE) prefix=$(PREFIX)$(if $(filter 1,$(PORT)), STANDALONE)"
ifeq ($(PORT),1)
	WINEPREFIX="$(PREFIX)" WINEDEBUG=$(WINEDBG) \
	    AM2_GAMEDIR="$(GAMEDIRWIN)" \
	    AM2_CONTROL=$(CONTROL) AM2_CTL_PORT=$(CTLPORT) \
	    $(WINE) explorer /desktop=$(DESKNAME),$(DESKTOP) \
	    "$(PORTEXEWIN)" $(ARGS)
else
	WINEPREFIX="$(PREFIX)" WINEDEBUG=$(WINEDBG) \
	    AM2_GAMELOG=$(GAMELOG) AM2_TRACE=$(TRACE) AM2_OBSERVE=$(OBSERVE) \
	    AM2_CONTROL=$(CONTROL) AM2_CTL_PORT=$(CTLPORT) AM2_LOG=$(LOGFILE) \
	    $(WINE) explorer /desktop=$(DESKNAME),$(DESKTOP) \
	    "$(LAUNCHEXE)" "$(GAMEEXE)" $(ARGS)
endif

# Unpatched, straight from the GOG install -- the A/B reference.
# The standalone build: an EXE that replaces ArmyMen2.exe in the game folder,
# needing the original at BUILD time only.  It is a different PRODUCT from
# am2hook.dll rather than a variation on it -- no harness, no patching, its own
# entry point -- which is why it has a target of its own rather than a
# variable on `run`.
#
# The layout is not decoration.  The image's relocations are stripped, so the
# 3,582 pointers inside its own data cannot be told from the bytes around them
# and cannot be moved; .origdat is OUR section, placed at the addresses those
# pointers were written for, with our own code linked above it.  Every table
# migrated out of that blob into typed C data is one less thing holding the
# layout in place.
SA_SRC   := $(wildcard src/game/*.cpp) $(wildcard src/game/win32/*.cpp) \
            $(wildcard src/platform/crt/*.cpp) \
            src/standalone/runtime.cpp build/standalone/fixups.cpp \
            build/standalone/staticinit.cpp \
            build/standalone/imports.cpp \
            build/standalone/tables.cpp
# control.c and input.c come from the harness unchanged: the socket is what
# makes a standalone run drivable and dumpable, and input.c turned out to have
# no DirectInput dependency at all -- it is a state store the hook reads, so
# it links here even though nothing reads it yet.
SA_CSRC  := src/inject/control.c src/inject/input.c
SA_OBJ   := $(patsubst %.cpp,$(BUILD)/sa/%.o,$(SA_SRC)) \
            $(patsubst %.c,$(BUILD)/sa/%.o,$(SA_CSRC)) \
            $(BUILD)/sa/origdata.o $(BUILD)/sa/origgap.o
SA_FLAGS := $(CXXFLAGS) -DAM2_STANDALONE -Ibuild/standalone
SA_LIBS  := -lddraw -ldinput -ldsound -lwinmm -lole32 -ldxguid -lws2_32
SA_LDF   := -mwindows -static -static-libgcc -static-libstdc++ \
            -Wl,--image-base,0x400000 \
            -Wl,--section-start,.origdat=0x0046F000 \
            -Wl,--section-start,.origbss=$$(cat build/standalone/origbss.addr) \
            -Wl,--section-start,.origgap=0x00401000 \
            -Wl,-Ttext,0x00700000

.PHONY: standalone standalone-generate
standalone-generate:
	./.venv/bin/python tools/mkglobals.py

$(BUILD)/sa/%.o: %.cpp | standalone-generate
	@mkdir -p $(dir $@)
	$(CXX) $(SA_FLAGS) -c $< -o $@

$(BUILD)/sa/%.o: %.c | standalone-generate
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DAM2_STANDALONE -Ibuild/standalone -c $< -o $@

$(BUILD)/sa/origdata.o: build/standalone/origdata.S | standalone-generate
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

$(BUILD)/sa/origgap.o: build/standalone/origgap.S | standalone-generate
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

# Header dependencies, for the same reason the main build tracks them: the
# standalone objects depend on src/inject/standalone.h and orig.h, and
# without this a change to a SEAM rebuilt nothing. That produced several
# "the fix did not take" runs -- a stale font.o still held `mov $0x4646a9`
# for a free the header had long since redefined.
-include $(SA_OBJ:.o=.d)

standalone: standalone-generate
	$(MAKE) $(BUILD)/ArmyMen2.exe

$(BUILD)/ArmyMen2.exe: $(SA_OBJ)
	$(CXX) -o $@ $(SA_OBJ) $(SA_LDF) $(SA_LIBS)
	@echo "standalone: $@  -- copy it into the game folder to replace ArmyMen2.exe"

run-stock:
	WINEPREFIX="$(PREFIX)" WINEDEBUG=$(WINEDBG) \
	    $(WINE) explorer /desktop=$(DESKNAME),$(DESKTOP) "$(GAMEEXE)"

# The NATIVE build: the same reconstruction as the standalone, linked as a
# Linux executable against src/platform -- the Win32 and DirectX surface the
# game's win32/ modules call, implemented over SDL3 -- instead of against
# Wine. One executable, no runner: SDL is the outermost layer and everything
# the game sees below its window is emulated to it.
#
# It is i386, not x86_64, and cannot be otherwise: the carried .data holds
# 32-bit pointers and every record offset in the reconstruction assumes a
# 4-byte pointer. The section placement is the standalone's -- .origdat at
# the original's VAs, our code above at 0x00700000 -- and the same generated
# files serve both; only the assembler directives in them differ by format.
# Needs glibc-devel.i686, libstdc++-devel.i686 and SDL3-devel.i686.
#
#     make native                          builds BOTH binaries
#     make native-dev                      builds build/armymen2-dev alone
#     AM2_GAMEDIR="$(GAMEDIR)" build/armymen2 -nointro
#
# TWO BINARIES, ONE TREE. The player's binary carries no control socket, no
# injected input and no savestates -- src/standalone/noinput.cpp answers
# device.cpp's three injected-input calls with "nothing". The development
# binary is the same sources with AM2_DEVTOOLS defined plus the harness's
# control.c and input.c and src/standalone/devtools.cpp: the control socket
# on port 31337 (AM2_CTL_PORT), a fixed-address arena for every game
# allocation, and `snap save|load FILE` savestates over that arena and the
# carried globals; F5 writes the GAME's save and F9 reloads it. The two object trees are
# separate because the define changes what a header expands to.
NATIVE_CXX   := g++
NATIVE_CC    := gcc
NATIVE_SRC   := $(SA_SRC) $(wildcard src/platform/*.cpp) \
                src/standalone/noinput.cpp src/standalone/devtools.cpp
NATIVE_OBJ   := $(patsubst %.cpp,$(BUILD)/native/%.o,$(NATIVE_SRC)) \
                $(BUILD)/native/origdata.o $(BUILD)/native/origgap.o
DEV_CSRC     := $(SA_CSRC)
DEV_OBJ      := $(patsubst %.cpp,$(BUILD)/native-dev/%.o,$(NATIVE_SRC)) \
                $(patsubst %.c,$(BUILD)/native-dev/%.o,$(DEV_CSRC)) \
                $(BUILD)/native-dev/origdata.o $(BUILD)/native-dev/origgap.o
NATIVE_DEFS  := -DAM2_STANDALONE -DAM2_NATIVE -Ibuild/standalone \
                -isystem src/platform/include -include callconv.h
NATIVE_ARCH  := -m32 -fno-pie -no-pie
NATIVE_CXXF  := $(NATIVE_ARCH) -O2 -g -Wall -Wextra -std=gnu++14 \
                -fno-strict-aliasing -fno-exceptions -fno-rtti $(DEPFLAGS) \
                $(NATIVE_DEFS)
NATIVE_CF    := $(NATIVE_ARCH) -O2 -g -Wall -Wextra -std=gnu11 \
                -fno-strict-aliasing $(DEPFLAGS) $(NATIVE_DEFS)
NATIVE_LDF   := $(NATIVE_ARCH) -static-libgcc \
                -Wl,--section-start,.origgap=0x00401000 \
                -Wl,--section-start,.origdat=0x0046F000 \
                -Wl,--section-start,.origbss=$$(cat build/standalone/origbss.addr) \
                -Wl,-Ttext=0x00700000 -Wl,-z,norelro -Wl,--no-warn-rwx-segments
NATIVE_LIBS  := -lSDL3 -lpthread -lm

# The HYBRID build: the ORIGINAL ArmyMen2.exe, loaded by our own PE loader
# and run over src/platform, with no Wine anywhere. src/hybrid/loader.cpp
# is WinMain: it maps the retail image at 0x00400000 -- which an ELF whose
# text is at 0x00700000 leaves free -- fills its import table with the
# platform layer's functions, and calls its entry point. No reconstruction
# is linked in at all, so it is the platform layer under the ORIGINAL code,
# which is the other half of what the native build checks.
#
#     make hybrid
#     AM2_GAMEDIR="$(GAMEDIR)" AM2_LOG=/tmp/hybrid.log build/armymen2-hybrid -nointro
HYBRID_SRC   := $(wildcard src/platform/*.cpp) src/hybrid/loader.cpp
HYBRID_OBJ   := $(patsubst %.cpp,$(BUILD)/hybrid/%.o,$(HYBRID_SRC))
HYBRID_DEFS  := -DAM2_NATIVE -DAM2_HYBRID -isystem src/platform/include -include callconv.h
HYBRID_CXXF  := $(NATIVE_ARCH) -O2 -g -Wall -Wextra -std=gnu++14 \
                -fno-strict-aliasing -fno-exceptions -fno-rtti $(DEPFLAGS) \
                $(HYBRID_DEFS)
HYBRID_LDF   := $(NATIVE_ARCH) -static-libgcc \
                -Wl,-Ttext=0x00700000 -Wl,-z,norelro -Wl,--no-warn-rwx-segments

#
# The development binary adds the harness's control socket (src/inject's
# control.c and input.c, over the stubs in src/hybrid/devtools.cpp), so a
# run can be driven and its object table dumped exactly as the native
# development binary's can. Same port, same tools.
HYBRID_DEV_OBJ := $(patsubst %.cpp,$(BUILD)/hybrid-dev/%.o,$(HYBRID_SRC) src/hybrid/devtools.cpp) \
                  $(patsubst %.c,$(BUILD)/hybrid-dev/%.o,$(SA_CSRC))
HYBRID_CF    := $(NATIVE_ARCH) -O2 -g -Wall -Wextra -std=gnu11 -fno-strict-aliasing \
                $(DEPFLAGS) $(HYBRID_DEFS)

.PHONY: hybrid hybrid-dev
hybrid: $(BUILD)/armymen2-hybrid $(BUILD)/armymen2-hybrid-dev
hybrid-dev: $(BUILD)/armymen2-hybrid-dev

$(BUILD)/hybrid/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(HYBRID_CXXF) -c $< -o $@

$(BUILD)/hybrid-dev/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(HYBRID_CXXF) -DAM2_DEVTOOLS -c $< -o $@

$(BUILD)/hybrid-dev/%.o: %.c
	@mkdir -p $(dir $@)
	$(NATIVE_CC) $(HYBRID_CF) -DAM2_DEVTOOLS -c $< -o $@

-include $(HYBRID_OBJ:.o=.d) $(HYBRID_DEV_OBJ:.o=.d)

$(BUILD)/armymen2-hybrid: $(HYBRID_OBJ)
	$(NATIVE_CXX) -o $@ $(HYBRID_OBJ) $(HYBRID_LDF) $(NATIVE_LIBS)
	@echo "hybrid: $@"

$(BUILD)/armymen2-hybrid-dev: $(HYBRID_DEV_OBJ)
	$(NATIVE_CXX) -o $@ $(HYBRID_DEV_OBJ) $(HYBRID_LDF) $(NATIVE_LIBS)
	@echo "hybrid-dev: $@"

.PHONY: native native-dev
# BOTH, because the two go out of step silently otherwise: every drive and
# every fixture in tools/ runs build/armymen2-dev, so a `make native` that
# left it stale meant testing yesterday's code against today's source and
# having nothing say so. The player's binary is the one that matters and the
# development one is the one that gets run, which is the wrong way round for
# a target that builds only the first.
# NOT `native: standalone-generate`. Every object rule already has it as an
# order-only prerequisite, so the sub-make runs it once; naming it here too
# ran the generator TWICE per `make native`, printing its summary twice for
# no work. It is cheap, but a build that says it did something twice invites
# the reader to believe it had to.
native:
	$(MAKE) $(BUILD)/armymen2 $(BUILD)/armymen2-dev

# Just the development binary, for when only that is wanted.
native-dev:
	$(MAKE) $(BUILD)/armymen2-dev

$(BUILD)/native/%.o: %.cpp | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(NATIVE_CXXF) -c $< -o $@

$(BUILD)/native/%.o: %.c | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CC) $(NATIVE_CF) -c $< -o $@

$(BUILD)/native/origdata.o: build/standalone/origdata.S | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CC) $(NATIVE_ARCH) -c $< -o $@

$(BUILD)/native/origgap.o: build/standalone/origgap.S | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CC) $(NATIVE_ARCH) -c $< -o $@

$(BUILD)/native-dev/%.o: %.cpp | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(NATIVE_CXXF) -DAM2_DEVTOOLS -c $< -o $@

$(BUILD)/native-dev/%.o: %.c | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CC) $(NATIVE_CF) -DAM2_DEVTOOLS -c $< -o $@

$(BUILD)/native-dev/origdata.o: build/standalone/origdata.S | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CC) $(NATIVE_ARCH) -c $< -o $@

$(BUILD)/native-dev/origgap.o: build/standalone/origgap.S | standalone-generate
	@mkdir -p $(dir $@)
	$(NATIVE_CC) $(NATIVE_ARCH) -c $< -o $@

-include $(NATIVE_OBJ:.o=.d)
-include $(DEV_OBJ:.o=.d)

$(BUILD)/armymen2: $(NATIVE_OBJ)
	$(NATIVE_CXX) -o $@ $(NATIVE_OBJ) $(NATIVE_LDF) $(NATIVE_LIBS)
	@echo "native: $@"

$(BUILD)/armymen2-dev: $(DEV_OBJ)
	$(NATIVE_CXX) -o $@ $(DEV_OBJ) $(NATIVE_LDF) $(NATIVE_LIBS)
	@echo "native-dev: $@"

clean:
	rm -rf $(BUILD)
