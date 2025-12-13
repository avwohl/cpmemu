#!/bin/bash
# Run ZEXDOC and output PC every 100000 instructions to see if we're progressing
export DEBUG_TRACE=1  # Enable debug tracing if supported
timeout 10 strace -e trace=write src/cpm_emulator --z80 tests/zexdoc.com 2>&1 | grep -E "aluop|IXH|IXL|hanging" | head -50
