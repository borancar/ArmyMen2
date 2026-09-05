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
          "crt_get_osfhandle", "crt_dosmaperr")

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
