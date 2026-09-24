#!/usr/bin/env python3
"""Property check for cpmemu's converted text files.

    text_image_prop.py EMULATOR TEXT_OPS.COM [--cases N] [--seed S] [--keep DIR]

For random host text files - LF, CR LF, CR LF ending in a ^Z, and CP/M's own
form, CR LF padded with ^Z to a record - and random scripts of sequential and
random reads and writes, file size, close and open, every one run by
tests/text_ops.com under the emulator: each read returns what the same calls
would read from the file's CP/M image, and the host file ends as that image
written as host text.

The model is the definition in src/cpmemu.cc's TextImage comment:

  - the image is the host text converted - LF with no CR before it to CR LF,
    ending at the first ^Z - and padded with ^Z to a whole record; a write
    puts 128 bytes at record * 128, growing the image, a gap as NULs;
  - the host file is the image up to its first ^Z, CR LF to LF unless the
    file's first line ended CR LF, then a ^Z if the file had one, padded with
    ^Z to a record if the file was a whole number of records - except that
    the lines before the one the first change is in keep the bytes they had;
  - a close writes the host file, and an open reads it again.

Every host file made here is already in its own style, so the first write
back of any case leaves exactly host_text(image).  The exception matters
after a close and an open: a line the guest wrote as CR CR LF is host text
CR LF in an LF file, which reads back as CR LF, and is left as it is by a
later write that does not reach it.

The host files are made so that line ends fall at, before, after and across
record boundaries - a line of 126, 127 or 128 bytes puts its CR, its LF or
both at byte 127 of a record.  Exits 1, and prints the seed, the file and the
script, for the first case that differs.  Runs on Python 3.9.
"""

import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile

EOF = 0x1A


# --- the model ---------------------------------------------------------------

class Image(object):
    def __init__(self, host):
        cpm = bytearray()
        self.crlf = False
        self.eof_mark = False
        self.lines = [(0, 0)]     # where each line starts: (image, host)
        seen_lf = prev_cr = False
        for h, c in enumerate(host):
            if c == EOF:
                self.eof_mark = True
                break
            if c == 0x0A:
                if not seen_lf:
                    self.crlf = prev_cr
                seen_lf = True
                if not prev_cr:
                    cpm.append(0x0D)
                cpm.append(0x0A)
                self.lines.append((len(cpm), h + 1))
            else:
                cpm.append(c)
            prev_cr = c == 0x0D
        self.eof_pad = self.eof_mark and len(host) % 128 == 0
        while len(cpm) % 128:
            cpm.append(EOF)
        self.cpm = cpm
        self.loaded = host
        self.changed = None       # the first image byte a write changed

    def records(self):
        return len(self.cpm) // 128

    def read(self, rec):
        if rec * 128 >= len(self.cpm):
            return None
        return bytes(self.cpm[rec * 128:rec * 128 + 128])

    def mark(self, at):
        self.changed = at if self.changed is None else min(self.changed, at)

    def write(self, rec, data):
        at = rec * 128
        if at + 128 > len(self.cpm):
            self.mark(len(self.cpm))
            self.cpm.extend(b'\0' * (at + 128 - len(self.cpm)))
        for k in range(128):
            if self.cpm[at + k] != data[k]:
                self.mark(at + k)
                break
        self.cpm[at:at + 128] = data

    def host(self):
        """The host file after a write back: the lines before the one the
        first change is in as they were, then the image to its first ^Z as
        host text."""
        end = self.cpm.find(bytes([EOF]))
        end = len(self.cpm) if end < 0 else end
        if self.changed is None or self.changed > end:
            return self.loaded
        p, h = [ln for ln in self.lines if ln[0] <= self.changed][-1]
        text = bytes(self.cpm[p:end])
        out = bytearray(self.loaded[:h])
        i = 0
        while i < len(text):
            if not self.crlf and text[i] == 0x0D and i + 1 < len(text) and text[i + 1] == 0x0A:
                i += 1
                continue
            out.append(text[i])
            i += 1
        if self.eof_mark:
            out.append(EOF)
            while self.eof_pad and len(out) % 128:
                out.append(EOF)
        return bytes(out)


