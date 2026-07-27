#!/usr/bin/env python3
"""Does the STOCK NetHack 5.0.0 tty build save with 'S' + 'y'?

This isolates the core from the Vulture windowport: if tty saves cleanly with
the same key sequence, then a Vulture GUI save failure is a port defect.
Usage: tty_save_test.py <nethack_binary> <hackdir>
"""
import os, sys, pty, select, time, re

NETHACK, HACKDIR = sys.argv[1], sys.argv[2]

env = dict(os.environ)
env.update(HACKDIR=HACKDIR, TERM="xterm", LINES="40", COLUMNS="120")
env.pop("NETHACKOPTIONS", None)
env["NETHACKOPTIONS"] = "name:ttytest,!tutorial,!legacy,!autopickup"

ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][AB0]|\x1b[=>]|\r")
clean = lambda b: ANSI.sub(b"", b)

pid, fd = pty.fork()
if pid == 0:
    os.chdir(HACKDIR)
    os.execve(NETHACK, [NETHACK, "-u", "ttytest", "-D"], env)

buf = bytearray()
def drain(quiet=0.5, hard=5.0):
    start = last = time.time()
    while True:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                d = os.read(fd, 65536)
            except OSError:
                return
            if not d:
                return
            buf.extend(d); last = time.time()
        if time.time() - last >= quiet or time.time() - start >= hard:
            return
def send(s):
    try:
        os.write(fd, s.encode())
    except OSError:
        pass
    time.sleep(0.25)
tail = lambda n=700: clean(bytes(buf))[-n:]
has = lambda p: re.search(p, clean(bytes(buf))) is not None

# ---- get into the game --------------------------------------------------
drain(1.0, 8)
for _ in range(28):
    s = tail(500)
    if b"--More--" in s or b"(end)" in s:      send(" ")
    elif b"tutorial" in s:                      send("n")
    elif b"pick character" in s or b"[ynaq]" in s: send("y")
    elif b"Is this ok?" in s:                   send("y")
    elif has(rb"Dlvl:"):                        break
    else:                                       send(" ")
    drain(0.4, 3)

in_game = has(rb"Dlvl:")
print("[tty] reached the dungeon:", in_game)
if not in_game:
    print("[tty] FINAL SCREEN:", tail(900).decode("latin-1"))
    os.close(fd); sys.exit(2)

# ---- the actual experiment: save with S then y --------------------------
before = set(os.listdir(os.path.join(HACKDIR, "save"))) if os.path.isdir(os.path.join(HACKDIR, "save")) else set()
send("S")
drain(0.6, 4)
print("[tty] after 'S', screen tail:", tail(200).decode("latin-1").replace("\n", " ")[-160:])
send("y")
drain(1.0, 6)
time.sleep(2)

after = set(os.listdir(os.path.join(HACKDIR, "save"))) if os.path.isdir(os.path.join(HACKDIR, "save")) else set()
newsaves = after - before
print("[tty] new save files:", sorted(newsaves) if newsaves else "NONE")
print("[tty] RESULT:", "SAVE WORKS" if newsaves else "SAVE FAILED")

try:
    os.close(fd)
except OSError:
    pass
try:
    os.waitpid(pid, os.WNOHANG)
except OSError:
    pass
sys.exit(0 if newsaves else 1)
