#!/usr/bin/env python3
"""Client for the game's control socket.

The harness listens on 127.0.0.1:31337 and injects below DirectInput, which is
what makes the game drivable without a display server or a window manager --
X11 synthetic input is unreliable here because Xvfb has no window manager, so
there is no foreground window and DirectInput discards mouse events.

    tools/am2ctl.py key return tap          one command
    tools/am2ctl.py -f script.txt           a script, one command per line
    tools/am2ctl.py -                       read commands from stdin
    tools/am2ctl.py                         interactive

Blank lines and `#` comments are ignored in scripts. A line of the form
`sleep <secs>` pauses locally rather than being sent.
"""

import argparse
import socket
import sys
import time

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 31337


class Control:
    def __init__(self, host=DEFAULT_HOST, port=DEFAULT_PORT, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""

    def send(self, line):
        """Send one command and return its reply line."""
        self.sock.sendall(line.encode("latin-1") + b"\n")
        while b"\n" not in self.buf:
            try:
                chunk = self.sock.recv(4096)
            except TimeoutError:
                # Connecting succeeded but nothing answered. The port staying
                # open proves little: when the game exits, its wineserver keeps
                # the listening socket, so connect() still succeeds against a
                # dead game. The other cause is the listener being busy with an
                # earlier client that never disconnected.
                raise TimeoutError(
                    "connected but got no reply -- either the game has exited "
                    "(wineserver holds the port open after it dies) or the "
                    "listener is stuck on a previous client"
                ) from None
            if not chunk:
                raise ConnectionError("control socket closed")
            self.buf += chunk
        reply, _, self.buf = self.buf.partition(b"\n")
        return reply.decode("latin-1").rstrip("\r")

    def close(self):
        self.sock.close()


def run_script(ctl, lines, echo=True):
    rc = 0
    for raw in lines:
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("sleep "):
            time.sleep(float(line.split(None, 1)[1]))
            if echo:
                print(f"{line}")
            continue
        reply = ctl.send(line)
        if echo:
            print(f"{line:<32} {reply}")
        if reply.startswith("err"):
            rc = 1
    return rc


def main():
    ap = argparse.ArgumentParser(description="control the running game")
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("-f", "--file", help="script file, or - for stdin")
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("command", nargs="*", help="a single command to send")
    args = ap.parse_args()

    try:
        ctl = Control(args.host, args.port)
    except OSError as e:
        sys.exit(f"cannot reach control socket at {args.host}:{args.port} -- "
                 f"is the game running with CONTROL=1? ({e})")

    try:
        if args.command:
            print(ctl.send(" ".join(args.command)))
            return 0
        if args.file:
            src = sys.stdin if args.file == "-" else open(args.file)
            return run_script(ctl, src, echo=not args.quiet)
        # Interactive.
        print(f"connected to {args.host}:{args.port}; blank line or Ctrl-D quits")
        for line in iter(lambda: input("am2> "), ""):
            print(ctl.send(line))
        return 0
    except (EOFError, KeyboardInterrupt):
        return 0
    finally:
        ctl.close()


if __name__ == "__main__":
    sys.exit(main())
