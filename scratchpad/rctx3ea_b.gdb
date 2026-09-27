set pagination off
set height 0
set confirm off
break RoachBehaviour
commands
  silent
  if *(unsigned int*)($esp+4) == ROACHADDR
    printf "PORT ctx=0x%x\n", *(unsigned int*)($esp+12)
    x/16xw *(unsigned int*)($esp+12)
    detach
    quit
  end
  continue
end
continue
