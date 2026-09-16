#!/usr/bin/env python3
"""Fold code-read blob strings to inline C literals, by macro redirect.

A handful of strings are still read out of the carried blob by the
reconstruction -- CRT import names and runtime-error banners, the CD-required
message, the multiplayer status/log lines, the team-win event names, the
bad-map/rules panel messages. Each is used ONLY as a string (never as an
address: a loop bound like ADDR_OPTION_TABLE_END, which shares this shape, is
deliberately NOT here). Redirecting the macro to a C literal in the native/SA
build makes those reads hit the binary's own .rodata, and the blob's copies go
dead -- tools/deadstrings.py then drops them (it excludes these VAs from its
"coded" keep via folded_vas()).

Safe because in the native build AM2_IMAGE(a) == a (am2_image_slide is 0), so
`STR(x)`/`AM2_IMAGE(x)` on a literal address yields the literal. The hybrid,
which maps the image elsewhere, does not take these redirects (they are under
AM2_STANDALONE).

Emits build/standalone/foldstr.h, included by src/inject/standalone.h. Runs in
standalone-generate before deadstrings. Asserts every literal round-trips to the
exact image bytes, so a wrong transcription fails the build, not a play test.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import am2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "build", "standalone")

# Vetted string-only macros (tools context-analysis + spot check). NOT any
# macro used as an address (comparisons, loop bounds, pointer arithmetic).
FOLD = [
    "STR_KERNEL32", "STR_USER32", "STR_MESSAGEBOXA",
    "STR_ISPROCESSORFEATUREPRESENT",
    "STR_GETLASTACTIVEPOPUP", "STR_GETACTIVEWINDOW",
    "STR_RTERR_CAPTION", "STR_RTERR_BANNER", "STR_RTERR_NONAME",
    "CRT_EXPONENT_TEXT",
    "STR_REMOTE_ACKING_X", "STR_REMOTE_ACKING_D",
    "STR_CAPS_GUAR_YES", "STR_CAPS_GUAR_NO",
    "STR_LOBBY_AS_HOST", "STR_LOBBY_AS_SLAVE",
    "CD_REQUIRED_TEXT", "CD_REQUIRED_CAPTION",
    "STR_TRUE", "STR_FALSE",
    "STR_BOOL_TRUE_CAP", "STR_BOOL_TRUE_LOW",
    "STR_BOOL_FALSE_CAP", "STR_BOOL_FALSE_LOW",
    "STR_BOOL_UT", "STR_BOOL_LT", "STR_BOOL_1",
    "STR_BOOL_UF", "STR_BOOL_LF", "STR_BOOL_0",
    "STR_RTERR_CRLF2", "STR_RTERR_ELLIPSIS",
    "NAME_GREENWINS", "NAME_TANWINS", "NAME_BLUEWINS", "NAME_GREYWINS",
    "NAME_GREENTEAMWINS", "NAME_TANTEAMWINS", "NAME_BLUETEAMWINS",
    "NAME_GREYTEAMWINS",
    "MSG_BAD_MAP", "MSG_BAD_RULES", "MSG_BAD_INSTALL",
    "MSG_NO_MAP", "MSG_NO_RULES",
]

# The FULL-name macros to fold (any prefix), resolved from orig.h, widget.h and
# widget.cpp -- the menu-bitmap filename macros (AM2_BMP_*), plus a few HUD/status
# format strings. These are read as strings only: AM2_BMP_* pass to MakeButton /
# ButtonConstruct, which take a uint32 and AM2_IMAGE it to const char* (the
# folded (const char*) literal is a 32-bit pointer on i386 and flows through
# unchanged). Vetted string-only -- macros used as an ADDRESS (a bound like
# ADDR_OPTION_TABLE_END) are deliberately NOT here.
FOLD_MACROS = [
    "ADDR_STR_ISPROCESSORFEATUREPRESENT",
    "AM2_BMP_03_100_00", "AM2_BMP_03_100_01", "AM2_BMP_03_100_02",
    "AM2_BMP_03_101_00", "AM2_BMP_03_101_01", "AM2_BMP_03_101_02",
    "AM2_BMP_03_102_00", "AM2_BMP_03_102_01", "AM2_BMP_03_102_02",
    "AM2_BMP_03_103_00", "AM2_BMP_03_103_01", "AM2_BMP_03_103_02",
    "AM2_BMP_03_104_00", "AM2_BMP_03_104_01", "AM2_BMP_03_104_02",
    "AM2_BMP_03_105_00", "AM2_BMP_03_105_01", "AM2_BMP_03_105_02",
    "AM2_BMP_03_106_00", "AM2_BMP_03_106_01", "AM2_BMP_03_106_02",
    "AM2_BMP_03_111_00", "AM2_BMP_03_111_01", "AM2_BMP_03_111_02",
    "AM2_BMP_03_120_00", "AM2_BMP_03_120_01", "AM2_BMP_03_120_02",
    "AM2_BMP_03_121_00", "AM2_BMP_03_121_01", "AM2_BMP_03_121_02",
    "AM2_BMP_03_126_00", "AM2_BMP_03_126_01", "AM2_BMP_03_126_02",
    "AM2_BMP_BACK0", "AM2_BMP_BACK1",
    "AM2_BMP_BACK19_0", "AM2_BMP_BACK19_1", "AM2_BMP_BACK19_2",
    "AM2_BMP_BACK2", "AM2_BMP_CAN0", "AM2_BMP_CAN1", "AM2_BMP_CAN2",
    "AM2_BMP_CANCEL0", "AM2_BMP_CANCEL1", "AM2_BMP_CANCEL2", "AM2_BMP_CHECK0",
    "AM2_BMP_CHECK1", "AM2_BMP_CHECK2", "AM2_BMP_CHECK3", "AM2_BMP_DEFAULT0",
    "AM2_BMP_DEFAULT1", "AM2_BMP_DEFAULT2", "AM2_BMP_DELETE0",
    "AM2_BMP_DELETE1", "AM2_BMP_DELETE12_0", "AM2_BMP_DELETE12_1",
    "AM2_BMP_DELETE12_2", "AM2_BMP_DELETE2", "AM2_BMP_DNARROW1",
    "AM2_BMP_DNARROW2", "AM2_BMP_EGG0", "AM2_BMP_EGG1", "AM2_BMP_EGG2",
    "AM2_BMP_GREEN0", "AM2_BMP_GREEN1", "AM2_BMP_HOST0", "AM2_BMP_HOST1",
    "AM2_BMP_HOST2", "AM2_BMP_HSCROLLBAR", "AM2_BMP_JOIN0", "AM2_BMP_JOIN1",
    "AM2_BMP_JOIN2", "AM2_BMP_LOAD0", "AM2_BMP_LOAD1", "AM2_BMP_LOAD2",
    "AM2_BMP_LTARROW1", "AM2_BMP_LTARROW2", "AM2_BMP_NEW0", "AM2_BMP_NEW1",
    "AM2_BMP_NEW2", "AM2_BMP_OK0", "AM2_BMP_OK1", "AM2_BMP_OK2",
    "AM2_BMP_RECRUIT0", "AM2_BMP_RECRUIT1", "AM2_BMP_RECRUIT2",
    "AM2_BMP_RED0", "AM2_BMP_RED1", "AM2_BMP_RTARROW1", "AM2_BMP_RTARROW2",
    "AM2_BMP_SCROLLBAR0", "AM2_BMP_SCROLLBAR1", "AM2_BMP_SELECT0",
    "AM2_BMP_SELECT1", "AM2_BMP_SELECT2", "AM2_BMP_UPARROW1",
    "AM2_BMP_UPARROW2", "AM2_HUD_STR_AMMO_FMT", "AM2_HUD_STR_SARGE",
    "AM2_STR_CHILD_SUFFIX", "AM2_STR_NOT_RESPONDING",
]

# Files a FOLD_MACROS name may be #defined in (widget.cpp carries the button
# bitmap macros under `#ifndef` guards that a standalone-side #define wins).
_MACRO_FILES = ("src/inject/orig.h", "src/game/win32/widget.h",
                "src/game/win32/widget.cpp")


def _c_literal(s):
    out = '"'
    for ch in s:
        if ch == "\\":
            out += "\\\\"
        elif ch == '"':
            out += '\\"'
        elif ch == "\n":
            out += "\\n"
        elif ch == "\t":
            out += "\\t"
        elif ch == "\r":
            out += "\\r"
        elif 32 <= ord(ch) < 127:
            out += ch
        else:
            out += "\\x%02x" % ord(ch)
    return out + '"'


def _unescape(lit):
    return bytes(lit[1:-1], "latin-1").decode("unicode_escape").encode("latin-1")


def _macro_va(macro):
    """The 0x.. value of a `#define macro` in the _MACRO_FILES, or None."""
    for rel in _MACRO_FILES:
        txt = open(os.path.join(REPO, rel)).read()
        m = re.search(r'#define\s+' + re.escape(macro) + r'\s+0x([0-9A-Fa-f]+)u',
                      txt)
        if m:
            return int(m.group(1), 16)
    return None


def entries():
    """[(full_macro, va, image_string)] for both fold lists. FOLD names are the
    ADDR_-prefixed short form; FOLD_MACROS are full macro names of any prefix."""
    oh = open(os.path.join(REPO, "src", "inject", "orig.h")).read()
    img = am2.Image()
    out = []
    for name in FOLD:
        m = re.search(r'#define\s+ADDR_' + name + r'\s+0x([0-9A-Fa-f]+)u', oh)
        if not m:
            raise SystemExit("foldstrings: no ADDR_%s in orig.h" % name)
        out.append(("ADDR_" + name, int(m.group(1), 16), None))
    for macro in FOLD_MACROS:
        va = _macro_va(macro)
        if va is None:
            raise SystemExit("foldstrings: no #define %s found" % macro)
        out.append((macro, va, None))
    resolved = []
    for macro, va, _ in out:
        s = img.cstring(va)
        if s is None:
            raise SystemExit("foldstrings: 0x%08X (%s) is not a string"
                             % (va, macro))
        resolved.append((macro, va, s))
    return resolved


def folded_vas():
    return {va for _macro, va, _s in entries()}


def main():
    os.makedirs(OUT, exist_ok=True)
    lines = ["/* Generated by tools/foldstrings.py -- do not edit. */",
             "#ifndef AM2_FOLDSTR_H", "#define AM2_FOLDSTR_H", ""]
    ents = entries()
    for macro, va, s in ents:
        lit = _c_literal(s)
        assert _unescape(lit) == s.encode("latin-1"), (macro, lit)
        lines.append("#undef %s" % macro)
        lines.append("#define %s ((uintptr_t)(const char *)%s)" % (macro, lit))
    lines += ["", "#endif", ""]
    open(os.path.join(OUT, "foldstr.h"), "w").write("\n".join(lines))
    print("foldstrings: folded %d strings to literals" % len(ents))


if __name__ == "__main__":
    main()
