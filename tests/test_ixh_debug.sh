#!/bin/bash
# Debug aid, not a test: run zexdoc under strace and show the writes that
# mention the IXH/IXL aluop group, to see whether it is progressing or stuck.
# Needs strace on PATH.

set -u

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
root=$(dirname -- "$here")
emu=$root/src/cpmemu

if [ ! -x "$emu" ]; then
    echo "emulator not found at $emu - run: make -C $root/src" >&2
    exit 1
fi
if ! command -v strace >/dev/null 2>&1; then
    echo "strace not found on PATH" >&2
    exit 1
fi

timeout 10 strace -e trace=write "$emu" --z80 "$root/tests/zexdoc.com" </dev/null 2>&1 \
    | grep -E "aluop|IXH|IXL|hanging" \
    | head -50
