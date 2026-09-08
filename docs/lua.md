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

Give the leader the flamethrower (the `phoenix!` cheat -- enable cheats first):

    -- native dev build
    lua poke32(0x004FCF94, 1); call(sym("CheatLine"), "phoenix!")
    -- hybrid / injected
    lua poke32(0x004FCF94, 1); call(0x00417B80, "phoenix!")

`0x004FCF94` is `ADDR_CHEAT_ENABLED`; `0x00417B80` is `CheatLine`, which reads
the typed phrase and dispatches -- so any cheat phrase works the same way.

## Caveats

Commands run on the SOCKET thread, exactly as `poke` already does, so they race
the game thread. That is fine for interactive poking; it is NOT frame-exact, so
do not use it to set up a lockstep/side-by-side reproduction -- two sockets are
not pump-synchronised. `call` into a function that allocates or mutates lists
can race the game's heap; for a one-shot give at a quiet moment it is fine.

Everything here is dev tooling and comes out cleanly before release: it is one
command in `control.c`, one file, and the vendored `third_party/lua`.
