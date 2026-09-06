#!/usr/bin/env python3
"""Check the reconstructed CRT's stdio against the original's, in ONE process
and with no Wine: build/armymen2-hybrid-dev runs the original CRT over
src/platform, and with AM2_CRTCHECK set it links src/platform/crt beside it
and runs src/hybrid/crtcheck.cpp instead of the game.

That module scripts the same sequence of fopen/fread/fgets/fseek/ftell/
fwrite/fflush/fclose calls through both stacks, on separate copies of the
same generated file, and compares each call's return value, errno and
_doserrno, the FILE's flag, cnt, bufsiz and ptr-base, a hash of the bytes
handed back, and the file left behind. Both stacks reach the same
CreateFileA and ReadFile, so nothing but the CRT is being compared.

    tools/crtcheck.py            # builds hybrid-dev if needed, runs it

A second section covers the directory and time calls -- the find family
over a small tree and ten patterns, chdir and getcwd, mkdir, rmdir, remove
and chmod, getenv by name in both cases, and time -- and the timezone
globals each stack leaves behind. __tzset runs once and time() caches its
minute, so the check resets that shared state before each stack. It runs
three times: TZ unset, TZ=PST8PDT and TZ=JST-9, which are __tzset's two
arms and, inside the parser, a sign and a daylight name each way.

The corpus is in crtcheck.cpp's header, and so is what it does not reach:
devices and pipes, which no file in a directory can be. CHECKS names the
functions, since none has a counter: nothing patches the CRT.

MUTATION-CHECKED in nine directions. Never setting FCRLF fails 35 calls;
forgetting ftell's Ctrl-Z byte 6; not shrinking the buffer on fseek 12,528;
not trimming the Ctrl-Z on an update open 163 calls and 15 files; never
setting IOCTRLZ 1,981; opening 'a' without O_APPEND 1,438 calls and 75
files; not setting EOF on a direct read 1,385. Not expanding LF on write is
caught too, and then takes the process down: a _write that reports fewer
bytes than asked makes fseek's flush fail, a failed flush on an update
stream leaves IOWRT set, the fgets after it leaves cnt at -1 through
_filbuf's error arm, and the next fwrite copies past the buffer -- which is
what the ORIGINAL does on a failed write, reproduced.

Over the directory and time section, eleven of fourteen mutations fail:
keeping NORMAL as an attribute 16 values, mapping no-more-files to EINVAL
8, inverting chmod's write bit 2, not reporting getcwd's ERANGE 2, a
day-late epoch and a leap table used always 85 each, the sign of a
negative TZ offset 86 (in the JST-9 run only), the API bias not applied
86 (in the unset run only), cvtdate's last-week clamp and its end-of-DST
bias 1 each (in the PST8PDT run only, where the US rules are computed),
and a case-sensitive getenv 2 to 3. The three that pass are theorems on
this platform, not gaps: _isindst's own _daylight guard is shadowed by the
same test in __loctotime_t; time()'s daylight flag needs a
GetTimeZoneInformation that reports daylight dates and src/platform's
never does; and _chdir's UNC arm needs a current directory beginning with
a doubled separator, which getcwd never yields. _mbctoupper is not
discriminated either, by construction: the drive letter it upper-cases is
'/' on this platform.

A third section is strtod: 116 fixed strings -- every place the text can
end, the exponent letters, the extremes of the range and the cap, the
largest and smallest doubles, ties at bit 53 -- and 1,500 random ones in
the parser's grammar, compared on the double's bits, the end offset and
errno. Seven of nine mutations fail it: rounding on the bit after the
round bit 1 value, round-half-up 6 (the original truncates a tie),
dropped integer digits not scaling the exponent 130, the exponent cap 1,
an E with nothing after it keeping the E 46, underflow without ERANGE 37,
a normalisation off by one word 803. The two that pass: the denormal
shift is a theorem, since __ld12cvt answers 2 for a denormal and strtod
then returns zero with ERANGE, so no denormal mantissa is observable
through it; the 25th digit's rounding of the 24th moves the value by one
part in 10^24, and even inputs built to straddle a midpoint do not carry
that far through the power-of-ten multiply -- a gap, verified by reading.

A fourth section is memcpy, which the vector set also covers but cannot
overlap: 8,424 copies over one pattern, destinations and sources at every
offset that selects an unrolled arm, both ways round and on top of each
other, compared on the whole buffer. The original copies from the end
when the destination lies inside the source's span, so it is memmove
under the other name; copying forward there fails 2,008 cases.

A fifth section is the heap, and its oracle is exact where the FILE
tables' could not be: one small-block heap is shared by both stacks, so
the check snapshots all of it -- every committed group's 32 KB, every
region's bitmaps and lists, the header list and the five globals -- runs
one malloc, free or realloc through each stack from that state, and
compares the two resulting states' digests and answers. 6,000 random
operations over sizes on both sides of the 1016-byte threshold, then a
burst of 3,600 blocks that fills three regions, twenty freed from the
first while the scan pointer sits on the last and twenty asked for, and
everything freed in a permuted order so that groups empty and the
deferred decommit fires: 24,279 operations, 0 differ, 3 skipped because
a region's release unmaps what a snapshot cannot put back. A block above
the threshold is HeapAlloc's, whose address nothing controls; there the
digest and null-ness are what is compared, and a block that was
reallocated above and back down is HeapAlloc's still, as the original
keeps it. Thirteen mutations, all failing: scanning from the list start
40, ignoring the last-used group 3,930, leaving a remainder on its old
list 1,294, not absorbing the next block 375, not the previous 411,
never deferring the decommit 63, refusing to grow in place 110, keeping
the whole block on a shrink 670, pages of 0xFE0 instead of 0xFF0 14,
realloc copying the new size 259, _msize off by eight 3,173, the onexit
table run forwards 1, and the table never grown 14.

A sixth is atexit and the exit path: three handlers registered through
each stack into the one table, forty more through the reconstruction so
it grows past its 32 entries, and each stack's doexit with retcaller set,
which runs the table from the end and the two terminator tables and comes
back -- the same 46 in the same reverse order both times. Last, because
the pre-terminator closes every stream.

What the corpus cannot reach: the _read arm for a read whose entire
content is one CR with an LF behind it, which through the FILE layer needs
a read of exactly one byte and the smallest is the two-byte fallback
buffer; and the mode letters S, R, T and D, which change only the
attributes handed to CreateFileA, which the platform ignores. Both stay
verified by reading.
"""
import os
import subprocess
import sys

