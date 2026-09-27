set pagination off
set height 0
set confirm off
watch *(unsigned char*)0x0c6caa5c
commands
  silent
  printf "F540 <- %d pc=%p\n", *(unsigned char*)0x0c6caa5c, $pc
  bt 5
  printf "----\n"
  continue
end
break sdl.cpp:1211
commands
  silent
  set $n=$n+1
  if $n>=3
    detach
    quit
  end
  continue
end
set $n=0
continue
