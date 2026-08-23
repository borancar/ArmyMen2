#!/bin/bash
# Play one scripted run twice -- once on the game's own code, once on the
# reconstruction -- and compare what came out.
#
#   tools/ab.sh bootcamp     fullscreen, into the first Boot Camp mission
#   tools/ab.sh windowed     -w, title screen only; the frame is static
#   tools/ab.sh intro        -dbg, so the Smacker film plays
#   tools/ab.sh audio        Boot Camp again, with a silent sound device
#   tools/ab.sh mission      past both dialogs into live play, and scrolling
#   tools/ab.sh campaign     SINGLE PLAYER into MAP 01, the only `variable` path
#   tools/ab.sh quit         out through the menu, so the teardown runs
#   tools/ab.sh all          all seven
#
# This is the strongest check available and the only one that compares against
# the original rather than against expectations. The registry invariant checks
# one subsystem; a screenshot checks that something plausible appeared. This
# checks that 70-odd reconstructed functions produce the same log and the same
# pixels as the code they replaced.
#
# AM2_NOPATCH=1 is what makes it possible: it installs the harness -- logger,
# input hook, control socket -- and none of the reconstruction. `run-stock`
# cannot serve, because it drops the harness too and so cannot be driven or
# logged, and does not start at all on this machine.
#
# Two things are deliberately not compared. The counts, because the per-function
# counters *are* the trace stubs and those are installed with everything else,
# so the unpatched side has none. And the harness's own log lines -- `patch:`,
# `trace `, `dinput:`, `gamelog:` -- which differ by construction and by
# allocation address.
#
# A moving scene will differ by a handful of pixels: the two runs are not
# frame-synchronised and nothing can make them be. `windowed` is the one to
# watch, because its frame is static and it should be pixel-perfect.
#
# Each configuration has a pixel budget and exceeding it fails the run. That is
# not decoration: a reconstruction of the map tile painter once drew 33,137
# wrong pixels and this script reported "A/B clean", because it only ever
# failed on the log. The number was printed and nothing read it.

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DISP="${AM2_DISPLAY:-:99}"
WORK="${AM2_AB_DIR:-/tmp/am2-ab-$$}"
mkdir -p "$WORK" || exit 1

# Everything the harness itself emits, plus one piece of game noise. What is
# left is the game's own messages.
#
# The `]` lines are a loading progress bar the game prints a character at a
# time, and how many arrive depends on how long the load took -- so they differ
# between two runs of the same thing and mean nothing. They also end in CR,
# which is why an earlier `^\]$` never matched them and this compared them for
# a while by accident, passing only because the two counts happened to agree.
# Line endings are normalised before filtering for the same reason.
# The counter-dump pattern allows DIGITS in the name. It read `[A-Za-z_]+`,
# which silently let through every function whose name has one in it --
# BlitCopy16, ObjIsType2, Update3DAudioVolumes. That never showed up until
# `quit`, because trace_report() runs on DLL_PROCESS_DETACH and every other
# configuration kills the process instead of letting it leave.
HARNESS='^(patch:|trace |trace:|verify:|am2hook|gamelog:|dinput:|\]|  [A-Za-z_][A-Za-z0-9_]* +[0-9]+$|==== session)'

# Game output whose COUNT depends on how long the run lived, not on whether the
# reconstruction is right. Kept apart from HARNESS above on purpose: that filter
# removes our own noise, this one removes the game's own -- a different claim,
# and one that has to be justified per line rather than assumed.
#
# `-dbg` emits a single character per frame during live play. Over a mission
# that is tens of thousands of lines, and the two sides never run the same
# number of frames: the first mission A/B compared 24,914 lines against 21,741
# and reported a difference that was entirely wall-clock. Same shape as the `]`
# loading-progress lines already in HARNESS, and the same fix.
#
# ANY single character, not a list of them. This started as `^[_ac]$` because
# those were the three seen; a later run produced `e`, `g` and `i` instead, so
# the set is not fixed and enumerating it just fails again later with a
# different letter. A one-character line carries no information to compare.
#
# How many were dropped is printed, and MIN_FRAMES below turns "none at all"
# into a failure, because a filter that can silently eat the whole log is
# exactly how an A/B comes to pass on nothing.
VOLATILE='^.$'

# Whether DirectSound came up is a fact about the MACHINE, not about the
# reconstruction, in every configuration that attaches no sound device. Wine
# sometimes finds a device and sometimes does not, so "Unable to create
# directsound object" appears on one side and not the other and the run fails
# for a reason that has nothing to do with the code.
#
# It has done that twice now, several commits apart, and both times three
# re-runs came back clean. Re-running until it passes is how a real difference
# gets explained away, so it is filtered instead -- but ONLY where there is no
# device to find.
#
# tools/ab.sh audio attaches one deliberately, and there DirectSound MUST
# start. This filter is not applied to it, so a reconstruction that broke
# buffer creation still fails the configuration built to catch that.
DEVICELESS='^Unable to create directsound object$'

