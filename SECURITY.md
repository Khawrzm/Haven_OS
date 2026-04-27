# Security Policy

## Reporting a vulnerability

If you find a security issue in HAVEN OS — memory corruption in the
emulator, an escape from the sandbox, an issue in the web build that
exposes the host page, etc. — **please don't open a public issue**.

Email the maintainer: **cartier403c@gmail.com**

Include:

- A short description of the issue
- Steps to reproduce (a minimal `kernel.bin` or input sequence is ideal)
- The architecture and build (native CLI / web / both)
- Any proposed mitigation

You should hear back within 72 hours. Once a fix is ready and shipped,
we'll credit you in the release notes (or keep you anonymous — your call).

---

## Threat model

HAVEN OS runs guest RV32IMA code inside an emulator. The interesting
boundaries are:

| Boundary                      | What it protects                                |
|-------------------------------|-------------------------------------------------|
| Guest RAM ↔ host memory       | Guest can only access RAM/UART/CLINT regions.   |
| Guest ↔ web page (Wasm)       | Wasm sandbox + browser process isolation.       |
| Guest ↔ host filesystem       | None — guest cannot reach the host FS today.    |

The emulator currently runs in M-mode only (no S/U separation inside the
guest), so guest "user processes" are not isolated from the guest kernel.
That's a known limitation tracked in the roadmap.

---

## Known non-issues

- **Guest can busy-loop forever.** Yes — the native host caps cycles in
  non-interactive mode and the web build can stop the worker. EBREAK
  halts cleanly.
- **Filesystem is in-RAM and wipes on reboot.** Intentional. Persistence
  is a roadmap item.
- **No telemetry, no analytics, no network.** This is the design.
