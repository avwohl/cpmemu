#!/usr/bin/env python3
"""Tests for cpmemu's GDB stub (--gdb) and --backtrace.

A small GDB remote serial protocol client, so the test needs no GDB.  It
assembles the guests gdb_*.asm with um80 and ul80, starts cpmemu with
--gdb=0 --gdb-port-file=..., and checks the packets the stub answers.

    python3 tests/gdb_stub_test.py [path/to/cpmemu]

Prints "PASS  name" or "FAIL  name" for each check, which tests/run_tests.sh
counts, and exits 1 if any check failed.  Prints "SKIP  ..." and exits 0
when um80 or ul80 is not on PATH, or exits 1 for that skip when the
environment has CPMEMU_REQUIRE_ALL=1 (the Windows CI job sets it).  The design
is in docs/debugger_plan.md and the user's view is in docs/gdb_stub.md.
"""

import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print("PASS  gdb: %s" % name)
    else:
        failed += 1
        print("FAIL  gdb: %s" % name)
        if detail:
            for line in str(detail).splitlines():
                print("        %s" % line)


# ---------------------------------------------------------------------------
# The protocol
# ---------------------------------------------------------------------------

def checksum(data):
    return sum(data) & 0xFF


def escape(data):
    out = bytearray()
    for b in data:
        if b in (0x23, 0x24, 0x7D, 0x2A):
            out.append(0x7D)
            out.append(b ^ 0x20)
        else:
            out.append(b)
    return bytes(out)


class Rsp:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=20)
        self.buf = b""

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def _fill(self, timeout):
        self.sock.settimeout(timeout)
        data = self.sock.recv(65536)
        if not data:
            raise EOFError("the stub closed the connection")
        self.buf += data

    def send_raw(self, packet_payload):
        if isinstance(packet_payload, str):
            packet_payload = packet_payload.encode("latin-1")
        self.sock.sendall(b"$" + packet_payload + b"#" + ("%02x" % checksum(packet_payload)).encode())

    def recv(self, timeout=20):
        """One packet payload, as a str.  Acks it."""
        while True:
            # drop acks
            while self.buf[:1] in (b"+", b"-"):
                self.buf = self.buf[1:]
            start = self.buf.find(b"$")
            if start >= 0:
                end = self.buf.find(b"#", start)
                if end >= 0 and len(self.buf) >= end + 3:
                    body = self.buf[start + 1:end]
                    want = int(self.buf[end + 1:end + 3], 16)
                    self.buf = self.buf[end + 3:]
                    if checksum(body) != want:
                        raise ValueError("bad checksum from the stub on %r" % body)
                    self.sock.sendall(b"+")
                    return body.decode("latin-1")
            self._fill(timeout)

    def cmd(self, payload, timeout=20):
        self.send_raw(payload)
        return self.recv(timeout)

    def interrupt(self):
        self.sock.sendall(b"\x03")

    def handshake(self):
        reply = self.cmd("qSupported:multiprocess+;swbreak+;hwbreak+;vContSupported+;xmlRegisters=z80")
        return reply

    # conveniences -----------------------------------------------------
    def reg(self, n):
        h = self.cmd("p%x" % n)
        return int.from_bytes(bytes.fromhex(h), "little")

    def pc(self):
        return self.reg(5)

    def mem(self, addr, length):
        out = b""
        while length > 0:
            n = min(length, 0x1000)
            h = self.cmd("m%x,%x" % (addr, n))
            if h.startswith("E"):
                raise ValueError("m%x,%x gave %s" % (addr, n, h))
            out += bytes.fromhex(h)
            addr += n
            length -= n
        return out

    def monitor(self, text):
        h = self.cmd("qRcmd," + text.encode().hex())
        return bytes.fromhex(h).decode("latin-1")


def frames(bt_text):
    """The addresses in the #N lines of a backtrace, in order."""
    return [int(m.group(1), 16) for m in re.finditer(r"^#\d+\s+0x([0-9A-Fa-f]+)", bt_text, re.M)]


# ---------------------------------------------------------------------------
# Building and running guests
# ---------------------------------------------------------------------------

class Guest:
    def __init__(self, com, syms):
        self.com = com
        self.syms = syms
        with open(com, "rb") as f:
            self.image = f.read()

    def __getattr__(self, name):
        try:
            return self.__dict__["syms"][name.upper()]
        except KeyError:
            raise AttributeError(name)


