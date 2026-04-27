/*
 * kernel.c — HAVEN Micro-Kernel
 * Runs on our RISC-V emulator. No Linux needed.
 * Implements: UART I/O, shell, filesystem, basic commands
 * Dragon403 × Niyah · Riyadh 2026
 */

/* ── UART I/O (memory-mapped) ── */
#define UART_BASE 0x10000000U
#define UART_THR  (*(volatile char*)(UART_BASE + 0))
#define UART_LSR  (*(volatile char*)(UART_BASE + 5))

static void putchar_uart(char c) {
    while (!(UART_LSR & 0x20)) {}
    UART_THR = c;
}

static int getchar_uart(void) {
    if (UART_LSR & 0x01) {
        return UART_THR;
    }
    return -1;
}

static void print(const char *s) {
    while (*s) {
        if (*s == '\n') putchar_uart('\r');
        putchar_uart(*s++);
    }
}

static void print_num(unsigned int n) {
    char buf[12];
    int i = 0;
    if (n == 0) { putchar_uart('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) putchar_uart(buf[--i]);
}

/* ── String Helpers ── */
static int str_len(const char *s) {
    int n = 0; while (s[n]) n++;
    return n;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_starts(const char *str, const char *pre) {
    while (*pre) {
        if (*str++ != *pre++) return 0;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ── Mini Filesystem (in RAM) ── */
#define MAX_FILES 64
#define MAX_NAME  32
#define MAX_CONTENT 4096

typedef struct {
    char name[MAX_NAME];
    char content[MAX_CONTENT];
    int size;
    int is_dir;
    int parent; /* index of parent dir, -1 for root */
    int used;
} FileNode;

static FileNode fs[MAX_FILES];
static int cwd = 0; /* current working directory index */

static void fs_init(void) {
    /* Clear all */
    for (int i = 0; i < MAX_FILES; i++) fs[i].used = 0;
    
    /* Root directory */
    str_copy(fs[0].name, "/", MAX_NAME);
    fs[0].is_dir = 1;
    fs[0].parent = -1;
    fs[0].used = 1;
    
    /* /home */
    str_copy(fs[1].name, "home", MAX_NAME);
    fs[1].is_dir = 1; fs[1].parent = 0; fs[1].used = 1;
    
    /* /home/dragon403 */
    str_copy(fs[2].name, "dragon403", MAX_NAME);
    fs[2].is_dir = 1; fs[2].parent = 1; fs[2].used = 1;
    
    /* /etc */
    str_copy(fs[3].name, "etc", MAX_NAME);
    fs[3].is_dir = 1; fs[3].parent = 0; fs[3].used = 1;
    
    /* /etc/hostname */
    str_copy(fs[4].name, "hostname", MAX_NAME);
    str_copy(fs[4].content, "haven-os\n", MAX_CONTENT);
    fs[4].size = 9; fs[4].parent = 3; fs[4].used = 1;
    
    /* /etc/os-release */
    str_copy(fs[5].name, "os-release", MAX_NAME);
    str_copy(fs[5].content,
        "NAME=\"HAVEN OS\"\nVERSION=\"4.0.0\"\nID=haven\n"
        "PRETTY_NAME=\"HAVEN OS 4.0.0 (Sovereign)\"\n"
        "ARCH=riscv32\nBUILDER=Dragon403\n", MAX_CONTENT);
    fs[5].size = str_len(fs[5].content);
    fs[5].parent = 3; fs[5].used = 1;
    
    /* /home/dragon403/hello.c */
    str_copy(fs[6].name, "hello.c", MAX_NAME);
    str_copy(fs[6].content,
        "#include <stdio.h>\n"
        "int main() {\n"
        "    printf(\"Hello from HAVEN OS!\\n\");\n"
        "    return 0;\n"
        "}\n", MAX_CONTENT);
    fs[6].size = str_len(fs[6].content);
    fs[6].parent = 2; fs[6].used = 1;
    
    /* /proc */
    str_copy(fs[7].name, "proc", MAX_NAME);
    fs[7].is_dir = 1; fs[7].parent = 0; fs[7].used = 1;
    
    /* /proc/cpuinfo */
    str_copy(fs[8].name, "cpuinfo", MAX_NAME);
    str_copy(fs[8].content,
        "processor\t: 0\n"
        "hart\t\t: 0\n"
        "isa\t\t: rv32ima\n"
        "mmu\t\t: none\n"
        "uarch\t\t: haven-rv32\n"
        "builder\t\t: Dragon403\n", MAX_CONTENT);
    fs[8].size = str_len(fs[8].content);
    fs[8].parent = 7; fs[8].used = 1;
    
    cwd = 2; /* Start in /home/dragon403 */
}

static int fs_find_child(int parent, const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs[i].used && fs[i].parent == parent && str_eq(fs[i].name, name))
            return i;
    }
    return -1;
}

static int fs_alloc(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs[i].used) return i;
    }
    return -1;
}

/* ── Shell ── */
#define CMD_MAX 256
static char cmd_buf[CMD_MAX];
static int cmd_pos = 0;

static void print_prompt(void) {
    print("\033[32mdragon403\033[0m@\033[36mhaven-os\033[0m:");
    /* Print current path */
    char path[128] = "";
    int idx = cwd;
    int depth = 0;
    int stack[16];
    while (idx > 0) { stack[depth++] = idx; idx = fs[idx].parent; }
    if (depth == 0) {
        print("/");
    } else {
        for (int i = depth - 1; i >= 0; i--) {
            print("/");
            print(fs[stack[i]].name);
        }
    }
    print("$ ");
}

/* ── Command Handlers ── */
static void cmd_help(void) {
    print("HAVEN OS Shell — Built-in Commands:\n");
    print("  ls        List files\n");
    print("  cd DIR    Change directory\n");
    print("  pwd       Print working directory\n");
    print("  cat FILE  Show file contents\n");
    print("  echo TEXT Write text (> FILE to save)\n");
    print("  touch F   Create empty file\n");
    print("  mkdir DIR Create directory\n");
    print("  rm FILE   Remove file\n");
    print("  whoami    Current user\n");
    print("  uname -a  System info\n");
    print("  uptime    System uptime\n");
    print("  free      Memory info\n");
    print("  clear     Clear screen\n");
    print("  help      This message\n");
    print("  neofetch  System info (fancy)\n");
}

static void cmd_ls(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs[i].used && fs[i].parent == cwd) {
            if (fs[i].is_dir)
                print("\033[34m");
            else
                print("\033[0m");
            print(fs[i].name);
            if (fs[i].is_dir) print("/");
            print("\033[0m  ");
        }
    }
    print("\n");
}

