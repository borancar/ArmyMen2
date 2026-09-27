set pagination off
set height 0
set width 0
set confirm off
# record 23 HIGH is at 0x4F912C; watch it, act only at gen 565
watch *(short*)0x0004F912C
commands
  silent
  if *(int*)0x004F93C0 == 565
    printf "=== HYB gen565 write, new HIGH[23]=%d ===\n", *(short*)0x0004F912C
    printf "PC=%p\n", $pc
    x/12i $pc-40
    printf "--- regs ---\n"
    info registers eax ebx ecx edx esi edi ebp esp
    printf "--- stack [esp..esp+0x60] ---\n"
    x/24xw $esp
    printf "--- try frame: esp+8 arg words ---\n"
    detach
    quit
  end
  continue
end
continue
