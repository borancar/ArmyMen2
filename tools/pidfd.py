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

`kill` signals the DESCENDANTS of PID as well, deepest first, each through
a pidfd of its own, and says what it signalled. It has to: `$!` is not
always the program that was started. Two `strace` runs were "killed" by
this tool and waited for, and the wait returned -- because the pid was a
wrapper. The tracers were reparented to init with their games, ran for
three and a half hours, and held two unlinked trace files of 9 GB and
2.8 GB open on a tmpfs /tmp. A pid list is walked from /proc before any
signal is sent, so a child that is spawned during the walk can be missed;
run it twice if that matters.

The signal is `SIGKILL` or `KILL`, either spelling.

`wait` answers 2 for a pid that is not there at all, which for a waiter is
the same as "already gone" and is worth telling apart when it happens at
once.
"""
import os
import select
import signal
import sys


def children_of():
    """Every live pid's parent, read from /proc once."""
    parent = {}
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % name) as f:
                stat = f.read()
        except OSError:
            continue
        # comm is in parentheses and may hold spaces; ppid follows the state.
        tail = stat[stat.rindex(")") + 2:].split()
        parent[int(name)] = int(tail[1])
    return parent


def descendants(pid):
    """pid's descendants, deepest first, so a wrapper dies after its child."""
    parent = children_of()
    out = []
    frontier = [pid]
    while frontier:
        p = frontier.pop()
        kids = [c for c, pp in parent.items() if pp == p]
        out.extend(kids)
        frontier.extend(kids)
    out.reverse()
    return out


def comm(pid):
    try:
        with open("/proc/%d/comm" % pid) as f:
            return f.read().strip()
    except OSError:
        return "?"


def signal_named(name):
    name = name.upper()
    if not name.startswith("SIG"):
        name = "SIG" + name
    return getattr(signal, name)


def send(pid, sig):
    if pid <= 2:
        sys.stderr.write("pidfd: refusing pid %d\n" % pid)
        return False
    try:
        fd = os.pidfd_open(pid)
    except ProcessLookupError:
        return False
    try:
        signal.pidfd_send_signal(fd, sig)
    except ProcessLookupError:
        return False
    finally:
        os.close(fd)
    return True


def main(argv):
    if len(argv) < 3 or argv[1] not in ("wait", "kill"):
        sys.stderr.write(__doc__)
        return 2
    pid = int(argv[2])
    if pid <= 2:
        sys.stderr.write("pidfd: refusing pid %d\n" % pid)
        return 2
    if argv[1] == "kill":
        sig = signal_named(argv[3]) if len(argv) > 3 else signal.SIGTERM
        targets = descendants(pid) + [pid]
        sent = 0
        for p in targets:
            name = comm(p)
            if send(p, sig):
                sent += 1
                sys.stderr.write("pidfd: %s %d (%s)\n" % (sig.name, p, name))
        if not sent:
            sys.stderr.write("pidfd: no process %d\n" % pid)
            return 2
        return 0
    try:
        fd = os.pidfd_open(pid)
    except ProcessLookupError:
        sys.stderr.write("pidfd: no process %d\n" % pid)
        return 2
    timeout = float(argv[3]) if len(argv) > 3 else None
    ready, _, _ = select.select([fd], [], [], timeout)
    return 0 if ready else 3


if __name__ == "__main__":
    sys.exit(main(sys.argv))
