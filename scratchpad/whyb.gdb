set pagination off
set height 0
set confirm off
watch *(short*)0x0004F912C
commands
  silent
  set $g = *(int*)0x004F93C0
  if $g == 565
    printf ">>> HYB gen565 HIGH[23]<-%d  roach 0xc84400c pos=(%d,%d) uid=0x%x\n", *(short*)0x0004F912C, *(short*)0x0c84401e, *(short*)0x0c844020, *(unsigned int*)0x0c844010
    printf "=== DONE ===\n"
    detach
    quit
  end
  continue
end
continue
