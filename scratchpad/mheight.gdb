set pagination off
set height 0
set confirm off
watch *(short*)0x12573422
commands
  silent
  printf "HEIGHT <- %d  pc=%p\n", *(short*)0x12573422, $pc
  bt 4
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
