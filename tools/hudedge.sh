#!/bin/sh
# Drive the HUD's EDGE STRIP -- ADDR_HUD_WIDGET_C, the 16-pixel bar down the
# right of the screen -- and print what it did.
#
# Same shape and same reason as tools/hudstrip.sh: it reads the GAME's own
# memory over the control socket, so it runs identically under AM2_NOPATCH=1.
# The game must ALREADY be in a live mission with both dialogs cleared.
#
# The strip is the panel's TAB, so its most visible effect is on a widget that
# is not itself: clicking it toggles ADDR_HUD_WIDGET_B's HUDPANEL_OFF_OPEN and
# the panel slides. The FLAG is compared and not the panel's rectangle, because
# the slide is an animation and its position depends on when the dump lands.
cd "$(dirname "$0")/.." || exit 1
D="tools/drive.sh"
rw() { $D ctl "dump $1 $2" | awk '{print $3}'; }
i32() { python3 -c "print(int.from_bytes(bytes.fromhex('$1')[:4],'little',signed=True))"; }
str() { python3 -c "
b=bytes.fromhex('$1').split(b'\x00')[0]
print(repr(b.decode('latin1')) if b else \"''\")"; }
at() { python3 -c "print('%X'%(0x$E+$1))"; }

E=$(python3 -c "print('%08X'%int.from_bytes(bytes.fromhex('$(rw 4FCF4C 4)'),'little'))")
P=$(python3 -c "print('%08X'%int.from_bytes(bytes.fromhex('$(rw 4FCF54 4)'),'little'))")
echo "edge strip present: $([ "$E" != 00000000 ] && echo yes || echo no)"
echo "its vtable: $(python3 -c "print('%08x'%int.from_bytes(bytes.fromhex('$(rw $E 4)'),'little'))")"

echo "-- the selected trooper"
echo "   health   $(i32 "$(rw "$(at 0x64)" 4)") of 90"
echo "   caption A $(str "$(rw "$(at 0x68)" 8)")"
echo "   caption B $(str "$(rw "$(at 0x8c)" 8)")"
echo "   ammo     $(i32 "$(rw "$(at 0xac)" 4)")"
echo "   second   $(i32 "$(rw "$(at 0x88)" 4)")"

echo "-- the panel tab"
POPEN=$(python3 -c "print('%X'%(0x$P+0x5c))")
echo "   open before   $(i32 "$(rw $POPEN 4)")"
$D ctl "cursor 632 250" >/dev/null; sleep 1
$D ctl "mouse left tap" >/dev/null; sleep 2
echo "   after a click $(i32 "$(rw $POPEN 4)")"
sleep 3
$D ctl "mouse left tap" >/dev/null; sleep 2
echo "   and again     $(i32 "$(rw $POPEN 4)")"
