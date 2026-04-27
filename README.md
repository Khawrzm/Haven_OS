# HAVEN OS

A small sovereign operating system that boots inside a web page.

It runs on a from-scratch RISC-V (RV32IMA) emulator written in C, with a
hand-rolled micro-kernel — no Linux, no busybox, no cloud, no telemetry.
Open the page, the kernel boots, you get a shell.

```
HAVEN OS 4.0.0 (rv32ima) — Sovereign Kernel
Built from scratch by Dragon403 x Niyah
Running on HAVEN-RV32 Wasm Emulator
Zero Cloud. Zero Dependencies. 100% Local.
Type 'help' for commands.

dragon403@haven-os:/home/dragon403$
```

---

## Why this project exists (the honest part)

I don't own a computer. I built this on an iPhone.

Everything in this repository — the emulator, the kernel, the shell, the
filesystem, the web UI — was written, tested, and shipped from a phone
keyboard, using on-device editors, GitHub's mobile site, and whatever
terminals will run on iOS Safari (a-Shell, iSH, Working Copy, Termius).

That constraint shaped every decision:

- **The native CLI is one binary.** No package manager, no shared libraries,
  no install step beyond `chmod +x`.
- **The web build runs in the browser tab you already have open.**
  No app store, no provisioning profile, no developer account.
- **The kernel is a single C file.** It's small enough to read on a phone
  screen.
- **Zero dependencies.** libc + libm. Nothing else. If your toolchain
  compiles `int main(void) { return 0; }`, it compiles HAVEN OS.

