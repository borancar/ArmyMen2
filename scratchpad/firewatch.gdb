set pagination off
set height 0
set confirm off
watch *(unsigned int*)0x0c78b6cc
commands
  silent
  if *(unsigned int*)0x0c78b6cc == 0xf52c21ec
    printf "=== FIRE_X <- garbage 0x%x ===\n", *(unsigned int*)0x0c78b6cc
    bt 8
    printf "=== END ===\n"
    detach
    quit
  end
  continue
end
continue