# Configurations that are supposed to reach live gameplay, and the least number
# of per-frame markers that proves they did.
#
# This is not belt and braces. A mission run had the reconstruction reach
# gameplay and the original stop at the briefing on "Press SPACE to continue",
# and with the frame markers filtered that compares 14 lines against 14 and
# reports CLEAN. The run that found it only failed because the marker character
# had changed and the filter missed it -- so the check that caught the bug was
# an accident, and this is the one that will not be.
#
# AM2_AB_MIN_FRAMES overrides it, mainly so the check itself can be tested --
# the same reason AM2_AB_PIXELS exists.
MIN_FRAMES="${AM2_AB_MIN_FRAMES:-500}"

# Lines emitted by the comm threads rather than the main one. Their CONTENT is
# deterministic and worth comparing; their POSITION is not, because the packet
# thread and the receive thread finish whenever the scheduler lets them.
#
# Seen twice: "Packet Thread Exited with return code 259" and " Receive thread
# got event 0" swap places between runs. Both logs then hold the same ten
# lines and diff still fails, on ordering alone.
#
# So these are pulled out and compared as a sorted set, and the rest of the log
# is compared in sequence exactly as before. Sorting the WHOLE log instead
# would hide a genuine ordering change anywhere in it, which is a real thing to
# want to catch -- the map loader and the palette have to happen in order.
THREADED='(Packet Thread|Receive thread)'

drive() { AM2_DISPLAY="$DISP" "$REPO/tools/drive.sh" "$@"; }