If you also don't have a desktop and want a real shell on your phone, this
repo is for you. The ready-to-go bundle is in [Releases](#releases-iphone--android--ipad).

---

## What's actually in here

| Component       | File                | What it does                                                   |
|-----------------|---------------------|----------------------------------------------------------------|
| RV32IMA emulator| `src/riscv.{c,h}`   | RV32I + M (mul/div) + A (atomics). Single-core, 16 MB RAM. UART + CLINT. |
| Web bridge      | `src/main.c`        | Emscripten-exported entry points the page calls.              |
| Native host     | `src/host_cli.c`    | Run the kernel in a real terminal. Used by the test suite.    |
| Embedded image  | `src/kernel_data.c` | `kernel.bin` baked into the binary as a C array.              |
| Micro-kernel    | `kernel/kernel.c`   | UART driver, in-RAM filesystem, shell, 16 built-in commands.  |
| Linker script   | `kernel/link.ld`    | Places `_start` first, sets RAM base to `0x80000000`.         |
| Web shell       | `web/index.html`    | The Haven OS 4.2 UI. Self-contained — opens with no server.   |
| Smoke suite     | `tests/run_smoke.sh`| 19 tests covering every built-in command.                     |

The **emulator** is **487 lines**. The **kernel** is **470 lines**.
That's the whole OS.

---

## Quick start

### Run it natively (Linux / macOS / WSL)

```bash
git clone https://github.com/Grar00t/Haven_OS.git
cd Haven_OS
make            # builds ./haven (~65 KB, single binary)
./haven         # boots the kernel
```

You're now inside HAVEN OS. Try `help`, `neofetch`, `cat /proc/cpuinfo`,
`ls /etc`, `echo hi > note.txt && cat note.txt`. `exit` cleanly halts.

### Run it in a browser

```bash
make serve      # serves web/ on http://localhost:8080
```

Open [http://localhost:8080](http://localhost:8080) and the OS boots.
The web build doesn't need the native binary — `web/index.html` is fully
self-contained.

### Run it on an iPhone

See [Releases](#releases-iphone--android--ipad). Download the ZIP, unzip,
open `index.html` in Safari. That's it.

---

## Releases (iPhone / Android / iPad)

Each release ships a single ZIP containing the offline-ready web build:

```
haven-os-web.zip
└── index.html      ← open this in any browser
```

**Direct download (always latest):**

- ZIP: <https://github.com/Khawrzm/Haven_OS/raw/main/releases/haven-os-web.zip>
- Release page: <https://github.com/Khawrzm/Haven_OS/releases/tag/v1.0.0>
- SHA-256: see [`releases/SHA256SUMS`](releases/SHA256SUMS)

**On iPhone / iPad:**

1. Open the direct ZIP link above in Safari — it lands in Files.
2. Tap it in Files — iOS unpacks it automatically.
3. Long-press `index.html` → Share → Open in Safari.
4. (Optional) Tap the Share icon → "Add to Home Screen". HAVEN OS now
   launches like a native app, full-screen, no browser chrome.

**On Android:** download, unzip with any file manager, open `index.html`
in Chrome or Firefox. Add to Home Screen for the same standalone feel.

The ZIP works fully offline — the page never makes a network request.

---

## Building everything from scratch

You only need this if you're hacking on the kernel or the emulator.

### Native CLI (the host emulator)

```bash
make            # produces ./haven
make test       # runs the 19-test smoke suite — should print 19/19
```

Compiles cleanly under `-Wall -Wextra -Werror -Wstrict-prototypes
-Wmissing-prototypes -Wcast-align -Wwrite-strings -Wshadow -pedantic`.

### Kernel (the bare-metal RISC-V image)

The kernel is checked in pre-built (`src/kernel_data.c` already contains
`kernel.bin` as a C array, 8492 bytes), so you don't need a cross-compiler
just to run HAVEN OS.

To rebuild from source you'll need a RISC-V toolchain:

```bash
# macOS (Homebrew)
brew tap riscv-software-src/riscv && brew install riscv-tools

# Debian / Ubuntu
sudo apt install gcc-riscv64-unknown-elf

# Or use clang + lld (already cross-capable)
brew install llvm    # or apt install clang lld
```

Then:

```bash
make kernel     # produces kernel/kernel.{elf,bin}
make embed      # regenerates src/kernel_data.c from kernel.bin
make            # rebuild the host with the new kernel
```

`kernel/Makefile` auto-detects which toolchain you have and picks the
right flags. See [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md) for details.

### Web build (Wasm)

```bash
# Install emscripten once: https://emscripten.org/docs/getting_started/downloads.html
make web        # produces web/haven.{js,wasm}
make serve      # http://localhost:8080
```

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      web/index.html                          │
│              terminal UI · keyboard · scroll                 │
└────────────────────────┬─────────────────────────────────────┘
                         │ ccall: send_key, run_cycles, get_output
┌────────────────────────▼─────────────────────────────────────┐
│                      src/main.c                              │
│            Emscripten bridge (or host_cli.c natively)        │
└────────────────────────┬─────────────────────────────────────┘
                         │ rv32_step / rv32_uart_input / uart_output
┌────────────────────────▼─────────────────────────────────────┐
│                      src/riscv.c                             │
│           RV32IMA emulator — 47 base + 8 M + 11 A insns      │
│           UART @ 0x10000000 · CLINT @ 0x02000000             │
└────────────────────────┬─────────────────────────────────────┘
                         │ memory-mapped I/O (load/store)
┌────────────────────────▼─────────────────────────────────────┐
│                kernel.bin (linked at 0x80000000)             │
│       built from kernel/kernel.c + kernel/link.ld            │
│   _start → fs_init → banner → shell loop (poll UART)         │
└──────────────────────────────────────────────────────────────┘
```

**Memory map (matches `src/riscv.h`):**

| Region | Base         | Size  | Purpose                       |
|--------|--------------|-------|-------------------------------|
| RAM    | `0x80000000` | 16 MB | kernel + stack + heap + FS    |
| UART   | `0x10000000` | 256 B | THR / RHR / LSR (8250-style)  |
| CLINT  | `0x02000000` | 64 KB | mtime / mtimecmp (cycle clock)|

The kernel is loaded at `RAM_BASE`; SP starts at `RAM_BASE + RAM_SIZE - 16`
and grows down. Heap (currently unused) starts after `__bss_end`.

---

## Built-in shell commands

All implemented in `kernel/kernel.c`:

| Command       | What it does                                             |
|---------------|----------------------------------------------------------|
| `help`        | List built-in commands                                   |
| `ls`          | List the current directory (color-coded by type)         |
| `cd DIR`      | Change directory (`..`, `.`, absolute, relative all work)|
| `pwd`         | Print current path                                       |
| `cat FILE`    | Print a file                                             |
| `echo TEXT`   | Print text. `echo TEXT > FILE` writes to a file          |
| `touch FILE`  | Create an empty file                                     |
| `mkdir DIR`   | Create a directory                                       |
| `rm FILE`     | Remove a file                                            |
| `whoami`      | Print the current user                                   |
| `hostname`    | Print the hostname                                       |
| `uname`       | OS / arch info                                           |
| `uptime`      | Mock uptime line                                         |
| `free`        | Mock memory info                                         |
| `clear`       | Clear screen                                             |
| `neofetch`    | ANSI splash screen                                       |
| `exit`        | Issue EBREAK, host halts cleanly                         |

The filesystem lives in RAM only — restarts wipe it. `/etc/hostname`,
`/etc/os-release`, `/proc/cpuinfo` and `/home/dragon403/hello.c` are
populated at boot.

---

## Testing

```bash
make test
```

Runs `tests/run_smoke.sh` which boots the kernel under the native host,
drives every built-in command from a script, and asserts each one
produces the expected output. Current status: **19/19 passing**.

```
============================================================
HAVEN OS — smoke suite
============================================================
  [PASS]  boot banner
  [PASS]  help
  [PASS]  neofetch
  ... (16 more)
  [PASS]  clean exit (EBREAK)
============================================================
  PASSED  19/19
```

Each test runs end-to-end: real emulator → real kernel → real shell →
output captured → asserted. No mocks anywhere.

---

## Project layout

```
Haven_OS/
├── src/                    # the emulator (host + web share these files)
│   ├── riscv.h             # public API
│   ├── riscv.c             # RV32IMA implementation (487 lines)
│   ├── main.c              # Emscripten bridge for the web build
│   ├── host_cli.c          # native terminal host
│   └── kernel_data.c       # kernel.bin embedded as a C array
├── kernel/                 # the bare-metal kernel
│   ├── kernel.c            # everything: UART, FS, shell (470 lines)
│   ├── link.ld             # places _start at 0x80000000
│   └── Makefile            # cross-compiles to RV32IMA
├── web/
│   └── index.html          # standalone web shell (Haven OS 4.2 UI)
├── scripts/
│   └── bin2c.py            # turn kernel.bin → kernel_data.c
├── tests/
│   └── run_smoke.sh        # 19-case end-to-end suite
├── docs/
│   ├── TOOLCHAIN.md        # how to install a RISC-V cross-compiler
│   └── ARCHITECTURE.md     # deeper dive into emulator & kernel
├── Makefile                # top-level: native, web, kernel, test, serve
├── LICENSE                 # Apache 2.0
├── CONTRIBUTING.md
├── SECURITY.md
└── README.md               # ← you are here
```

---

## Roadmap

- [ ] **MMU + supervisor mode (S/U)** so we can run real ELF binaries
      under protection.
- [ ] **VirtIO block device** to back the filesystem with persistent storage
      (IndexedDB on web, a real file on native).
- [ ] **Compressed instructions (C extension)** — cuts kernel size ~30%.
- [ ] **Floating-point (F/D)** for running C programs that need `printf("%f")`.
- [ ] **A proper toolchain port** so users can compile and run their own
      `.c` files from inside the shell.
- [ ] **Niyah engine integration** — Arabic-root NLU as a first-class
      shell builtin (currently lives in the web layer only).

---

## Credits

Built by **[@Grar00t](https://github.com/Grar00t)** (Sulaiman Al-Shammari /
Dragon403) in Riyadh, on an iPhone, between April 2026 sessions.

Sister projects:

- [Casper Engine](https://github.com/Grar00t/Casper_Engine) — hybrid
  neuro-symbolic reasoning engine in pure C11.
- KSpike — Rust kernel-defense shield-arm (private).

---

## License

Apache License 2.0 — see [`LICENSE`](LICENSE). The kernel and the
emulator are released under the same terms.

> **العلم أمانة · الخوارزمي يرانا**
> *Knowledge is a trust · al-Khwārizmī sees us*