static int resolve_path(const char *path) {
    if (!path || !path[0]) return cwd;
    int node = (path[0] == '/') ? 0 : cwd;
    char part[MAX_NAME];
    int pi = 0;
    const char *p = (path[0] == '/') ? path + 1 : path;
    while (1) {
        if (*p == '/' || *p == 0) {
            part[pi] = 0;
            if (pi > 0) {
                if (str_eq(part, "..")) {
                    if (fs[node].parent >= 0) node = fs[node].parent;
                } else if (!str_eq(part, ".")) {
                    int child = fs_find_child(node, part);
                    if (child < 0) return -1;
                    node = child;
                }
            }
            pi = 0;
            if (*p == 0) break;
            p++;
        } else {
            if (pi < MAX_NAME - 1) part[pi++] = *p;
            p++;
        }
    }
    return node;
}

static void cmd_cat(const char *arg) {
    int idx = resolve_path(arg);
    if (idx < 0) {
        print("cat: "); print(arg); print(": No such file\n");
        return;
    }
    if (fs[idx].is_dir) {
        print("cat: "); print(arg); print(": Is a directory\n");
        return;
    }
    print(fs[idx].content);
}

static void cmd_cd(const char *arg) {
    if (!arg[0] || str_eq(arg, "~")) { cwd = 2; return; }
    int idx = resolve_path(arg);
    if (idx < 0) {
        print("cd: "); print(arg); print(": No such directory\n");
        return;
    }
    if (!fs[idx].is_dir) {
        print("cd: "); print(arg); print(": Not a directory\n");
        return;
    }
    cwd = idx;
}