# Play one configuration once. $1 = side (orig|recon), $2 = config name.
play() {
    local side="$1" cfg="$2" args wait

    case "$cfg" in
        bootcamp) args="-nointro -dbg"    ; wait=20 ;;
        # 60 rather than 30: at 30 the client area is often still black when
        # the shot lands, and a black frame compared against a painted one is
        # 195,000 pixels of nothing. See the budget note further down.
        windowed) args="-nointro -dbg -w" ; wait=60 ;;
        intro)    args="-dbg"             ; wait=40 ;;
        # The same run as bootcamp, but with an audio device. Without one
        # DirectSound never starts and nine reconstructed functions -- the
        # whole streaming path, InitDirectSound, InitWaveSounds -- do not
        # execute at all, so `bootcamp` compares them not at all. ALSA's `null`
        # plugin supplies a device that discards everything and needs no sound
        # server. See tools/alsa/asoundrc.
        audio)    args="-nointro -dbg"    ; wait=20
                  export ALSA_CONFIG_PATH="$REPO/tools/alsa/asoundrc" ;;
        # Deeper than bootcamp: past BOTH opening dialogs and then scrolling,
        # which is the only way to reach the live frame path. bootcamp stops at
        # the briefing with the dialogs up, and while they are up the game does
        # not compose frames at all -- ComposeFrame sat frozen at 170 for as
        # long as the instruction sign was on screen, and the scroll merge at
        # 0x0041D060 never ran once.
        mission)  args="-nointro -dbg"    ; wait=20 ;;
        # Leaving through the menu, which is the only way the teardown runs at
        # all: drive.sh stop kills the tree, so ShutdownDirectDraw,
        # ShutdownInput, ReleaseSoundBuffers and the sprite and sound frees
        # had never executed once in this project's life. The first time this
        # was driven it found a real bug -- see ReleaseSprite in sprite.cpp.
        # The campaign, which is a different map from Boot Camp and the only
        # path that reaches a `variable` statement: kitchen1.txt declares two
        # and no Boot Camp script declares any. So ScriptVariable and the whole
        # name-table layer are compared here or nowhere.
        campaign) args="-nointro -dbg"    ; wait=25 ;;
        # OPTIONS -> CONTROLS, which is the only configuration that compares
        # the menu widget layer. That dialog is 78,174 LabelDraw calls -- every
        # caption on it, from "SARGE CONTROLS" to "EXIT VEHICLE" -- and it
        # costs two clicks, no typing and no mission. Nothing else in this
        # suite draws a widget at all: bootcamp and campaign pass through the
        # menus on their way somewhere and stop composing frames the moment a
        # dialog is up.
        controls) args="-nointro -dbg"    ; wait=25 ;;
        # The only configuration that reaches the EDIT BOX. The CONTROLS
        # dialog's key-capture boxes look like text fields and are a different
        # class; all five Edit* counters read 0 there. This one drives
        # MULTI-PLAYER -> TCP/IP -> SELECT -> START A WAR to ENTER BATTLE NAME,
        # which has two real text fields, and types into one -- so it compares
        # the handler install as well as the painting.
        #
        # It needs AM2_MULTIPLAYER=1, because the title screen's MULTI-PLAYER
        # entry is patched out of this build (docs/binarypatches.md) and the
        # whole DirectPlay subsystem is otherwise unreachable. Both sides get
        # it, so the comparison is still like for like.
        multi)    args="-nointro -dbg"    ; wait=25 ;;
        # MULTIPLAYER OPTIONS -- 43 checkboxes in two columns under five group
        # headings, and the only screen in the game that is DECLARED rather
        # than built: a 43-record table at 0x004865B8 says where every box
        # sits, what it is called, and which bit of which of two masks it owns.
        #
        # It is reached by POKING the menu-request pair, because the ordinary
        # route needs a DirectPlay session that will not open on this machine.
        # That is legitimate rather than a cheat: those are the GAME's globals
        # and the poke is the same write ESCAPE makes, so the same three
        # commands drive both sides and AM2_NOPATCH=1 takes them unchanged.
        # comm+0x3D8 is poked too -- the host flag, which is what puts OK and
        # DEFAULT on the panel beside CANCEL. Without it the screen is
        # read-only and neither OptionsApply nor OptionsDefaults can run.
        #
        # The three clicks it then makes are the three functions: a group
        # header (OptionsSyncGroup), DEFAULT (OptionsDefaults) and OK
        # (OptionsApply, which names itself "Options changed by host." and
        # puts that line in the comms panel -- so the final frame carries the
        # evidence that the apply reached its end).
        mpoptions) args="-nointro -dbg"  ; wait=25 ;;
        # OPTIONS -> DIFFICULTY, which is the only screen with a LIST BOX --
        # the Easy/Medium/Hard rows. `ctl widgets` says that dialog is the only
        # place the class at 0x0046FCC0 is instantiated, so its painter and its
        # update are checkable here or nowhere.
        difficulty) args="-nointro -dbg" ; wait=25 ;;
        # OPTIONS -> AUDIO, the AUDIO CONTROLS dialog. It is the only screen
        # in the game with a SCROLL BAR: three of them, the SOUND EFFECTS,
        # MUSIC and VOICE sliders, each with an ltarrow and an rtarrow child.
        # `ctl widgets` says class 0x0046FCFC appears nowhere else, so its
        # painter and its destructor are checkable here or nowhere.
        #
        # And the widget dump CANNOT see the thing this configuration is for.
        # A scroll bar's own sprite lives at 0x0064, not at the base's 0x0038,
        # so every bar prints spr=-1 and a wrong bar would look identical. The
        # pixels are the evidence here, which is the reverse of `controls`.
        # With a sound device, because this is the one configuration that
        # reaches SpeakLine at all -- the VOICE bar's handler plays one line
        # at random out of thirty groups, and everywhere else SpeakLine is a
        # unit reacting to something in a live mission.
        #
        # Measured rather than assumed: the game's own LCG at 0x0048CC1C
        # advances FOUR steps across one click on the voice bar and none
        # across a click on the effects bar. One of those four is the
        # handler's own `rand() % 30`; the rest are past SpeakLine's
        # owner check, so the call really does go through.
        audiovol) args="-nointro -dbg"   ; wait=25
                  export ALSA_CONFIG_PATH="$REPO/tools/alsa/asoundrc" ;;
        quit)     args="-nointro -dbg"    ; wait=25
                  export ALSA_CONFIG_PATH="$REPO/tools/alsa/asoundrc" ;;
        *) echo "ab.sh: unknown configuration '$cfg'" >&2; return 1 ;;
    esac

    [ "$side" = orig ] && export AM2_NOPATCH=1 || unset AM2_NOPATCH
    local extra=""
    [ "$cfg" = multi ] && extra="AM2_MULTIPLAYER=1"
    [ "$cfg" = mpoptions ] && extra="AM2_MULTIPLAYER=1"
    AM2_MAKEVARS="TRACE=1" drive start "$wait" "ARGS=$args" $extra \
        >/dev/null 2>&1

    # Boot Camp needs driving; the other two show what they show.
    if [ "$cfg" = bootcamp ] || [ "$cfg" = audio ] || [ "$cfg" = mission ]; then
        "$REPO/tools/point.py" 306 143 --click >/dev/null 2>&1
        sleep 25
        drive ctl "key RETURN tap" >/dev/null 2>&1
        sleep 30
    fi

    if [ "$cfg" = campaign ]; then
        # SINGLE PLAYER -> the player row -> SELECT -> NEW. The recruit dialog
        # is deliberately NOT used: a name that already exists is rejected in
        # silence, so a run that had gone before would sit on the dialog and
        # look exactly like a broken reconstruction. Selecting the existing
        # player is idempotent and the first run creates it.
        "$REPO/tools/point.py" 306 182 --click >/dev/null 2>&1
        sleep 6
        # SELECT PLAYER is up here, and until this dump existed nothing in the
        # suite compared it -- campaign drove straight through it on its way to
        # the map and compared only the log and the final frame. Its tree is
        # the sharp check for this screen, as it is for CONTROLS.
        drive ctl "widgets" 2>/dev/null | tr '|' '\n' \
            > "$WORK/$cfg-$side.widgets" || true
        # No shot: campaign's pixel budget is -1 because it ends in live play,
        # and a frame compared against a check that cannot fail is not a check.
        # The tree is the evidence here.
        if "$REPO/tools/point.py" 240 177 --click >/dev/null 2>&1; then :; fi
        sleep 4
        "$REPO/tools/point.py" 455 221 --click >/dev/null 2>&1   # SELECT
        sleep 6
        "$REPO/tools/point.py" 455 181 --click >/dev/null 2>&1   # NEW
        sleep 25
        # MAP 01: KITCHEN, the strategic map. RETURN starts the mission.
        drive ctl "key RETURN tap" >/dev/null 2>&1
        sleep 25
        drive ctl "mouse left tap" >/dev/null 2>&1
        sleep 8
    fi

    if [ "$cfg" = multi ]; then
        # Coordinates from CLAUDE.md. point.py needs the exact button centre
        # here: two pixels above MULTI-PLAYER lands between buttons, does
        # nothing, and reads exactly like a dead code path.
        "$REPO/tools/point.py" 306 222 --click >/dev/null 2>&1   # MULTI-PLAYER
        sleep 4
        "$REPO/tools/point.py" 200 176 --click >/dev/null 2>&1   # the TCP/IP row
        sleep 3
        "$REPO/tools/point.py" 515 221 --click >/dev/null 2>&1   # SELECT
        sleep 4
        "$REPO/tools/point.py" 321 222 --click >/dev/null 2>&1   # START A WAR
        sleep 5
        # Typing is the point: a character only reaches the field if
        # EditTakeFocus installed g_charHandler.
        drive ctl "type Zulu" >/dev/null 2>&1
        sleep 3
        # The tree here holds the toggles -- the "send" indicators beside the
        # two fields -- and their chosen sprite. A wrong toggle sprite is 212
        # pixels, which no budget catches; it is one changed line here.
        drive ctl "widgets" 2>/dev/null | tr '|' '\n' \
            > "$WORK/$cfg-$side.widgets" || true
    fi

    if [ "$cfg" = mpoptions ]; then
        "$REPO/tools/point.py" 306 222 --click >/dev/null 2>&1   # MULTI-PLAYER
        sleep 4
        "$REPO/tools/point.py" 200 176 --click >/dev/null 2>&1   # TCP/IP
        sleep 3
        "$REPO/tools/point.py" 515 221 --click >/dev/null 2>&1   # SELECT
        sleep 4
        # The comm object is a pointer, so its address has to be read at
        # runtime rather than written down. dump prints little-endian bytes.
        local raw ptr
        raw=$(drive ctl "dump 4751B0 4" 2>/dev/null | awk '{print $3}')
        ptr=$(echo "$raw" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/')
        if [ -n "$ptr" ]; then
            drive ctl "poke $(printf '%X' $((0x$ptr + 0x3D8))) 1" >/dev/null 2>&1
        else
            echo "  WARNING: could not read the comm pointer -- the panel will"
            echo "           be read-only and two of the three clicks do nothing"
        fi
        # The menu request, which is the route the game itself takes.
        drive ctl "poke 511DC8 7" >/dev/null 2>&1
        drive ctl "poke 511DC4 1" >/dev/null 2>&1
        sleep 5
        "$REPO/tools/point.py" 582 339 --click >/dev/null 2>&1   # OPTIONS
        sleep 4
        drive ctl "widgets" 2>/dev/null | tr '|' '\n' \
            > "$WORK/$cfg-$side.widgets" || true
        drive shot "ab-$cfg-dlg-$side" >/dev/null 2>&1
        # MISCELLANEOUS, not POWER-UPS. Both are group headers, but POWER-UPS
        # is RECORD ZERO -- so a constructor that gave every checkbox a group
        # index of 0 synced the right group by accident and this
        # configuration passed. It did, for two commits. MISCELLANEOUS is
        # record 17 and only the right index reaches it.
        "$REPO/tools/point.py" 69 335 --click >/dev/null 2>&1
        sleep 2
        "$REPO/tools/point.py" 576 240 --click >/dev/null 2>&1   # DEFAULT
        sleep 3
        drive shot "ab-$cfg-mid-$side" >/dev/null 2>&1
        "$REPO/tools/point.py" 576 190 --click >/dev/null 2>&1   # OK
        sleep 4
        # And the JOIN panel, which is the SAME class as the host one with a
        # different backdrop and role -- menu request 9 where the host is 7.
        # One more poke reaches a screen nothing else in the suite does, and
        # it is what makes OpenMpJoin compared rather than merely run.
        drive ctl "poke 511DC8 9" >/dev/null 2>&1
        drive ctl "poke 511DC4 1" >/dev/null 2>&1
        sleep 5
        drive shot "ab-$cfg-alt-$side" >/dev/null 2>&1
    fi

    if [ "$cfg" = audiovol ]; then
        "$REPO/tools/point.py" 306 262 --click >/dev/null 2>&1   # OPTIONS
        sleep 5
        "$REPO/tools/point.py" 306 172 --click >/dev/null 2>&1   # AUDIO
        sleep 6
        drive shot "ab-$cfg-dlg-$side" >/dev/null 2>&1
        # Nudge the SOUND EFFECTS bar right. A still dialog never moves a
        # thumb, and the thumb's x is the ONE thing ScrollBarPaint computes --
        # without this the configuration compares three bars that could have
        # been drawn by any arithmetic at all.
        #
        # All THREE bars, not just the first. They are 69 pixels apart and
        # their handlers differ in what they do with the answer -- a sound,
        # the music stream's volume, a random voice line -- so dragging only
        # the top one left two thirds of the family uncompared.
        local bar
        for bar in 186 255 324; do
            local i=0
            while [ $i -lt 4 ]; do
                "$REPO/tools/point.py" 355 "$bar" --click >/dev/null 2>&1
                sleep 1
                i=$((i + 1))
            done
        done
        sleep 3
        # A shot AFTER the clicks, and the first version of this configuration
        # did not take one -- so the four clicks it makes to move a thumb
        # contributed nothing to any comparison. The dlg frame is taken before
        # them and the final frame after the dialog has gone.
        drive shot "ab-$cfg-mid-$side" >/dev/null 2>&1
        drive ctl "widgets" 2>/dev/null | tr '|' '\n' \
            > "$WORK/$cfg-$side.widgets" || true
        # CANCEL rather than OK: OK writes the volume to the registry, so a run
        # would leave the next one starting somewhere else. It is also the
        # click that DESTROYS the dialog, its three bars and their six arrows.
        "$REPO/tools/point.py" 449 276 --click >/dev/null 2>&1
        sleep 5
    fi

    if [ "$cfg" = difficulty ]; then
        "$REPO/tools/point.py" 306 262 --click >/dev/null 2>&1   # OPTIONS
        sleep 5
        "$REPO/tools/point.py" 306 252 --click >/dev/null 2>&1   # DIFFICULTY
        sleep 6
        drive ctl "widgets" 2>/dev/null | tr '|' '\n' \
            > "$WORK/$cfg-$side.widgets" || true
    fi

    if [ "$cfg" = controls ]; then
        # OPTIONS, then CONTROLS. Both are plain title-screen buttons and the
        # cursor is drawn on both screens, so point.py is the right tool here.
        "$REPO/tools/point.py" 306 262 --click >/dev/null 2>&1
        sleep 5
        # A shot DURING the sequence, not only after it. The OPTIONS menu with
        # one entry lit is a transient the final frame cannot show, and three
        # mutations that are genuinely wrong code passed this configuration for
        # exactly that reason -- WidgetRepaint never deferring to an ancestor,
        # WidgetTakeFocus focusing the wrong child, and both flags the base
        # constructor writes as 1. Widget focus and highlight state IS the
        # transient, so a menu layer verified only on a settled frame is barely
        # verified at all.
        drive shot "ab-$cfg-mid-$side" >/dev/null 2>&1
        "$REPO/tools/point.py" 306 212 --click >/dev/null 2>&1
        sleep 6
        drive shot "ab-$cfg-dlg-$side" >/dev/null 2>&1
        # The widget tree, while the dialog is up. This is the sharp check for
        # this layer and the pixels are the blunt one: STATUS.md's table has
        # three reconstructions whose defects are 212, 72 and 0 pixels, all
        # under any budget that survives a blinking caret, and every one of
        # them lives in state that is printed here exactly. Setting the base
        # constructor's 0x0050 to 0 -- invisible to all three frames -- changes
        # all 25 lines of this.
        drive ctl "widgets" 2>/dev/null | tr '|' '\n' \
            > "$WORK/$cfg-$side.widgets" || true
        # CANCEL, which is the only thing here that DESTROYS widgets: the
        # dialog and all twenty-odd of its children come down through slot 0.
        # Without this the destructors are reconstructed and never run.
        "$REPO/tools/point.py" 575 290 --click >/dev/null 2>&1
        sleep 5
    fi

    if [ "$cfg" = mission ]; then
        # MESSAGE FROM HQ.
        "$REPO/tools/point.py" 476 224 --click >/dev/null 2>&1
        sleep 6
        # The full-screen instruction sign behind it; any click clears it, so
        # position is irrelevant and a raw button press is the honest way to say
        # that. It used to be the only way -- point.py located the pointer by
        # colour on a screenshot, this screen draws no pointer, and every
        # attempt silently did nothing. That is fixed: point.py sets the cursor
        # globals directly now and works where nothing is drawn. Left as a bare
        # tap because "anywhere" is still what this click means.
        drive ctl "mouse left tap" >/dev/null 2>&1
        sleep 6
        # Now the mission is live and the view can scroll, which is what brings
        # the dirty-rectangle merge and the whole live frame path into play.
        for _ in 1 2 3 4 5 6; do
            drive ctl "mouse move 200 0" >/dev/null 2>&1
        done
        sleep 4
        for _ in 1 2 3 4 5 6; do
            drive ctl "mouse move 0 200" >/dev/null 2>&1
        done
        sleep 6
    fi

    if [ "$cfg" = quit ]; then
        # QUIT, then OK on CONFIRM GAME EXIT. The process then leaves on its
        # own; the counters reach the log because trace_report() runs on
        # DLL_PROCESS_DETACH, which is the only way to see a teardown count.
        "$REPO/tools/point.py" 306 383 --click >/dev/null 2>&1
        # A shot of CONFIRM GAME EXIT while it is up, and this configuration
        # went a long time without one -- the only frame it compared was the
        # title screen before the click, so the dialog it exists to reach was
        # never in the pixels at all.
        #
        # The wait is what makes the frame comparable. That dialog's body is a
        # TYPEWRITER label: it reveals one character every 100 ms, so a shot
        # taken too early catches the two sides at different characters and is
        # unsynchronised by construction. "Are you sure you want to quit?" is
        # thirty characters and settles in about three seconds; six is the
        # margin. Measured -- shots at 4 s and 14 s are identical.
        sleep 6
        drive shot "ab-$cfg-dlg-$side" >/dev/null 2>&1
        "$REPO/tools/point.py" 475 224 --click >/dev/null 2>&1
        local waited=0
        while pgrep -f 'ArmyMen2[.]exe' >/dev/null 2>&1 && [ $waited -lt 40 ]; do
            sleep 2
            waited=$((waited + 2))
        done
    fi

    drive shot "ab-$cfg-$side" >/dev/null 2>&1

    # DISPLAY must be set here, or `make config` answers for ID 0 and names a
    # log file this run never wrote. That happened, and because two missing
    # files diff as identical it reported a clean A/B on no data at all -- so
    # the emptiness check below is not paranoia, it is the bug.
    eval "$(cd "$REPO" && DISPLAY="$DISP" make -s config)"
    if [ ! -f "$GAMEDIR/$LOGFILE" ]; then
        echo "ab.sh: no log at $GAMEDIR/$LOGFILE -- did the run start?" >&2
        drive stop >/dev/null 2>&1
        return 1
    fi
    tr -d '\r' < "$GAMEDIR/$LOGFILE" | grep -vE "$HARNESS" | grep -v '^$' \
        > "$WORK/$cfg-$side.raw"
    grep -cE "$VOLATILE" "$WORK/$cfg-$side.raw" > "$WORK/$cfg-$side.volatile" \
        || echo 0 > "$WORK/$cfg-$side.volatile"
    # See DEVICELESS above: dropped everywhere except `audio`, which is the one
    # configuration where a sound device is supposed to be there.
    if [ "$cfg" = audio ]; then
        env_filter='^$'
    else
        env_filter="$DEVICELESS"
    fi
    grep -cE "$env_filter" "$WORK/$cfg-$side.raw" > "$WORK/$cfg-$side.envdrop" \
        || echo 0 > "$WORK/$cfg-$side.envdrop"
    grep -vE "$VOLATILE" "$WORK/$cfg-$side.raw" \
        | grep -vE "$env_filter" > "$WORK/$cfg-$side.all"
    grep -vE "$THREADED" "$WORK/$cfg-$side.all" > "$WORK/$cfg-$side.log"
    grep -E "$THREADED" "$WORK/$cfg-$side.all" | sort \
        > "$WORK/$cfg-$side.threaded"
    if [ ! -s "$WORK/$cfg-$side.log" ]; then
        echo "ab.sh: $cfg/$side produced no game log lines -- refusing to" >&2
        echo "       call that a match. Check AM2_GAMELOG and the filter." >&2
        drive stop >/dev/null 2>&1
        return 1
    fi
    drive stop >/dev/null 2>&1
    sleep 2
}

