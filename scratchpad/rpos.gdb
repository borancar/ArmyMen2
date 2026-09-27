set pagination off
set height 0
set confirm off
watch *(unsigned int*)0x0c6ca52e
commands
  silent
  printf "POS <- x=%d y=%d  pc=%p\n", (short)(*(unsigned int*)0x0c6ca52e & 0xffff), (short)((*(unsigned int*)0x0c6ca52e>>16)&0xffff), $pc
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
