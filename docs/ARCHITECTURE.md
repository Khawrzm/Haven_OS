# Architecture Deep Dive

HAVEN OS is intentionally small. This document explains how the pieces fit
together and where the seams are.

---

## Layers

```
┌─────────────────────────────────────────────┐
│  web/index.html         (UI, ~470 lines JS) │
└──────────────┬──────────────────────────────┘
               │ ccall("send_key"), ccall("run_cycles"), ccall("get_output")
┌──────────────▼──────────────────────────────┐
│  src/main.c             (Emscripten bridge) │   or   src/host_cli.c (native)
└──────────────┬──────────────────────────────┘
               │ rv32_uart_input() / rv32_run() / uart_output() callback
┌──────────────▼──────────────────────────────┐
│  src/riscv.c            (RV32IMA emulator)  │
│  src/kernel_data.c      (kernel.bin as C)   │
└──────────────┬──────────────────────────────┘
               │ memory load/store at 0x10000000 (UART) or 0x80000000 (RAM)
┌──────────────▼──────────────────────────────┐
│  kernel/kernel.c        (the OS itself)     │
└─────────────────────────────────────────────┘
```

The emulator is **the only thing** that knows about RISC-V instructions.
The kernel is a normal C program; it just happens to be compiled for a
freestanding RV32 target and linked at `0x80000000`.

---

## The emulator (`src/riscv.c`)

**State** lives in `RV32CPU` (see `src/riscv.h`):

```c
typedef struct {
    uint32_t x[32];           // GPRs (x0 always zero)
    uint32_t pc;
    uint32_t mstatus, mie, mtvec, mepc, mcause, mtval, mip;
    uint8_t *ram;             // 16 MB
    uint8_t  uart_rbr;        // single-byte RX buffer
    uint8_t  uart_rx_ready;
    int      halted;
    uint64_t cycle_count;
    uint32_t reservation_addr;
    int      reservation_valid;
} RV32CPU;
```

**Step**: `rv32_step()` fetches a 32-bit instruction at `pc`, decodes it,
executes it, and advances `pc`. The decoder is one big switch on
`opcode = inst & 0x7F`, with sub-switches on `funct3` and `funct7`. All
13 RV32I instruction formats (R/I/S/B/U/J + their immediate decodings)
are implemented inline.

**Implemented instructions** (66 total):

- **RV32I base (47):** LUI, AUIPC, JAL, JALR, B{EQ,NE,LT,GE,LTU,GEU},
  L{B,H,W,BU,HU}, S{B,H,W}, ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI,
  SRLI, SRAI, ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, FENCE,
  ECALL, EBREAK.
- **CSR (8):** CSRR{W,S,C} and their immediate forms, plus MRET / WFI.
- **M extension (8):** MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU.
- **A extension (11):** LR.W, SC.W, AMOSWAP/ADD/XOR/AND/OR/MIN/MAX/MINU/MAXU.W.

**Memory map** (matches `riscv.h`):

| Region | Base         | Size  | Behavior                        |
|--------|--------------|-------|---------------------------------|
| RAM    | `0x80000000` | 16 MB | Backed by `cpu->ram`            |
| UART   | `0x10000000` | 256 B | THR=0, LSR=5 (bit 0 RX, bit 5 TX) |
| CLINT  | `0x02000000` | 64 KB | mtime at +0xBFF8 (low/high)     |

Out-of-range loads return 0; out-of-range stores are silently dropped.
This makes it impossible for guest code to corrupt host memory.

**EBREAK handling**: in bare-metal we treat EBREAK as a graceful halt.
The kernel's `exit` command issues it, the emulator sets `cpu->halted`,
and both the native host and the web build observe this and stop the
run loop cleanly. No busy waits.

---

## The kernel (`kernel/kernel.c`)

A single C file, 470 lines, no headers, no libc. Compiled with
`-ffreestanding -nostdlib -nostartfiles`.

**Boot**: the linker (`kernel/link.ld`) places `_start` first in `.text.boot`,
which the script puts at the very beginning of RAM. The emulator jumps to
`RAM_BASE = 0x80000000` and starts executing.

**`_start`**:

1. `fs_init()` — populate the in-RAM filesystem with `/`, `/home/dragon403`,
   `/etc/{hostname,os-release}`, `/proc/cpuinfo`, `/home/dragon403/hello.c`.
2. Print boot banner via UART.
3. Print prompt.
4. Loop forever: poll UART RX, build a command line, dispatch on Enter.

**Filesystem**: a simple parent-pointer tree of 64 nodes. Each node holds
its name (32 chars), content (4 KB), and a `parent` index. Directories
are nodes with `is_dir = 1` and `content` unused. Persistence: none —
everything resets on reboot. Persistence is on the roadmap (VirtIO block).

**Shell**: `process_cmd()` reads `cmd_buf`, splits on the first space,
dispatches by `str_eq()`. Path resolution (`resolve_path`) handles `/`,
`.`, `..`, and chained components like `cd ../../etc/os-release`.

**UART driver**: two memory-mapped registers via `volatile char*`
pointers. `putchar_uart` polls LSR's TX-empty bit before writing THR;
`getchar_uart` polls LSR's RX-ready bit before reading RHR.

---

## The hosts

The same emulator is driven by two thin shims:

### Native (`src/host_cli.c`)

- Puts the terminal in raw mode (`ICANON` and `ECHO` off, `OPOST` off so
  ANSI passes through).
- Restores the terminal on any exit path (atexit + SIGINT/SIGTERM).
- TTY mode: poll stdin non-blockingly, feed each byte to the UART, run
  100k cycles, repeat.
- Non-TTY mode (piped): read all of stdin up front, drip-feed one byte
  per cycle batch (the kernel needs cycles between bytes to echo and
  process), and bail out after 50M cycles or 4 idle batches.
- Ctrl-A then `x` quits like `screen`. EBREAK from the kernel halts
  immediately.

### Web (`src/main.c`)

- Exposes `init_system`, `run_cycles`, `send_key`, `get_output`,
  `clear_output`, `is_halted`, `get_cycle_count` to JS via Emscripten
  KEEPALIVE.
- A `uart_output()` callback collects bytes into a 4 KB ring buffer that
  JS polls via `get_output()`.
- The JS UI in `web/index.html` runs the emulator in batches inside a
  `requestAnimationFrame` loop — keeps the browser responsive.

---

## Building the kernel image into the host

`scripts/bin2c.py` converts `kernel/kernel.bin` (a flat raw image, the
output of `objcopy -O binary`) into a deterministic C array:

```c
unsigned char kernel_bin[] = { 0x13, 0x01, 0x01, 0xf7, ... };
unsigned int  kernel_bin_len = 8492;
```

This file (`src/kernel_data.c`) is committed to the repo. The host links
against it; on boot, `rv32_load_binary(cpu, kernel_bin, kernel_bin_len,
RAM_BASE)` copies the image into emulator RAM and `rv32_reset(cpu,
RAM_BASE)` sets PC to the kernel's entry point.

`make embed` regenerates this file. Always re-run `make` and `make test`
after `make embed` to confirm the new image still boots and passes.