def model(host, ops):
    """What the emulator must answer, and the host file it must leave."""
    img = Image(host)
    pos = 0
    results = []
    for op in ops:
        kind = op[0]
        if kind in ('O', 'C'):
            img = Image(img.host())         # the host file, read again
            if kind == 'O':
                pos = 0
            results.append((0, None))
        elif kind == 'R':
            data = img.read(pos)
            if data is None:
                results.append((1, None))
            else:
                results.append((0, data))
                pos += 1
        elif kind == 'G':
            pos = op[1]
            data = img.read(pos)
            results.append((1, None) if data is None else (0, data))
        elif kind == 'W':
            img.write(pos, op[1])
            pos += 1
            results.append((0, None))
        elif kind == 'P':
            pos = op[1]
            img.write(pos, op[2])
            results.append((0, None))
        elif kind == 'Z':
            pos = op[1]
            results.append((0, None))
        elif kind == 'F':
            results.append((0, img.records()))
    return img.host(), results


# --- running it --------------------------------------------------------------

def encode(ops):
    out = bytearray()
    for op in ops:
        rec = bytearray(128)
        rec[0] = ord(op[0])
        if op[0] in ('G', 'P', 'Z'):
            n = op[1]
            rec[1:4] = bytes([n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
        out += rec
        if op[0] == 'W':
            out += op[1]
        elif op[0] == 'P':
            out += op[2]
    return bytes(out)


def run(emu, guest, host, ops, work):
    for f in os.listdir(work):
        os.remove(os.path.join(work, f))  # run.cfg too: it names the guest
    with open(os.path.join(work, 't.txt'), 'wb') as f:
        f.write(host)
    with open(os.path.join(work, 'ops.dat'), 'wb') as f:
        f.write(encode(ops))
    # A mode rule, so that T.TXT is text however its bytes come to look: a
    # write of NULs would otherwise have it open binary the next time, by
    # the rule a name on the text list is opened with under auto, which is
    # checked on its own in tests/run_tests.sh.
    with open(os.path.join(work, 'run.cfg'), 'w') as f:
        f.write('program = %s\n*.TXT = text\n' % guest)
    p = subprocess.run([emu, os.path.join(work, 'run.cfg'), 'T.TXT'], cwd=work,
                       stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=60)
    with open(os.path.join(work, 't.txt'), 'rb') as f:
        after = f.read()
    try:
        with open(os.path.join(work, 'res.dat'), 'rb') as f:
            res = f.read()
    except OSError:
        res = b''
    results = []
    i = 0
    for op in ops:
        if i + 128 > len(res):
            results.append(('missing', None))
            continue
        status = res[i]
        rec = res[i:i + 128]
        i += 128
        if op[0] in ('R', 'G') and status == 0:
            results.append((status, res[i:i + 128]))
            i += 128
        elif op[0] == 'F':
            results.append((status, rec[4] | rec[5] << 8 | rec[6] << 16))
        else:
            results.append((status, None))
    return after, results, p.returncode


# --- making cases ------------------------------------------------------------

LINE_LENGTHS = [0, 1, 2, 3, 60, 124, 125, 126, 127, 128, 129, 130, 254, 255, 256]


def gen_host(rng):
    style = rng.choice(['lf', 'crlf', 'crlf', 'cpm', 'dos', 'lfz', 'empty', 'nonl'])
    if style == 'empty':
        return b''
    eol = b'\n' if style in ('lf', 'lfz', 'nonl') else b'\r\n'
    lines = []
    for _ in range(rng.randint(1, 8)):
        n = rng.choice(LINE_LENGTHS) if rng.random() < 0.6 else rng.randint(0, 90)
        lines.append(bytes(rng.choice(b'abcdefghij ') for _ in range(n)))
    body = eol.join(lines) + (b'' if style == 'nonl' else eol)
    if style == 'cpm':
        body += bytes([EOF])
        while len(body) % 128:
            body += bytes([EOF])
    elif style in ('dos', 'lfz'):
        body += bytes([EOF])
    return body


def text_record(rng):
    """128 bytes a program writing text might write, line ends included."""
    out = bytearray()
    while len(out) < 128:
        r = rng.random()
        if r < 0.12:
            out += b'\r\n'
        elif r < 0.15:
            out += rng.choice([b'\r', b'\n'])       # a lone one
        elif r < 0.18:
            out.append(EOF)
            fill = rng.choice([bytes([EOF]), b'x', b'\0'])
            while len(out) < 128:
                out += fill
        else:
            out.append(rng.choice(b'KLMNOP qr'))
    return bytes(out[:128])


def edge_record(rng):
    """A record whose first or last byte is half a line end."""
    body = bytearray(text_record(rng).replace(bytes([EOF]), b'z'))
    choice = rng.randint(0, 3)
    if choice == 0:
        body[127] = 0x0D
    elif choice == 1:
        body[0] = 0x0A
    elif choice == 2:
        body[126:128] = b'\r\n'
    else:
        body[0] = EOF
    return bytes(body)


def gen_ops(rng, host, sequential=False):
    img = Image(host)
    ops = []
    pos = 0
    for _ in range(rng.randint(1, 10)):
        n = img.records()
        r = rng.random()
        if sequential and 0.30 <= r < 0.50:
            r = 0.55   # no random reads or writes, and no gap
        if sequential and 0.75 <= r < 0.82:
            r = 0.85   # nor a file size
        if r < 0.18:
            ops.append(('R',))
            if img.read(pos) is not None:
                pos += 1
        elif r < 0.30:
            data = edge_record(rng) if rng.random() < 0.4 else text_record(rng)
            ops.append(('W', data))
            img.write(pos, data)
            pos += 1
        elif r < 0.40:
            pos = rng.randint(0, n + 1)
            ops.append(('G', pos))
        elif r < 0.50:
            pos = rng.randint(0, n + (2 if rng.random() < 0.2 else 1))
            data = edge_record(rng) if rng.random() < 0.4 else text_record(rng)
            ops.append(('P', pos, data))
            img.write(pos, data)
        elif r < 0.60:
            pos = rng.randint(0, n if sequential else n + 1)
            ops.append(('Z', pos))
        elif r < 0.75 and n > 0:
            # The append: read to the end, back up a record, write it again
            # with new text from its ^Z on.
            for _ in range(n + 1):
                ops.append(('R',))
            last = bytearray(img.read(n - 1))
            z = last.find(bytes([EOF]))
            ops.append(('Z', n - 1))
            if z < 0:
                last = bytearray(text_record(rng))
                ops.append(('Z', n))
                pos = n
            else:
                add = rng.choice([b'APPENDED\r\n', b'more', b'\r\n', b'x\r\ny\r\n'])
                last[z:z + len(add)] = add
                last = last[:128]
                while len(last) < 128:
                    last.append(EOF)
                pos = n - 1
            ops.append(('W', bytes(last)))
            img.write(pos, bytes(last))
            pos += 1
        elif r < 0.82:
            ops.append(('F',))
        elif r < 0.91:
            ops.append(('C',))
            img = Image(img.host())
        else:
            ops.append(('O',))
            img = Image(img.host())
            pos = 0
    if rng.random() < 0.5:
        ops.append(('C',))
    return ops


def show(host, ops):
    lines = ['host file: %r' % (host,)]
    for op in ops:
        lines.append('  ' + ' '.join(repr(x) for x in op))
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('emulator')
    ap.add_argument('guest')
    ap.add_argument('--cases', type=int, default=400)
    ap.add_argument('--seed', type=int, default=20260924)
    ap.add_argument('--keep', help='work in this directory and leave it')
    ap.add_argument('--only', type=int, help='run this case alone')
    ap.add_argument('--count', action='store_true',
                    help='run every case and count the ones that differ')
    ap.add_argument('--sequential', action='store_true',
                    help='sequential reads and writes, positioned by CR, only')
    args = ap.parse_args()
    emu = os.path.abspath(args.emulator)
    guest = os.path.abspath(args.guest)
    work = args.keep or tempfile.mkdtemp()
    os.makedirs(work, exist_ok=True)
    failed = 0
    try:
        for case in ([args.only] if args.only is not None else range(args.cases)):
            rng = random.Random(args.seed * 100003 + case)
            host = gen_host(rng)
            ops = gen_ops(rng, host, args.sequential)
            want_host, want = model(host, ops)
            got_host, got, rc = run(emu, guest, host, ops, work)
            bad = None
            if rc != 0:
                bad = 'emulator exited %d' % rc
            else:
                for i, (w, g) in enumerate(zip(want, got)):
                    if w != g:
                        bad = 'call %d (%s): expected %r, got %r' % (i, ops[i][0], w, g)
                        break
                if bad is None and got_host != want_host:
                    bad = 'host file: expected %r\n                 got %r' % (want_host, got_host)
            if bad:
                failed += 1
                if args.count:
                    continue
                print('case %d of seed %d: %s' % (case, args.seed, bad))
                print(show(host, ops))
                return 1
        if failed:
            print('%d of %d cases differ' % (failed, args.cases))
            return 1
        print('%d cases' % args.cases)
        return 0
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
