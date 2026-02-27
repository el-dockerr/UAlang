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
13. [Standard Libraries and Cross-Platform I/O](#standard-libraries-and-cross-platform-io)
14. [Tutorial: Your First Interactive App (The UA Calculator)](#tutorial-your-first-interactive-app-the-ua-calculator)
15. [Using `@DEFINE` — Hardware Constants Without Runtime Cost](#using-define--hardware-constants-without-runtime-cost)
16. [What to Read Next](#what-to-read-next)

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

## Standard Libraries and Cross-Platform I/O

One of UA's most powerful features is writing **portable I/O code** that works across every supported target — without you needing to know the underlying operating system or CPU conventions. This chapter explains how the `std_io` library achieves this and how you can use it in your own programs.

### The Problem: Every Platform Is Different

Printing a string to the screen sounds simple, but every OS and CPU does it differently:

| Platform | Mechanism | Syscall Write | Syscall Read | Syscall Register |
|----------|-----------|---------------|--------------|------------------|
| x86-64 Linux | `SYSCALL` | 1 | 0 | RAX (R0) |
| x86-32 Linux | `INT 0x80` | 4 | 3 | EAX (R0) |
| ARM Linux | `SVC #0` | 4 | 3 | r7 (R7) |
| ARM64 Linux | `SVC #0` | 64 | 63 | X8 (via R7) |
| RISC-V Linux | `ECALL` | 64 | 63 | a7 (R7) |
| x86-64 Windows | Win32 API | WriteFile | ReadFile | dispatcher |

The syscall numbers are different, the registers for arguments are different, and even the instruction to trigger the call is different. Writing raw syscalls would force you to maintain six separate codepaths.

### The Solution: `std_io` and the Precompiler

UA solves this with two mechanisms working together:

1. **The `SYS` instruction** — a single MVIS opcode that emits the correct syscall instruction for the target (SYSCALL, INT 0x80, SVC #0, ECALL, or a Win32 API call).
2. **Precompiler conditionals** (`@IF_ARCH`, `@IF_SYS`) — let the library set up the right registers for each platform, all in one source file.

The `std_io` library uses these to give you two simple, universal functions:

```asm
@IMPORT std_io

    ; --- Printing ---
    LDS  R0, "Hello, world!\n"
    CALL std_io.print          ; print null-terminated string at R0

    ; --- Reading ---
    BUFFER input, 64           ; allocate a 64-byte buffer
    GET  R0, input             ; R0 = buffer address
    LDI  R1, 64               ; R1 = max bytes to read
    CALL std_io.read           ; read from stdin into buffer
    ; R0 = bytes actually read (on Linux)
```

That's it. The same code compiles unchanged for x86-64, ARM, ARM64, RISC-V, x86-32, on both Linux and Windows (where applicable).

### How `std_io.print` Works (Under the Hood)

Let's trace what happens when you call `std_io.print` on different platforms.

Every version of `print` follows the same algorithm:

1. **Save the buffer pointer** into the register the OS expects.
2. **Compute strlen** — walk bytes with `LOADB` until a null byte is found.
3. **Load the syscall number** and file descriptor into their platform-specific registers.
4. **Call `SYS`** to invoke the OS.
5. **`RET`** back to the caller.

The only things that change between platforms are *which registers hold what* and *which syscall number to use*.

#### Example: x86-64 Linux vs. ARM Linux

**x86-64 Linux** — the write syscall expects:
- R0 (RAX) = syscall number (1)
- R7 (RDI) = file descriptor (1 = stdout)
- R6 (RSI) = buffer pointer
- R2 (RDX) = byte count

```asm
@IF_ARCH x86
print:
    MOV  R6, R0          ; RSI = buf
    ; ... strlen loop sets R2 = length ...
    LDI  R0, 1           ; RAX = 1 (write)
    LDI  R7, 1           ; RDI = 1 (stdout)
    SYS                  ; SYSCALL
    RET
@ENDIF
```

**ARM Linux** — the write syscall expects:
- R7 = syscall number (4)
- R0 = file descriptor (1 = stdout)
- R1 = buffer pointer
- R2 = byte count

```asm
@IF_ARCH arm
@IF_SYS linux
print:
    MOV  R1, R0          ; r1 = buf
    ; ... strlen loop sets R2 = length ...
    LDI  R7, 4           ; r7 = 4 (write)
    LDI  R0, 1           ; r0 = 1 (stdout)
    SYS                  ; SVC #0
    RET
@ENDIF
@ENDIF
```

Notice how the *algorithm* is identical — only the register assignments and syscall numbers differ. The `@IF_ARCH` / `@IF_SYS` blocks ensure only the correct version is compiled.

### How `std_io.read` Works

The `read` function is even simpler because there is no strlen step — the caller provides the maximum byte count directly in R1.

The calling convention is:
- **R0** = pointer to a buffer (created with `BUFFER`)
- **R1** = maximum number of bytes to read

After the call, **R0** contains the number of bytes actually read (on Linux). On Windows, the byte count is stored internally by the dispatcher.

A complete example:

```asm
@IMPORT std_io

    BUFFER my_buf, 128       ; 128-byte input buffer
    JMP main

main:
    ; Prompt the user
    LDS  R0, "Enter your name: "
    CALL std_io.print

    ; Read input
    GET  R0, my_buf          ; R0 = buffer address
    LDI  R1, 127             ; leave 1 byte for null terminator
    CALL std_io.read         ; R0 = bytes read

    ; Null-terminate the input
    MOV  R2, R0              ; R2 = bytes read
    GET  R3, my_buf
    ADD  R3, R2              ; R3 = address of first byte after input
    LDI  R1, 0
    STOREB R1, R3            ; write null terminator

    ; Echo it back
    LDS  R0, "Hello, "
    CALL std_io.print
    GET  R0, my_buf
    CALL std_io.print
    HLT
```

### The ARM64 Trick: Hidden Register X8

AArch64 (ARM64) Linux is unique: it puts the syscall number in register **X8**, but UA only maps R0–R7 to X0–X7. There's no R8!

The UAS compiler handles this transparently. When you write `SYS` and compile for ARM64, the backend automatically emits:

```
MOV X8, X7      ; copy R7 (X7) into X8
SVC #0          ; supervisor call
```

This means you use the same convention as ARM and RISC-V — **put the syscall number in R7** — and the compiler takes care of the rest. You never need to worry about X8.

### The Win32 Dispatcher

On Windows, there are no syscall numbers. Instead, the `SYS` instruction jumps to a built-in **syscall dispatcher** in the generated executable:

- If **R0 = 1** (or any nonzero value), the dispatcher calls **WriteFile** via kernel32.dll → prints the buffer.
- If **R0 = 0**, the dispatcher calls **ReadFile** via kernel32.dll → reads into the buffer.

The register setup is the same as Linux x86-64 (R6 = buffer, R2 = count), so the `std_io` library's x86 section handles both Linux and Windows without any `@IF_SYS` split. The x86 print and read blocks are simply guarded by `@IF_ARCH x86` and work on both operating systems.

### Writing Your Own Precompiler-Guarded Libraries

You can use the same technique in your own code. The key precompiler directives are:

| Directive | Purpose |
|-----------|---------|
| `@IF_ARCH x86` | Include block only for x86-64 |
| `@IF_ARCH arm64` | Include block only for AArch64 |
| `@IF_SYS linux` | Include block only for Linux |
| `@IF_SYS win32` | Include block only for Windows |
| `@ENDIF` | End a conditional block |
| `@ARCH_ONLY x86, arm` | Abort compilation if arch doesn't match |
| `@SYS_ONLY linux` | Abort compilation if system doesn't match |

Conditionals nest freely:

```asm
@IF_SYS linux
    @IF_ARCH arm
        ; ARM + Linux only
    @ENDIF
    @IF_ARCH arm64
        ; ARM64 + Linux only
    @ENDIF
@ENDIF
```

### Quick Reference: Syscall Register Cheat Sheet

When writing your own syscalls (beyond what `std_io` provides), here's the register mapping:

**x86-64 (SYSCALL):**
| Argument | UA Register | Native Register |
|----------|------------|------------------|
| Syscall# | R0 | RAX |
| Arg 1 | R7 | RDI |
| Arg 2 | R6 | RSI |
| Arg 3 | R2 | RDX |
| Return | R0 | RAX |

**ARM / ARM64 / RISC-V (SVC / ECALL):**
| Argument | UA Register | ARM | ARM64 | RISC-V |
|----------|------------|-----|-------|--------|
| Syscall# | R7 | r7 | X7→X8 | a7 |
| Arg 1 | R0 | r0 | X0 | a0 |
| Arg 2 | R1 | r1 | X1 | a1 |
| Arg 3 | R2 | r2 | X2 | a2 |
| Return | R0 | r0 | X0 | a0 |

**x86-32 (INT 0x80):**
| Argument | UA Register | Native Register |
|----------|------------|------------------|
| Syscall# | R0 | EAX |
| Arg 1 | R3 | EBX |
| Arg 2 | R1 | ECX |
| Arg 3 | R2 | EDX |
| Return | R0 | EAX |

---

## Tutorial: Your First Interactive App (The UA Calculator)

You've learned registers, instructions, control flow, I/O, and standard libraries. Now let's put it all together by building something real: **an interactive calculator** that reads two numbers and an operator from the keyboard, performs the math, and prints the result.

This tutorial walks through [tests/calc.ua](../tests/calc.ua) line by line.

### Why a Calculator?

A calculator is the perfect first "real" program because it exercises every major UA concept:

| Concept | How the Calculator Uses It |
|---------|---------------------------|
| `BUFFER` | Allocating memory to capture keyboard input |
| `VAR` / `GET` / `SET` | Storing and retrieving the parsed numbers |
| `CALL` | Invoking library functions (`std_io.print`, `std_io.read`, `std_string.parse_int`, `std_string.to_string`) |
| `CMP` / `JZ` | Branching on the operator character |
| `ADD` / `SUB` | The actual arithmetic |
| `LOADB` / `STOREB` | Byte-level work inside `parse_int` and `to_string` |
| `@IMPORT` | Pulling in standard libraries |
| `@ARCH_ONLY` | Restricting to supported targets |

### The ASCII Trap: Why You Need `parse_int` and `to_string`

This is the single most important concept for beginners working close to the metal.

When you type **`5`** on your keyboard and press Enter, the operating system does **not** hand your program the number 5. It hands you the byte **`0x35`** (decimal 53) — the ASCII code for the *character* '5'. A newline (`0x0A`) follows it.

So the "number" `42` arrives as three bytes in your buffer:

```
Buffer:  [ 0x34 ] [ 0x32 ] [ 0x0A ]   ← raw bytes
           '4'      '2'      '\n'
```

If you tried to `ADD` these bytes directly, you'd get `0x34 + 0x32 = 0x66` — which is the letter 'f'. Not helpful.

**`std_string.parse_int`** fixes this. It walks each byte, subtracts 48 (the ASCII value of '0'), multiplies a running total by 10, and adds the digit:

```
'4' → 0x34 - 48 = 4      total = 0 * 10 + 4 = 4
'2' → 0x32 - 48 = 2      total = 4 * 10 + 2 = 42
'\n' → stop
                          Result: 42  ✓
```

The reverse problem hits when you want to *print* a result. The number `123` is a single integer in a register — but the screen expects three separate ASCII characters ('1', '2', '3'). **`std_string.to_string`** extracts digits by dividing by 10, converts each to ASCII (add 48), and writes them to a buffer.

### Walking Through `calc.ua`

#### 1. Header and Imports

```asm
@ARCH_ONLY x86, x86_32, arm, arm64, riscv
@IMPORT std_io
@IMPORT std_string
```

`@ARCH_ONLY` locks the program to architectures that support console I/O (everything except the bare-metal 8051). The two `@IMPORT` lines pull in the I/O and string-conversion libraries.

#### 2. Data Section

```asm
    BUFFER input_1, 32       ; keyboard buffer for first number
    BUFFER input_2, 32       ; keyboard buffer for second number
    BUFFER input_op, 8       ; keyboard buffer for operator (+, -)
    BUFFER output_buf, 32    ; output buffer for result string

    VAR num1                 ; parsed first operand
    VAR num2                 ; parsed second operand
    VAR result               ; computed result
```

**`BUFFER`** reserves raw byte arrays. When you call `std_io.read`, the OS writes the user's keystrokes into these buffers. 32 bytes is generous for a number — it accommodates up to 31 digits plus a null terminator.

**`VAR`** creates named integer variables. After parsing a string into an integer, we `SET` the variable so we can `GET` it back later.

> **Key difference:** `GET R0, input_1` gives you the *address* of the buffer (a pointer). `GET R0, num1` gives you the *value* stored in the variable.

#### 3. Reading and Parsing a Number

```asm
    LDS  R0, "Enter first number: "
    CALL std_io.print

    GET  R0, input_1         ; R0 = address of input buffer
    LDI  R1, 31              ; max bytes to read
    CALL std_io.read         ; OS fills buffer with keystrokes

    GET  R0, input_1         ; R0 = buffer address (for parse_int)
    CALL std_string.parse_int
    SET  num1, R0            ; save the integer
```

This three-step pattern — **prompt → read → parse** — repeats for each input. Notice we pass 31 (not 32) to `std_io.read`, leaving room for a null terminator.

After `parse_int` returns, R0 holds a real integer that `ADD` and `SUB` can work with.

#### 4. Operator Dispatch

```asm
    GET  R3, input_op        ; R3 = address of operator buffer
    LOADB R0, R3             ; R0 = first byte (the operator character)

    LDI  R1, 43              ; '+' = ASCII 43
    CMP  R0, R1
    JZ   do_add

    LDI  R1, 45              ; '-' = ASCII 45
    CMP  R0, R1
    JZ   do_sub

    LDS  R0, "Error: unknown operator\n"
    CALL std_io.print
    HLT
```

Here's where UA's clean branching shines. Compare this to raw x86 assembly:

| UA (clear intent) | x86-64 (cryptic) |
|---|---|
| `CMP R0, R1` | `cmp al, 0x2B` |
| `JZ do_add` | `je 0x004010A0` |

In UA, you compare two named registers and jump to a human-readable label. No magic hex addresses, no implicit flag registers, no mental gymnastics. The **intent** is front and center: *"if the byte equals '+', go do addition."*

The fall-through case (neither '+' nor '-') prints an error and halts — defensive programming even in assembly.

#### 5. Performing the Math

```asm
do_add:
    GET  R0, num1
    GET  R1, num2
    ADD  R0, R1              ; R0 = num1 + num2
    SET  result, R0
    JMP  show_result

do_sub:
    GET  R0, num1
    GET  R1, num2
    SUB  R0, R1              ; R0 = num1 - num2
    SET  result, R0
    JMP  show_result
```

Load both operands from variables, perform one arithmetic instruction, save the result. This is as clean as assembly gets.

#### 6. Converting Back and Printing

```asm
show_result:
    GET  R0, result          ; R0 = computed integer
    GET  R1, output_buf      ; R1 = output buffer address
    CALL std_string.to_string

    LDS  R0, "Result: "
    CALL std_io.print

    GET  R0, output_buf
    CALL std_io.print

    LDS  R0, "\n"
    CALL std_io.print

    HLT
```

`to_string` converts the integer in R0 into ASCII characters, writing them into `output_buf`. Then we print a label, the result string, and a newline.

### Inside `parse_int`: Digit-by-Digit Conversion

For the curious, here's the core loop from `std_string.parse_int` with annotations:

```asm
parse_int:
    MOV  R3, R0          ; R3 = string pointer
    LDI  R0, 0           ; running total = 0
    LDI  R6, 10          ; constant: multiplier (and newline sentinel!)
    LDI  R7, 48          ; constant: ASCII '0'

parse_int_loop:
    LOADB R1, R3         ; load one byte from the string
    CMP  R1, ...         ; if null, newline, or \r → stop
    JZ   parse_int_done
    SUB  R1, R7          ; convert ASCII to digit (e.g. '5' - 48 = 5)
    MUL  R0, R6          ; total = total × 10
    ADD  R0, R1          ; total = total + digit
    INC  R3              ; next character
    JMP  parse_int_loop
```

Notice the clever reuse of R6: the value 10 serves double duty as both the multiplication constant and the newline character check.

### Inside `to_string`: Digits in Reverse

The hardest part of `to_string` is that division extracts digits **backwards** — least significant first:

```
123 ÷ 10 = 12 remainder 3  → write '3'
 12 ÷ 10 =  1 remainder 2  → write '2'
  1 ÷ 10 =  0 remainder 1  → write '1'
```

The buffer now contains `"321"`. A swap-based reversal loop fixes the order to `"123"` before null-terminating.

Since UA has no `MOD` instruction, the remainder is computed manually:

```asm
    MOV  R0, R3          ; R0 = value
    MOV  R2, R3          ; R2 = value (backup)
    DIV  R0, R7          ; R0 = value / 10
    PUSH R0              ; save quotient
    MUL  R0, R7          ; R0 = quotient × 10
    SUB  R2, R0          ; R2 = value - quotient×10 = remainder
```

This is the kind of low-level trick you learn working close to the metal — and it works identically on every architecture UA supports.

### Compiling and Running

**Linux (x86-64):**
```bash
./uas tests/calc.ua -arch x86 -sys linux -format elf -o calc
chmod +x calc
./calc
```

**Windows (x86-64):**
```bash
uas.exe tests/calc.ua -arch x86 -sys win32 -format pe -o calc.exe
calc.exe
```

**ARM64 Linux (e.g. Raspberry Pi 4 with 64-bit OS):**
```bash
./uas tests/calc.ua -arch arm64 -sys linux -format elf -o calc
chmod +x calc
./calc
```

Sample session (identical on every platform):

```
Enter first number: 100
Enter second number: 58
Operator (+ or -): -
Result: 42
```

### Exercises

Try extending the calculator on your own:

1. **Add multiplication:** Check for `'*'` (ASCII 42) and use `MUL R0, R1`.
2. **Add division:** Check for `'/'` (ASCII 47) and use `DIV R0, R1`. What happens if the second number is 0?
3. **Loop it:** Instead of `HLT` after printing, `JMP` back to `main` to let the user do multiple calculations.
4. **Handle negatives on input:** Modify `parse_int` to check for a leading `'-'` (ASCII 45) and negate the result if found.

---

## Using `@DEFINE` — Hardware Constants Without Runtime Cost

When writing bare-metal or embedded programs, you deal with **hardware register addresses** — magic hex numbers like `0x98` or `0x89`.  Scattering these through your code makes it hard to read and easy to get wrong.  The `@DEFINE` directive solves this by giving names to constants *at compile time*.

### What `@DEFINE` Does

```asm
@DEFINE LED_PORT  0x80
@DEFINE DELAY     255
```

After these lines, every time the precompiler sees `LED_PORT` on a code line, it replaces it with `0x80` — before the lexer ever sees it.  The result is exactly as if you had typed `0x80` yourself.  **No variable, no RAM, no runtime overhead.**

### Your First `@DEFINE` Program

Create a file called `define_demo.ua`:

```asm
; define_demo.ua — @DEFINE basics
@DEFINE ANSWER   42
@DEFINE LIMIT    10

    LDI  R0, 0          ; counter = 0

loop:
    INC  R0
    CMP  R0, LIMIT      ; expands to: CMP R0, 10
    JNZ  loop

    LDI  R1, ANSWER      ; expands to: LDI R1, 42
    HLT
```

Compile and run:

```bash
ua define_demo.ua -arch x86 --run
```

The precompiler replaces `LIMIT` → `10` and `ANSWER` → `42` before lexing.  The machine code is identical to writing the numbers directly — but your source is self-documenting.

### Important Rules

| Rule | What It Means |
|------|---------------|
| **Whole-token only** | `@DEFINE P0 0x80` replaces `P0` but **not** `DPH0` or `P0x` |
| **One per line** | Each `@DEFINE` goes on its own line |
| **Order matters** | A macro is only visible to lines *after* its `@DEFINE` |
| **Max 512 macros** | More than enough for any hardware platform |
| **No nesting** | `@DEFINE A B` then `@DEFINE B 5` — `A` expands to `B`, not to `5` |

### Hardware Libraries: Pre-Built `@DEFINE` Collections

Writing `@DEFINE` for every register by hand would be tedious.  UA ships with **hardware definition libraries** that do it for you:

| Library | Import | What You Get |
|---------|--------|--------------|
| **hw_mcs51** | `@IMPORT hw_mcs51` | 8051 SFRs: `P0`, `SCON`, `SBUF`, `TMOD`, `TH1`, `IE`, etc. |
| **hw_x86_pc** | `@IMPORT hw_x86_pc` | PC I/O ports: `PORT_COM1`, `PORT_VGA_CMD`, `PORT_KEYBOARD`, etc. |
| **hw_riscv_virt** | `@IMPORT hw_riscv_virt` | QEMU virt: `UART0_BASE`, `CLINT_BASE`, `PLIC_BASE`, etc. |
| **hw_arm_virt** | `@IMPORT hw_arm_virt` | QEMU virt: `PL011_BASE`, `GIC_DIST`, `GIC_CPU`, etc. |

These libraries are guarded by `@ARCH_ONLY`, so they only compile for the correct target.

### Example: 8051 UART Transmit

Here's a real bare-metal program that configures the 8051 serial port and transmits the letter `'A'`.  Compare the macro version (left) with what the precompiler produces (right):

**What you write:**

```asm
@IMPORT hw_mcs51

    LDI  R0, 0x20
    LDI  R1, TMOD          ; Timer 1, Mode 2
    STORE R0, R1

    LDI  R0, 0xFD
    LDI  R1, TH1            ; 9600 baud
    STORE R0, R1

    LDI  R0, 0x50
    LDI  R1, SCON           ; Serial Mode 1, REN
    STORE R0, R1

    LDI  R0, 0x41           ; 'A'
    LDI  R1, SBUF           ; transmit!
    STORE R0, R1
    HLT
```

**What the precompiler produces** (after macro expansion):

```asm
    LDI  R0, 0x20
    LDI  R1, 0x89           ; TMOD = 0x89
    STORE R0, R1

    LDI  R0, 0xFD
    LDI  R1, 0x8D           ; TH1  = 0x8D
    STORE R0, R1

    LDI  R0, 0x50
    LDI  R1, 0x98           ; SCON = 0x98
    STORE R0, R1

    LDI  R0, 0x41
    LDI  R1, 0x99           ; SBUF = 0x99
    STORE R0, R1
    HLT
```

The human reads `SCON` and `SBUF`; the CPU sees `0x98` and `0x99`.  Best of both worlds.

Compile the full demo:

```bash
ua examples/8051_uart_tx.ua -arch mcs51 -o uart_tx.bin
```

### When to Use `@DEFINE` vs. `VAR`

| Feature | `@DEFINE` | `VAR` |
|---------|-----------|-------|
| **When it's resolved** | Compile time (precompiler) | Run time (CPU instructions) |
| **RAM cost** | Zero | Uses memory for each variable |
| **Can change at runtime** | No — it's a fixed substitution | Yes — `SET` / `GET` read/write freely |
| **Best for** | Hardware addresses, magic numbers, configuration constants | Counters, accumulators, user data |

**Rule of thumb:** If a value is known at compile time and never changes, use `@DEFINE`.  If it needs to change while the program runs, use `VAR`.

---

## What to Read Next

| Document | What It Covers |
|----------|---------------|
| [MVIS Opcodes Reference](mvis-opcodes.md) | Every MVIS instruction with syntax, behavior, and backend notes |
| [Architecture-Specific Opcodes](arch-specific-opcodes.md) | Non-MVIS instructions: x86, 8051, ARM, RISC-V specifics |
| [Language Reference](language-reference.md) | Complete syntax reference — registers, operands, string literals |
| [Compiler Usage](compiler-usage.md) | CLI flags, output formats, build instructions |
| [Architecture](architecture.md) | Internal compiler pipeline and design |
