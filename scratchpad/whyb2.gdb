set pagination off
set height 0
set confirm off
watch *(short*)0x0004F912C
commands
  silent
  set $g = *(int*)0x004F93C0
  if $g == 565
    printf ">>> HYB gen565 HIGH[23]<-%d roach pos=(%d,%d) facing=%d field530=%d rowcnt=%d\n", *(short*)0x0004F912C, *(short*)0x0c84401e, *(short*)0x0c844020, *(unsigned char*)0x0c84404c, *(unsigned char*)0x0c84453c, *(int*)0x0c84407c
    printf "=== DONE ===\n"
    detach
    quit
  end
  continue
end
continue
