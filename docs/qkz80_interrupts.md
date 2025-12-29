# qkz80 Interrupt Support

The qkz80 library provides portable interrupt support for Z80 emulation. The implementation is timing-agnostic - the caller decides when to trigger interrupts based on their own timing mechanism (wall-clock time, CPU cycles, or other criteria).

## Interrupt Types

### Maskable Interrupt (INT)

Can be disabled by the program using DI instruction. Controlled by IFF1 flag.

Behavior depends on interrupt mode (IM register):
- **IM 0**: Execute instruction on data bus (typically RST instruction)
- **IM 1**: Always jump to 0x0038 (RST 38H)
- **IM 2**: Vector table lookup using I register and supplied vector

### Non-Maskable Interrupt (NMI)

Cannot be disabled. Always jumps to 0x0066.

## API

### State Fields

```cpp
bool int_pending;        // Maskable interrupt pending
bool nmi_pending;        // NMI pending
qkz80_uint8 int_vector;  // Vector for IM0/IM2
```

### Request Methods

```cpp
// Request maskable interrupt with vector (for IM0/IM2)
void request_int(qkz80_uint8 vector = 0xFF);

// Request NMI
void request_nmi(void);

// Convenience: request INT using RST number (0-7)
// RST n jumps to address n*8
void request_rst(qkz80_uint8 rst_num);
```

### Delivery Method

```cpp
// Check and deliver pending interrupts
// Call at instruction boundaries (after execute())
// Returns true if an interrupt was delivered
bool check_interrupts(void);
```

## Usage Examples

### Time-Based Interrupts (like MP/M)

Use wall-clock time to trigger periodic interrupts (e.g., 60Hz tick):

```cpp
#include <chrono>

auto next_tick_time_ = std::chrono::steady_clock::now();
const auto tick_interval_ = std::chrono::microseconds(16667);  // 60Hz

while (running) {
    auto now = std::chrono::steady_clock::now();

    // Check if it's time for a tick interrupt
    if (now >= next_tick_time_) {
        next_tick_time_ += tick_interval_;
        cpu.request_rst(7);  // RST 38H for timer
    }

    // Deliver any pending interrupts
    cpu.check_interrupts();

    // Execute one instruction
    cpu.execute();
}
```

### Cycle-Based Interrupts (like RomWBW)

Use CPU cycle count to trigger interrupts at regular intervals:

```cpp
unsigned long long next_tick_cycles_ = 0;
const unsigned int tick_interval_cycles_ = 50000;  // Every 50K cycles

while (running) {
    // Check if enough cycles have passed
    if (cpu.cycles >= next_tick_cycles_) {
        next_tick_cycles_ = cpu.cycles + tick_interval_cycles_;
        cpu.request_int(0xFF);  // RST 38H
    }

    // Deliver any pending interrupts
    cpu.check_interrupts();

    // Execute one instruction
    cpu.execute();
}
```

### NMI Example

```cpp
// Trigger NMI (e.g., from a button press or external event)
if (nmi_button_pressed) {
    cpu.request_nmi();
}

cpu.check_interrupts();
cpu.execute();
```

### IM 2 Vectored Interrupts

For systems using IM 2 with an interrupt vector table:

```cpp
// Set up I register to point to vector table page
cpu.regs.I = 0xFE;  // Table at 0xFE00

// Set interrupt mode 2
cpu.regs.IM = 2;

// Enable interrupts
cpu.regs.IFF1 = 1;
cpu.regs.IFF2 = 1;

// Request interrupt with low byte of vector address
// CPU will read jump address from (I << 8) | vector
cpu.request_int(0x10);  // Vector at 0xFE10

cpu.check_interrupts();
```

## Notes

- `check_interrupts()` should be called at instruction boundaries
- NMI has higher priority than INT
- NMI preserves IFF1 in IFF2 (restored by RETN)
- INT clears both IFF1 and IFF2
- The `cycles` field is incremented by interrupt delivery (11-19 T-states depending on type)

## cpmemu Command-Line Options

The cpmemu program supports cycle-based interrupts via command-line options:

```
--int-cycles=N      Enable timer interrupt every N cycles (e.g., 50000)
--int-rst=N         RST number for interrupt (0-7, default 7 = RST 38H)
```

Example:
```bash
# Run with 60Hz-ish interrupts (assuming ~4MHz = 4M cycles/sec, 60Hz = 66666 cycles)
./cpmemu --int-cycles=66666 --int-rst=7 tasking_test.com
```

## Downstream Projects

- **mpm2**: Uses time-based 60Hz tick interrupts for MP/M II multitasking
- **romwbw_emu**: Uses cycle-based interrupts for RomWBW timer emulation
- **cpmemu**: Supports optional cycle-based interrupts via `--int-cycles`
