set pagination off
set height 0
set confirm off
break *0x00408640
commands
  silent
  if *(unsigned int*)($esp+4) == 0xc6c89ec
    printf "HYB ctx=0x%x\n", *(unsigned int*)($esp+12)
    x/16xw *(unsigned int*)($esp+12)
    detach
    quit
  end
  continue
end
continue
