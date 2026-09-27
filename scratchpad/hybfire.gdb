set pagination off
set height 0
set confirm off
watch *(unsigned int*)0x0c78b6cc
commands
  silent
  printf "HYB FIRE_X <- 0x%x  (pc=%p)\n", *(unsigned int*)0x0c78b6cc, $pc
  if *(unsigned int*)0x0c78b6cc == 0x07fb0272
    printf "=== reached cursor value 0x07fb0272 ===\n"
    bt 6
    printf "=== END ===\n"
    detach
    quit
  end
  continue
end
continue
