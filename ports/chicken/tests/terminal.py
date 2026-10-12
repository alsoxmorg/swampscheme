"""Exercise the real terminal backend without requiring manual input."""
import fcntl
import os
import pty
import select
import struct
import subprocess
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def run(script, marker, trigger):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    before = termios.tcgetattr(slave)
    def session():
        os.setsid()
        fcntl.ioctl(0, termios.TIOCSCTTY, 0)
    proc = subprocess.Popen([os.environ.get("CSI", "chicken-csi"), "-I", ".", "-s", script],
                            cwd=ROOT, stdin=slave, stdout=slave, stderr=slave,
                            env={**os.environ, "TERM": "xterm-256color", "LC_ALL": "C.UTF-8"},
                            preexec_fn=session)
    output = b""
    sent = False
    deadline = time.monotonic() + 15
    try:
        while time.monotonic() < deadline:
            if select.select([master], [], [], .1)[0]:
                output += os.read(master, 65536)
            if not sent and trigger in output:
                os.write(master, b"q")
                sent = True
            if proc.poll() is not None:
                while select.select([master], [], [], 0)[0]:
                    output += os.read(master, 65536)
                break
        else:
            raise AssertionError("Terminal test timed out: " + repr(output))
        assert proc.wait() == 0, output
        assert marker in output, output
        assert termios.tcgetattr(slave) == before, "Terminal settings were not restored"
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        os.close(master)
        os.close(slave)

run("tests/terminal.scm", b"TERMINAL-PASS", b"READY")
run("run-tui.scm", b"swampscheme termbox TUI", b"waiting for input")
print("Terminal tests and existing TUI demo passed")
