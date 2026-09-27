set pagination off
set height 0
set confirm off
watch *(unsigned char*)0x0c8444dc
commands
  silent
  printf "FACING <- %d  pc=%p pump~\n", *(unsigned char*)0x0c8444dc, $pc
  bt 4
  printf "----\n"
  continue
end
break sdl.cpp:1211
commands
  silent
  set $n=$n+1
  if $n>=4
    detach
    quit
  end
  continue
end
set $n=0
continue
