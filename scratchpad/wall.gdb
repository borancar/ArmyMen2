set pagination off
set height 0
set confirm off
break air.cpp:1660 if steps>=0 && rec==(char*)0x4f9128
commands
  silent
  printf "PORT rec23 SEED v=0x%x b=0x%x d=%d dir=%d gen=%d\n", *(unsigned int*)((char*)v+4), *(unsigned int*)((char*)b+4), (int)d, dir, *(int*)0x004F93C0
  continue
end
break air.cpp:1674 if rec==(char*)0x4f9128
commands
  silent
  printf "PORT rec23 MIN? v=0x%x b=0x%x d=%d HIGHnow=%d gen=%d\n", *(unsigned int*)((char*)v+4), *(unsigned int*)((char*)b+4), (int)d, *(short*)0x4f912c, *(int*)0x004F93C0
  continue
end
break sdl.cpp:1211
commands
  silent
  set $n=$n+1
  if $n>=12
    printf "=== END ===\n"
    detach
    quit
  end
  continue
end
set $n=0
continue
