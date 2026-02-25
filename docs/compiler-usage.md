# UA Compiler Usage Guide

This document covers building the UA compiler, command-line usage, output formats, and practical examples.

---

## Table of Contents

1. [Building the Compiler](#building-the-compiler)
2. [Command-Line Syntax](#command-line-syntax)
3. [Flags Reference](#flags-reference)
4. [Output Formats](#output-formats)
5. [Usage Examples](#usage-examples)
6. [Exit Codes](#exit-codes)
7. [Error Messages](#error-messages)
8. [Troubleshooting](#troubleshooting)

---

## Building the Compiler

UA is written in pure C99 with no external dependencies. A single compiler invocation builds the entire project.

### GCC (Linux / macOS / MSYS2 on Windows)

```bash
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o UA \
    main.c lexer.c parser.c codegen.c \
    backend_8051.c backend_x86_64.c emitter_pe.c emitter_elf.c
```

### GCC on Windows (producing UA.exe)

```bash
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o UA.exe ^
    main.c lexer.c parser.c codegen.c ^
    backend_8051.c backend_x86_64.c emitter_pe.c emitter_elf.c
```

### Clang

```bash
cd src
clang -std=c99 -Wall -Wextra -pedantic -o UA \
    main.c lexer.c parser.c codegen.c \
    backend_8051.c backend_x86_64.c emitter_pe.c emitter_elf.c
```

### MSVC

```cmd
cd src
cl /std:c11 /W4 /Fe:UA.exe ^
    main.c lexer.c parser.c codegen.c ^
    backend_8051.c backend_x86_64.c emitter_pe.c emitter_elf.c
```

**Source files:** 7 `.c` files, 6 `.h` headers  
**Output:** `UA` (or `UA.exe` on Windows)  
**Requirements:** Any C99-conformant compiler

---

## Command-Line Syntax

```
UA <input> -arch <architecture> [-o <output>] [-sys <system>] [--run]
```

All flags can appear in any order, but the input file must be present.

---

## Flags Reference

| Flag | Argument | Required | Default | Description |
|------|----------|----------|---------|-------------|
| *(positional)* | `<input>` | **Yes** | — | Path to the `.UA` source file |
| `-arch` | `x86` \| `mcs51` | **Yes** | — | Target architecture |
| `-o` | `<path>` | No | `a.out` or `a.exe` | Output file path |
| `-sys` | `baremetal` \| `win32` \| `linux` | No | *(none)* | Target operating system |
| `--run` | — | No | off | JIT-execute the generated code |

### `-arch` — Target Architecture

| Value | Architecture | Word Size | Description |
|-------|-------------|-----------|-------------|
| `x86` | Intel x86-64 | 64-bit | Desktop / server processors |
| `mcs51` | Intel 8051 | 8-bit | Embedded microcontrollers |

### `-o` — Output File

Sets the output file path. Defaults:

- **Without `-sys`:** `a.out`
- **With `-sys win32`:** `a.exe`
- **With `-sys linux`:** `a.elf`

### `-sys` — Target System

| Value | Effect |
|-------|--------|
| `baremetal` | Raw binary output (same as no `-sys`) |
| `win32` | Wraps code in a Windows PE executable (.exe) |
| `linux` | Wraps code in a Linux ELF executable |

`-sys win32` and `-sys linux` require `-arch x86`.

### `--run` — JIT Execution

Assembles the code and immediately executes it in memory. Available only with `-arch x86`.

- On **Windows**: uses `VirtualAlloc` with `PAGE_EXECUTE_READWRITE`
- On **POSIX**: uses `mmap` with `PROT_READ | PROT_WRITE | PROT_EXEC`

After execution, the return value in RAX (R0) is printed.

---

## Output Formats

### Raw Binary

The default output mode. Produces a flat file containing only machine code bytes — no headers, no metadata.

```bash
UA program.UA -arch x86 -o code.bin
UA firmware.UA -arch mcs51 -o firmware.bin
```

Use cases:
- Flashing to 8051 microcontrollers
- Loading into emulators or debuggers
- Embedding in bootloaders

### Windows PE Executable

Produces a minimal 64-bit Windows console application. The PE file includes:

- DOS header (with "MZ" signature)
- PE signature ("PE\0\0")
- COFF file header (AMD64 machine type)
- Optional header (PE32+ format, console subsystem)
- Single `.text` section containing the assembled code

```bash
UA program.UA -arch x86 -sys win32 -o program.exe
```

The generated `.exe` can be run directly on 64-bit Windows. The process exit code equals the value left in RAX (R0) when `HLT` (`RET`) executes.

**Technical details:**
- ImageBase: `0x00400000`
- EntryPoint RVA: `0x1000`
- FileAlignment: `0x200` (512 bytes)
- SectionAlignment: `0x1000` (4096 bytes)
- Subsystem: `IMAGE_SUBSYSTEM_WINDOWS_CUI` (console)

### Linux ELF Executable

Produces a minimal 64-bit Linux ELF executable. The file includes:

- ELF64 header (magic `\x7FELF`)
- Single `PT_LOAD` program header mapping the entire file as read+execute
- A call stub that invokes the user code
- An exit stub that calls `sys_exit` with the value in RAX (R0)

```bash
UA program.UA -arch x86 -sys linux -o program.elf
```

The generated ELF can be run directly on 64-bit Linux. The process exit code equals the value left in RAX (R0) when `HLT` executes.

**Technical details:**
- Base address: `0x00400000`
- Entry point: `0x00400078` (immediately after headers)
- Segment alignment: 2 MB (`0x200000`)
- Exit mechanism: `mov rdi, rax; mov eax, 60; syscall` (Linux `__NR_exit`)

### JIT Execution

Assembles code and executes it directly in memory without writing a file.

```bash
UA program.UA -arch x86 --run
```

Output includes:
1. Hex dump of generated machine code
2. The return value of RAX (R0) in decimal and hexadecimal

Example output:

```
  0000: 48 C7 C0 2A 00 00 00 C3  |H..*....|
  RAX (R0) = 42  (0x2A)
```

---

## Usage Examples

### Example 1: Hello World (sort of)

The simplest useful program — returns the value 42:

```asm
; hello.UA — returns 42 in R0
    LDI  R0, 42
    HLT
```

```bash
# JIT execute
UA hello.UA -arch x86 --run
# Output: RAX (R0) = 42  (0x2A)

# Build Windows executable
UA hello.UA -arch x86 -sys win32 -o hello.exe
hello.exe
echo %ERRORLEVEL%
# Output: 42
```

### Example 2: Arithmetic

```asm
; math.UA — compute (10 + 5) * 3 - 2 = 43
    LDI  R0, 10
    LDI  R1, 5
    ADD  R0, R1       ; R0 = 15
    LDI  R1, 3
    MUL  R0, R1       ; R0 = 45
    LDI  R1, 2
    SUB  R0, R1       ; R0 = 43
    HLT
```

```bash
UA math.UA -arch x86 --run
# Output: RAX (R0) = 43  (0x2B)
```

### Example 3: Loop with Conditional Jump

```asm
; count.UA — count from 0 to 99
    LDI  R0, 0
    LDI  R1, 100
loop:
    INC  R0
    CMP  R0, R1
    JNZ  loop
    HLT
```

```bash
UA count.UA -arch x86 --run
# Output: RAX (R0) = 100  (0x64)
```

### Example 4: Subroutine

```asm
; sub.UA — call a function that doubles R0
    LDI  R0, 21
    CALL double
    HLT

double:
    ADD  R0, R0
    RET
```

```bash
UA sub.UA -arch x86 --run
# Output: RAX (R0) = 42  (0x2A)
```

### Example 5: Cross-compile for 8051

```asm
; blink.UA — 8051 firmware skeleton
    LDI  R0, 0
loop:
    INC  R0
    CMP  R0, 0xFF
    JNZ  loop
    HLT
```

```bash
UA blink.UA -arch mcs51 -o blink.bin
# Produces a raw binary for flashing to an 8051 chip
```

### Example 6: Bitwise Operations

```asm
; bits.UA — mask, set, toggle, and complement
    LDI  R0, 0b11111111
    AND  R0, 0x0F       ; R0 = 0x0F (mask lower nibble)
    OR   R0, 0xA0       ; R0 = 0xAF (set upper bits)
    XOR  R0, 0xFF       ; R0 = 0x50 (toggle all bits)
    NOT  R0             ; R0 = ~0x50
    HLT
```

### Example 7: Build a Linux ELF Executable

```asm
; answer.UA — return 42 as exit code
    LDI  R0, 42
    HLT
```

```bash
UA answer.UA -arch x86 -sys linux -o answer.elf
chmod +x answer.elf
./answer.elf
echo $?
# Output: 42
```

The ELF emitter automatically appends a `sys_exit` stub, so `HLT` → `RET` cleanly exits the process with the value in R0.

---

## Exit Codes

The UA compiler itself uses these exit codes:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (parse error, unknown opcode, invalid operands, file I/O failure, etc.) |

When using `--run`, `-sys win32`, or `-sys linux`, the **assembled program's** return value is the value in R0 (RAX) at `HLT`.

---

## Error Messages

UA reports errors with the source line number:

```
[Parser] Error at line 5: unknown mnemonic 'MOOV'
[8051] Error at line 12: register R9 out of range for 8051 (max R7)
[x86-64] Error at line 8: undefined label 'start'
```

Common errors:

| Error | Cause | Fix |
|-------|-------|-----|
| `unknown mnemonic` | Typo in opcode name | Check spelling against the 27 supported opcodes |
| `wrong number of operands` | Incorrect operand count | See the operand shape table in the language reference |
| `expected register` | Non-register where register required | Use `R0`–`R7` |
| `register out of range` | Register index > 7 | Use R0–R7 only |
| `immediate out of range` | Value too large for backend | 8051: -128..255; x86: 32-bit |
| `undefined label` | Jumping to a non-existent label | Define the label somewhere in the file |
| `duplicate label` | Same label defined twice | Rename one of the labels |
| `JZ/JNZ target out of range` | 8051: jump > ±127 bytes | Move the target label closer or restructure code |

---

## Troubleshooting

### "Cannot open input file"

Ensure the file path is correct and the file exists. UA requires the input file as the first positional argument.

### "Unknown architecture"

Only `x86` and `mcs51` are supported. The flag is case-sensitive.

### JIT crashes or hangs

- Ensure your program ends with `HLT` (which generates `RET` on x86-64). Without it, execution will run off the end of the code buffer.
- Avoid corrupting R4 (RSP) — it's the stack pointer.

### PE executable returns wrong exit code

The exit code is the value in R0 (RAX) when `HLT` executes. Windows truncates it to 32 bits and may interpret high values as errors. Stick to 0–255 for predictable results.

### 8051 "size mismatch" warning

This indicates a bug in the instruction size calculation (pass 1 vs. pass 2 disagree). Report it with the `.UA` source file that triggered it.
