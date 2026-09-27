set pagination off
set height 0
set confirm off
printf "VIEWER roach 0xc84400c: uid=0x%x pos=(%d,%d)\n", *(unsigned int*)0x0c844010, *(short*)0x0c84401e, *(short*)0x0c844020
printf "BLOCKER 0x12021780: uid=0x%x type=%d pos=(%d,%d) HIT_MASK=0x%x\n", *(unsigned int*)0x12021784, *(int*)0x12021780, *(short*)0x12021792, *(short*)0x12021794, *(unsigned int*)0x120217f8
printf "  HIT_RECT l=%d t=%d r=%d bo=%d\n", *(int*)0x120217b0, *(int*)0x120217b4, *(int*)0x120217b8, *(int*)0x120217bc
printf "  BOX L=%d T=%d R=%d BO=%d\n", *(int*)0x120217fc, *(int*)0x12021800, *(int*)0x12021804, *(int*)0x12021808
detach
quit
