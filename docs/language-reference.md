# UA Language Reference

This document is the complete reference for the **Unified Assembly (UA)** instruction set — the portable assembly language used by the UA compiler.

---

## Table of Contents

1. [Source File Format](#source-file-format)
2. [Comments](#comments)
3. [Labels](#labels)
4. [Registers](#registers)
5. [Numeric Literals](#numeric-literals)
6. [Instruction Set](#instruction-set)
   - [Data Movement](#data-movement)
   - [Arithmetic](#arithmetic)
   - [Bitwise Logic](#bitwise-logic)
   - [Shift Operations](#shift-operations)
   - [Comparison](#comparison)
   - [Control Flow](#control-flow)
   - [Stack Operations](#stack-operations)
   - [System](#system)
7. [Operand Rules](#operand-rules)
8. [Backend-Specific Notes](#backend-specific-notes)

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

- Labels consist of letters (`a-z`, `A-Z`), digits (`0-9`), and underscores (`_`)
- Labels must start with a letter or underscore
- Maximum length: 64 characters
- Labels are **case-sensitive**
- Duplicate labels within a file are an error
- Forward references are supported (two-pass assembly)

---

## Registers

UA provides 16 virtual registers named `R0` through `R15`. The actual number of usable registers depends on the target backend:

| Backend | Usable Registers | Notes |
|---------|-------------------|-------|
| x86-64 | R0–R7 (8) | R8–R15 rejected (would require REX.B encoding) |
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
| 8051 | -128 to 255 | 8-bit values |

---

## Instruction Set

UA defines 27 instructions organized into seven categories. This is the **Minimum Viable Instruction Set (MVIS)**.

### Data Movement

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `MOV` | `MOV Rd, Rs` | Copy register Rs into Rd |
| `LDI` | `LDI Rd, #imm` | Load immediate value into Rd |
| `LOAD` | `LOAD Rd, Rs` | Load from memory: Rd ← \[Rs\] |
| `STORE` | `STORE Rs, Rd` | Store to memory: \[Rd\] ← Rs |

**Examples:**

```asm
    LDI   R0, 100       ; R0 = 100
    MOV   R1, R0        ; R1 = R0 (copy)
    LOAD  R2, R0        ; R2 = memory[R0]
    STORE R1, R0        ; memory[R0] = R1
```

> **8051 Note:** `LOAD` and `STORE` use indirect addressing (`@Ri`) and require the pointer register to be R0 or R1.

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

`CMP` performs a subtraction without storing the result. The flags (zero, carry) are set and used by subsequent conditional jumps (`JZ`, `JNZ`).

**Example:**

```asm
    CMP   R0, R1
    JZ    equal          ; jump if R0 == R1
    JNZ   not_equal      ; jump if R0 != R1
```

> **8051 Note:** Register comparison uses `CLR C; SUBB A,Rn`. Immediate comparison uses `CJNE A,#imm,$+3`.

### Control Flow

| Mnemonic | Syntax | Description |
|----------|--------|-------------|
| `JMP` | `JMP label` | Unconditional jump |
| `JZ` | `JZ label` | Jump if zero flag is set |
| `JNZ` | `JNZ label` | Jump if zero flag is clear |
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

> **x86-64 Note:** `JMP`, `JZ`, `JNZ`, and `CALL` use 32-bit relative offsets (rel32), allowing jumps up to ±2 GB.
>
> **8051 Note:** `JMP` and `CALL` use 16-bit absolute addresses (`LJMP`/`LCALL`). `JZ` and `JNZ` use 8-bit relative offsets (range: -128 to +127 bytes from the next instruction).

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
| `NOP` | `NOP` | No operation |
| `HLT` | `HLT` | Halt execution |

**Examples:**

```asm
    NOP                  ; do nothing
    INT   0x21           ; software interrupt 0x21
    HLT                  ; stop
```

> **x86-64 Note:** `HLT` generates `RET` (return to caller / OS). `INT` generates the native `INT n` instruction (`CD nn`).
>
> **8051 Note:** `HLT` generates an infinite loop (`SJMP $`, opcode `80 FE`). `INT #n` generates `LCALL` to the interrupt vector address `(n × 8) + 3`.

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
| JMP, JZ, JNZ, CALL | label |
| INT | imm |
| NOP, HLT, RET | *(none)* |

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

### 8051/MCS-51

- All operations are 8-bit
- Arithmetic and logic route through the accumulator (A register)
- `LOAD`/`STORE` use indirect addressing (`@R0` or `@R1` only)
- `MUL`/`DIV` use the `MUL AB` / `DIV AB` hardware instructions via the B register
- `SHL`/`SHR` use rotate instructions (`RL A` / `RR A`) — bits wrap around
- `JZ`/`JNZ` are limited to ±127 bytes (8-bit relative offset)
- `JMP`/`CALL` use 16-bit absolute addressing (`LJMP`/`LCALL`)
- `INT #n` is polyfilled as `LCALL (n*8)+3` (standard interrupt vector table layout)
- `HLT` emits `SJMP $` (0x80, 0xFE) — infinite self-loop
