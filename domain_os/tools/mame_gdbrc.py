#!/usr/bin/env python3
"""mame_gdbrc.py - drive a headless MAME (phoebus) through its gdbstub.

MAME's `-debugger gdbstub` answers the GDB remote protocol; its `qRcmd`
packet runs any MAME debugger command and returns the console text.  That
is the only headless way into the debugger (`-debugger none` resumes at
every stop and never reads a -debugscript), so this client launches MAME,
attaches, and executes a script of lines:

    mon <debugger command>     run a MAME debugger command, print its output
    cont [<seconds>]           continue until a breakpoint stops the CPU or
                               <seconds> wall time pass (then interrupt)
    raw <packet payload>       send one gdb packet verbatim, print the reply
    sleep <seconds>            wall-clock pause (MAME keeps running only if
                               a previous `mon go`/`gtime` resumed it)
    quit                       `mon quit` (MAME exits and flushes error.log)

Example:
    tools/mame_gdbrc.py --mame-dir ~/src/domainos-archeology/mame \
        --mame-args 'dn300 -disk1 dn300_sr10.3_ours.awd' --script boot.gdbrc
"""
import argparse
import os
import socket
import subprocess
import sys
import time


class GdbStub:
    def __init__(self, host, port, connect_timeout):
        deadline = time.time() + connect_timeout
        last = None
        while True:
            try:
                self.sock = socket.create_connection((host, port), timeout=2)
                break
            except OSError as e:
                last = e
                if time.time() > deadline:
                    raise RuntimeError("cannot connect to gdbstub %s:%d: %s" % (host, port, last))
                time.sleep(0.2)
        self.sock.settimeout(None)
        self.buf = b""

    @staticmethod
    def checksum(payload):
        return sum(payload) & 0xFF

    def send(self, payload):
        pkt = b"$" + payload + b"#" + b"%02x" % self.checksum(payload)
        self.sock.sendall(pkt)

    def _read_more(self, timeout):
        self.sock.settimeout(timeout)
        try:
            data = self.sock.recv(65536)
        except socket.timeout:
            return False
        finally:
            self.sock.settimeout(None)
        if not data:
            raise RuntimeError("gdbstub closed the connection")
        self.buf += data
        return True

    def recv_packet(self, timeout):
        """Return the next packet payload (bytes) or None on timeout."""
        deadline = time.time() + timeout
        while True:
            # drop acks/nacks before a packet
            while self.buf[:1] in (b"+", b"-") and self.buf:
                self.buf = self.buf[1:]
            start = self.buf.find(b"$")
            if start >= 0:
                end = self.buf.find(b"#", start)
                if end >= 0 and len(self.buf) >= end + 3:
                    payload = self.buf[start + 1:end]
                    self.buf = self.buf[end + 3:]
                    self.sock.sendall(b"+")
                    return payload
            remaining = deadline - time.time()
            if remaining <= 0:
                return None
            self._read_more(min(remaining, 1.0))

    def rcmd(self, command, timeout=120):
        self.send(b"qRcmd," + command.encode().hex().encode())
        reply = self.recv_packet(timeout)
        if reply is None:
            return None
        if reply == b"OK":
            return ""
        if reply[:1] == b"E":
            return "<error %s>" % reply.decode()
        return bytes.fromhex(reply.decode()).decode("latin-1")

    def cont(self, timeout):
        """Continue; return the stop packet, or None after interrupting on timeout."""
        self.send(b"c")
        reply = self.recv_packet(timeout)
        if reply is None:
            self.sock.sendall(b"\x03")
            reply = self.recv_packet(10)
        return reply.decode() if reply is not None else None


def run_script(stub, lines, out):
    for raw in lines:
        line = raw.split("//")[0].strip()
        if not line:
            continue
        word, _, rest = line.partition(" ")
        rest = rest.strip()
        if word == "mon":
            out.write("> %s\n" % rest)
            reply = stub.rcmd(rest)
            if reply is None:
                out.write("<no reply within timeout>\n")
            elif reply:
                out.write(reply if reply.endswith("\n") else reply + "\n")
        elif word == "cont":
            secs = float(rest) if rest else 60.0
            out.write("> cont (%.0fs)\n" % secs)
            t0 = time.time()
            stop = stub.cont(secs)
            out.write("stop: %s after %.1fs\n" % (stop, time.time() - t0))
        elif word == "raw":
            out.write("> raw %s\n" % rest)
            stub.send(rest.encode())
            reply = stub.recv_packet(60)
            out.write("reply: %s\n" % (None if reply is None else reply[:80].decode("latin-1") + ("..." if len(reply) > 80 else "")))
        elif word == "sleep":
            time.sleep(float(rest))
        elif word == "quit":
            out.write("> quit\n")
            stub.send(b"qRcmd," + b"quit".hex().encode())
            return
        else:
            raise SystemExit("unknown script line: %s" % raw)
        out.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mame-dir", default=os.path.expanduser("~/src/domainos-archeology/mame"))
    ap.add_argument("--mame-bin", default="./phoebus")
    ap.add_argument("--mame-args", required=True, help="machine and media, e.g. 'dn300 -disk1 x.awd'")
    ap.add_argument("--port", type=int, default=2159)
    ap.add_argument("--script", required=True, help="file of mon/cont/sleep/quit lines")
    ap.add_argument("--mame-log", default=None, help="MAME stdout/stderr file (default: <script>.mame.out)")
    ap.add_argument("--no-launch", action="store_true", help="attach to an already running MAME")
    args = ap.parse_args()

    proc = None
    if not args.no_launch:
        log_path = args.mame_log or args.script + ".mame.out"
        log = open(log_path, "w")
        cmd = [args.mame_bin] + args.mame_args.split() + [
            "-video", "none", "-sound", "none", "-log", "-skip_gameinfo", "-nothrottle",
            "-debug", "-debugger", "gdbstub", "-debugger_port", str(args.port),
        ]
        env = dict(os.environ, DYLD_FRAMEWORK_PATH="/Library/Frameworks", SDL_VIDEODRIVER="dummy")  # no window on macOS
        proc = subprocess.Popen(cmd, cwd=args.mame_dir, stdout=log, stderr=subprocess.STDOUT, env=env)
    try:
        stub = GdbStub("127.0.0.1", args.port, 60)
        with open(args.script) as f:
            lines = f.readlines()
        run_script(stub, lines, sys.stdout)
    finally:
        if proc is not None:
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            sys.stdout.write("mame exit code %s\n" % proc.returncode)


if __name__ == "__main__":
    main()
