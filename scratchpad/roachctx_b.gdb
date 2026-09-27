set pagination off
set height 0
set confirm off
break RoachBehaviour
commands
  silent
  if *(unsigned int*)($esp+4) == 0x0c6ca51c
    printf "PORT RoachBehaviour obj=0x%x ctx=0x%x\n", *(unsigned int*)($esp+4), *(unsigned int*)($esp+12)
    set $c = *(unsigned int*)($esp+12)
    printf "CTX: "
    x/16xw $c
    detach
    quit
  end
  continue
end
continue