CHECKS = ("crt_fopen", "crt_fsopen", "crt_openfile", "crt_getstream",
          "crt_fclose", "crt_fread", "crt_fwrite", "crt_fgets", "crt_fseek",
          "crt_ftell", "crt_fflush", "crt_flush", "crt_flushall",
          "crt_flsbuf", "crt_filbuf", "crt_getbuf", "crt_freebuf",
          "crt_sopen", "crt_read", "crt_write", "crt_lseek", "crt_close",
          "crt_commit", "crt_chsize", "crt_setmode", "crt_isatty",
          "crt_alloc_osfhnd", "crt_set_osfhnd", "crt_free_osfhnd",
          "crt_get_osfhandle", "crt_dosmaperr",
          "crt_findfirst", "crt_findnext", "crt_findclose", "crt_chdir",
          "crt_getcwd", "crt_getdcwd", "crt_validdrive", "crt_mkdir",
          "crt_rmdir", "crt_remove", "crt_chmod", "crt_time",
          "crt_loctotime_t", "crt_timet_from_ft", "crt_tzset",
          "crt_tzset_body", "crt_isindst", "crt_cvtdate", "crt_getenv",
          "crt_mbsnbicoll",
          "crt_strtod", "crt_fltin2", "crt_strgtold12", "crt_mtold12",
          "crt_ld12tod", "crt_ld12cvt", "crt_isctype", "crt_memcpy",
          "crt_malloc", "crt_operator_new", "crt_free", "crt_operator_delete",
          "crt_realloc", "crt_calloc", "crt_msize", "crt_nh_malloc",
          "crt_heap_alloc", "crt_callnewh", "crt_heap_init", "crt_sbh_heap_init",
          "crt_sbh_find_block", "crt_sbh_alloc_block", "crt_sbh_free_block",
          "crt_sbh_resize_block", "crt_sbh_alloc_new_region",
          "crt_sbh_alloc_new_group", "crt_atexit", "crt_onexit",
          "crt_onexitinit", "crt_exit", "crt_exit_quick", "crt_doexit",
          "crt_initterm", "crt_endstdio", "crt_fcloseall", "crt_seh_set",
          "crt_seh_restore")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    os.chdir(ROOT)
    build = subprocess.run(["make", "-s", "hybrid-dev"], capture_output=True, text=True)
    if build.returncode != 0:
        sys.stderr.write(build.stderr[-2000:])
        print("crtcheck: make hybrid-dev failed")
        return 2
    run = subprocess.run(["tools/crtcheck.sh"], capture_output=True, text=True)
    lines = [l for l in run.stderr.splitlines() if l.startswith("crtcheck:")]
    for l in lines[:45]:
        print(l)
    if run.returncode != 0 and not lines:
        print(f"crtcheck: exited {run.returncode} with nothing to say")
    return 0 if run.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
