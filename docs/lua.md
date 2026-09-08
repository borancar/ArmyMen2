# The Lua console

A dev convenience: a persistent Lua 5.4 interpreter reachable over the control
socket, so a running session can poke memory and call game functions without a
rebuild -- give the leader a weapon, flip a flag, read a struct. It links into
every build that carries the control socket (`am2hook.dll`, `armymen2-dev`,
`armymen2-hybrid-dev`, and the standalone) and nowhere else. Vendored Lua is
under `third_party/lua`; the C side is `src/inject/luaconsole.c`.

## Using it

Send `lua <code>` over the socket, exactly like any other control command:

    tools/drive.sh ctl "lua print(1+2)"
    ./.venv/bin/python tools/am2ctl.py --port 31337 "lua print(peek32(0x00511E4C))"

The reply is whatever the chunk `print`ed (newlines shown as `|`) plus any
error. The state is persistent, so a helper defined in one command is there in
the next:

    lua function u() return peek32(0x00511E4C) end   -- our leader's uid
    lua print(u())

## Bindings

Deliberately tiny -- everything else is written in Lua on top of these:

| binding | what |
|---|---|
| `peek8/16/32(addr)`   | read game memory |
| `poke8/16/32(addr,v)` | write game memory |
| `peekstr(addr)`       | read a NUL-terminated string |
| `call(addr, ...)`     | raw __cdecl call, up to 6 int/string args, returns the 32-bit result |
| `sym("name")`         | resolve a RECONSTRUCTED function's address by symbol (native build); 0 elsewhere |
| `print(...)`          | into the reply, not stdout |

## call() and which address to pass

`peek`/`poke` work on any build, because the game's DATA (globals at
`0x004xxxxx..0x0066xxxx`) is carried at its own address everywhere.

`call` is where the builds differ:

- **hybrid and injected** run the ORIGINAL code at its PE address, so pass the
  raw number: `call(0x00417B80, "phoenix!")`.
- **native** (`armymen2`, `armymen2-dev`) is the reconstruction, which lives at
  `0x00700000+`; the original PE code addresses are `int3` gap and calling one
  crashes. Resolve the reconstructed function by name instead:
  `call(sym("CheatLine"), "phoenix!")`. `sym` needs `-rdynamic` (the dev links
  have it) and finds extern-"C" names; C++-mangled ones it will not.

## Recipes

Give the leader a flamethrower (enable cheats first). Two cheats reach a
flamethrower and they differ in whether the rifle survives:

    -- native dev build
    lua poke32(0x004FCF94, 1); call(sym("CheatLine"), "village people")
    -- hybrid / injected
    lua poke32(0x004FCF94, 1); call(0x00417B80, "village people")

`0x004FCF94` is `ADDR_CHEAT_ENABLED`; `0x00417B80` is `CheatLine`, which reads
the typed phrase and dispatches -- so any cheat phrase works the same way.

  - `"village people"` (case 12) is `CheatGiveItem(kind 3)`: it drops a
    flamethrower pickup at the leader's feet and never touches
    `OBJ_OFF_WEAPON_UID`, so **the rifle stays equipped**. Walk over the pickup
    to carry both.
  - `"phoenix!"` (case 9) is `CheatSwapWeapon`: it *replaces* the equipped
    weapon with a flamethrower and sets the "flame unit" flag, so the rifle is
    gone and Sarge looks different. This is the "Flame On! / Flame Off!" cheat.

## Frame-exact injection (lockstep / side-by-side)

Over the control socket a `lua` command runs on the socket thread and races the
pump, so a give applied that way lands a pump or two apart in two lockstepped
games and they diverge. For a give that must be frame-exact, use the step
socket's `lua` action instead of the control socket. `tools/sidebyside.py`
exposes it as an **inject file**: write one line to `<out>/inject` and the
coordinator sends it to *both* games as an action for the same pump.

    echo 'lua poke32(0x004FCF94,1); local f=sym("CheatLine"); if f==0 then f=0x00417B80 end; call(f,"village people")' > /tmp/am2-sidebyside-*/inject

The same chunk runs in both builds, so it resolves `CheatLine` either way:
`sym("CheatLine")` in the native build, and the raw `0x00417B80` in the hybrid,
where that symbol does not exist (`sym` returns 0 there -- and 0 is truthy in
Lua, so test `== 0` explicitly, not `or`). Each game queues the line in
`am2_step_wait` and runs it in `am2_replay_apply`, on the game thread, at the
pump boundary before that pump's frame is produced -- so both apply it at the
identical point and stay frame-exact. The line is also recorded into the
run's `input.txt`, so a replay reproduces the give. Writing to the inject file
also resumes a held trap.

## Caveats

Over the CONTROL socket, commands run on the socket thread, exactly as `poke`
already does, so they race the game thread. That is fine for interactive
poking; it is NOT frame-exact -- for a lockstep/side-by-side reproduction use
the step socket's inject file (above), which is. `call` into a function that
allocates or mutates lists can race the game's heap on the control socket; the
inject path avoids that too, since it runs on the game thread at a pump
boundary.

Everything here is dev tooling and comes out cleanly before release: it is one
command in `control.c`, one file, and the vendored `third_party/lua`.