def build(workdir, name):
    src = os.path.join(HERE, name + ".asm")
    rel = os.path.join(workdir, name + ".rel")
    com = os.path.join(workdir, name + ".com")
    sym = os.path.join(workdir, name + ".sym")
    subprocess.check_call(["um80", "-o", rel, src], stdout=subprocess.DEVNULL)
    subprocess.check_call(["ul80", "-S", sym, "-o", com, rel], stdout=subprocess.DEVNULL)
    syms = {}
    with open(sym) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                syms[parts[1].upper()] = int(parts[0], 16)
    return Guest(com, syms)


class Run:
    """cpmemu started with --gdb=0, and a connected client."""

    def __init__(self, emu, workdir, guest, stdin=subprocess.DEVNULL, extra=(), env=None, pre_connect=None):
        self.workdir = workdir
        portfile = os.path.join(workdir, "port.%d" % os.getpid())
        if os.path.exists(portfile):
            os.remove(portfile)
        self.proc = subprocess.Popen(
            [emu, "--gdb=0", "--gdb-port-file=" + portfile] + list(extra) + [guest.com],
            stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=workdir,
            env=env)
        port = None
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.proc.poll() is not None:
                break
            try:
                with open(portfile) as f:
                    text = f.read()
                if text.endswith("\n"):
                    port = int(text)
                    break
            except (OSError, ValueError):
                pass
            time.sleep(0.02)
        if port is None:
            self.kill()
            raise RuntimeError("cpmemu gave no port: %s" % self.proc.stderr.read().decode(errors="replace"))
        if pre_connect:
            pre_connect(self)
        self.rsp = Rsp(port)
        self.guest = guest

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
        try:
            self.proc.communicate(timeout=5)
        except Exception:
            pass

    def finish(self, timeout=20):
        """Close the client, wait for cpmemu and return (status, stdout, stderr)."""
        self.rsp.close()
        out, err = self.proc.communicate(timeout=timeout)
        return self.proc.returncode, out, err.decode(errors="replace")


def scenario(fn):
    def wrapper(*args):
        try:
            fn(*args)
        except Exception:
            check(fn.__name__, False, traceback.format_exc())
    return wrapper


# ---------------------------------------------------------------------------
# The cases
# ---------------------------------------------------------------------------

