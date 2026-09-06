#!/usr/bin/env python3
"""Wait on, or signal, one process through a pidfd -- never a pattern.

A shell waiting on `pgrep -f PATTERN` matches its own command line, which
holds the pattern; CLAUDE.md records three waiters spinning forever that
way and a `pkill` that killed the shell issuing it. A pidfd (pidfs, Linux
5.3+) names exactly one process for as long as the descriptor is open:
it becomes readable when that process exits, and a signal sent through it
cannot reach a reused pid.

    tools/pidfd.py wait PID [TIMEOUT]   # exit 0 when it has exited, 3 on timeout
    tools/pidfd.py kill PID [SIG]       # SIGTERM by default; pid 1 and 2 refused

`wait` answers 2 for a pid that is not there at all, which for a waiter is
the same as "already gone" and is worth telling apart when it happens at
once.
"""
import os
import select
import signal
import sys


def main(argv):
    if len(argv) < 3 or argv[1] not in ("wait", "kill"):
        sys.stderr.write(__doc__)
        return 2
    pid = int(argv[2])
    if pid <= 2:
        sys.stderr.write("pidfd: refusing pid %d\n" % pid)
        return 2
    try:
        fd = os.pidfd_open(pid)
    except ProcessLookupError:
        sys.stderr.write("pidfd: no process %d\n" % pid)
        return 2
    if argv[1] == "kill":
        sig = getattr(signal, argv[3].upper() if len(argv) > 3 else "SIGTERM")
        if isinstance(sig, str):
            sig = getattr(signal, "SIG" + sig)
        signal.pidfd_send_signal(fd, sig)
        return 0
    timeout = float(argv[3]) if len(argv) > 3 else None
    ready, _, _ = select.select([fd], [], [], timeout)
    return 0 if ready else 3


if __name__ == "__main__":
    sys.exit(main(sys.argv))
