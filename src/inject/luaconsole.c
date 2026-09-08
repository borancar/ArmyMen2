/* See luaconsole.h. A persistent Lua 5.4 state (vendored under third_party/lua)
 * with a handful of memory/call primitives; print() is captured into a buffer
 * the control socket returns. */
#include <stdint.h>
#include <string.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include "luaconsole.h"

#include "../../third_party/lua/lua.h"
#include "../../third_party/lua/lauxlib.h"
#include "../../third_party/lua/lualib.h"

static lua_State *g_L;
static char       g_out[16384];
static size_t     g_outn;

static void out_append(const char *s, size_t n)
{
    if (g_outn + n + 1 > sizeof g_out)
        n = sizeof g_out - 1 - g_outn;
    if ((long)n <= 0)
        return;
    memcpy(g_out + g_outn, s, n);
    g_outn += n;
    g_out[g_outn] = 0;
}

/* ---- memory ------------------------------------------------------------- */

static int l_peek(lua_State *L, int sz)
{
    uintptr_t p = (uintptr_t)(uint32_t)luaL_checkinteger(L, 1);
    lua_Integer v = 0;

    if (sz == 1)      v = *(volatile uint8_t  *)p;
    else if (sz == 2) v = *(volatile uint16_t *)p;
    else              v = *(volatile uint32_t *)p;
    lua_pushinteger(L, v);
    return 1;
}
static int l_peek8 (lua_State *L) { return l_peek(L, 1); }
static int l_peek16(lua_State *L) { return l_peek(L, 2); }
static int l_peek32(lua_State *L) { return l_peek(L, 4); }

static int l_poke(lua_State *L, int sz)
{
    uintptr_t   p = (uintptr_t)(uint32_t)luaL_checkinteger(L, 1);
    lua_Integer v = luaL_checkinteger(L, 2);

    if (sz == 1)      *(volatile uint8_t  *)p = (uint8_t)v;
    else if (sz == 2) *(volatile uint16_t *)p = (uint16_t)v;
    else              *(volatile uint32_t *)p = (uint32_t)v;
    return 0;
}
static int l_poke8 (lua_State *L) { return l_poke(L, 1); }
static int l_poke16(lua_State *L) { return l_poke(L, 2); }
static int l_poke32(lua_State *L) { return l_poke(L, 4); }

/* Read a NUL-terminated string out of the game. */
static int l_peekstr(lua_State *L)
{
    const char *p = (const char *)(uintptr_t)(uint32_t)luaL_checkinteger(L, 1);
    lua_pushstring(L, p ? p : "");
    return 1;
}

/* call(addr, a1..a6) -- a raw cdecl call. Integer args pass by value; string
 * args pass their char* (the Lua string stays pinned across the call). cdecl
 * ignores the extra zero args, so one signature serves every arity. Returns
 * the 32-bit result. This is a foot-gun by design; the whole point is to
 * reach anything without a new binding. */
typedef uintptr_t (*am2_fn6)(uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t, uintptr_t, uintptr_t);
static int l_call(lua_State *L)
{
    uintptr_t a[6] = { 0, 0, 0, 0, 0, 0 };
    int       top  = lua_gettop(L);
    uintptr_t addr = (uintptr_t)(uint32_t)luaL_checkinteger(L, 1);
    int       i;

    for (i = 2; i <= top && i <= 7; i++) {
        if (lua_type(L, i) == LUA_TSTRING)
            a[i - 2] = (uintptr_t)lua_tostring(L, i);
        else
            a[i - 2] = (uintptr_t)(uint32_t)luaL_checkinteger(L, i);
    }
    lua_pushinteger(L,
        (lua_Integer)(uint32_t)((am2_fn6)addr)(a[0], a[1], a[2],
                                               a[3], a[4], a[5]));
    return 1;
}

/* sym("name") -- resolve a RECONSTRUCTED function's address by symbol, for the
 * native build, where the reconstruction lives at 0x00700000+ and the original
 * PE addresses (0x004xxxxx) are int3 gap. In the hybrid and injected builds the
 * original code IS at its PE address, so pass the raw number to call() there
 * instead; sym answers 0. Needs -rdynamic so the executable's symbols reach
 * dlsym; extern-"C" names (CheatLine, ...) resolve, C++-mangled ones do not. */
static int l_sym(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    void       *p    = 0;
#ifndef _WIN32
    p = dlsym(RTLD_DEFAULT, name);
#endif
    lua_pushinteger(L, (lua_Integer)(uint32_t)(uintptr_t)p);
    return 1;
}

/* print() -> the return buffer rather than stdout. */
static int l_print(lua_State *L)
{
    int n = lua_gettop(L), i;

    for (i = 1; i <= n; i++) {
        size_t      len;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1)
            out_append("\t", 1);
        out_append(s, len);
        lua_pop(L, 1);
    }
    out_append("\n", 1);
    return 0;
}

static const luaL_Reg k_funcs[] = {
    { "peek8",   l_peek8   }, { "peek16",  l_peek16  }, { "peek32", l_peek32 },
    { "poke8",   l_poke8   }, { "poke16",  l_poke16  }, { "poke32", l_poke32 },
    { "peekstr", l_peekstr }, { "call",    l_call    }, { "print",  l_print  },
    { "sym",     l_sym     },
    { NULL, NULL }
};

void am2_lua_init(void)
{
    if (g_L)
        return;
    g_L = luaL_newstate();
    if (!g_L)
        return;
    luaL_openlibs(g_L);
    lua_pushglobaltable(g_L);
    luaL_setfuncs(g_L, k_funcs, 0);   /* into _G, so print() is overridden */
    lua_pop(g_L, 1);
}

const char *am2_lua_eval(const char *code)
{
    g_outn = 0;
    g_out[0] = 0;
    if (!g_L)
        am2_lua_init();
    if (!g_L)
        return "error: no lua state\n";
    if (luaL_dostring(g_L, code) != LUA_OK) {
        const char *e = lua_tostring(g_L, -1);
        out_append("error: ", 7);
        if (e)
            out_append(e, strlen(e));
        out_append("\n", 1);
        lua_pop(g_L, 1);
    }
    return g_out;
}
