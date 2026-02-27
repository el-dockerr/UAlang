# MVIS — Minimum Viable Instruction Set

The **Minimum Viable Instruction Set (MVIS)** is the core of the UA language. These 37 instructions are **fully portable** — they work identically on every supported architecture. The compiler translates each MVIS opcode into the equivalent native machine instructions for the target CPU.

> **MVIS Doctrine:** Whether targeting a 64-bit desktop processor, an ARM mobile chip, or a tiny 8-bit 8051 microcontroller, the core logic remains identical. The compiler handles the architectural polyfills behind the scenes, keeping the language pure and unified.

---

## Table of Contents

1. [Instruction Summary](#instruction-summary)
2. [Data Movement](#data-movement)
   - [MOV — Register Copy](#mov--register-copy)
   - [LDI — Load Immediate](#ldi--load-immediate)
   - [LOAD — Memory Read](#load--memory-read)
   - [STORE — Memory Write](#store--memory-write)
   - [LDS — Load String Address](#lds--load-string-address)
   - [LOADB — Load Byte](#loadb--load-byte)
   - [STOREB — Store Byte](#storeb--store-byte)
3. [Arithmetic](#arithmetic)
   - [ADD — Addition](#add--addition)
   - [SUB — Subtraction](#sub--subtraction)
   - [MUL — Multiplication](#mul--multiplication)
   - [DIV — Division](#div--division)
   - [INC — Increment](#inc--increment)
   - [DEC — Decrement](#dec--decrement)
4. [Bitwise Logic](#bitwise-logic)
   - [AND — Bitwise AND](#and--bitwise-and)
   - [OR — Bitwise OR](#or--bitwise-or)
   - [XOR — Bitwise XOR](#xor--bitwise-xor)
   - [NOT — Bitwise Complement](#not--bitwise-complement)
5. [Shift Operations](#shift-operations)
   - [SHL — Shift Left](#shl--shift-left)
   - [SHR — Shift Right](#shr--shift-right)
6. [Comparison](#comparison)
   - [CMP — Compare](#cmp--compare)
7. [Control Flow](#control-flow)
   - [JMP — Unconditional Jump](#jmp--unconditional-jump)
   - [JZ — Jump if Zero](#jz--jump-if-zero)
   - [JNZ — Jump if Not Zero](#jnz--jump-if-not-zero)
   - [JL — Jump if Less](#jl--jump-if-less)
   - [JG — Jump if Greater](#jg--jump-if-greater)
   - [CALL — Call Subroutine](#call--call-subroutine)
   - [RET — Return](#ret--return)
8. [Stack Operations](#stack-operations)
   - [PUSH — Push Register](#push--push-register)
   - [POP — Pop Register](#pop--pop-register)
9. [System & Control](#system--control)
   - [INT — Software Interrupt](#int--software-interrupt)
   - [SYS — System Call](#sys--system-call)
   - [NOP — No Operation](#nop--no-operation)
   - [HLT — Halt](#hlt--halt)
10. [Variables & Memory](#variables--memory)
    - [VAR — Declare Variable](#var--declare-variable)
    - [SET — Store to Variable](#set--store-to-variable)
    - [GET — Load from Variable](#get--load-from-variable)
    - [BUFFER — Allocate Byte Buffer](#buffer--allocate-byte-buffer)
11. [Operand Shape Reference](#operand-shape-reference)

---

## Instruction Summary

| # | Mnemonic | Category | Operands | Description |
|---|----------|----------|----------|-------------|
| 1 | `MOV` | Data Movement | Rd, Rs | Copy register to register |
| 2 | `LDI` | Data Movement | Rd, #imm | Load immediate value into register |
| 3 | `LOAD` | Data Movement | Rd, Rs | Load word from memory address in Rs |
| 4 | `STORE` | Data Movement | Rs, Rd | Store word to memory address in Rd |
| 5 | `LDS` | Data Movement | Rd, "str" | Load address of string literal |
| 6 | `LOADB` | Data Movement | Rd, Rs | Load byte from memory, zero-extend |
| 7 | `STOREB` | Data Movement | Rs, Rd | Store low byte to memory |
| 8 | `ADD` | Arithmetic | Rd, Rs/imm | Rd = Rd + Rs or Rd + imm |
| 9 | `SUB` | Arithmetic | Rd, Rs/imm | Rd = Rd - Rs or Rd - imm |
| 10 | `MUL` | Arithmetic | Rd, Rs/imm | Rd = Rd * Rs or Rd * imm |
| 11 | `DIV` | Arithmetic | Rd, Rs/imm | Rd = Rd / Rs or Rd / imm |
| 12 | `INC` | Arithmetic | Rd | Rd = Rd + 1 |
| 13 | `DEC` | Arithmetic | Rd | Rd = Rd - 1 |
| 14 | `AND` | Bitwise | Rd, Rs/imm | Rd = Rd & Rs or Rd & imm |
| 15 | `OR` | Bitwise | Rd, Rs/imm | Rd = Rd \| Rs or Rd \| imm |
| 16 | `XOR` | Bitwise | Rd, Rs/imm | Rd = Rd ^ Rs or Rd ^ imm |
| 17 | `NOT` | Bitwise | Rd | Rd = ~Rd |
| 18 | `SHL` | Shift | Rd, Rs/imm | Shift Rd left by Rs/imm bits |
| 19 | `SHR` | Shift | Rd, Rs/imm | Shift Rd right by Rs/imm bits |
| 20 | `CMP` | Comparison | Ra, Rb/imm | Compare (sets flags for jumps) |
| 21 | `JMP` | Control Flow | label | Unconditional jump |
| 22 | `JZ` | Control Flow | label | Jump if equal (zero flag) |
| 23 | `JNZ` | Control Flow | label | Jump if not equal |
| 24 | `JL` | Control Flow | label | Jump if less (signed) |
| 25 | `JG` | Control Flow | label | Jump if greater (signed) |
| 26 | `CALL` | Control Flow | label | Call subroutine |
| 27 | `RET` | Control Flow | *(none)* | Return from subroutine |
| 28 | `PUSH` | Stack | Rs | Push register onto stack |
| 29 | `POP` | Stack | Rd | Pop stack into register |
| 30 | `INT` | System | #imm | Software interrupt |
| 31 | `SYS` | System | *(none)* | OS system call |
| 32 | `NOP` | System | *(none)* | No operation |
| 33 | `HLT` | System | *(none)* | Halt execution |
| 34 | `VAR` | Variables | name [, imm] | Declare named variable |
| 35 | `SET` | Variables | name, Rs/imm | Store to variable |
| 36 | `GET` | Variables | Rd, name | Load from variable |
| 37 | `BUFFER` | Memory | name, size | Allocate byte buffer |

---

## Data Movement

### MOV — Register Copy

```
MOV  Rd, Rs
```

Copies the value of register Rs into register Rd. The original value in Rs is unchanged.

| Field | Value |
|-------|-------|
| Operands | 2 registers |
| Flags affected | None |
| Architectures | All |

```asm
    LDI  R0, 42
    MOV  R1, R0     ; R1 = 42, R0 still = 42
```

**Backend translations:**

| Architecture | Native Instruction |
|-------------|-------------------|
| x86-64 | `MOV r64, r64` (REX.W 89 ModRM) |
| x86-32 | `MOV r32, r32` (89 ModRM) |
| ARM | `MOV Rd, Rs` (data processing) |
| ARM64 | `MOV Xd, Xn` (ORR Xd, XZR, Xn) |
| RISC-V | `MV rd, rs` (ADDI rd, rs, 0) |
| 8051 | `MOV A, Rs; MOV Rd, A` (via accumulator) |

---

### LDI — Load Immediate

```
LDI  Rd, #imm
```

Loads a constant numeric value into register Rd.

| Field | Value |
|-------|-------|
| Operands | 1 register, 1 immediate |
| Flags affected | None |
| Architectures | All |

```asm
    LDI  R0, 42          ; decimal
    LDI  R1, 0xFF        ; hexadecimal
    LDI  R2, 0b1010      ; binary
    LDI  R3, -1           ; negative
```

**Immediate ranges:**

| Architecture | Range | Notes |
|-------------|-------|-------|
| x86-64 | ±2 billion (32-bit, sign-extended to 64) | `MOV r64, imm32` |
| x86-32 | ±2 billion (32-bit native) | `MOV r32, imm32` |
| ARM | Full 32-bit | MOVW (+ MOVT if > 16 bits) |
| ARM64 | Full 32-bit | MOVZ (+ MOVK if > 16 bits) |
| RISC-V | Full 32-bit | LUI + ADDI |
| 8051 | 0–255 (8-bit) | `MOV Rn, #imm8` |

---

### LOAD — Memory Read

```
LOAD  Rd, Rs
```

Reads a word from the memory address stored in Rs and places it in Rd.

| Field | Value |
|-------|-------|
| Operands | 2 registers |
| Flags affected | None |
| Architectures | All |

```asm
    LOAD  R1, R0     ; R1 = memory[R0]
```

> **8051 restriction:** The pointer register (Rs) must be `R0` or `R1` due to indirect addressing limitations (`@R0`/`@R1` only).

---

### STORE — Memory Write

```
STORE  Rs, Rd
```

Writes the value of Rs to the memory address stored in Rd.

| Field | Value |
|-------|-------|
| Operands | 2 registers |
| Flags affected | None |
| Architectures | All |

```asm
    STORE  R1, R0    ; memory[R0] = R1
```

> **8051 restriction:** Same as LOAD — the destination address register must be `R0` or `R1`.

---

### LDS — Load String Address

```
LDS  Rd, "string literal"
```

Loads the memory address of a null-terminated string literal into Rd. The string data is stored in a string table appended to the output binary.

| Field | Value |
|-------|-------|
| Operands | 1 register, 1 string literal |
| Flags affected | None |
| Architectures | All |

```asm
    LDS  R0, "Hello, World!\n"   ; R0 = pointer to "Hello, World!\n"
```

Supported escape sequences: `\n`, `\t`, `\r`, `\0`, `\\`, `\"`

Duplicate string literals are automatically de-duplicated — identical strings share storage.

---

### LOADB — Load Byte

```
LOADB  Rd, Rs
```

Reads a single byte from the address in Rs, zero-extends it to the register width, and stores it in Rd. Essential for traversing strings character by character.

| Field | Value |
|-------|-------|
| Operands | 2 registers |
| Flags affected | None |
| Architectures | All |

```asm
    LOADB  R1, R0    ; R1 = zero_extend(byte at address R0)
```

**Backend translations:**

| Architecture | Native Instruction |
|-------------|-------------------|
| x86-64 | `MOVZX r64, byte [r64]` |
| x86-32 | `MOVZX r32, byte [r32]` |
| ARM | `LDRB Rd, [Rs]` |
| ARM64 | `LDRB Wd, [Xn]` |
| RISC-V | `LBU rd, 0(rs)` |
| 8051 | `MOV A, @Ri` (Rs must be R0 or R1) |

---

### STOREB — Store Byte

```
STOREB  Rs, Rd
```

Writes the low byte of Rs to the memory address in Rd.

| Field | Value |
|-------|-------|
| Operands | 2 registers |
| Flags affected | None |
| Architectures | All |

```asm
    STOREB  R1, R0   ; byte at address R0 = low byte of R1
```

---

## Arithmetic

### ADD — Addition

```
ADD  Rd, Rs       ; Rd = Rd + Rs
ADD  Rd, #imm     ; Rd = Rd + imm
```

| Field | Value |
|-------|-------|
| Operands | 1 register + 1 register or immediate |
| Flags affected | Zero, carry, overflow |
| Architectures | All |

```asm
    LDI  R0, 10
    ADD  R0, 5       ; R0 = 15
    ADD  R0, R1      ; R0 = R0 + R1
```

---

### SUB — Subtraction

```
SUB  Rd, Rs       ; Rd = Rd - Rs
SUB  Rd, #imm     ; Rd = Rd - imm
```

| Field | Value |
|-------|-------|
| Operands | 1 register + 1 register or immediate |
| Flags affected | Zero, carry, overflow |
| Architectures | All |

```asm
    LDI  R0, 20
    SUB  R0, 8       ; R0 = 12
```

---

### MUL — Multiplication

```
MUL  Rd, Rs       ; Rd = Rd * Rs
MUL  Rd, #imm     ; Rd = Rd * imm
```

| Field | Value |
|-------|-------|
| Operands | 1 register + 1 register or immediate |
| Flags affected | Varies by architecture |
| Architectures | All |

```asm
    LDI  R0, 6
    MUL  R0, 7       ; R0 = 42
```

> **8051 Note:** Uses `MUL AB` — result limited to 16 bits (high byte in B, low byte in A).

---

### DIV — Division

```
DIV  Rd, Rs       ; Rd = Rd / Rs
DIV  Rd, #imm     ; Rd = Rd / imm
```

| Field | Value |
|-------|-------|
| Operands | 1 register + 1 register or immediate |
| Flags affected | Varies by architecture |
| Architectures | All |

```asm
    LDI  R0, 42
    DIV  R0, 6       ; R0 = 7
```

> **x86 Note:** Uses signed division (`IDIV`). The backend saves/restores RDX (clobbered by IDIV).
>
> **8051 Note:** Uses `DIV AB` — 8-bit unsigned division.

---

### INC — Increment

```
INC  Rd           ; Rd = Rd + 1
```

| Field | Value |
|-------|-------|
| Operands | 1 register |
| Flags affected | Zero, overflow |
| Architectures | All |

---

### DEC — Decrement

```
DEC  Rd           ; Rd = Rd - 1
```

| Field | Value |
|-------|-------|
| Operands | 1 register |
| Flags affected | Zero, overflow |
| Architectures | All |

---

## Bitwise Logic

### AND — Bitwise AND

```
AND  Rd, Rs       ; Rd = Rd & Rs
AND  Rd, #imm     ; Rd = Rd & imm
```

Commonly used for masking bits:

```asm
    LDI  R0, 0xFF
    AND  R0, 0x0F    ; R0 = 0x0F (keep low nibble only)
```

---

### OR — Bitwise OR

```
OR  Rd, Rs        ; Rd = Rd | Rs
OR  Rd, #imm      ; Rd = Rd | imm
```

Commonly used for setting bits:

```asm
    OR   R0, 0x80    ; set bit 7
```

---

### XOR — Bitwise XOR

```
XOR  Rd, Rs       ; Rd = Rd ^ Rs
XOR  Rd, #imm     ; Rd = Rd ^ imm
```

Commonly used for toggling bits or clearing a register:

```asm
    XOR  R0, R0      ; R0 = 0 (common zeroing idiom)
```

---

### NOT — Bitwise Complement

```
NOT  Rd           ; Rd = ~Rd
```

Flips all bits in Rd.

---

## Shift Operations

### SHL — Shift Left

```
SHL  Rd, Rs       ; Rd = Rd << Rs
SHL  Rd, #imm     ; Rd = Rd << imm
```

Shifts Rd left by the specified number of bits. Vacated bits are filled with zeros. Equivalent to multiplying by powers of 2.

```asm
    LDI  R0, 1
    SHL  R0, 4       ; R0 = 16  (1 << 4)
```

> **8051 Note:** Implemented using rotate instructions (`RL A`). Bits wrap around instead of being discarded.

---

### SHR — Shift Right

```
SHR  Rd, Rs       ; Rd = Rd >> Rs
SHR  Rd, #imm     ; Rd = Rd >> imm
```

Shifts Rd right by the specified number of bits. This is a logical shift (zero-fill).

```asm
    LDI  R0, 16
    SHR  R0, 2       ; R0 = 4  (16 >> 2)
```

---

## Comparison

### CMP — Compare

```
CMP  Ra, Rb       ; compare Ra with Rb
CMP  Ra, #imm     ; compare Ra with immediate
```

Performs a subtraction (Ra - Rb) **without storing the result**. Only the CPU flags are updated. Use conditional jumps immediately after `CMP`:

```asm
    CMP  R0, R1
    JZ   equal       ; jump if R0 == R1
    JNZ  not_equal   ; jump if R0 != R1
    JL   less        ; jump if R0 < R1 (signed)
    JG   greater     ; jump if R0 > R1 (signed)
```

> **8051 Note:** Register comparison uses `CLR C; SUBB A, Rn`. Immediate comparison uses `CJNE A, #imm, $+3`.

---

## Control Flow

### JMP — Unconditional Jump

```
JMP  label
```

Jumps to the specified label unconditionally. Forward references are supported.

---

### JZ — Jump if Zero

```
JZ  label
```

Jumps to label if the zero flag is set (typically after `CMP Ra, Rb` where Ra == Rb).

---

### JNZ — Jump if Not Zero

```
JNZ  label
```

Jumps to label if the zero flag is clear (Ra != Rb after CMP).

---

### JL — Jump if Less

```
JL  label
```

Jumps to label if the previous comparison showed Ra < Rb (signed).

| Architecture | Native Encoding |
|-------------|----------------|
| x86-64/32 | `JL rel32` (0F 8C) |
| ARM | `BLT` (condition 0xB) |
| ARM64 | `B.LT` (condition 0xB) |
| RISC-V | `BLT t0, x0, offset` |
| 8051 | `JC rel8` (carry = less-than after SUBB) |

---

### JG — Jump if Greater

```
JG  label
```

Jumps to label if the previous comparison showed Ra > Rb (signed).

> **8051 Note:** Implemented as a 6-byte polyfill: `JC $+4; JZ $+2; SJMP target` (skip if less-or-equal, then jump).

---

### CALL — Call Subroutine

```
CALL  label
```

Pushes the return address onto the stack (or link register on ARM/RISC-V) and jumps to the label. Use `RET` to return.

---

### RET — Return

```
RET
```

Returns from a subroutine by popping the return address and jumping to it.

| Architecture | Native Instruction |
|-------------|-------------------|
| x86-64/32 | `RET` (0xC3) |
| ARM | `BX LR` |
| ARM64 | `RET` (BR X30) |
| RISC-V | `JALR x0, ra, 0` |
| 8051 | `RET` (0x22) |

---

## Stack Operations

### PUSH — Push Register

```
PUSH  Rs
```

Pushes the value of Rs onto the stack. The stack pointer is decremented.

```asm
    PUSH R0          ; save R0
```

---

### POP — Pop Register

```
POP  Rd
```

Pops the top value from the stack into Rd. The stack pointer is incremented.

```asm
    POP  R0          ; restore R0
```

> **Important:** Stack operations are LIFO — pop in the reverse order of push.

---

## System & Control

### INT — Software Interrupt

```
INT  #imm
```

Triggers a software interrupt with the given vector number.

| Architecture | Native Instruction |
|-------------|-------------------|
| x86-64/32 | `INT n` (0xCD, n) |
| ARM | `SVC #n` |
| ARM64 | `SVC #n` |
| RISC-V | `ECALL` |
| 8051 | `LCALL (n*8)+3` (interrupt vector polyfill) |

---

### SYS — System Call

```
SYS
```

Invokes the operating system's system call mechanism. Register conventions vary by architecture and OS — refer to the standard library sources for details.

| Architecture + System | Native Instruction |
|----------------------|-------------------|
| x86-64 Linux | `SYSCALL` (0F 05) |
| x86-64 Win32 | `CALL write_dispatcher` |
| x86-32 Linux | `INT 0x80` (CD 80) |
| ARM Linux | `SVC #0` |
| ARM64 | `SVC #0` |
| RISC-V | `ECALL` |
| 8051 | **Not supported** (no OS) |

---

### NOP — No Operation

```
NOP
```

Does nothing. Useful for alignment or as a placeholder during development.

---

### HLT — Halt

```
HLT
```

Stops program execution.

| Architecture | Native Instruction |
|-------------|-------------------|
| x86-64/32 | `RET` (returns to OS/JIT runner) |
| ARM | `BX LR` |
| ARM64 | `RET` |
| RISC-V | `JALR x0, ra, 0` |
| 8051 | `SJMP $` (infinite self-loop) |

---

## Variables & Memory

### VAR — Declare Variable

```
VAR  name              ; declare with default value 0
VAR  name, #imm        ; declare with initial value
```

Declares a named storage location. Variables persist across function calls and are accessible from anywhere.

```asm
    VAR  counter, 0
    VAR  result
```

| Architecture | Storage | Size |
|-------------|---------|------|
| x86-64 | Data section | 8 bytes |
| x86-32 | Data section | 4 bytes |
| ARM/ARM64 | Data section | 4/8 bytes |
| RISC-V | Data section | 8 bytes |
| 8051 | Internal RAM | 1 byte |

---

### SET — Store to Variable

```
SET  name, Rs         ; variable = register value
SET  name, #imm       ; variable = immediate value
```

---

### GET — Load from Variable

```
GET  Rd, name         ; register = variable value
```

---

### BUFFER — Allocate Byte Buffer

```
BUFFER  name, size
```

Allocates a contiguous block of zero-initialized bytes. Access the buffer's base address with `GET`, then use `LOADB`/`STOREB` for byte-level access.

```asm
    BUFFER  data, 64         ; allocate 64 bytes
    GET     R0, data         ; R0 = base address
    LDI     R1, 0x41         ; 'A'
    STOREB  R1, R0           ; data[0] = 'A'
```

---

## Operand Shape Reference

Every instruction has a fixed operand shape enforced at compile time:

| Shape | Meaning | Example Instructions |
|-------|---------|---------------------|
| *(none)* | No operands | `NOP`, `HLT`, `RET`, `SYS` |
| `reg` | One register | `NOT`, `INC`, `DEC`, `PUSH`, `POP` |
| `reg, reg` | Two registers | `MOV`, `LOAD`, `STORE`, `LOADB`, `STOREB` |
| `reg, reg/imm` | Register + register or immediate | `ADD`, `SUB`, `MUL`, `DIV`, `AND`, `OR`, `XOR`, `SHL`, `SHR`, `CMP` |
| `reg, imm` | Register + immediate | `LDI` |
| `reg, string` | Register + string literal | `LDS` |
| `label` | Label reference | `JMP`, `JZ`, `JNZ`, `JL`, `JG`, `CALL` |
| `imm` | Immediate only | `INT` |
| `name [, imm]` | Variable name (optional init) | `VAR` |
| `name, reg/imm` | Variable name + register/immediate | `SET` |
| `reg, name` | Register + variable name | `GET` |
| `name, size` | Buffer name + size immediate | `BUFFER` |

Incorrect operand shapes produce a compile-time error:

```
  parser: line 12: shape mismatch for ADD — expected register, got label
```
