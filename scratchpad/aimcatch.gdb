set pagination off
set height 0
set confirm off
break _Z8AimStartjaj
commands
  silent
  printf "=== port AimStart: at=0x%x  x=%d y=%d ===\n", (unsigned)*(unsigned*)($esp+12), (short)(*(unsigned*)($esp+12)&0xffff), (short)((*(unsigned*)($esp+12)>>16)&0xffff)
  bt 8
  printf "=== END ===\n"
  detach
  quit
end
continue