static void cmd_touch(const char *arg) {
    if (fs_find_child(cwd, arg) >= 0) return; /* already exists */
    int idx = fs_alloc();
    if (idx < 0) { print("touch: No space left\n"); return; }
    str_copy(fs[idx].name, arg, MAX_NAME);
    fs[idx].content[0] = 0;
    fs[idx].size = 0;
    fs[idx].is_dir = 0;
    fs[idx].parent = cwd;
    fs[idx].used = 1;
}

static void cmd_mkdir(const char *arg) {
    if (fs_find_child(cwd, arg) >= 0) {
        print("mkdir: "); print(arg); print(": Already exists\n");
        return;
    }
    int idx = fs_alloc();
    if (idx < 0) { print("mkdir: No space left\n"); return; }
    str_copy(fs[idx].name, arg, MAX_NAME);
    fs[idx].is_dir = 1;
    fs[idx].parent = cwd;
    fs[idx].used = 1;
}

static void cmd_rm(const char *arg) {
    int idx = fs_find_child(cwd, arg);
    if (idx < 0) {
        print("rm: "); print(arg); print(": No such file\n");
        return;
    }
    fs[idx].used = 0;
}

static void cmd_neofetch(void) {
    print("\033[32m");
    print("  ██╗  ██╗ █████╗ ██╗   ██╗███████╗███╗   ██╗\n");
    print("  ██║  ██║██╔══██╗██║   ██║██╔════╝████╗  ██║\n");
    print("  ███████║███████║██║   ██║█████╗  ██╔██╗ ██║\n");
    print("  ██╔══██║██╔══██║╚██╗ ██╔╝██╔══╝  ██║╚██╗██║\n");
    print("  ██║  ██║██║  ██║ ╚████╔╝ ███████╗██║ ╚████║\n");
    print("  ╚═╝  ╚═╝╚═╝  ╚═╝  ╚═══╝  ╚══════╝╚═╝  ╚═══╝\n");
    print("\033[0m\n");
    print("  \033[32mOS:\033[0m      HAVEN OS 4.0.0 (Sovereign)\n");
    print("  \033[32mArch:\033[0m    RISC-V 32-bit (rv32ima)\n");
    print("  \033[32mKernel:\033[0m  haven-kernel 1.0 (Wasm)\n");
    print("  \033[32mShell:\033[0m   haven-sh 1.0\n");
    print("  \033[32mCPU:\033[0m     HAVEN-RV32 @ Wasm\n");
    print("  \033[32mRAM:\033[0m     16 MB\n");
    print("  \033[32mBuilder:\033[0m Dragon403 x Niyah\n");
    print("  \033[32mCity:\033[0m    Riyadh, Saudi Arabia\n");
    print("\n  \033[31m███\033[33m███\033[32m███\033[36m███\033[34m███\033[35m███\033[0m\n\n");
}

static void cmd_echo(const char *arg) {
    /* Check for redirect: echo text > file */
    const char *p = arg;
    const char *redir = 0;
    while (*p) {
        if (*p == '>') { redir = p; break; }
        p++;
    }
    
    if (redir) {
        /* Get filename after > */
        const char *fname = redir + 1;
        while (*fname == ' ') fname++;
        
        /* Get text before > */
        int text_len = (int)(redir - arg);
        while (text_len > 0 && arg[text_len-1] == ' ') text_len--;
        
        /* Find or create file */
        int idx = fs_find_child(cwd, fname);
        if (idx < 0) {
            idx = fs_alloc();
            if (idx < 0) { print("echo: No space\n"); return; }
            str_copy(fs[idx].name, fname, MAX_NAME);
            fs[idx].is_dir = 0;
            fs[idx].parent = cwd;
            fs[idx].used = 1;
        }
        
        /* Write content */
        int i;
        for (i = 0; i < text_len && i < MAX_CONTENT - 2; i++)
            fs[idx].content[i] = arg[i];
        fs[idx].content[i++] = '\n';
        fs[idx].content[i] = 0;
        fs[idx].size = i;
    } else {
        print(arg);
        print("\n");
    }
}

