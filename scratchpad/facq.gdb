set pagination off
set height 0
set confirm off
watch *(unsigned int*)0x0c6ca5e0
commands
  silent
  if *(unsigned int*)0x0c6ca5e0 == 0x3e8
    printf "=== FOLLOW_UID <- 0x3e8  pc=%p ===\n", $pc
    bt 8
    printf "=== END ===\n"
    detach
    quit
  end
  continue
end
continue