compare() {
    local cfg="$1" rc=0

    local vo vr
    vo=$(cat "$WORK/$cfg-orig.volatile" 2>/dev/null || echo 0)
    vr=$(cat "$WORK/$cfg-recon.volatile" 2>/dev/null || echo 0)
    if [ "$vo" -gt 0 ] || [ "$vr" -gt 0 ]; then
        echo "  frames  $vo/$vr per-frame markers dropped as volatile"
    fi

    # The widget tree, where a configuration captured one. Compared as an exact
    # diff rather than against a budget, because it is exact: the same 25 lines
    # come back from the original and from the reconstruction.
    if [ -s "$WORK/$cfg-orig.widgets" ] && [ -s "$WORK/$cfg-recon.widgets" ]; then
        if diff -q "$WORK/$cfg-orig.widgets" "$WORK/$cfg-recon.widgets" \
             >/dev/null 2>&1; then
            echo "  widgets identical ($(wc -l < "$WORK/$cfg-orig.widgets" \
                 | tr -d ' ') nodes)"
        else
            echo "  widgets DIFFER:"
            diff "$WORK/$cfg-orig.widgets" "$WORK/$cfg-recon.widgets" \
                | head -20 | sed 's/^/          /'
            rc=1
        fi
    fi

    local eo er
    eo=$(cat "$WORK/$cfg-orig.envdrop" 2>/dev/null || echo 0)
    er=$(cat "$WORK/$cfg-recon.envdrop" 2>/dev/null || echo 0)
    if [ "$eo" != "$er" ]; then
        echo "  device  DirectSound started on one side and not the other"
        echo "          ($eo/$er) -- environment, not the reconstruction. Use"
        echo "          tools/ab.sh audio to compare the audio path itself."
    fi

    if [ "$cfg" = mission ] && { [ "$vo" -lt "$MIN_FRAMES" ] || [ "$vr" -lt "$MIN_FRAMES" ]; }; then
        echo "  frames  TOO FEW -- $cfg is supposed to reach live gameplay and"
        echo "          at least one side did not ($vo/$vr, want $MIN_FRAMES+)."
        echo "          Comparing these logs would compare the two ways of not"
        echo "          getting there. Re-run; the drive is not reliable."
        rc=1
    fi

    if ! diff -q "$WORK/$cfg-orig.threaded" "$WORK/$cfg-recon.threaded" \
            >/dev/null 2>&1; then
        echo "  threads DIFFER (compared as a set, so this is real):"
        diff "$WORK/$cfg-orig.threaded" "$WORK/$cfg-recon.threaded" \
            | sed 's/^/          /'
        rc=1
    fi

    if diff -q "$WORK/$cfg-orig.log" "$WORK/$cfg-recon.log" >/dev/null; then
        echo "  log     identical ($(wc -l < "$WORK/$cfg-orig.log") game messages)"
    else
        echo "  log     DIFFERS:"
        diff "$WORK/$cfg-orig.log" "$WORK/$cfg-recon.log" | sed 's/^/          /'
        rc=1
    fi

    # A pixel budget per configuration, because a verdict that only reads the
    # log will call a badly broken frame "clean". PaintMapTiles went in with
    # its tile rows misdecoded, drew 33,137 wrong pixels, and this script said
    # A/B clean -- the number was right there and nothing acted on it.
    #
    # windowed is NOT static, and the 0 it carried for most of the project was
    # a claim about a frame that never got painted. Wine now hands this prefix
    # a lockable primary, so the client area draws -- and four shots two
    # seconds apart differ by 4 pixels each in one 10x10 box at (325,232),
    # which is something blinking. With both sides painted the two frames
    # differ by 10. So the budget is the menu configurations' noise floor, not
    # zero, and a 195,000-pixel result still fails loudly: that one means one
    # side's client area stayed black, which happens and is worth re-running
    # before believing.
    #
    # The two Boot Camp runs animate a little and have sat at 22 for the whole
    # project. The intro is two unsynchronised playbacks of the same film and
    # cannot be compared at all.
    case "$cfg" in
        windowed) budget=50 ;;
        intro)    budget=-1 ;;      # -1 disables the check
        # Measured, not guessed -- see the note below the case.
        mission)  budget=-1 ;;
        # Live play again: two runs of a mission never sit on the same frame,
        # so the pixel figure is meaningless by construction. The log is the
        # evidence, as with intro and mission.
        campaign) budget=-1 ;;
        # The process is gone by the time the shot is taken, so there is no
        # frame to compare. The log is the evidence.
        quit)     budget=-1 ;;
        # The dialog itself is exact -- three runs at 0 -- but the CURSOR is
        # not, and this budget was 0 for exactly as long as it took a fourth
        # run to disagree. It differs about one run in five, always inside the
        # same 10x13 box at wherever the last click left the pointer, which is
        # at most 130 pixels. 200 covers that and nothing else: the errors this
        # screen is here to catch are thousands of pixels, not tens -- a
        # one-colour slip in LabelDraw is 17,110 and a width-from-height slip
        # in WidgetScreenRect is 305,939.
        controls) budget=200 ;;
        # A static dialog like controls, and the cursor moves the same way.
        difficulty) budget=200 ;;
        # 200, and measured in both directions rather than assumed. Two clean
        # runs give 0 on every frame; dropping the thumb offset from
        # ScrollBarPaint gives 336 on the dlg frame, which the DEFAULT budget
        # of 500 passed. That is the mutation this configuration exists to
        # catch, so the first version of it could not fail. 200 leaves room for
        # the cursor, which `controls` measured at 45 on the fourth run of
        # three that had all been 0.
        audiovol)   budget=200 ;;
        # 200, for the same reason and found the same way. The dlg frame is
        # CONFIRM GAME EXIT with its message settled; clean it is 54 -- the
        # cursor -- and dropping the trailing line from TyperPaint, which
        # deletes the whole message, is 361. The DEFAULT of 500 passed that.
        #
        # Twice in one session, so it is worth saying plainly: 500 is too loose
        # for any MENU configuration. A whole line of menu text is about 360
        # pixels, so a budget that cannot see 361 cannot see a missing line.
        # The gameplay configurations are different -- there the scene moves
        # and 500 is the noise floor, which is where that number came from.
        quit)       budget=200 ;;
        # Measured at 0, three runs -- but left at the default, and the
        # reason is worth knowing before trusting this number. A REAL defect
        # here is small: making EditTakeFocus skip installing g_charHandler,
        # so nothing typed ever appears, moves only **72** pixels, because
        # "Zulu" in a menu font is not many pixels. No budget can sit below
        # that and still survive a blinking caret, so this configuration does
        # NOT discriminate the handler install. What it covers is the path --
        # 12,552 EditUpdate calls and the whole dialog -- and gross errors.
        # The handler itself is checked by driving and looking at the field.
        multi)    budget=500 ;;
        # A static dialog like controls and difficulty, plus the same cursor
        # noise, so 200 for the same measured reason. The dlg and mid frames --
        # the OPTIONS dialog itself, which is what this configuration is for --
        # sit at 45, the cursor.
        #
        # It earned its keep on its first run. The final frame -- the lobby --
        # came out 918 pixels apart, all of them in the map preview, and that
        # was a real defect: MakeBitmap reserved the first ten palette entries
        # on the wrong sense of its flag, so the preview was allowed to remap
        # into the block Windows keeps. 918 -> 50 once fixed, and 50 is the
        # cursor. Nothing else in the suite reaches that screen, which is why
        # it had survived every green run. See STATUS.md.
        mpoptions) budget=200 ;;
        *)        budget=500 ;;
    esac
    # Overridable, mainly so the check itself can be tested.
    budget="${AM2_AB_PIXELS:-$budget}"

    "$REPO/.venv/bin/python" - "$REPO/build/shots" "$cfg" "$budget" <<'PY' || rc=1
