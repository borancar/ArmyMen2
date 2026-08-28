#!/bin/sh
# Drive the HUD's top strip and print what it did.
#
# It reads the GAME's own memory over the control socket, so it runs
# identically under AM2_NOPATCH=1 -- which is the whole point, because this
# class is invisible to every other check in the tree. Its paint came out at
# zero differing pixels inside the strip's own rectangle with the log empty,
# which is what an empty log looks like on both sides and proves nothing about
# the scrolling, the rewind or the console.
#
# The game must ALREADY be in a live mission with both opening dialogs
# cleared; this does not drive the menus. See tools/ab.sh's `mission`.
#
# What it does NOT compare, deliberately: the absolute row positions. The row
# widths come from TextExtent of the text that actually arrived, and `type`
# drops a character often enough that two runs get widths differing by one --
# which showed up as a 191-against-192 diff on the first attempt and read
# exactly like a defect. What this function computes is the INVARIANT beneath
# them, so that is what is printed.
cd "$(dirname "$0")/.." || exit 1
D="tools/drive.sh"
rw() { $D ctl "dump $1 4" | awk '{print $3}'; }
i32() { python3 -c "print(int.from_bytes(bytes.fromhex('$1'),'little'))"; }
f32() { python3 -c "import struct;print('%.1f'%struct.unpack('<f',bytes.fromhex('$1'))[0])"; }
at() { python3 -c "print('%X'%(0x$P+$1))"; }

# ADDR_HUD_WIDGET_A, which holds the strip.
P=$(python3 -c "print('%08X'%int.from_bytes(bytes.fromhex('$(rw 4FCF00)'),'little'))")
echo "widget present: $([ "$P" != 00000000 ] && echo yes || echo no)"

echo "-- hover"
$D ctl "cursor 400 300" >/dev/null; sleep 1
echo "   off strip      sprite=$([ "$(i32 "$(rw "$(at 0x64)")")" = 0 ] && echo none || echo set)"
$D ctl "cursor 10 10" >/dev/null; sleep 1
HOT=$(i32 "$(rw "$(at 0x5c)")"); NOW=$(i32 "$(rw "$(at 0x64)")")
echo "   on button      sprite=$([ "$NOW" = "$HOT" ] && echo HOT || echo other)"
$D ctl "cursor 400 10" >/dev/null; sleep 1
echo "   strip not btn  sprite=$([ "$(i32 "$(rw "$(at 0x64)")")" = 0 ] && echo none || echo set)"

echo "-- console"
$D ctl "key 0x0e tap" >/dev/null; sleep 1
echo "   typing=$(i32 "$(rw "$(at 0x48c)")") handler=$(rw 5125B8)"

echo "-- twelve messages"
for n in 1 2 3 4 5 6 7 8 9 10 11 12; do
  $D ctl "key 0x0e tap" >/dev/null 2>&1; sleep 0.3
  $D ctl "type AAAAAAAAAAAAAAAAAAAAAAAA" >/dev/null 2>&1; sleep 0.3
  $D ctl "mouse left tap" >/dev/null 2>&1; sleep 0.3
done
sleep 1
echo "   count=$(i32 "$(rw "$(at 0x594)")") viewW=$(i32 "$(rw "$(at 0x5b0)")")"
sleep 10
# The ABSOLUTE x values depend on the text that actually arrived, and `type`
# drops a character often enough that the two sides get widths differing by
# one. What this function computes is the INVARIANT -- row n sits at the
# running sum of (width + gap) of the rows before it -- so print the residual
# against that and not the raw numbers. Zero on both sides is the oracle.
ACC=0
BAD=0
for n in 0 1 2 3 4 5 6 7 8 9 10 11; do
  V=$(rw "$(at "0x6c+$n*0x58+0x50")"); W=$(rw "$(at "0x6c+$n*0x58+0x54")")
  X=$(f32 $V); WI=$(i32 $W)
  R=$(python3 -c "print('%.1f' % (float('$X') - $ACC))")
  [ "$R" = "0.0" ] || BAD=$((BAD+1))
  ACC=$((ACC + WI + 32))
done
echo "   rows off their running sum: $BAD of 12"
echo "   total width vs viewW: $([ $ACC -gt 598 ] && echo over || echo under)"
# And the settled scroll is row i's x, where i is the walk BACK from viewW.
T=598; I=11
while [ $I -ge 0 ]; do
  W=$(i32 "$(rw "$(at "0x6c+$I*0x58+0x54")")")
  [ $((T - W)) -lt 0 ] && break
  T=$((T - W - 32)); I=$((I - 1))
done
I=$((I + 1)); [ $I -lt 0 ] && I=0
EXP=$(f32 "$(rw "$(at "0x6c+$I*0x58+0x50")")")
GOT=$(f32 "$(rw "$(at 0x598)")")
echo "   settled scroll is row $I's x: $([ "$EXP" = "$GOT" ] && echo yes || echo "no ($GOT vs $EXP)")"
