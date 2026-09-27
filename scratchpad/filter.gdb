set pagination off
set height 0
set confirm off
break region.cpp:9679
commands
  silent
  printf "filter: weapon=%p\n", weapon
  printf "  weapon+0xC0 def=%p\n", *(void**)((char*)weapon+0xC0)
  printf "  kind=*def=%d (0x%x)\n", **(int**)((char*)weapon+0xC0), **(int**)((char*)weapon+0xC0)
  printf "  k=%d  g_mouseButton0=%d\n", k, *(int*)0
  bt 3
  detach
  quit
end
continue
