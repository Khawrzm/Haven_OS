# Contributing to HAVEN OS

Thanks for your interest. HAVEN OS is small on purpose — the whole OS is
~1,000 lines of C. That means every change has a real effect, and every
PR is read carefully.

This document covers how to build, test, and submit changes.

---

## Ground rules

- **Pure C99**, libc + libm only. No C++. No new dependencies.
- **Compiles cleanly under** `-Wall -Wextra -Werror -Wstrict-prototypes
  -Wmissing-prototypes -Wcast-align -Wwrite-strings -Wshadow -pedantic`.
  The top-level `Makefile` enforces this.
- **All 19 smoke tests must keep passing** (`make test`). Add a test for
  every new behavior.
- **No globals where state can be local.** Pass `RV32CPU *` explicitly.
- **ASCII source.** English in code and commit messages. Non-English
  discussion is welcome in issues and PR descriptions.

---

## Building & testing

```bash
git clone https://github.com/Grar00t/Haven_OS.git
cd Haven_OS

make            # native CLI: ./haven (~65 KB)
make test       # 19/19 must pass
make web        # Wasm build (needs emcc)
make kernel     # rebuild kernel.bin (needs RV32 cross-toolchain)
make clean
```

If you don't have a RISC-V cross-compiler, you can still develop the
emulator and the host: the kernel is checked in pre-built at
`src/kernel_data.c`.

---

## Where things live

| Want to change…                       | Edit…                  |
|---------------------------------------|------------------------|
| Decoder, memory map, or new RV ext    | `src/riscv.c`          |
| Public emulator API or constants      | `src/riscv.h`          |
| Native terminal UX (raw mode, signals)| `src/host_cli.c`       |
| Web bridge / Emscripten exports       | `src/main.c` + Makefile|
| Shell command, FS layout, banner      | `kernel/kernel.c`      |
| Kernel memory layout / sections       | `kernel/link.ld`       |
| Web shell UI (boot screen, input)     | `web/index.html`       |
| Smoke test cases                      | `tests/run_smoke.sh`   |

---

## PR workflow

1. Fork `Grar00t/Haven_OS` and branch:
   `git checkout -b feat/short-description`
2. Make one logical change per PR — don't bundle unrelated edits.
3. Run `make clean && make && make test`. All 19 tests must pass.
4. Commit with scoped messages:
   ```
   feat(emulator): add C-extension decoder
   fix(kernel): handle backspace at column 0
   docs(readme): correct iOS install steps
   refactor(host): extract raw-mode setup
   ```
5. Push to your fork and open a PR against `main` describing:
   - What changed
   - Why it changed
   - Pasted output of `make test`

---

## Adding a new RISC-V instruction

1. Add the opcode to the decoder switch in `src/riscv.c`.
2. Add a tiny test in `tests/run_smoke.sh` that exercises it through the
   kernel (or add a unit test under `tests/`).
3. Update `README.md` if the supported ISA string changes
   (currently `rv32ima`).

## Adding a new shell command

1. Implement `cmd_xxx(const char *arg)` in `kernel/kernel.c`.
2. Wire it into `process_cmd()`'s dispatch chain.
3. Add it to `cmd_help()` so users can find it.
4. Add a smoke test asserting expected output.
5. Rebuild the kernel (`make kernel embed`) and the host (`make`).

---

## Filing bugs

Please include:

- Architecture (`uname -m`)
- OS / device (Linux, macOS, iOS Safari, Android Chrome, …)
- The exact command you ran
- The full failing output

For security issues, see [`SECURITY.md`](SECURITY.md) — don't open a
public issue.

---

## License

By contributing you agree to license your contribution under
**Apache License 2.0** (see `LICENSE`).
