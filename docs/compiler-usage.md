# UA Compiler Usage Guide

This document covers building the UA compiler, command-line usage, output formats, and practical examples.

---

## Table of Contents

1. [Building the Compiler](#building-the-compiler)
2. [Command-Line Syntax](#command-line-syntax)
3. [Flags Reference](#flags-reference)
4. [Precompiler Directives](#precompiler-directives)
5. [Output Formats](#output-formats)
6. [Usage Examples](#usage-examples)
7. [Exit Codes](#exit-codes)
8. [Error Messages](#error-messages)
9. [Troubleshooting](#troubleshooting)

---

## Building the Compiler

UA is written in pure C99 with no external dependencies. A single compiler invocation builds the entire project.

### GCC (Linux / macOS / MSYS2 on Windows)

```bash
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o UA \
    main.c lexer.c parser.c codegen.c precompiler.c \
    backend_8051.c backend_x86_64.c backend_x86_32.c backend_arm.c \
    backend_arm64.c backend_risc_v.c \
    emitter_pe.c emitter_elf.c emitter_macho.c
```

### GCC on Windows (producing UA.exe)

```bash
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o UA.exe ^
    main.c lexer.c parser.c codegen.c precompiler.c ^
    backend_8051.c backend_x86_64.c backend_x86_32.c backend_arm.c ^
    backend_arm64.c backend_risc_v.c ^
    emitter_pe.c emitter_elf.c emitter_macho.c
```

### Clang

```bash
cd src
clang -std=c99 -Wall -Wextra -pedantic -o UA \
    main.c lexer.c parser.c codegen.c precompiler.c \
    backend_8051.c backend_x86_64.c backend_x86_32.c backend_arm.c \
    backend_arm64.c backend_risc_v.c \
    emitter_pe.c emitter_elf.c emitter_macho.c
```

### MSVC

```cmd
cd src
cl /std:c11 /W4 /Fe:UA.exe ^
    main.c lexer.c parser.c codegen.c precompiler.c ^
    backend_8051.c backend_x86_64.c backend_x86_32.c backend_arm.c ^
    backend_arm64.c backend_risc_v.c ^
    emitter_pe.c emitter_elf.c emitter_macho.c
```

**Source files:** 15 `.c` files, 14 `.h` headers  
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
| `-arch` | `x86` \| `x86_32` \| `arm` \| `arm64` \| `riscv` \| `mcs51` | **Yes** | — | Target architecture |
| `-o` | `<path>` | No | `a.out` or `a.exe` | Output file path |
| `-sys` | `baremetal` \| `win32` \| `linux` \| `macos` | No | *(none)* | Target operating system |
| `--run` | — | No | off | JIT-execute the generated code |

### `-arch` — Target Architecture

| Value | Architecture | Word Size | Description |
|-------|-------------|-----------|-------------|
| `x86` | Intel x86-64 | 64-bit | Desktop / server processors |
| `x86_32` | Intel x86-32 (IA-32) | 32-bit | 32-bit x86 processors |
| `arm` | ARM ARMv7-A | 32-bit | ARM application processors |
| `arm64` | ARM AArch64 | 64-bit | ARMv8-A 64-bit processors (alias: `aarch64`) |
| `riscv` | RISC-V RV64I+M | 64-bit | RISC-V 64-bit processors (alias: `rv64`) |
| `mcs51` | Intel 8051 | 8-bit | Embedded microcontrollers |

### `-o` — Output File

Sets the output file path. Defaults:

- **Without `-sys`:** `a.out`
- **With `-sys win32`:** `a.exe`
- **With `-sys linux`:** `a.elf`
- **With `-sys macos`:** `a.out` (Mach-O)

### `-sys` — Target System

| Value | Effect |
|-------|--------|
| `baremetal` | Raw binary output (same as no `-sys`) |
| `win32` | Wraps code in a Windows PE executable (.exe) |
| `linux` | Wraps code in a Linux ELF executable |
| `macos` | Wraps code in a macOS Mach-O executable |

`-sys win32` requires `-arch x86` or `-arch x86_32`. Other `-sys` values work with any architecture that has an appropriate emitter.

### `--run` — JIT Execution

Assembles the code and immediately executes it in memory. Available only with `-arch x86`.

- On **Windows**: uses `VirtualAlloc` with `PAGE_EXECUTE_READWRITE`
- On **POSIX**: uses `mmap` with `PROT_READ | PROT_WRITE | PROT_EXEC`

After execution, the return value in RAX (R0) is printed.

---

## Precompiler Directives

Before lexing, the UA precompiler evaluates all lines beginning with `@`.  Directives are processed top-to-bottom, line-by-line.  Blank lines are emitted in place of directives to preserve line numbering for error messages.

### `@IF_ARCH <arch>`

Conditionally include the following lines only when the target architecture matches.  The comparison is **case-insensitive** and matches the value passed to `-arch` exactly.

```asm
@IF_ARCH x86
    MOV R0, R1          ; emitted only for -arch x86
@ENDIF

@IF_ARCH mcs51
    LDI R0, 0xFF        ; emitted only for -arch mcs51
@ENDIF
```

### `@IF_SYS <system>`

Conditionally include the following lines only when the target system matches.  Matches the value passed to `-sys` (case-insensitive).  If `-sys` was not specified, the condition is always **false**.

```asm
@IF_SYS win32
    INT #0x21           ; Windows-specific interrupt
@ENDIF

@IF_SYS linux
    INT #0x80           ; Linux-specific interrupt
@ENDIF
```

### `@ENDIF`

Closes the most recent `@IF_ARCH` or `@IF_SYS` block.  Every `@IF_*` must have a matching `@ENDIF`.

### Nesting

Conditional blocks can be nested up to 64 levels:

```asm
@IF_ARCH x86
    @IF_SYS win32
        ; x86 + Windows only
    @ENDIF
@ENDIF
```

### `@IMPORT <path>`

Include the contents of another `.ua` file at this position.  The imported file is also preprocessed (directives inside it are evaluated).  Each file is imported **at most once** — duplicate `@IMPORT` directives for the same resolved path are silently skipped.

Paths are resolved relative to the importing file's directory.  Both quoted and unquoted forms are accepted:

```asm
@IMPORT "lib/math.ua"
@IMPORT utils.ua
```

Import nesting is limited to 16 levels to prevent circular references.

### `@DUMMY [message]`

Mark a section of code as a stub.  A diagnostic is printed to stderr during compilation.  **No code is emitted.**

```asm
@DUMMY This function is not yet implemented
@DUMMY
```

Output during compilation:

```
[Precompiler] DUMMY program.ua:12: This function is not yet implemented
[Precompiler] DUMMY program.ua:13: (no implementation)
```

### `@ARCH_ONLY <arch1>, <arch2>, ...`

Abort compilation unless the current `-arch` matches **at least one** of the comma-separated architecture names (case-insensitive).  This is a hard guard — compilation stops immediately with an error if the target is not in the list.

```asm
@ARCH_ONLY x86, x86_32    ; only compile for x86 family
SYS                        ; uses native SYSCALL / INT 0x80
```

Error when compiling with `-arch arm`:

```
[Precompiler] file.ua:1: @ARCH_ONLY — current architecture 'arm' is not in
  the supported set [x86, x86_32]
```

Valid architecture names: `x86`, `x86_32`, `arm`, `arm64`, `riscv`, `mcs51`.

### `@SYS_ONLY <sys1>, <sys2>, ...`

Abort compilation unless the current `-sys` matches **at least one** of the comma-separated system names (case-insensitive).  If no `-sys` was specified on the command line, this directive always fails.

```asm
@SYS_ONLY linux, macos     ; POSIX targets only
SYS                        ; uses SYSCALL / SVC #0
```

Error when compiling with `-sys win32`:

```
[Precompiler] file.ua:1: @SYS_ONLY — current system 'win32' is not in
  the supported set [linux, macos]
```

Valid system names: `baremetal`, `win32`, `linux`, `macos`.

### Opcode Compliance Validation

After parsing, and before backend code generation, the compiler runs an **opcode compliance check**.  Every instruction in the IR is verified against a per-opcode table of supported architectures and systems.  If any opcode is not valid for the target, the build fails:

```
  UA Compliance Error
  -------------------
  Line 5: opcode 'SYS' is not supported on architecture 'mcs51'
  Supported architectures: x86, x86_32, arm, arm64, riscv
```

All 37 built-in opcodes are currently universal.  The compliance infrastructure is in place for future architecture-specific instructions.

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
- `.text` section containing the assembled code
- `.idata` section with an Import Directory Table (when Win32 API calls are used)

```bash
UA program.UA -arch x86 -sys win32 -o program.exe
```

The generated `.exe` can be run directly on 64-bit Windows.

**Win32 mode behaviour:**

When `-sys win32` is specified, the x86-64 backend appends runtime dispatcher stubs after the user code:

- **`HLT`** calls `ExitProcess(0)` via `kernel32.dll` instead of `RET`, ensuring clean process termination.
- **`SYS`** calls a write dispatcher that translates the Linux syscall register convention (R0=syscall#, R7=fd, R6=buf, R2=count) into `WriteFile` / `GetStdHandle` API calls. This allows the same UA code to work on both Linux and Windows.

The PE emitter automatically generates the `.idata` section with an Import Address Table (IAT) referencing `kernel32.dll` functions: `GetStdHandle`, `WriteFile`, and `ExitProcess`.

**Technical details:**
- ImageBase: `0x00400000`
- EntryPoint RVA: `0x1000`
- FileAlignment: `0x200` (512 bytes)
- SectionAlignment: `0x1000` (4096 bytes)
- Subsystem: `IMAGE_SUBSYSTEM_WINDOWS_CUI` (console)
- Import table: `kernel32.dll` (GetStdHandle, WriteFile, ExitProcess)

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
UA firmware.UA -arch mcs51 -o blink.bin
# Produces a raw binary for flashing to an 8051 chip
```

### Example 6: Cross-compile for ARM

```asm
; armadd.UA — simple addition on ARM
    LDI  R0, 10
    LDI  R1, 5
    ADD  R0, R1
    HLT
```

```bash
UA armadd.UA -arch arm -o armadd.bin
# Produces a raw ARM binary

UA armadd.UA -arch arm -sys linux -o armadd.elf
# Produces a Linux ELF executable for ARM
```

### Example 7: Cross-compile for x86-32

```asm
; calc32.UA — arithmetic on 32-bit x86
    LDI  R0, 100
    LDI  R1, 50
    ADD  R0, R1
    HLT
```

```bash
UA calc32.UA -arch x86_32 -o calc32.bin
# Produces raw 32-bit x86 machine code

UA calc32.UA -arch x86_32 -sys win32 -o calc32.exe
# Produces a 32-bit Windows PE executable
```

### Example 8: Bitwise Operations

```asm
; bits.UA — mask, set, toggle, and complement
    LDI  R0, 0b11111111
    AND  R0, 0x0F       ; R0 = 0x0F (mask lower nibble)
    OR   R0, 0xA0       ; R0 = 0xAF (set upper bits)
    XOR  R0, 0xFF       ; R0 = 0x50 (toggle all bits)
    NOT  R0             ; R0 = ~0x50
    HLT
```

### Example 9: Build a Linux ELF Executable

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

### Example 10: Conditional Jumps (JL / JG)

```asm
; clamp.UA — clamp R0 to the range [10, 100]
    LDI  R0, 150         ; test value
    LDI  R1, 10
    LDI  R2, 100

    CMP  R0, R1
    JL   too_low          ; if R0 < 10, jump to too_low
    CMP  R0, R2
    JG   too_high         ; if R0 > 100, jump to too_high
    JMP  done

too_low:
    MOV  R0, R1           ; R0 = 10
    JMP  done

too_high:
    MOV  R0, R2           ; R0 = 100

done:
    HLT                   ; R0 = 100
```

```bash
UA clamp.UA -arch x86 --run
# Output: RAX (R0) = 100  (0x64)
```

### Example 11: Buffer Allocation

```asm
; buffer.UA — allocate a buffer and fill the first byte
    BUFFER  data, 32          ; allocate 32 zero-initialized bytes
    GET     R0, data          ; R0 = address of buffer
    LDI     R1, 0x42          ; 'B'
    STOREB  R1, R0            ; write 'B' to first byte
    LOADB   R2, R0            ; R2 = 0x42
    MOV     R0, R2
    HLT                       ; R0 = 0x42
```

```bash
UA buffer.UA -arch x86 --run
# Output: RAX (R0) = 66  (0x42)
```

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
| `unknown mnemonic` | Typo in opcode name | Check spelling against the 37 supported opcodes |
| `wrong number of operands` | Incorrect operand count | See the operand shape table in the language reference |
| `expected register` | Non-register where register required | Use `R0`–`R7` |
| `register out of range` | Register index > 7 | Use R0–R7 only |
| `immediate out of range` | Value too large for backend | 8051: -128..255; x86: 32-bit |
| `undefined label` | Jumping to a non-existent label | Define the label somewhere in the file |
| `duplicate label` | Same label defined twice | Rename one of the labels |
| `JZ/JNZ/JL target out of range` | 8051: jump > ±127 bytes | Move the target label closer or restructure code |

---

## Troubleshooting

### "Cannot open input file"

Ensure the file path is correct and the file exists. UA requires the input file as the first positional argument.

### "Unknown architecture"

Supported values: `x86`, `x86_32` (or `ia32`), `arm`, `arm64` (or `aarch64`), `riscv` (or `rv64`), and `mcs51`. The flag is case-insensitive.

### JIT crashes or hangs

- Ensure your program ends with `HLT` (which generates `RET` on x86-64). Without it, execution will run off the end of the code buffer.
- Avoid corrupting R4 (RSP) — it's the stack pointer.

### PE executable returns wrong exit code

On win32, `HLT` calls `ExitProcess(0)` so the exit code is always 0. For bare PE executables (without `-sys win32`), the exit code is the value in R0 (RAX) when `HLT` executes. Windows truncates it to 32 bits and may interpret high values as errors. Stick to 0–255 for predictable results.

### 8051 "size mismatch" warning

This indicates a bug in the instruction size calculation (pass 1 vs. pass 2 disagree). Report it with the `.UA` source file that triggered it.
