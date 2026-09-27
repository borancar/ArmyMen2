set pagination off
set height 0
set confirm off
watch *(short*)0x0004F912C
commands
  silent
  set $g = *(int*)0x004F93C0
  set $v = *(short*)0x0004F912C
  printf "HIGH[23] <- %d  gen=%d\n", $v, $g
  if $g == 565
    printf "=== BACKTRACE at gen 565 ===\n"
    bt 5
    info args
    info locals
    printf "=== DONE ===\n"
    detach
    quit
  end
  continue
end
continue
