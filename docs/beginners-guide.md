# UA Beginner's Guide

Welcome to **Unified Assembly (UA)** — a portable assembly language that lets you write low-level code once and compile it to six different CPU architectures. This guide will take you from zero to running programs.

---

## Table of Contents

1. [What Is UA?](#what-is-ua)
2. [Building the Compiler](#building-the-compiler)
3. [Your First Program](#your-first-program)
4. [Understanding the Basics](#understanding-the-basics)
   - [Registers](#registers)
   - [Instructions](#instructions)
   - [Labels and Control Flow](#labels-and-control-flow)
   - [Comments](#comments)
5. [Working with Numbers](#working-with-numbers)
6. [Variables](#variables)
7. [Functions](#functions)
8. [Hello World with I/O](#hello-world-with-io)
9. [Standard Libraries](#standard-libraries)
10. [Conditional Compilation](#conditional-compilation)
11. [Common Patterns](#common-patterns)
12. [Architecture-Specific Code](#architecture-specific-code)
13. [What to Read Next](#what-to-read-next)

---

## What Is UA?

Most assembly languages are tied to a single CPU — x86 assembly only works on Intel/AMD chips, ARM assembly only works on ARM chips, etc. **UA changes that.** You write your program once using UA's unified instruction set, then compile it to any of these targets:

| Architecture | Flag | Typical Use |
|-------------|------|-------------|
| x86-64 | `-arch x86` | Desktop PCs, servers (64-bit) |
| x86-32 (IA-32) | `-arch x86_32` | Legacy 32-bit x86 systems |
| ARM (ARMv7-A) | `-arch arm` | Smartphones, Raspberry Pi |
| ARM64 (AArch64) | `-arch arm64` | Apple Silicon, modern ARM servers |
| RISC-V (RV64I) | `-arch riscv` | Open-source hardware, embedded |
| 8051 (MCS-51) | `-arch mcs51` | Microcontrollers, embedded |

The core instruction set — called **MVIS** (Minimum Viable Instruction Set) — is identical across all targets. The compiler handles the translation to native machine code behind the scenes.

---

## Building the Compiler

UA is a single-binary compiler written in C99 with zero dependencies. Build it with one command:

**Linux / macOS:**

```bash
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o ua \
    main.c lexer.c parser.c codegen.c precompiler.c \
    backend_8051.c backend_x86_64.c backend_x86_32.c \
    backend_arm.c backend_arm64.c backend_risc_v.c \
    emitter_pe.c emitter_elf.c emitter_macho.c
```

**Windows:**

```cmd
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o ua.exe ^
    main.c lexer.c parser.c codegen.c precompiler.c ^
    backend_8051.c backend_x86_64.c backend_x86_32.c ^
    backend_arm.c backend_arm64.c backend_risc_v.c ^
    emitter_pe.c emitter_elf.c emitter_macho.c
```

That's it. No build system, no package manager, no dependencies.

---

## Your First Program

Create a file called `first.ua`:

```asm
; first.ua — my first UA program
    LDI  R0, 10       ; load the number 10 into register R0
    LDI  R1, 32       ; load 32 into R1
    ADD  R0, R1        ; R0 = R0 + R1 = 42
    HLT                ; stop execution
```

### Compile and Run

**JIT execution** (runs immediately on your x86-64 machine):

```bash
ua first.ua -arch x86 --run
```

**Compile to a Linux ELF binary:**

```bash
ua first.ua -arch x86 -sys linux -o first
./first
```

**Compile to a Windows executable:**

```bash
ua first.ua -arch x86 -sys win32 -o first.exe
first.exe
```

**Cross-compile for a different architecture:**

```bash
ua first.ua -arch arm -o first.bin          # ARM binary
ua first.ua -arch riscv -sys linux -o first  # RISC-V ELF
ua first.ua -arch mcs51 -o firmware.bin      # 8051 firmware
```

---

## Understanding the Basics

### Registers

Registers are the fastest storage locations inside a CPU. UA gives you **8 general-purpose registers** named `R0` through `R7`. Think of them as named variables that live directly in the CPU:

| Register | Typical Use |
|----------|-------------|
| `R0` | Accumulator, return values |
| `R1`–`R3` | General computation |
| `R4` | Stack pointer (use with care!) |
| `R5` | Base pointer (use with care!) |
| `R6`–`R7` | General computation |

> **Tip:** Avoid modifying `R4` and `R5` directly — they manage the call stack on most architectures.

The compiler maps these to real hardware registers automatically. For example, `R0` becomes `RAX` on x86-64, `r0` on ARM, and `a0` on RISC-V.

### Instructions

Every UA instruction fits on one line and follows this pattern:

```
    MNEMONIC  operand1, operand2    ; optional comment
```

For example:

```asm
    LDI   R0, 42       ; load immediate: R0 = 42
    ADD   R0, R1        ; add registers:  R0 = R0 + R1
    MOV   R2, R0        ; copy register:  R2 = R0
    INC   R0            ; increment:      R0 = R0 + 1
    NOP                 ; do nothing (no operands)
```

Operands can be:
- **Registers:** `R0`, `R1`, ... `R7`
- **Immediates (numbers):** `42`, `0xFF`, `0b1010`
- **Labels:** `start`, `loop`, `my_function`
- **Strings:** `"Hello\n"`

### Labels and Control Flow

Labels mark positions in your code. They end with a colon and let you jump around:

```asm
start:
    LDI  R0, 0          ; R0 = 0 (our counter)

loop:
    INC  R0              ; R0 = R0 + 1
    CMP  R0, 10          ; compare R0 with 10
    JNZ  loop            ; if R0 != 10, go back to "loop"

    ; When we get here, R0 = 10
    HLT
```

**Jump instructions:**

| Instruction | Meaning |
|-------------|---------|
| `JMP label` | Always jump to label |
| `JZ label` | Jump if last comparison was equal (zero) |
| `JNZ label` | Jump if not equal (not zero) |
| `JL label` | Jump if less than (signed) |
| `JG label` | Jump if greater than (signed) |

All jump instructions follow a `CMP` instruction that sets the CPU flags.

### Comments

Comments start with `;` and extend to the end of the line:

```asm
    LDI  R0, 42     ; this is a comment
; this entire line is a comment
```

---

## Working with Numbers

UA supports three number formats:

| Format | Examples | Notes |
|--------|---------|-------|
| Decimal | `42`, `-7`, `0` | Default |
| Hexadecimal | `0xFF`, `0x1A` | Prefix `0x` |
| Binary | `0b1010`, `0b11001100` | Prefix `0b` |

```asm
    LDI  R0, 255        ; decimal
    LDI  R1, 0xFF       ; hex (same as 255)
    LDI  R2, 0b11111111 ; binary (same as 255)
```

---

## Variables

Registers are fast but limited. **Variables** give you named storage that persists across function calls:

```asm
    ; Declare variables
    VAR  counter, 0      ; variable "counter" initialized to 0
    VAR  result           ; variable "result" (default: 0)

    ; Write to a variable
    LDI  R0, 42
    SET  counter, R0     ; counter = 42
    SET  result, 99      ; result = 99 (immediate value)

    ; Read from a variable
    GET  R0, counter     ; R0 = 42
    GET  R1, result      ; R1 = 99
```

**Complete example — counting to 10:**

```asm
    VAR count, 0

    LDI R0, 0

loop:
    INC  R0
    SET  count, R0
    CMP  R0, 10
    JNZ  loop

    ; count = 10, R0 = 10
    HLT
```

---

## Functions

Functions in UA are labels with an optional parameter list. You call them with `CALL` and return with `RET`:

```asm
    JMP  main              ; skip past function definitions

; Function: add two numbers
; Input:  variables a, b
; Output: R0 = a + b
add(a, b):
    GET  R0, a
    GET  R1, b
    ADD  R0, R1
    RET

main:
    VAR  a
    VAR  b

    SET  a, 15
    SET  b, 27
    CALL add
    ; R0 now holds 42

    HLT
```

**Key points:**
- Function parameters are just variable names — they document what the function expects
- Arguments are passed via named variables (`SET` before `CALL`)
- Return values are typically left in `R0`
- `CALL` pushes the return address; `RET` pops it and jumps back
- Always `JMP` past function bodies at the top of your program (or they'll execute when the program starts!)

### Syntactic Sugar

You can call functions with a shorthand syntax:

```asm
    add()                  ; same as: CALL add
    std_io.print()         ; same as: CALL std_io.print
```

---

## Hello World with I/O

UA includes a standard I/O library for printing to the console. Here's the classic Hello World:

```asm
@IMPORT std_io

    LDS   R0, "Hello, World!\n"
    CALL  std_io.print
    HLT
```

**Step by step:**

1. `@IMPORT std_io` — imports the standard I/O library (provides `std_io.print`)
2. `LDS R0, "Hello, World!\n"` — loads the address of the string into R0
3. `CALL std_io.print` — calls the print function (expects string address in R0)
4. `HLT` — stops the program

**Compile and run:**

```bash
ua hello.ua -arch x86 -sys linux -o hello
./hello
Hello, World!
```

### String Escape Sequences

| Escape | Meaning |
|--------|---------|
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\0` | Null byte |
| `\\` | Backslash |
| `\"` | Double quote |

---

## Standard Libraries

UA ships with four standard libraries, all written in UA itself:

### std_io — Console Output

```asm
@IMPORT std_io

    LDS  R0, "Hello!\n"
    CALL std_io.print       ; print string pointed to by R0
```

### std_string — String Utilities

```asm
@IMPORT std_string

    LDS  R0, "test"
    CALL std_string.strlen  ; R1 = 4 (string length)
```

### std_math — Integer Math

```asm
@IMPORT std_math

    ; Power: 2^10 = 1024
    SET  std_math.base, 2
    SET  std_math.exp, 10
    CALL std_math.pow        ; R0 = 1024

    ; Factorial: 5! = 120
    SET  std_math.n, 5
    CALL std_math.factorial  ; R0 = 120

    ; Maximum: max(7, 42) = 42
    SET  std_math.a, 7
    SET  std_math.b, 42
    CALL std_math.max        ; R0 = 42

    ; Absolute value: abs(-15) = 15
    SET  std_math.val, -15
    CALL std_math.abs        ; R0 = 15
```

### std_arrays — Byte Array Operations

```asm
@IMPORT std_arrays

    BUFFER my_buf, 32

    ; Fill buffer with 0xFF
    GET  R0, my_buf
    SET  std_arrays.dst, R0
    SET  std_arrays.count, 32
    SET  std_arrays.value, 0xFF
    CALL std_arrays.fill_bytes
```

---

## Conditional Compilation

UA's precompiler lets you write platform-specific code in a single file:

### @IF_ARCH / @IF_SYS — Conditional Blocks

```asm
@IF_ARCH x86
    ; This code is only compiled for x86-64
    LDI R0, 64
@ENDIF

@IF_ARCH mcs51
    ; This code is only compiled for 8051
    LDI R0, 8
@ENDIF

@IF_SYS linux
    ; Linux-only code
    SYS
@ENDIF
```

### @ARCH_ONLY / @SYS_ONLY — Hard Guards

If your entire file is architecture-specific, use hard guards at the top:

```asm
@ARCH_ONLY x86, x86_32     ; abort compilation if target isn't x86 family
@SYS_ONLY linux, win32      ; abort compilation if system isn't Linux or Windows
```

Unlike `@IF_ARCH` (which silently skips code), `@ARCH_ONLY` **stops compilation** with an error if the target doesn't match.

---

## Common Patterns

### Counting Loop

```asm
    LDI  R0, 0          ; counter = 0
    LDI  R1, 10         ; limit = 10

loop:
    ; ... do work with R0 ...
    INC  R0
    CMP  R0, R1
    JNZ  loop            ; repeat until R0 == 10
    HLT
```

### Conditional (If/Else)

```asm
    CMP  R0, 0
    JZ   is_zero         ; if R0 == 0, go to is_zero

not_zero:
    ; R0 is not zero — do something
    JMP  done

is_zero:
    ; R0 is zero — do something else

done:
    HLT
```

### Maximum of Two Values

```asm
    ; Assume R0 and R1 hold two values
    CMP  R0, R1
    JG   r0_bigger       ; if R0 > R1, skip

    MOV  R0, R1          ; R0 was smaller, replace with R1

r0_bigger:
    ; R0 now holds the larger value
    HLT
```

### Stack Save/Restore

```asm
my_function:
    PUSH R3              ; save R3 (we'll clobber it)
    PUSH R6              ; save R6

    ; ... use R3 and R6 freely ...

    POP  R6              ; restore R6 (LIFO order!)
    POP  R3              ; restore R3
    RET
```

### Working with Byte Buffers

```asm
    BUFFER data, 16          ; allocate 16 bytes
    GET    R0, data          ; R0 = address of buffer

    ; Write 'A' to first byte
    LDI    R1, 0x41          ; 'A' = 0x41
    STOREB R1, R0            ; buffer[0] = 'A'

    ; Read it back
    LOADB  R2, R0            ; R2 = buffer[0] = 0x41
```

### Complete Program: Sum 1 to N

```asm
@IMPORT std_io

    JMP main

; sum_n: compute 1 + 2 + ... + R0
; Input:  R0 = N
; Output: R0 = sum
sum_n:
    LDI  R1, 0           ; R1 = accumulator (sum)
    LDI  R2, 1           ; R2 = counter (starts at 1)

sum_loop:
    ADD  R1, R2           ; sum += counter
    INC  R2               ; counter++
    CMP  R2, R0
    JG   sum_done         ; if counter > N, done
    JMP  sum_loop

sum_done:
    ADD  R1, R0           ; add N itself
    MOV  R0, R1           ; return sum in R0
    RET

main:
    LDI  R0, 10           ; compute sum(1..10)
    CALL sum_n
    ; R0 = 55
    HLT
```

---

## Architecture-Specific Code

The MVIS instructions work everywhere, but UA also offers **architecture-specific opcodes** for when you need hardware features unique to a particular CPU. These opcodes will only compile for their supported architectures.

For a quick overview:

| Opcode | Architectures | Purpose |
|--------|--------------|---------|
| `CPUID` | x86, x86_32 | Query CPU identification info |
| `RDTSC` | x86, x86_32 | Read hardware timestamp counter |
| `BSWAP` | x86, x86_32 | Byte-swap register (endian conversion) |
| `PUSHA` | x86_32 only | Push all general-purpose registers |
| `POPA` | x86_32 only | Pop all general-purpose registers |
| `DJNZ` | mcs51 only | Decrement register and jump if not zero |
| `CJNE` | mcs51 only | Compare and jump if not equal |
| `SETB` | mcs51 only | Set a bit |
| `CLR` | mcs51 only | Clear a bit or the accumulator |
| `RETI` | mcs51 only | Return from interrupt |
| `WFI` | arm, arm64, riscv | Wait for interrupt (low-power sleep) |
| `DMB` | arm, arm64 | Data memory barrier |
| `EBREAK` | riscv only | Debugger breakpoint |
| `FENCE` | riscv only | Memory ordering fence |

Using an architecture-specific opcode on the wrong target will produce a **compliance error** at compile time:

```
  UA Compliance Error
  -------------------
  Line 5: opcode 'CPUID' is not supported on architecture 'arm'
  Supported architectures: x86, x86_32
```

Use `@IF_ARCH` to guard architecture-specific code:

```asm
@IF_ARCH x86
    CPUID                ; only compiled for x86-64
@ENDIF

@IF_ARCH mcs51
    DJNZ R0, loop       ; only compiled for 8051
@ENDIF
```

For full details, see:
- [MVIS Opcodes Reference](mvis-opcodes.md) — complete MVIS instruction set
- [Architecture-Specific Opcodes](arch-specific-opcodes.md) — non-MVIS instructions per architecture

---

## What to Read Next

| Document | What It Covers |
|----------|---------------|
| [MVIS Opcodes Reference](mvis-opcodes.md) | Every MVIS instruction with syntax, behavior, and backend notes |
| [Architecture-Specific Opcodes](arch-specific-opcodes.md) | Non-MVIS instructions: x86, 8051, ARM, RISC-V specifics |
| [Language Reference](language-reference.md) | Complete syntax reference — registers, operands, string literals |
| [Compiler Usage](compiler-usage.md) | CLI flags, output formats, build instructions |
| [Architecture](architecture.md) | Internal compiler pipeline and design |
