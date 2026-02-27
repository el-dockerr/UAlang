# UA Language Reference

This document is the complete reference for the **Unified Assembly (UA)** instruction set — the portable assembly language used by the UA compiler.

---

## Table of Contents

1. [Source File Format](#source-file-format)
2. [Precompiler Directives](#precompiler-directives)
3. [Comments](#comments)
4. [Labels](#labels)
5. [Variables](#variables)
6. [Functions](#functions)
7. [Registers](#registers)
8. [Numeric Literals](#numeric-literals)
9. [String Literals](#string-literals)
10. [Standard Libraries](#standard-libraries)
11. [Instruction Set](#instruction-set)
   - [Data Movement](#data-movement)
   - [Arithmetic](#arithmetic)
   - [Bitwise Logic](#bitwise-logic)
   - [Shift Operations](#shift-operations)
   - [Comparison](#comparison)
   - [Control Flow](#control-flow)
   - [Stack Operations](#stack-operations)
   - [System](#system)
   - [Variables](#variable-instructions)
   - [Memory Allocation](#memory-allocation)
12. [Operand Rules](#operand-rules)
13. [Backend-Specific Notes](#backend-specific-notes)

---

## Source File Format

- File extension: `.UA`
- Encoding: ASCII / UTF-8
- One instruction per line
- Whitespace (spaces and tabs) is ignored around operands
- Blank lines are allowed
- Lines are terminated by newline (`\n` or `\r\n`)

**Example:**

```asm
; A minimal UA program
    LDI  R0, 42
    LDI  R1, 8
    ADD  R0, R1
    HLT
```

---

## Precompiler Directives

Before lexing, the UA precompiler evaluates lines starting with `@`.  Directives are **not** assembly instructions — they control conditional compilation, file inclusion, and stub markers.

| Directive | Description |
|-----------|-------------|
| `@IF_ARCH <arch>` | Begin a conditional block — included only when `-arch` matches |
| `@IF_SYS <system>` | Begin a conditional block — included only when `-sys` matches |
| `@ENDIF` | Close the most recent `@IF_ARCH` or `@IF_SYS` block |
| `@IMPORT <path>` | Include another `.ua` file (each file imported at most once) |
| `@DUMMY [message]` | Emit a stub diagnostic to stderr; no code generated |

### Conditional Compilation

```asm
@IF_ARCH x86
    ; This section is only assembled when -arch x86
    LDI R0, 42
@ENDIF

@IF_SYS linux
    ; This section is only assembled when -sys linux
    INT #0x80
@ENDIF
```

Conditional blocks can be **nested** (up to 64 levels):

```asm
@IF_ARCH x86
    @IF_SYS win32
        ; x86 + Windows only
    @ENDIF
@ENDIF
```

### File Import

```asm
@IMPORT "lib/utils.ua"
@IMPORT helpers.ua          ; unquoted form also accepted
```

- Paths are resolved relative to the **importing file's** directory
- Each unique file is imported **once** — duplicate `@IMPORT` lines are skipped
- Imported files are preprocessed recursively (their directives are evaluated)
- Import nesting is limited to 16 levels

#### Namespace Prefixing

All labels, function definitions, and variable declarations in an imported file are automatically prefixed with the filename (without extension and path):

```asm
; main.ua
@IMPORT "lib/math.ua"

    CALL math.add_values     ; calls add_values defined in math.ua
    GET  R0, math.result     ; accesses variable 'result' from math.ua
    math.double_it()         ; syntactic sugar for CALL math.double_it
```

The prefix is derived from the filename: `lib/math.ua` → `math`, `helpers.ua` → `helpers`.

This prevents name collisions when importing multiple files and provides clear provenance for every symbol.

### Stub Markers

```asm
@DUMMY This feature is not yet implemented
@DUMMY
```

Prints a diagnostic to stderr during compilation.  No code is emitted.

---

## Comments

Line comments begin with `;` and extend to the end of the line:

```asm
    LDI R0, 10      ; this is a comment
; this entire line is a comment
```

There are no block comments.

---

## Labels

Labels mark code addresses for use with jump and call instructions. A label is an identifier followed by a colon (`:`):

```asm
start:
    LDI R0, 0
loop:
    INC R0
    CMP R0, 100
    JNZ loop
    HLT
```

**Rules:**

- Labels consist of letters (`a-z`, `A-Z`), digits (`0-9`), underscores (`_`), and dots (`.`)
- Labels must start with a letter or underscore
- Maximum length: 128 characters
- Labels are **case-sensitive**
- Duplicate labels within a file are an error
- Forward references are supported (two-pass assembly)
- Dots are used for namespace-qualified names (e.g., `math.start`)

---

## Variables

Variables are compiler-managed named storage locations. Unlike registers, variables persist across function calls and can be accessed from anywhere in the program.

### Declaring Variables

```asm
    VAR  counter             ; declare variable, initialized to 0
    VAR  result, 42          ; declare variable with initial value
```

### Writing to Variables

```asm
    SET  counter, R0         ; store register value into variable
    SET  result, 99          ; store immediate value into variable
```

### Reading from Variables

```asm
    GET  R0, counter         ; load variable value into register
    GET  R1, result
```

### Storage Model

Variables are stored differently depending on the target architecture:

| Backend | Storage | Size | Address Range |
|---------|---------|------|---------------|
| x86-64 | Data section after code | 8 bytes | RIP-relative addressing |
| x86-32 | Data section after code | 4 bytes | Absolute addressing |
| ARM | Data section after code | 4 bytes | MOVW/MOVT + LDR/STR via r12 |
| 8051 | Internal RAM (direct) | 1 byte | 0x08–0x7F |

**Rules:**

- Maximum 256 variables per program (120 for 8051 due to RAM limits)
- Variable names follow the same rules as labels
- Variables must be declared before use with `SET` or `GET`
- `VAR` declarations with an initial value emit initialization code at the declaration point
- On 8051, immediate values in `SET` are limited to 8 bits (0–255)

### Example: Using Variables

```asm
    VAR  x, 10
    VAR  y, 20

    GET  R0, x           ; R0 = 10
    GET  R1, y           ; R1 = 20
    ADD  R0, R1          ; R0 = 30
    SET  x, R0           ; x = 30
    HLT
```

---

## Functions

Functions are labels with declared parameter names. The parameter list documents which variables the function expects to be available.

### Defining Functions

```asm
my_function(arg1, arg2):
    GET  R0, arg1
    GET  R1, arg2
    ADD  R0, R1
    RET
```

Both syntaxes are equivalent:
```asm
my_func:          ; plain label (no documented parameters)
my_func(a, b):    ; function definition (parameters a, b documented)
```

### Calling Functions

Functions can be called with `CALL` or using syntactic sugar:

```asm
    CALL my_function         ; standard call
    my_function()            ; syntactic sugar — equivalent to CALL my_function
    CALL my_function(R0, R1) ; call with argument annotations
```

### Rules

- Maximum 8 parameters per function definition
- Parameters must be declared as variables (using `VAR`) before the function is called
- Function definitions are labels — they follow all label rules
- The parameter list is metadata for documentation and validation; the actual argument passing uses `SET`/`GET` on the named variables

### Complete Example

```asm
    JMP  main

    VAR  a
    VAR  b

add(a, b):
    GET  R0, a
    GET  R1, b
    ADD  R0, R1
    RET

main:
    SET  a, 15
    SET  b, 27
    CALL add
    ; R0 now contains 42
    HLT
```

---

## Registers

UA provides 16 virtual registers named `R0` through `R15`. The actual number of usable registers depends on the target backend:

| Backend | Usable Registers | Notes |
|---------|-------------------|-------|
| x86-64 | R0–R7 (8) | R8–R15 rejected (would require REX.B encoding) |
| x86-32 | R0–R7 (8) | Maps to IA-32 32-bit registers |
| ARM | R0–R7 (8) | Maps directly to ARM r0–r7; r12 used as scratch |
| 8051 | R0–R7 (8) | Maps to bank-0 registers |

Register names are **case-insensitive**: `R0`, `r0`, and `R0` are the same register.

### x86-64 Register Mapping

| UA Register | x86-64 Register | Purpose |
|-------------|------------------|---------|
| R0 | RAX | Accumulator / return value |
| R1 | RCX | General purpose |
| R2 | RDX | General purpose |
| R3 | RBX | General purpose (callee-saved) |
| R4 | RSP | Stack pointer |
| R5 | RBP | Base pointer |
| R6 | RSI | General purpose |
| R7 | RDI | General purpose |

> **Warning:** R4 (RSP) and R5 (RBP) are the stack and base pointers. Modifying them directly can corrupt the stack.

### x86-32 (IA-32) Register Mapping

| UA Register | x86-32 Register | Purpose |
|-------------|------------------|--------|
| R0 | EAX | Accumulator / return value |
| R1 | ECX | General purpose |
| R2 | EDX | General purpose |
| R3 | EBX | General purpose (callee-saved) |
| R4 | ESP | Stack pointer |
| R5 | EBP | Base pointer |
| R6 | ESI | General purpose |
| R7 | EDI | General purpose |

> **Warning:** R4 (ESP) and R5 (EBP) are the stack and base pointers. Modifying them directly can corrupt the stack.

### ARM (ARMv7-A) Register Mapping

| UA Register | ARM Register | Purpose |
|-------------|--------------|--------|
| R0 | r0 | General purpose / return value |
| R1 | r1 | General purpose |
| R2 | r2 | General purpose |
| R3 | r3 | General purpose |
| R4 | r4 | General purpose |
| R5 | r5 | General purpose |
| R6 | r6 | General purpose |
| R7 | r7 | General purpose |

> **Note:** ARM r12 (IP) is used internally as a scratch register for large immediates. r13 (SP), r14 (LR), and r15 (PC) are reserved and cannot be used as UA registers.

### 8051 Register Mapping

| UA Register | 8051 Register | Direct Address |
|-------------|---------------|----------------|
| R0 | R0 | 0x00 |
| R1 | R1 | 0x01 |
| R2 | R2 | 0x02 |
| R3 | R3 | 0x03 |
| R4 | R4 | 0x04 |
| R5 | R5 | 0x05 |
| R6 | R6 | 0x06 |
| R7 | R7 | 0x07 |

All registers are in 8051 register bank 0.

### ARM64 (AArch64) Register Mapping

| UA Register | AArch64 Register | Purpose |
|-------------|------------------|--------|
| R0 | X0 | General purpose / return value |
| R1 | X1 | General purpose |
| R2 | X2 | General purpose |
| R3 | X3 | General purpose |
| R4 | X4 | General purpose |
| R5 | X5 | General purpose |
| R6 | X6 | General purpose |
| R7 | X7 | General purpose |

> **Note:** X9 and X10 are used internally as scratch registers. X30 (LR) and X31 (SP/XZR) are reserved.

### RISC-V (RV64I) Register Mapping

| UA Register | RISC-V Register | ABI Name | Purpose |
|-------------|-----------------|----------|--------|
| R0 | x10 | a0 | Argument / return value |
| R1 | x11 | a1 | Argument |
| R2 | x12 | a2 | Argument |
| R3 | x13 | a3 | Argument |
| R4 | x14 | a4 | Argument |
| R5 | x15 | a5 | Argument |
| R6 | x16 | a6 | Argument |
| R7 | x17 | a7 | Argument / syscall number |

> **Note:** x5 (t0) and x6 (t1) are used internally as scratch registers. x0 (zero, hardwired), x1 (ra), and x2 (sp) are reserved.

---

## Numeric Literals

Numbers can be expressed in three bases:

| Format | Prefix | Example | Value |
|--------|--------|---------|-------|
| Decimal | *(none)* | `42`, `-7`, `0` | 42, -7, 0 |
| Hexadecimal | `0x` or `0X` | `0xFF`, `0x1A` | 255, 26 |
| Binary | `0b` or `0B` | `0b1010`, `0B11001100` | 10, 204 |

Immediate values are prefixed with `#` in the instruction:

```asm
    LDI   R0, 42        ; decimal
    LDI   R1, 0xFF      ; hexadecimal
    AND   R0, 0b1111    ; binary mask
```

### Range Limits

| Backend | Immediate Range | Notes |
|---------|----------------|-------|
| x86-64 | -2,147,483,648 to 2,147,483,647 | 32-bit sign-extended to 64-bit |
| x86-32 | -2,147,483,648 to 2,147,483,647 | 32-bit native |
| ARM | -2,147,483,648 to 2,147,483,647 | 32-bit via MOVW/MOVT |
| ARM64 | -2,147,483,648 to 2,147,483,647 | 32-bit via MOVZ/MOVK (up to 64-bit with multiple MOVK) |
| RISC-V | -2,147,483,648 to 2,147,483,647 | 32-bit via LUI+ADDI |
| 8051 | -128 to 255 | 8-bit values |

---

## String Literals

String literals are enclosed in double quotes and used with the `LDS` instruction:

```asm
    LDS  R0, "Hello, World!\n"
```

### Escape Sequences

| Sequence | Character | Byte Value |
|----------|-----------|------------|
| `\n` | Newline | `0x0A` |
| `\t` | Horizontal tab | `0x09` |
| `\r` | Carriage return | `0x0D` |
| `\0` | Null byte | `0x00` |
| `\\` | Backslash | `0x5C` |
| `\"` | Double quote | `0x22` |

Any other character following a backslash is kept as-is.

### Storage

String data is stored in a **string table** appended after the variable data section. Each string is null-terminated. Duplicate string literals are automatically de-duplicated by the backend — identical strings share the same storage.

The layout of the output binary is:

```
[ code section ][ variable data ][ string data ]
```

---

## Standard Libraries

UA ships with standard library files in the `lib/` directory adjacent to the compiler executable. Library files are imported using `@IMPORT` with the `std_` prefix:

```asm
@IMPORT std_io
@IMPORT std_string
```

When a `@IMPORT` path starts with `std_`, the compiler automatically resolves it to the `lib/` directory next to the executable, appending `.ua` if needed.

### std_io

I/O functions for console output. Uses `@IF_ARCH` / `@IF_SYS` precompiler guards to provide platform-specific implementations.

| Function | Description |
|----------|-------------|
| `std_io.print` | Write a null-terminated string to stdout. Pass string address in R0. All registers may be clobbered. |

**Supported platforms:**

| Architecture | System | Syscall Convention |
|-------------|--------|-------------------|
| x86 | linux | `SYSCALL` — RAX=1 (write), RDI=fd, RSI=buf, RDX=count |
| x86 | win32 | Write dispatcher translates Linux convention to `WriteFile` API |
| x86_32 | linux | `INT 0x80` — EAX=4 (write), EBX=fd, ECX=buf, EDX=count |
| arm | linux | `SVC #0` — R7=4 (write), R0=fd, R1=buf, R2=count |
| riscv | linux | `ECALL` — a7=64 (write), a0=fd, a1=buf, a2=count |

ARM64 and 8051 are not yet supported (ARM64: syscall register X8 not accessible; 8051: no OS).

```asm
@IMPORT std_io
    LDS  R0, "Hello, World!\n"
    CALL std_io.print
    HLT
```

### std_string

String manipulation functions. Uses architecture-neutral MVIS instructions and works on all backends.

| Function | Description |
|----------|-------------|
| `std_string.strlen` | Compute the length of a null-terminated string. Pass address in R0, result returned in R1. Clobbers R0, R2, R3. |

> **8051 Note:** `strlen` uses `LOADB` with R0 as the pointer (indirect addressing via `@R0`), which only accesses internal RAM (0x00–0xFF).

```asm
@IMPORT std_string
    LDS  R0, "test"
    CALL std_string.strlen   ; R1 = 4
```

### std_math

Integer math utility functions. Architecture-neutral — works on all backends.

| Function | Description |
|----------|-------------|
| `std_math.pow` | Integer exponentiation. Set `std_math.base` and `std_math.exp`, then `CALL std_math.pow`. Result in R0. |
| `std_math.factorial` | Compute n!. Set `std_math.n`, then `CALL std_math.factorial`. Result in R0. |
| `std_math.max` | Return the larger of two values. Set `std_math.a` and `std_math.b`, then `CALL std_math.max`. Result in R0. |
| `std_math.abs` | Absolute value. Set `std_math.val`, then `CALL std_math.abs`. Result in R0. |

**Example:**

```asm
@IMPORT std_math

    ; Compute 2^10 = 1024
    SET  std_math.base, 2
    SET  std_math.exp, 10
    CALL std_math.pow        ; R0 = 1024

    ; Compute 5! = 120
    SET  std_math.n, 5
    CALL std_math.factorial  ; R0 = 120

    ; max(7, 42) = 42
    SET  std_math.a, 7
    SET  std_math.b, 42
    CALL std_math.max        ; R0 = 42

    ; abs(-15) = 15
    SET  std_math.val, -15
    CALL std_math.abs        ; R0 = 15
```

### std_arrays

Byte-array utility functions for working with `BUFFER`-allocated memory. Architecture-neutral — works on all backends.

| Function | Description |
|----------|-------------|
| `std_arrays.fill_bytes` | Fill a buffer region with a byte value. Set `std_arrays.dst` (address), `std_arrays.count` (length), `std_arrays.value` (byte). |
| `std_arrays.copy_bytes` | Copy bytes between buffers. Set `std_arrays.src` (source address), `std_arrays.dst` (dest address), `std_arrays.count` (length). |

**Example:**

```asm
@IMPORT std_arrays

    BUFFER  my_buf, 32

    ; Fill 32 bytes with 0xFF
    GET  R0, my_buf
    SET  std_arrays.dst, R0
    SET  std_arrays.count, 32
    SET  std_arrays.value, 0xFF
    CALL std_arrays.fill_bytes

    ; Copy first 16 bytes of my_buf to another location
    BUFFER  other_buf, 16
    GET  R0, my_buf
    SET  std_arrays.src, R0
    GET  R0, other_buf
    SET  std_arrays.dst, R0
    SET  std_arrays.count, 16
    CALL std_arrays.copy_bytes
```

---

## Instruction Set

UA defines 37 instructions organized into nine categories. This is the **Minimum Viable Instruction Set (MVIS)**.

### Data Movement

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `MOV` | `MOV Rd, Rs` | Copy register Rs into Rd |
| `LDI` | `LDI Rd, #imm` | Load immediate value into Rd |
| `LOAD` | `LOAD Rd, Rs` | Load from memory: Rd ← \[Rs\] |
| `STORE` | `STORE Rs, Rd` | Store to memory: \[Rd\] ← Rs |
| `LDS` | `LDS Rd, "str"` | Load string address: Rd ← pointer to string literal |
| `LOADB` | `LOADB Rd, Rs` | Load byte from memory: Rd ← zero-extend(byte \[Rs\]) |
| `STOREB` | `STOREB Rs, Rd` | Store byte to memory: byte \[Rd\] ← low byte of Rs |

**Examples:**

```asm
    LDI   R0, 100       ; R0 = 100
    MOV   R1, R0        ; R1 = R0 (copy)
    LOAD  R2, R0        ; R2 = memory[R0]
    STORE R1, R0        ; memory[R0] = R1
```

> **8051 Note:** `LOAD` and `STORE` use indirect addressing (`@Ri`) and require the pointer register to be R0 or R1.

#### LDS — Load String Address

`LDS` loads the address of a null-terminated string literal into a register. The string data is stored after the variable data section in the output binary. Duplicate strings are de-duplicated by the backend.

```asm
    LDS  R0, "Hello, World!\n"   ; R0 = pointer to string data
```

String literals support escape sequences: `\n` (newline), `\t` (tab), `\r` (carriage return), `\0` (null), `\\` (backslash), `\"` (double quote).

> **x86-64 Note:** `LDS` uses `LEA r64, [RIP+disp32]` (RIP-relative addressing). The displacement is patched during code generation.
>
> **x86-32 Note:** `LDS` uses `LEA r32, [disp32]` with an absolute address fixup.
>
> **ARM Note:** `LDS` uses `MOVW`+`MOVT` to load the absolute string address.
>
> **ARM64 Note:** `LDS` uses `MOVZ`+`MOVK` to load the absolute string address.
>
> **RISC-V Note:** `LDS` uses `LUI`+`ADDI` to load the string address.
>
> **8051 Note:** `LDS` emits `MOV DPTR, #imm16` (stub — 8051 has limited string support).

#### LOADB / STOREB — Byte-Granularity Memory Access

`LOADB` reads a single byte from the address in Rs and zero-extends it into Rd. `STOREB` writes the low byte of Rs to the address in Rd.

```asm
    LOADB R1, R0     ; R1 = zero-extend(byte at address R0)
    STOREB R1, R0    ; byte at address R0 = low byte of R1
```

These instructions are essential for traversing null-terminated strings character by character.

### Arithmetic

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `ADD` | `ADD Rd, Rs` or `ADD Rd, #imm` | Rd = Rd + Rs/imm |
| `SUB` | `SUB Rd, Rs` or `SUB Rd, #imm` | Rd = Rd - Rs/imm |
| `MUL` | `MUL Rd, Rs` or `MUL Rd, #imm` | Rd = Rd × Rs/imm |
| `DIV` | `DIV Rd, Rs` or `DIV Rd, #imm` | Rd = Rd ÷ Rs/imm |
| `INC` | `INC Rd` | Rd = Rd + 1 |
| `DEC` | `DEC Rd` | Rd = Rd - 1 |

**Examples:**

```asm
    LDI   R0, 10
    LDI   R1, 3
    ADD   R0, R1         ; R0 = 13
    SUB   R0, 1          ; R0 = 12
    MUL   R0, R1         ; R0 = 36
    DIV   R0, R1         ; R0 = 12
    INC   R0             ; R0 = 13
    DEC   R0             ; R0 = 12
```

> **x86-64 Note:** `DIV` uses signed division (`IDIV`). The polyfill saves and restores RDX because `IDIV` clobbers RDX:RAX.
>
> **8051 Note:** `MUL` and `DIV` use the hardware `MUL AB` / `DIV AB` instructions via the accumulator (A) and B register.

### Bitwise Logic

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `AND` | `AND Rd, Rs` or `AND Rd, #imm` | Rd = Rd & Rs/imm |
| `OR` | `OR Rd, Rs` or `OR Rd, #imm` | Rd = Rd \| Rs/imm |
| `XOR` | `XOR Rd, Rs` or `XOR Rd, #imm` | Rd = Rd ^ Rs/imm |
| `NOT` | `NOT Rd` | Rd = ~Rd (bitwise complement) |

**Examples:**

```asm
    LDI   R0, 0xFF
    AND   R0, 0x0F       ; R0 = 0x0F (mask lower nibble)
    OR    R0, 0xF0       ; R0 = 0xFF
    XOR   R0, 0xFF       ; R0 = 0x00
    NOT   R0             ; R0 = ~R0
```

### Shift Operations

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `SHL` | `SHL Rd, Rs` or `SHL Rd, #imm` | Shift Rd left by Rs/imm bits |
| `SHR` | `SHR Rd, Rs` or `SHR Rd, #imm` | Shift Rd right by Rs/imm bits |

**Examples:**

```asm
    LDI   R0, 1
    SHL   R0, 4          ; R0 = 16 (1 << 4)
    SHR   R0, 2          ; R0 = 4  (16 >> 2)
```

> **x86-64 Note:** Register-based shifts use the CL register. The backend saves/restores RCX automatically.
>
> **8051 Note:** `SHL` emits `RL A` (rotate left), `SHR` emits `RR A` (rotate right), repeated *n* times for immediate operands. These are rotations, not true logical shifts — bits wrap around.

### Comparison

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `CMP` | `CMP Ra, Rb` or `CMP Ra, #imm` | Compare Ra with Rb/imm (sets flags) |

`CMP` performs a subtraction without storing the result. The flags (zero, carry, sign) are set and used by subsequent conditional jumps (`JZ`, `JNZ`, `JL`, `JG`).

**Example:**

```asm
    CMP   R0, R1
    JZ    equal          ; jump if R0 == R1
    JNZ   not_equal      ; jump if R0 != R1
    JL    less           ; jump if R0 < R1 (signed)
    JG    greater        ; jump if R0 > R1 (signed)
```

> **8051 Note:** Register comparison uses `CLR C; SUBB A,Rn`. Immediate comparison uses `CJNE A,#imm,$+3`.

### Control Flow

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `JMP` | `JMP label` | Unconditional jump |
| `JZ` | `JZ label` | Jump if zero flag is set (equal after CMP) |
| `JNZ` | `JNZ label` | Jump if zero flag is clear (not equal after CMP) |
| `JL` | `JL label` | Jump if less (signed, after CMP) |
| `JG` | `JG label` | Jump if greater (signed, after CMP) |
| `CALL` | `CALL label` | Call subroutine (pushes return address) |
| `RET` | `RET` | Return from subroutine |

**Example:**

```asm
    CALL  my_function
    HLT

my_function:
    LDI   R0, 42
    RET
```

> **x86-64 Note:** `JMP`, `JZ`, `JNZ`, `JL`, `JG`, and `CALL` use 32-bit relative offsets (rel32), allowing jumps up to ±2 GB. `JL` emits `0F 8C rel32` (JL near), `JG` emits `0F 8F rel32` (JG near).
>
> **ARM Note:** `JL` emits `BLT` (condition code 0xB), `JG` emits `BGT` (condition code 0xC). Both use 24-bit signed offsets (±32 MB).
>
> **ARM64 Note:** `JL` emits `B.LT` (condition 0xB), `JG` emits `B.GT` (condition 0xC). Both use 19-bit signed offsets (±1 MB).
>
> **RISC-V Note:** `JL` emits `BLT t0, x0` (branch if scratch < 0 after CMP subtraction). `JG` emits `BLT x0, t0` (swapped operands: branch if 0 < scratch, i.e., result > 0). Both use B-type encoding with a 12-bit signed offset.
>
> **8051 Note:** `JMP` and `CALL` use 16-bit absolute addresses (`LJMP`/`LCALL`). `JZ` and `JNZ` use 8-bit relative offsets (range: -128 to +127 bytes). `JL` emits `JC rel8` (2 bytes — carry flag is set by `SUBB` if the first operand is less). `JG` uses a 6-byte polyfill: `JC $+4; JZ $+2; SJMP target` (skip if less or equal, jump if strictly greater).

### Stack Operations

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `PUSH` | `PUSH Rs` | Push register onto the stack |
| `POP` | `POP Rd` | Pop top of stack into register |

**Example:**

```asm
    LDI   R0, 99
    PUSH  R0             ; save R0
    LDI   R0, 0          ; overwrite R0
    POP   R0             ; restore R0 (R0 = 99 again)
```

> **8051 Note:** `PUSH` and `POP` use direct addressing (`PUSH direct` / `POP direct`) with the register's bank-0 address (0x00–0x07).

### System

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `INT` | `INT #imm` | Software interrupt |
| `SYS` | `SYS` | System call (OS-level) |
| `NOP` | `NOP` | No operation |
| `HLT` | `HLT` | Halt execution |

**Examples:**

```asm
    NOP                  ; do nothing
    INT   0x21           ; software interrupt 0x21
    SYS                  ; invoke OS system call
    HLT                  ; stop
```

> **x86-64 Note:** `HLT` generates `RET` (return to caller / OS). `INT` generates the native `INT n` instruction (`CD nn`). `SYS` generates the `SYSCALL` instruction (`0F 05`).
>
> **x86-32 Note:** `SYS` generates `INT 0x80` (`CD 80`) for Linux system calls.
>
> **ARM Note:** `SYS` generates `SVC #0` (supervisor call).
>
> **ARM64 Note:** `SYS` generates `SVC #0`.
>
> **RISC-V Note:** `SYS` generates `ECALL`.
>
> **8051 Note:** `HLT` generates an infinite loop (`SJMP $`, opcode `80 FE`). `INT #n` generates `LCALL` to the interrupt vector address `(n × 8) + 3`. `SYS` is not supported (no operating system on 8051).

### Variable Instructions

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `VAR` | `VAR name` or `VAR name, #imm` | Declare a named variable (optional initializer) |
| `SET` | `SET name, Rs` or `SET name, #imm` | Store a register or immediate into a variable |
| `GET` | `GET Rd, name` | Load a variable's value into a register |

**Examples:**

```asm
    VAR  counter, 0          ; declare with initial value
    VAR  flags               ; declare (default 0)

    LDI  R0, 42
    SET  counter, R0         ; counter = 42
    SET  flags, 0xFF         ; flags = 0xFF

    GET  R1, counter         ; R1 = 42
    GET  R2, flags           ; R2 = 0xFF
```

> **x86-64 Note:** Variables are stored as 8-byte values in a data section appended after code. `SET`/`GET` use RIP-relative MOV instructions with 32-bit displacement.
>
> **x86-32 Note:** Variables are 4-byte values accessed via absolute `[disp32]` addressing.
>
> **ARM Note:** Variables are 4-byte values. The compiler loads the variable address into r12 (scratch) using MOVW+MOVT, then uses LDR/STR for the actual access. For `SET` with an immediate, r11 is also used as a scratch register.
>
> **8051 Note:** Variables occupy one byte each in internal RAM (direct addresses 0x08–0x7F). `SET`/`GET` use direct-addressing MOV instructions. Maximum 120 variables.

### Memory Allocation

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `BUFFER` | `BUFFER name, size` | Allocate a named contiguous byte buffer of the given size |

`BUFFER` reserves a contiguous block of zero-initialized bytes in the data section (or internal RAM on 8051). Unlike `VAR` (which stores a single word), `BUFFER` allocates an arbitrary number of bytes.

**Example:**

```asm
    BUFFER  my_buf, 64       ; allocate 64 bytes named "my_buf"
    GET     R0, my_buf       ; R0 = address of the buffer
    LDI     R1, 0x41         ; 'A'
    STOREB  R1, R0           ; write 'A' to first byte
```

`BUFFER` names follow the same rules as variable and label names. The `size` operand is a mandatory immediate specifying the number of bytes to allocate.

**Storage Model:**

| Backend | Storage | Location |
|---------|---------|----------|
| x86-64 | Data section after code | After variables, before strings (8-byte aligned start address) |
| x86-32 | Data section after code | After variables, before strings |
| ARM | Data section after code | After variables, before strings |
| ARM64 | Data section after code | After variables, before strings |
| RISC-V | Data section after code | After variables, before strings |
| 8051 | Internal RAM | Consecutive bytes starting after variables (0x08+) |

The data layout with buffers is:

```
[ code section ][ variable data ][ buffer data ][ string data ]
```

Accessing buffer contents uses `GET` to obtain the base address, then `LOADB`/`STOREB` with register arithmetic for byte-level access.

> **8051 Note:** Buffer bytes are allocated in internal RAM (direct addresses). Since 8051 RAM is limited to ~120 usable bytes, buffer sizes must be modest. Buffers share the address space with variables.

---

## Operand Rules

Each instruction has a fixed **operand shape** enforced at parse time:

| Shape | Meaning | Example |
|-------|---------|---------|
| `reg` | A register (R0–R15) | `NOT R0` |
| `reg, reg` | Two registers | `MOV R0, R1` |
| `reg, reg_or_imm` | Register + register or immediate | `ADD R0, R1` or `ADD R0, 5` |
| `imm` | An immediate value | `INT 0x21` |
| `label` | A label reference | `JMP start` |
| *(none)* | No operands | `NOP`, `HLT`, `RET` |

| Instruction | Shape |
|-------------|-------|
| MOV | reg, reg |
| LDI | reg, imm |
| LOAD, STORE | reg, reg |
| ADD, SUB, MUL, DIV, AND, OR, XOR, SHL, SHR | reg, reg_or_imm |
| CMP | reg, reg_or_imm |
| NOT, INC, DEC | reg |
| PUSH, POP | reg |
| JMP, JZ, JNZ, JL, JG, CALL | label |
| INT | imm |
| NOP, HLT, RET, SYS | *(none)* |
| LDS | reg, string |
| LOADB, STOREB | reg, reg |
| VAR | name [, imm] |
| SET | name, reg_or_imm |
| GET | reg, name |
| BUFFER | name, size |

Incorrect operand shapes produce a compile-time error with the source line number.

---

## Backend-Specific Notes

### x86-64

- All operations are 64-bit (REX.W prefix)
- `LDI` uses `MOV r64, imm32` (sign-extended to 64-bit)
- `DIV` is signed (`IDIV`) and includes a polyfill to save/restore RDX
- `SHL`/`SHR` with a register operand saves/restores RCX (shift amount must be in CL)
- `LOAD`/`STORE` handle the RSP (SIB byte) and RBP (displacement byte) special cases
- `HLT` emits `RET` (0xC3) — returns control to the JIT runner or OS
- `JL` emits `0F 8C rel32` (6 bytes), `JG` emits `0F 8F rel32` (6 bytes)
- `BUFFER` allocates zero-initialized bytes in the data section after variables

### x86-32 (IA-32)

- All operations are 32-bit (no REX prefix)
- `LDI` uses `MOV r32, imm32` (5 bytes, `B8+rd`)
- `INC`/`DEC` use the single-byte encodings `40+rd`/`48+rd` (not available in 64-bit mode)
- `PUSH`/`POP` use single-byte encodings `50+rd`/`58+rd`
- `DIV` is signed (`IDIV`) using `CDQ` for sign extension (instead of `CQO`)
- `SHL`/`SHR` with a register operand saves/restores ECX
- `LOAD`/`STORE` handle the ESP (SIB byte) and EBP (displacement byte) special cases
- `HLT` emits `RET` (0xC3)
- `JL` emits `0F 8C rel32` (6 bytes), `JG` emits `0F 8F rel32` (6 bytes)
- `BUFFER` allocates zero-initialized bytes in the data section after variables
- No JIT support — use `-arch x86` for JIT execution

### ARM (ARMv7-A)

- All instructions are 32-bit fixed width (4 bytes each)
- All instructions use condition code AL (always execute)
- `LDI` uses `MOVW` for values 0–65535 (4 bytes); adds `MOVT` for larger values (8 bytes total)
- ALU instructions with immediates: if the value fits in ARM’s rotated-imm8 encoding, it is encoded inline; otherwise, the value is loaded into r12 (scratch) first
- `MUL` uses the ARM `MUL` instruction; `DIV` uses `SDIV` (requires ARMv7VE / integer divide extension)
- `SHL`/`SHR` use barrel-shifted MOV (`LSL`/`LSR`)
- `JMP`/`JZ`/`JNZ`/`CALL` use ARM branch instructions with 24-bit signed offsets (±32 MB range)
- Branch offsets account for the ARM pipeline (PC+8)
- `RET` and `HLT` emit `BX LR` (branch to link register)
- `INT #n` emits `SVC #n` (supervisor call)
- `PUSH` emits `STR Rd, [SP, #-4]!` (pre-indexed store with writeback)
- `POP` emits `LDR Rd, [SP], #4` (post-indexed load)
- `NOP` emits the canonical `MOV R0, R0` (0xE1A00000)
- No JIT support on x86 hosts — use for cross-compilation only

### 8051/MCS-51

- All operations are 8-bit
- Arithmetic and logic route through the accumulator (A register)
- `LOAD`/`STORE` use indirect addressing (`@R0` or `@R1` only)
- `MUL`/`DIV` use the `MUL AB` / `DIV AB` hardware instructions via the B register
- `SHL`/`SHR` use rotate instructions (`RL A` / `RR A`) — bits wrap around
- `JZ`/`JNZ` are limited to ±127 bytes (8-bit relative offset)
- `JL` emits `JC rel8` (2 bytes — carry flag set by `SUBB` means less-than)
- `JG` uses a 6-byte polyfill: `JC $+4; JZ $+2; SJMP target` (skip if less or equal, jump if strictly greater)
- `BUFFER` allocates consecutive bytes in internal RAM (shares address space with variables)
- `JMP`/`CALL` use 16-bit absolute addressing (`LJMP`/`LCALL`)
- `INT #n` is polyfilled as `LCALL (n*8)+3` (standard interrupt vector table layout)
- `HLT` emits `SJMP $` (0x80, 0xFE) — infinite self-loop