/* ── Process Command ── */
static void process_cmd(void) {
    /* Skip leading spaces */
    char *cmd = cmd_buf;
    while (*cmd == ' ') cmd++;
    if (*cmd == 0) return;
    
    /* Split command and argument */
    char *arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg = 0; arg++; }
    while (*arg == ' ') arg++;
    
    /* Dispatch */
    if (str_eq(cmd, "help"))         cmd_help();
    else if (str_eq(cmd, "ls"))      cmd_ls();
    else if (str_eq(cmd, "cat"))     cmd_cat(arg);
    else if (str_eq(cmd, "cd"))      cmd_cd(arg);
    else if (str_eq(cmd, "pwd")) {
        int idx = cwd;
        int depth = 0;
        int stack[16];
        while (idx > 0) { stack[depth++] = idx; idx = fs[idx].parent; }
        if (depth == 0) print("/");
        else for (int i = depth - 1; i >= 0; i--) { print("/"); print(fs[stack[i]].name); }
        print("\n");
    }
    else if (str_eq(cmd, "touch"))   cmd_touch(arg);
    else if (str_eq(cmd, "mkdir"))   cmd_mkdir(arg);
    else if (str_eq(cmd, "rm"))      cmd_rm(arg);
    else if (str_eq(cmd, "echo"))    cmd_echo(arg);
    else if (str_eq(cmd, "whoami"))  print("dragon403\n");
    else if (str_eq(cmd, "hostname")) print("haven-os\n");
    else if (str_eq(cmd, "uname")) {
        print("HAVEN OS 4.0.0 haven-os rv32ima haven-kernel 1.0 RISC-V\n");
    }
    else if (str_eq(cmd, "uptime")) {
        print("up 0 min, 1 user, load: 0.00\n");
    }
    else if (str_eq(cmd, "free")) {
        print("       total    used    free\n");
        print("Mem:   16384K   1024K   15360K\n");
        print("Swap:      0K      0K       0K\n");
    }
    else if (str_eq(cmd, "clear")) {
        print("\033[2J\033[H");
    }
    else if (str_eq(cmd, "neofetch")) cmd_neofetch();
    else if (str_eq(cmd, "exit")) {
        print("Halting system.\n");
        /* EBREAK — the native host treats this as a clean shutdown.
         * On the web build the emulator just traps and the JS layer can
         * decide what to do; either way we never spin a busy loop. */
        __asm__ volatile ("ebreak");
        while(1) { __asm__ volatile ("wfi"); } /* unreachable on host */
    }
    else {
        print(cmd);
        print(": command not found\n");
    }
}

/* ── Main ── */
void _start(void) __attribute__((section(".text.boot")));
void _start(void) {
    fs_init();
    
    print("\033[2J\033[H"); /* clear screen */
    print("\033[32m");
    print("HAVEN OS 4.0.0 (rv32ima) — Sovereign Kernel\n");
    print("Built from scratch by Dragon403 x Niyah\n");
    print("Running on HAVEN-RV32 Wasm Emulator\n");
    print("\033[36m");
    print("Zero Cloud. Zero Dependencies. 100% Local.\n");
    print("\033[0m");
    print("Type 'help' for commands.\n\n");
    
    print_prompt();
    
    /* Main loop — poll UART */
    while (1) {
        int ch = getchar_uart();
        if (ch < 0) continue;
        
        if (ch == '\r' || ch == '\n') {
            print("\n");
            cmd_buf[cmd_pos] = 0;
            process_cmd();
            cmd_pos = 0;
            print_prompt();
        } else if (ch == 127 || ch == 8) { /* Backspace */
            if (cmd_pos > 0) {
                cmd_pos--;
                print("\b \b");
            }
        } else if (cmd_pos < CMD_MAX - 1) {
            cmd_buf[cmd_pos++] = (char)ch;
            putchar_uart((char)ch);
        }
    }
}
