/*
 * host_cli.c — native host for the HAVEN RV32IMA emulator.
 *
 * Lets you boot kernel.bin in a terminal without a browser. Used by:
 *   - tests/run_smoke.sh  (regression runner)
 *   - developers iterating on the kernel without rebuilding the Wasm bundle
 *
 * Build:  make haven         (top-level Makefile)
 * Usage:  ./haven [kernel.bin]
 *
 * If no path is given, the embedded kernel_bin[] from src/kernel_data.c is
 * used — exactly the same image the web build runs.
 */

#include "riscv.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

/* ── Embedded kernel (defined in kernel_data.c) ───────────────────── */
extern const unsigned char kernel_bin[];
extern const unsigned int  kernel_bin_len;

/* ── Globals (small program, no PIMPL needed) ─────────────────────── */
static struct termios g_orig_termios;
static int            g_termios_saved = 0;
static RV32CPU       *g_cpu           = NULL;

/* ── Terminal handling ────────────────────────────────────────────── */
static void restore_termios(void)
{
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_termios_saved = 0;
    }
}

static int enable_raw_mode(void)
{
    if (!isatty(STDIN_FILENO)) return 0; /* not interactive — leave as is */

    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) return -1;
    g_termios_saved = 1;
    atexit(restore_termios);

    struct termios raw = g_orig_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cc[VMIN]  = 0;  /* non-blocking read */
    raw.c_cc[VTIME] = 0;
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void on_signal(int sig)
{
    (void)sig;
    restore_termios();
    fputs("\r\n[haven] interrupted\r\n", stderr);
    _exit(130);
}

/* ── UART output callback (referenced by riscv.c) ─────────────────── */
void uart_output(uint8_t ch)
{
    /* Cooked mode would re-translate; we already turned OPOST off. */
    fputc((int)ch, stdout);
    fflush(stdout);
}

/* ── Helpers ──────────────────────────────────────────────────────── */
static int read_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    struct stat st;
    if (stat(path, &st) != 0) { fclose(fp); return -1; }

    uint8_t *buf = malloc((size_t)st.st_size);
    if (!buf) { fclose(fp); return -1; }

    size_t n = fread(buf, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (n != (size_t)st.st_size) { free(buf); return -1; }

    *out     = buf;
    *out_len = n;
    return 0;
}

static int poll_stdin_byte(uint8_t *out)
{
    unsigned char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) {
        *out = c;
        return 1;
    }
    return 0;
}

/* ── Main ─────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    const uint8_t *image     = kernel_bin;
    uint32_t       image_len = kernel_bin_len;
    uint8_t       *loaded    = NULL;

    if (argc > 1) {
        size_t n = 0;
        if (read_file(argv[1], &loaded, &n) != 0) {
            fprintf(stderr, "haven: cannot read %s: %s\n", argv[1], strerror(errno));
            return 1;
        }
        image     = loaded;
        image_len = (uint32_t)n;
        fprintf(stderr, "[haven] loaded %s (%u bytes)\n", argv[1], image_len);
    } else {
        fprintf(stderr, "[haven] booting embedded kernel (%u bytes)\n", image_len);
    }

    g_cpu = rv32_create();
    if (!g_cpu) {
        fprintf(stderr, "haven: out of memory\n");
        free(loaded);
        return 1;
    }

    rv32_reset(g_cpu, RAM_BASE);
    rv32_load_binary(g_cpu, image, image_len, RAM_BASE);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    if (enable_raw_mode() != 0) {
        fprintf(stderr, "haven: failed to enter raw mode (continuing in cooked mode)\n");
    }

    /* If stdin isn't a tty (piped / redirected), read everything up front
     * and feed it byte-by-byte between cycle batches. We also bound total
     * cycles so a script that doesn't include `exit` still terminates. */
    int stdin_is_tty = isatty(STDIN_FILENO);
    uint8_t *prefeed     = NULL;
    size_t   prefeed_len = 0;
    size_t   prefeed_pos = 0;
    if (!stdin_is_tty) {
        size_t cap = 4096;
        prefeed = malloc(cap);
        if (prefeed) {
            uint8_t buf[1024];
            ssize_t n;
            while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
                if (prefeed_len + (size_t)n > cap) {
                    cap *= 2;
                    uint8_t *grown = realloc(prefeed, cap);
                    if (!grown) break;
                    prefeed = grown;
                }
                memcpy(prefeed + prefeed_len, buf, (size_t)n);
                prefeed_len += (size_t)n;
            }
        }
    }

    /* In non-interactive mode cap total cycles to avoid hangs from kernels
     * that loop on UART poll. ~50M cycles is plenty for the smoke suite. */
    const uint64_t cycle_budget   = stdin_is_tty ? 0 : 50ULL * 1000ULL * 1000ULL;
    int            idle_no_input  = 0;

    /* Main loop: feed stdin into the UART, run a batch of cycles, repeat. */
    while (!g_cpu->halted) {
        int fed = 0;

        if (stdin_is_tty) {
            uint8_t ch;
            while (poll_stdin_byte(&ch)) {
                /* Ctrl-A then 'x' = quit (screen-style escape). */
                static int armed = 0;
                if (armed) {
                    if (ch == 'x' || ch == 'X') goto stop;
                    armed = 0;
                } else if (ch == 0x01) { /* Ctrl-A */
                    armed = 1;
                    continue;
                }
                rv32_uart_input(g_cpu, ch);
                fed = 1;
            }
        } else if (prefeed && prefeed_pos < prefeed_len) {
            /* Drip-feed the script: one byte per batch. The kernel needs
             * cycles between bytes to echo and process each character. */
            if (!g_cpu->uart_rx_ready) {
                rv32_uart_input(g_cpu, prefeed[prefeed_pos++]);
                fed = 1;
            }
        } else if (!stdin_is_tty) {
            /* Script exhausted — give the kernel a moment to flush, then stop. */
            if (++idle_no_input > 3) goto stop;
        }
        (void)fed;

        rv32_run(g_cpu, 100000);

        if (cycle_budget && g_cpu->cycle_count >= cycle_budget) {
            fprintf(stderr, "\r\n[haven] cycle budget reached (%llu) — stopping\r\n",
                    (unsigned long long)cycle_budget);
            break;
        }
    }

    free(prefeed);

stop:
    rv32_destroy(g_cpu);
    free(loaded);
    fputs("\r\n[haven] halted\r\n", stderr);
    return 0;
}
