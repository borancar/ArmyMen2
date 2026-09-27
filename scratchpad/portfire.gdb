set pagination off
set height 0
set confirm off
watch *(unsigned int*)0x0c78b6cc
commands
  silent
  printf "PRT FIRE_X <- 0x%x  pc=%p\n", *(unsigned int*)0x0c78b6cc, $pc
  continue
end
break sdl.cpp:1211
commands
  silent
  set $n=$n+1
  if $n>=3
    printf "=== pump advanced, stop ===\n"
    detach
    quit
  end
  continue
end
set $n=0
continue