import sys, os, glob
from PIL import Image
import numpy as np
shots, cfg, budget = sys.argv[1], sys.argv[2], int(sys.argv[3])

def find(side, tag):
    stem = "ab-%s-%s" % (cfg, side) if not tag \
        else "ab-%s-%s-%s" % (cfg, tag, side)
    hits = glob.glob(os.path.join(shots, "*", stem + ".png"))
    return max(hits, key=os.path.getmtime) if hits else None

# A configuration may take extra shots DURING its sequence, not only at the
# end. Every one found is compared against the same budget; a settled final
# frame cannot show a transient, and a menu is mostly transients.
#
# Four slots: the final frame, then mid, dlg and alt. A configuration uses as
# many as it has distinct screens worth comparing -- mpoptions uses all four,
# because the host panel, the options dialog before and after DEFAULT, and the
# join panel are four different screens on one run.
bad = 0
compared = 0
for tag in ("", "mid", "dlg", "alt"):
    o, r = find("orig", tag), find("recon", tag)
    if not o or not r:
        continue
    label = "pixels" if not tag else tag[:6].rjust(6)
    a = np.asarray(Image.open(o).convert("RGB")).astype(int)
    b = np.asarray(Image.open(r).convert("RGB")).astype(int)
    if a.shape != b.shape:
        print("  %s  DIFFERENT SIZES %s vs %s" % (label, a.shape, b.shape))
        bad = 1
        continue
    compared += 1
    n = (np.abs(a - b).sum(axis=2) > 0).sum()
    over = budget >= 0 and n > budget
    print("  %s  %d of %d differ (%.4f%%)%s"
          % (label, n, a[..., 0].size, 100.0 * n / a[..., 0].size,
             "  -- OVER the budget of %d" % budget if over
             else "" if n == 0 else "  -- expected on a moving scene"))
    if over:
        print("  %s  the frame is wrong, not merely unsynchronised" % label)
        bad = 1
if not compared and not bad:
    print("  pixels  no screenshots to compare")
sys.exit(bad)
PY
    return $rc
}

# Every argument, not just the first. `cfgs="${1:-bootcamp}"` silently
# dropped the rest, so `ab.sh bootcamp controls` ran bootcamp alone and then
# printed "A/B clean" -- which reads as both configurations passing and is the
# same failure mode as the two missing files that once diffed as identical.
cfgs="${*:-bootcamp}"
[ "$cfgs" = all ] && cfgs="bootcamp windowed intro audio mission campaign controls difficulty audiovol multi mpoptions quit"

fail=0
for cfg in $cfgs; do
    echo "== $cfg"
    play orig  "$cfg" || exit 1
    play recon "$cfg" || exit 1
    compare "$cfg" || fail=1
done

echo
[ $fail -eq 0 ] && echo "A/B clean." \
                || echo "A/B found differences -- read them before believing them."
echo "artifacts in $WORK"
exit $fail
