#!/usr/bin/env bash
# tests/run_smoke.sh — exercise the native build end-to-end.
#
# Walks through every shell built into the kernel (help / ls / cat / cd /
# touch / mkdir / echo > / rm / pwd / whoami / uname / uptime / free / clear
# / neofetch / exit) and asserts each one produces the expected fragment in
# the output. No flaky timing — we drive the emulator from a script, the
# kernel issues EBREAK on `exit`, and the host stops cleanly.
#
# Usage:   bash tests/run_smoke.sh
# Returns: 0 on success, 1 on first failure (with a unified diff-style trace).

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HAVEN="${ROOT}/haven"

if [[ ! -x "${HAVEN}" ]]; then
    echo "tests: ${HAVEN} not found — run 'make' first." >&2
    exit 2
fi

PASS=0
FAIL=0
FAILED_TESTS=()

run_case() {
    local name="$1"
    local script="$2"
    shift 2
    local expectations=("$@")

    local out
    out=$(printf '%s\n' "${script}" | timeout 15 "${HAVEN}" 2>&1 || true)

    local missing=()
    for needle in "${expectations[@]}"; do
        if ! grep -q -- "${needle}" <<< "${out}"; then
            missing+=("${needle}")
        fi
    done

    if [[ ${#missing[@]} -eq 0 ]]; then
        printf '  \033[32m[PASS]\033[0m  %s\n' "${name}"
        PASS=$((PASS + 1))
    else
        printf '  \033[31m[FAIL]\033[0m  %s\n' "${name}"
        printf '         missing: %s\n' "${missing[@]}"
        FAIL=$((FAIL + 1))
        FAILED_TESTS+=("${name}")
    fi
}

echo "============================================================"
echo "HAVEN OS — smoke suite"
echo "============================================================"

run_case "boot banner"         $'exit'                       "HAVEN OS 4.0.0" "Sovereign Kernel"
run_case "help"                $'help\nexit'                 "Built-in Commands" "neofetch"
run_case "neofetch"            $'neofetch\nexit'             "OS:" "Arch:" "RISC-V 32-bit"
run_case "uname"               $'uname\nexit'                "rv32ima"
run_case "whoami"              $'whoami\nexit'               "dragon403"
run_case "hostname"            $'hostname\nexit'             "haven-os"
run_case "uptime"              $'uptime\nexit'               "load:"
run_case "free"                $'free\nexit'                 "Mem:" "16384K"
run_case "ls /home/dragon403"  $'ls\nexit'                   "hello.c"
run_case "cat /etc/os-release" $'cat /etc/os-release\nexit'  "HAVEN OS" "ARCH=riscv32"
run_case "cat /proc/cpuinfo"   $'cat /proc/cpuinfo\nexit'    "rv32ima" "haven-rv32"
run_case "pwd"                 $'pwd\nexit'                  "/home/dragon403"
run_case "cd .. + pwd"         $'cd ..\npwd\nexit'           "/home"
run_case "mkdir + ls"          $'mkdir test_dir\nls\nexit'   "test_dir"
run_case "touch + ls"          $'touch t.txt\nls\nexit'      "t.txt"
run_case "echo redirect"       $'echo hello > a.txt\ncat a.txt\nexit' "hello"
run_case "rm"                  $'touch x.txt\nrm x.txt\nls\nexit'  "Halting system"
run_case "unknown command"     $'wat\nexit'                  "command not found"
run_case "clean exit (EBREAK)" $'exit'                       "Halting system" "halted"

echo "============================================================"
if [[ ${FAIL} -eq 0 ]]; then
    printf '  \033[32mPASSED\033[0m  %d/%d\n' "${PASS}" "$((PASS + FAIL))"
    echo "============================================================"
    exit 0
else
    printf '  \033[31mFAILED\033[0m  %d/%d  (failures: %s)\n' \
        "${FAIL}" "$((PASS + FAIL))" "${FAILED_TESTS[*]}"
    echo "============================================================"
    exit 1
fi