@scenario
def case_connection(emu, wd, g):
    r = Run(emu, wd, g["gdb_main"])
    try:
        sup = r.rsp.handshake()
        check("qSupported offers PacketSize, qXfer, swbreak and vContSupported",
              all(w in sup for w in ("PacketSize=", "qXfer:features:read+", "swbreak+", "vContSupported+")), sup)
        check("vMustReplyEmpty is answered with an empty packet", r.rsp.cmd("vMustReplyEmpty") == "")
        check("an unknown packet is answered with an empty packet", r.rsp.cmd("qNoSuchPacket") == "")
        check("qAttached says 1", r.rsp.cmd("qAttached") == "1")
        check("qC, qfThreadInfo and qsThreadInfo name one thread",
              (r.rsp.cmd("qC"), r.rsp.cmd("qfThreadInfo"), r.rsp.cmd("qsThreadInfo")) == ("QC1", "m1", "l"))
        check("Hg and Hc are accepted", (r.rsp.cmd("Hg0"), r.rsp.cmd("Hc-1")) == ("OK", "OK"))
        check("vCont? lists c, C, s, S and r (GDB needs c and C)", r.rsp.cmd("vCont?") == "vCont;c;C;s;S;r")
        check("the program stops before 0100H with T05", r.rsp.cmd("?") == "T05" and r.rsp.pc() == 0x100)

        xml = ""
        while True:
            part = r.rsp.cmd("qXfer:features:read:target.xml:%x,200" % len(xml))
            xml += part[1:]
            if part[0] == "l":
                break
        regs = re.findall(r'<reg name="([^"]+)" bitsize="(\d+)"', xml)
        want = sum(int(b) // 8 * 2 for _, b in regs)
        check("target.xml names GDB's 13 z80 registers with a 16-bit pc",
              [n for n, _ in regs] == ["af", "bc", "de", "hl", "sp", "pc", "ix", "iy", "af'", "bc'", "de'", "hl'", "ir"]
              and dict(regs)["pc"] == "16" and "<architecture>z80</architecture>" in xml, xml[:300])
        gl = r.rsp.cmd("g")
        check("the g reply has the length target.xml gives", len(gl) == want == 52, "got %d, xml says %d" % (len(gl), want))
        check("pc in the g reply is 16 bits and is 0100H", gl[20:24] == "0001", gl)
        r.rsp.cmd("D")
    finally:
        r.finish()


@scenario
def case_main(emu, wd, g):
    gm = g["gdb_main"]
    r = Run(emu, wd, gm)
    try:
        c = r.rsp
        c.handshake()
        check("a breakpoint is accepted", c.cmd("Z0,%x,1" % gm.sub1) == "OK")
        check("continue stops at the breakpoint with T05swbreak", c.cmd("c") == "T05swbreak:;")
        check("pc is at the breakpoint", c.pc() == gm.sub1)
        check("registers hold what the program loaded (HL 1234, BC 5678)",
              c.reg(3) == 0x1234 and c.reg(1) == 0x5678)
        check("the program bytes are as the program file has them, with a breakpoint set",
              c.mem(0x100, len(gm.image)) == gm.image)
        check("a step runs one instruction (INC HL)", c.cmd("s") == "T05" and c.pc() == gm.sub1 + 1 and c.reg(3) == 0x1235)
        check("a step at RET returns to the caller", c.cmd("vCont;s") == "T05" and c.pc() == gm.after_sub)
        check("a breakpoint is removed", c.cmd("z0,%x,1" % gm.sub1) == "OK")
        check("range stepping runs to the end of the range",
              c.cmd("vCont;r%x,%x" % (gm.after_sub, gm.call5)) == "T05" and c.pc() == gm.call5)

        check("a step at CALL 5 goes to 0005H", c.cmd("s") == "T05" and c.pc() == 5)
        check("a step at 0005H goes to the BDOS trap", c.cmd("s") == "T05" and c.pc() == 0xFD00)
        bt = c.monitor("bt")
        check("monitor bt at the BDOS trap shows the call from CALL 5",
              frames(bt) == [0xFD00, gm.after5], bt)
        check("a step at the BDOS trap runs the trap and stops at the return address",
              c.cmd("s") == "T05" and c.pc() == gm.after5)
        bt = c.monitor("bt")
        check("the BDOS trap return pops its shadow entry: monitor bt shows only the pc (D7 rule 3)",
              frames(bt) == [gm.after5] and "out of step" not in bt, bt)

        check("memory is written with M and read back", c.cmd("M8000,2:abcd") == "OK" and c.cmd("m8000,2") == "abcd")
        data = bytes([0x7D, 0x23, 0x24, 0x2A, 0x41])
        check("memory is written with X, which escapes } # $ and *",
              c.cmd(b"X8010,5:" + escape(data)) == "OK" and c.mem(0x8010, 5) == data)
        check("a read at 10000H gets E01", c.cmd("m10000,1") == "E01")
        check("a write at 10000H gets E01", c.cmd("M10000,1:00") == "E01" and c.cmd(b"X10000,1:a") == "E01")
        check("a breakpoint at 10000H gets E01", c.cmd("Z0,10000,1") == "E01")
        check("a read that runs past the top is cut short", len(c.cmd("mfffe,10")) == 4)

        check("P writes a register and p reads it back", c.cmd("P2=3412") == "OK" and c.reg(2) == 0x1234)
        gl = c.cmd("g")
        check("G writes all the registers", c.cmd("G" + gl.replace(gl[8:12], "cdab", 1)) == "OK" and c.reg(2) == 0xABCD)
        check("p of an unknown register gets E01", c.cmd("pd") == "E01")

        reply = c.cmd("c")
        check("the exit is reported with W00", reply == "W00", reply)
    finally:
        status, out, err = r.finish()
    check("the program's own output comes through (K) and cpmemu exits 0", status == 0 and out == b"K", "%r %r" % (status, out))


@scenario
def case_syscall(emu, wd, g):
    gm = g["gdb_main"]
    r = Run(emu, wd, gm)
    try:
        c = r.rsp
        c.handshake()
        check("QCatchSyscalls for BDOS 2 is accepted", c.cmd("QCatchSyscalls:1;2") == "OK")
        check("continue stops at the BDOS call with syscall_entry",
              c.cmd("c") == "T05syscall_entry:2;" and c.pc() == 0xFD00)
        check("continue runs the call and stops with syscall_return",
              c.cmd("c") == "T05syscall_return:2;" and c.pc() == gm.after5)
        check("QCatchSyscalls:0 turns the catch off", c.cmd("QCatchSyscalls:0") == "OK")
        check("the program then runs to the end", c.cmd("c") == "W00")
    finally:
        r.finish()


@scenario
def case_detach_kill(emu, wd, g):
    gm = g["gdb_main"]
    r = Run(emu, wd, gm)
    try:
        r.rsp.handshake()
        check("D is answered with OK", r.rsp.cmd("D") == "OK")
    finally:
        status, out, err = r.finish()
    check("after D the program runs to its end (output K, status 0)", status == 0 and out == b"K", "%r %r" % (status, out))

    r = Run(emu, wd, gm)
    try:
        r.rsp.handshake()
        r.rsp.send_raw("k")
    finally:
        status, out, err = r.finish()
    check("k ends cpmemu before the program has run (no output, status 1)", status == 1 and out == b"", "%r %r" % (status, out))


@scenario
def case_console_interrupt(emu, wd, g):
    gw = g["gdb_wait"]
    r = Run(emu, wd, gw, stdin=subprocess.PIPE)
    try:
        c = r.rsp
        c.handshake()
        c.send_raw("c")
        time.sleep(0.4)
        c.interrupt()
        reply = c.recv(10)
        check("^C reaches a guest that waits for a key: T02", reply == "T02", reply)
        check("the stop is at the BDOS trap, with the call still to run", c.pc() == 0xFD00)
        c.cmd("Z0,%x,1" % gw.got)
        r.proc.stdin.write(b"q")
        r.proc.stdin.flush()
        reply = c.cmd("c", 10)
        check("the call runs again on resume and reads the key: A is q at the next instruction",
              reply == "T05swbreak:;" and c.pc() == gw.got and (c.reg(0) >> 8) == ord("q"), reply)
        c.cmd("z0,%x,1" % gw.got)
        reply = c.cmd("c", 10)
        check("the guest then exits with W00", reply == "W00", reply)
    finally:
        r.finish()


@scenario
def case_running_interrupt(emu, wd, g):
    gs = g["gdb_spin"]
    r = Run(emu, wd, gs)
    try:
        c = r.rsp
        c.handshake()
        c.send_raw("c")
        time.sleep(0.3)
        c.interrupt()
        reply = c.recv(10)
        check("^C stops a running program: T02", reply == "T02", reply)
        check("the stop is inside the loop", c.pc() == gs.start)
        if os.name == "nt":
            # No SIGTERM on Windows: end the run with k.  A close alone
            # lets the spinning guest run on, and finish() then waits for
            # a program that never ends.
            c.send_raw("k")
            return
        c.send_raw("c")
        time.sleep(0.3)
        r.proc.send_signal(signal.SIGTERM)
        reply = c.recv(10)
        check("SIGTERM ends the run with an X packet for signal 15", reply == "X0f", reply)
    finally:
        r.finish()


@scenario
def case_piped_input(emu, wd, g):
    # Bytes on stdin before GDB connects belong to the guest.
    gw = g["gdb_wait"]
    def feed(run):
        run.proc.stdin.write(b"q")
        run.proc.stdin.flush()
        time.sleep(0.3)
    r = Run(emu, wd, gw, stdin=subprocess.PIPE, pre_connect=feed)
    try:
        c = r.rsp
        c.handshake()
        c.cmd("Z0,%x,1" % gw.got)
        reply = c.cmd("c", 10)
        check("input piped before GDB connects reaches the guest: A is q",
              reply == "T05swbreak:;" and (c.reg(0) >> 8) == ord("q"), reply)
        c.cmd("z0,%x,1" % gw.got)
        check("the guest then exits with W00", c.cmd("c", 10) == "W00")
    finally:
        r.finish()


@scenario
def case_bios_error(emu, wd, g):
    # An abnormal end through finish(1, true): W01, and --backtrace's chain.
    ge = g["gdb_bioserr"]
    env = dict(os.environ, CPM_BIOS_DISK="error")
    r = Run(emu, wd, ge, env=env)
    try:
        c = r.rsp
        c.handshake()
        reply = c.cmd("c")
        check("a BIOS disk error ends the run with W01", reply == "W01", reply)
    finally:
        status, out, err = r.finish()
    check("cpmemu exits 1 after that", status == 1, "%r %s" % (status, err))
    p = subprocess.run([emu, "--backtrace", ge.com], stdin=subprocess.DEVNULL, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=wd, timeout=30)
    err = p.stderr.decode(errors="replace")
    m = re.search(r"Backtrace[^\n]*\n((?:#\d+ .*\n)+)", err)
    got = frames(m.group(1)) if m else []
    check("--backtrace after a BIOS call shows the call into the BIOS and the call from start, with no stale entry",
          p.returncode == 1 and len(got) == 2 and got[1] == ge.after, err)


@scenario
def case_hlt(emu, wd, g):
    gh = g["gdb_hlt"]
    r = Run(emu, wd, gh)
    try:
        c = r.rsp
        c.handshake()
        reply = c.cmd("c")
        check("a HLT that nothing can end stops the program with T05", reply == "T05" and c.pc() == gh.after, reply)
        reply = c.cmd("c")
        check("the program runs on past the HLT to its exit", reply == "W00", reply)
    finally:
        status, out, err = r.finish()
    check("cpmemu exits 0 after that", status == 0, "%r %s" % (status, err))


@scenario
def case_backtrace_recursive(emu, wd, g):
    gr = g["gdb_rec"]
    r = Run(emu, wd, gr)
    try:
        c = r.rsp
        c.handshake()
        c.cmd("Z0,%x,1" % gr.bottom)
        reply = c.cmd("c")
        bt = c.monitor("bt")
        check("monitor bt on a recursive guest: bottom, two returns into rec, the return into start",
              reply == "T05swbreak:;" and frames(bt) == [gr.bottom, gr.after, gr.after, gr.done], "%s\n%s" % (reply, bt))
        check("monitor bt marks each frame [shadow call] and says nothing about being out of step",
              bt.count("[shadow call]") == 3 and "out of step" not in bt, bt)
        c.cmd("z0,%x,1" % gr.bottom)
        c.cmd("Z0,%x,1" % gr.done)
        reply = c.cmd("c")
        bt = c.monitor("bt")
        check("after the three returns the shadow stack is empty", reply == "T05swbreak:;" and frames(bt) == [gr.done], bt)
        check("an unknown monitor command is answered with text", "bt" in c.monitor("nosuchcommand"))
        check("the exit follows", c.cmd("c") == "W00")
    finally:
        r.finish()


@scenario
def case_reentrant(emu, wd, g):
    gr = g["gdb_reent"]
    r = Run(emu, wd, gr)
    try:
        c = r.rsp
        c.handshake()
        for label in (gr.body_a, gr.after_a, gr.body_b, gr.after_b, gr.after_outer):
            c.cmd("Z0,%x,1" % label)

        c.cmd("c")
        bt = c.monitor("bt")
        check("D7 rule 2: inside proc_a the stack is proc_a, outer, start",
              frames(bt) == [gr.body_a, gr.after_a, gr.after_outer], bt)

        sp = c.reg(4)
        check("a read watchpoint on the return address stops after the POP that reads it: T05rwatch",
              c.cmd("Z3,%x,1" % sp) == "OK" and c.cmd("c") == "T05rwatch:%x;" % sp
              and c.pc() == gr.body_a + 2 and c.reg(2) == gr.after_a, "")
        c.cmd("z3,%x,1" % sp)

        c.cmd("c")
        bt = c.monitor("bt")
        check("D7 rule 2: proc_a's callee-pops epilogue (pop de, pop hl, push de, ret) pops its entry",
              c.pc() == gr.after_a and frames(bt) == [gr.after_a, gr.after_outer] and "out of step" not in bt, bt)

        c.cmd("c")
        bt = c.monitor("bt")
        check("inside proc_b the stack is proc_b, outer, start",
              c.pc() == gr.body_b and frames(bt) == [gr.body_b, gr.after_b, gr.after_outer], bt)

        c.cmd("c")
        bt = c.monitor("bt")
        check("a routine that leaves through JP (HL) keeps its entry, and the stack is not out of step",
              c.pc() == gr.after_b and "out of step" not in bt and len(frames(bt)) == 3, bt)

        c.cmd("c")
        bt = c.monitor("bt")
        check("D7 rule 2: the RET of outer drops the stale entry of proc_b with it",
              c.pc() == gr.after_outer and frames(bt) == [gr.after_outer] and "out of step" not in bt, bt)
        check("the exit follows", c.cmd("c") == "W00")
    finally:
        r.finish()


@scenario
def case_fill(emu, wd, g):
    gf = g["gdb_fill"]
    r = Run(emu, wd, gf)
    try:
        c = r.rsp
        c.handshake()
        c.cmd("Z0,%x,1" % gf.fill)
        c.cmd("Z0,%x,1" % gf.filled)
        hits = 0
        for _ in range(5):
            if c.cmd("c") == "T05swbreak:;" and c.pc() == gf.fill:
                hits += 1
        check("a breakpoint inside a loop stops each time round", hits == 5)
        c.cmd("z0,%x,1" % gf.fill)
        check("a write watchpoint is accepted", c.cmd("Z2,210,1") == "OK")
        reply = c.cmd("c")
        check("a write watchpoint stops after the write: T05watch, with HL at the address",
              reply == "T05watch:210;" and c.reg(3) == 0x210 and c.pc() == gf.fill + 2, reply)
        check("a write watchpoint is removed and an access watchpoint set",
              c.cmd("z2,210,1") == "OK" and c.cmd("Z4,218,1") == "OK")
        reply = c.cmd("c")
        check("an access watchpoint reports awatch", reply == "T05awatch:218;" and c.reg(3) == 0x218, reply)
        c.cmd("z4,218,1")
        reply = c.cmd("c")
        check("the program fills the TPA and stops at the breakpoint after the loop",
              reply == "T05swbreak:;" and c.pc() == gf.filled, reply)
        check("the program bytes at 0100H are unchanged: no breakpoint was written into the guest",
              c.mem(0x100, len(gf.image)) == gf.image)
        tpa = c.mem(0x200, 0xF000 - 0x200)
        check("the whole fill is as the program left it", tpa == b"\xa5" * len(tpa))
        c.cmd("z0,%x,1" % gf.filled)
        check("the exit follows", c.cmd("c") == "W00")
    finally:
        r.finish()


@scenario
def case_backtrace_option(emu, wd, g):
    gb = g["gdb_bt"]
    p = subprocess.run([emu, "--backtrace", gb.com], stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=wd, timeout=30)
    err = p.stderr.decode(errors="replace")
    m = re.search(r"Backtrace[^\n]*\n((?:#\d+ .*\n)+)", err)
    text = m.group(1) if m else ""
    got = frames(text)
    check("--backtrace prints the PC and two return addresses when the run ends badly",
          p.returncode == 1 and len(got) == 3 and got[1:] == [gb.ret_b, gb.ret_a], "%d\n%s" % (p.returncode, err))

    gm = g["gdb_main"]
    p = subprocess.run([emu, "--backtrace", gm.com], stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=wd, timeout=30)
    check("--backtrace prints nothing when the program ends normally",
          p.returncode == 0 and p.stdout == b"K" and b"Backtrace" not in p.stderr, p.stderr.decode(errors="replace"))

    p = subprocess.run([emu, gb.com], stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=wd, timeout=30)
    check("without --backtrace the same run prints no backtrace",
          p.returncode == 1 and b"Backtrace" not in p.stderr)


def main():
    emu = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "src", "cpmemu")
    emu = os.path.abspath(emu)
    if not (shutil.which("um80") and shutil.which("ul80")):
        print("SKIP  gdb stub tests (no assembler: pip install um80)")
        # CPMEMU_REQUIRE_ALL=1 is run_tests.sh --require: a skip is a failure
        return 1 if os.environ.get("CPMEMU_REQUIRE_ALL") == "1" else 0
    wd = tempfile.mkdtemp(prefix="gdbstub.")
    try:
        names = ["gdb_main", "gdb_wait", "gdb_rec", "gdb_reent", "gdb_fill", "gdb_hlt", "gdb_spin", "gdb_bt", "gdb_bioserr"]
        g = {}
        for n in names:
            try:
                g[n] = build(wd, n)
            except Exception as e:
                check("assembling tests/%s.asm" % n, False, str(e))
                return 1
        for case in (case_connection, case_main, case_syscall, case_detach_kill,
                     case_console_interrupt, case_piped_input, case_running_interrupt,
                     case_bios_error, case_hlt,
                     case_backtrace_recursive, case_reentrant, case_fill,
                     case_backtrace_option):
            case(emu, wd, g)
    finally:
        shutil.rmtree(wd, ignore_errors=True)
    print("%d passed, %d failed" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
