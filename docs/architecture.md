# UA Internal Architecture

This document describes the internal design of the UA compiler — its pipeline, data structures, and how each stage transforms source code into native machine code.

---

## Table of Contents

1. [Pipeline Overview](#pipeline-overview)
2. [Stage 0: Precompiler](#stage-0-precompiler)
3. [Stage 1: Lexer](#stage-1-lexer)
4. [Stage 2: Parser / IR](#stage-2-parser--ir)
5. [Stage 3: Backend Code Generation](#stage-3-backend-code-generation)
   - [Two-Pass Assembly](#two-pass-assembly)
   - [x86-64 Backend](#x86-64-backend)   - [x86-32 (IA-32) Backend](#x86-32-ia-32-backend)
   - [ARM (ARMv7-A) Backend](#arm-armv7-a-backend)   - [8051 Backend](#8051-backend)
5. [Stage 4: Output](#stage-4-output)
   - [Raw Binary](#raw-binary)
   - [PE Emitter](#pe-emitter)
   - [ELF Emitter](#elf-emitter)
   - [JIT Executor](#jit-executor)
6. [Key Data Structures](#key-data-structures)
7. [Source File Map](#source-file-map)
8. [Design Decisions](#design-decisions)

---

## Pipeline Overview

UA uses a classic five-stage pipeline. Each stage is a pure function that transforms one representation into the next:

```
 Source Text (.UA)
       │
       ▼
 ┌──────────────┐
 │ Precompiler  │   preprocess()
 │ precompiler  │─────────────────► Preprocessed Source Text
 │    .c        │              (directives evaluated, imports inlined)
 └──────────────┘
       │
       ▼
 ┌───────────┐
 │   Lexer   │   tokenize()
 │  lexer.c  │──────────────────► Token[]
 └───────────┘                    (flat array of typed tokens)
       │
       ▼
 ┌───────────┐
 │  Parser   │   parse()
 │ parser.c  │──────────────────► Instruction[]
 └───────────┘                    (architecture-neutral IR)
       │
       ▼
 ┌───────────────┐
 │  Compliance   │   validate_opcode_compliance()
 │   main.c      │──────────────► Pass / Fail
 └───────────────┘   (checks every opcode against arch/sys bitmask table)
       │
       ▼
 ┌───────────────┐
 │   Backend     │   generate_x86_64() / generate_x86_32() / generate_arm()
 │ backend_*.c   │   generate_arm64() / generate_risc_v() / generate_8051()
 └───────────────┘────────────────► CodeBuffer
                               (raw machine code bytes)
       │
       ▼
 ┌───────────────┐
 │   Output      │   write_binary() / emit_pe_exe() / emit_elf_exe() /
 │  main.c /     │   emit_macho_exe() / execute_jit()
 │  emitter_*.c  │──────────────► File on disk or JIT execution
 └───────────────┘
```

Each stage is independent: the precompiler knows nothing about tokens, the lexer knows nothing about architectures, the parser knows nothing about machine encodings, the compliance checker bridges the IR and target config, and the backends know nothing about file formats.

---

## Stage 0: Precompiler

**Files:** `precompiler.h`, `precompiler.c`

The precompiler runs before the lexer and performs a text-to-text transformation.  It evaluates `@`-directives and produces a clean source string ready for tokenization.

### Directives

| Directive | Purpose |
|-----------|--------|
| `@IF_ARCH <arch>` | Push conditional — include block only when `-arch` matches |
| `@IF_SYS <system>` | Push conditional — include block only when `-sys` matches |
| `@ENDIF` | Pop one conditional level |
| `@IMPORT <path>` | Include another `.ua` file (at most once per unique path) |
| `@DUMMY [message]` | Emit a stub diagnostic to stderr; no code generated |
| `@arch_only <a>,<b>,...` | Abort compilation unless `-arch` matches one listed name |
| `@sys_only <s>,<t>,...` | Abort compilation unless `-sys` matches one listed name |

### Conditional Nesting

Conditionals use a two-counter scheme:

- **`total_depth`** — incremented on every `@IF_*`, decremented on `@ENDIF`
- **`active_depth`** — how many nested levels have their condition satisfied

A line is in an *active* region if and only if `active_depth == total_depth`.  This allows arbitrarily nested blocks (up to 64 levels) without a boolean stack.

### Import De-duplication

The precompiler maintains a list of normalised file paths that have been imported.  When `@IMPORT` is encountered:

1. The path is resolved relative to the importing file's directory
2. Separators are normalised to `/`
3. If the resolved path is already in the imported list → skip silently
4. Otherwise → read the file, recursively preprocess it, and append to the output

Import depth is limited to 16 levels to prevent circular references.

### Line Preservation

Directive lines and inactive (conditionally excluded) lines are replaced by blank lines in the output.  This preserves line numbering so that subsequent lexer/parser error messages reference correct line numbers in the original source file.

### Architecture & System Guards

`@arch_only` and `@sys_only` are processed in active regions (they are not conditional-nesting directives).  When encountered:

1. Parse the comma-separated list of architecture/system names
2. Compare each token (case-insensitive) against the current `-arch`/`-sys`
3. If **none** match → print a diagnostic and return `-1` (fatal error)
4. If a match is found → emit a blank line and continue normally

For `@sys_only`, if no `-sys` flag was specified on the command line, the directive always fails with a dedicated error message.

---

## Stage 1: Lexer

**Files:** `lexer.h`, `lexer.c`

The lexer performs single-pass, left-to-right scanning of the source text. It produces a flat array of `Token` structures.

### Token Types

| Type | Examples | Description |
|------|---------|-------------|
| `TOKEN_OPCODE` | `ADD`, `LDI`, `JMP` | One of the 37 recognized mnemonics |
| `TOKEN_REGISTER` | `R0`, `R15`, `r7` | Register name (case-insensitive) |
| `TOKEN_NUMBER` | `42`, `0xFF`, `0b1010` | Numeric literal |
| `TOKEN_LABEL` | `loop:`, `start:` | Label definition (with colon) |
| `TOKEN_IDENTIFIER` | `loop`, `start` | Label reference (without colon) |
| `TOKEN_COMMA` | `,` | Operand separator |
| `TOKEN_HASH` | `#` | Immediate prefix (optional) |
| `TOKEN_NEWLINE` | `\n` | Line terminator |
| `TOKEN_COMMENT` | `; text` | Discarded by parser |
| `TOKEN_EOF` | — | End of input |

### How It Works

1. Skip whitespace (spaces, tabs)
2. If `;` → consume to end of line, emit `TOKEN_COMMENT`
3. If letter → read identifier, check against `OPCODES[]` table (37 entries) → emit `TOKEN_OPCODE` or `TOKEN_IDENTIFIER`; if followed by `:`, emit `TOKEN_LABEL`
4. If digit or `-` → read number (decimal, `0x` hex, `0b` binary) → emit `TOKEN_NUMBER`
5. If `,` → emit `TOKEN_COMMA`
6. If `#` → emit `TOKEN_HASH`
7. If `\n` or `\r\n` → emit `TOKEN_NEWLINE`
8. At end of input → emit `TOKEN_EOF`

The token array is dynamically allocated (initial capacity 128, doubles as needed).

### Register Parsing

Register names are parsed by `parse_register()`: expects `R` or `r` followed by 1–2 digits. The numeric value is stored in the token. Valid range: R0–R15 (backend may reject R8+).

---

## Stage 2: Parser / IR

**Files:** `parser.h`, `parser.c`

The parser consumes the token array and produces an array of `Instruction` structures — the architecture-neutral intermediate representation (IR).

### Shape-Table-Driven Validation

The parser uses two lookup tables to validate each instruction:

**Mnemonic Table** — maps opcode strings to `Opcode` enum values:

```c
{ "MOV",   OP_MOV   },
{ "ADD",   OP_ADD   },
{ "JMP",   OP_JMP   },
...
```

**Shape Table** — defines the expected operand pattern for each opcode:

```c
[OP_MOV]  = { 2, { SHAPE_REG, SHAPE_REG } },
[OP_LDI]  = { 2, { SHAPE_REG, SHAPE_IMM } },
[OP_ADD]  = { 2, { SHAPE_REG, SHAPE_REG_OR_IMM } },
[OP_JMP]  = { 1, { SHAPE_LABEL } },
[OP_NOP]  = { 0, {} },
[OP_INC]  = { 1, { SHAPE_REG } },
[OP_INT]  = { 1, { SHAPE_IMM } },
...
```

For each instruction, the parser:
1. Reads the opcode token → looks up the shape
2. Reads operands, checking each against the expected shape
3. If a mismatch occurs → reports an error with the line number
4. Builds an `Instruction` with typed `Operand` values

### Label Handling

Labels appear in the IR as special entries with `is_label = 1` and `label_name` set. They emit no bytes — they're resolved to addresses during backend pass 1.

### Operand Tagged Union

Each operand is a tagged union:

```c
typedef struct {
    OperandType type;     /* OPERAND_REGISTER, OPERAND_IMMEDIATE, OPERAND_LABEL */
    union {
        int      reg;     /* register index 0-15 */
        int64_t  imm;     /* immediate value */
        char     label[MAX_LABEL_LEN];
    } data;
} Operand;
```

---

## Stage 3: Backend Code Generation

### Two-Pass Assembly

Both backends use a two-pass assembly strategy to support forward references:

```
Pass 1: Walk all instructions, compute sizes, assign addresses to labels
Pass 2: Walk again, emit actual machine code bytes using resolved addresses
```

This allows code like:

```asm
    JMP  end      ; forward reference — target not yet known
    LDI  R0, 42
end:
    HLT
```

### x86-64 Backend

**Files:** `backend_x86_64.h`, `backend_x86_64.c`

#### Register Encoding

```c
X64_REG_ENC[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
// R0=RAX, R1=RCX, R2=RDX, R3=RBX, R4=RSP, R5=RBP, R6=RSI, R7=RDI
```

#### Pass 1: Size Calculation

`instruction_size_x64()` returns the exact byte count for each instruction. This is critical — pass 2 must emit exactly this many bytes, or jump targets will be wrong.

Notable sizes:
| Instruction | Size (bytes) | Notes |
|------------|-------|-------|
| `LDI Rd, #imm` | 7 | REX.W + MOV r64, imm32 |
| `MOV Rd, Rs` | 3 | REX.W + MOV r/m64, r64 |
| `ADD Rd, Rs` | 3 | REX.W + ADD r/m64, r64 |
| `ADD Rd, #imm` | 7 | REX.W + ADD r/m64, imm32 |
| `MUL Rd, Rs` | 4 | REX.W + IMUL r64, r/m64 |
| `DIV Rd, Rs` | 13 | PUSH RDX + MOV + CQO + IDIV + MOV + POP RDX |
| `JMP label` | 5 | JMP rel32 |
| `JZ label` | 6 | JZ rel32 (0x0F 0x84) |
| `JL label` | 6 | JL rel32 (0x0F 0x8C) |
| `JG label` | 6 | JG rel32 (0x0F 0x8F) |
| `CALL label` | 5 | CALL rel32 |
| `INC Rd` | 3 | REX.W + FF /0 |
| `NOP` | 1 | 0x90 |
| `HLT` | 1 | RET (0xC3) |
| `LDS Rd, "str"` | 7 | LEA r64, [RIP+disp32] |
| `LOADB Rd, Rs` | 4–5 | MOVZX r64, byte [r64] (RSP/RBP need SIB/disp8) |
| `STOREB Rs, Rd` | 2–3 | MOV byte [r64], r8 (RSP/RBP need SIB/disp8) |
| `SYS` | 2 | SYSCALL (0x0F 0x05) |
| `BUFFER name, size` | 0 | Directive — allocates zero-initialized bytes in data section |

#### Pass 2: Code Emission

The backend uses specialized emit helpers for each instruction encoding:

- `emit_alu_r64_r64()` — generic REX.W + opcode + ModR/M for ADD, SUB, etc.
- `emit_load_r64_mem()` — MOV r64, [r64] with RSP (SIB byte) and RBP (disp8=0) special cases
- `emit_imul_r64_r64()` — two-byte opcode (0x0F 0xAF)
- `emit_cqo()` — sign-extend RAX into RDX:RAX for division

#### Pass 3: Fixup Patching

Jump and call instructions emit placeholder `rel32` values during pass 2. After all code is emitted, the backend patches them:

```c
for (each fixup) {
    int32_t rel = target_address - (fixup_offset + 4);
    patch_rel32(buffer, fixup_offset, rel);
}
```

#### Special Cases

**RSP (R4) in LOAD/STORE:** x86-64 requires a SIB byte (0x24) when the base register is RSP (encoding 4), even for simple `[RSP]` addressing.

**RBP (R5) in LOAD/STORE:** x86-64 requires `mod=01` with a zero displacement byte when the base register is RBP (encoding 5), because `mod=00 + RBP` encodes RIP-relative addressing instead.

**DIV polyfill:** The `IDIV` instruction uses implicit RDX:RAX as the dividend and clobbers both. The backend saves RDX with `PUSH`, moves the dividend to RAX, sign-extends with `CQO`, divides, moves the result, and restores RDX with `POP`.

**SHL/SHR with register:** x86-64 requires the shift count in CL. The backend saves RCX with `PUSH`, moves the shift amount, performs the shift, restores RCX with `POP`, and pads with `NOP` instructions to match the fixed instruction size from pass 1.

#### String Table

Each backend maintains a **string table** for `LDS` instructions. During pass 1, all string literals are collected into a de-duplicated table. Identical strings share the same offset, saving space.

The string table is appended after buffer data (and variable data) in the output binary:

```
[ code (pass 2 output) ][ variable data ][ buffer data ][ string data ]
```

The `LDS` instruction loads the absolute or RIP-relative address of the string into a register. For x86-64, this uses `LEA r64, [RIP+disp32]` where the displacement is patched to point into the string section. The string data includes a null terminator for each entry.

### x86-32 (IA-32) Backend

**Files:** `backend_x86_32.h`, `backend_x86_32.c`

The x86-32 backend generates 32-bit IA-32 machine code. It follows the same two-pass assembly + fixup strategy as the x86-64 backend, but without REX prefixes and with 32-bit operand sizes.

#### Register Encoding

```c
X32_REG_ENC[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
// R0=EAX, R1=ECX, R2=EDX, R3=EBX, R4=ESP, R5=EBP, R6=ESI, R7=EDI
```

#### Key Differences from x86-64

| Feature | x86-64 | x86-32 |
|---------|--------|--------|
| Operand size | 64-bit (REX.W prefix) | 32-bit (no prefix needed) |
| `LDI Rd, #imm` | 7 bytes (`REX.W + MOV r64, imm32`) | 5 bytes (`MOV r32, imm32`, `B8+rd`) |
| `MOV Rd, Rs` | 3 bytes (`REX.W + 89 ModR/M`) | 2 bytes (`89 ModR/M`) |
| `ADD Rd, Rs` | 3 bytes | 2 bytes |
| `INC Rd` | 3 bytes (`REX.W FF /0`) | 1 byte (`40+rd`) |
| `DEC Rd` | 3 bytes (`REX.W FF /1`) | 1 byte (`48+rd`) |
| `PUSH/POP` | 2 bytes (with REX if needed) | 1 byte (`50+rd`/`58+rd`) |
| Sign-extend for DIV | `CQO` (REX.W + 99) | `CDQ` (99) |

#### Instruction Sizes

| Instruction | Size (bytes) | Notes |
|------------|-------|-------|
| `LDI Rd, #imm` | 5 | `MOV r32, imm32` (`B8+rd`) |
| `MOV Rd, Rs` | 2 | `89 ModR/M` |
| `ADD/SUB Rd, Rs` | 2 | Standard ALU r/m32, r32 |
| `ADD/SUB Rd, #imm` | 6-7 | `81 /0 imm32` or `83 /0 imm8` |
| `MUL Rd, Rs` | 3 | `IMUL r32, r/m32` (`0F AF`) |
| `DIV Rd, Rs` | 9 | PUSH EDX + MOV + CDQ + IDIV + MOV + POP EDX |
| `JMP label` | 5 | `JMP rel32` |
| `JZ/JNZ label` | 6 | `0F 84/85 rel32` |\n| `JL label` | 6 | `0F 8C rel32` |\n| `JG label` | 6 | `0F 8F rel32` |
| `INC/DEC Rd` | 1 | Single-byte `40+rd`/`48+rd` |
| `PUSH/POP Rd` | 1 | Single-byte `50+rd`/`58+rd` |
| `NOP` | 1 | `90` |
| `HLT` | 1 | `RET` (`C3`) |

### ARM (ARMv7-A) Backend

**Files:** `backend_arm.h`, `backend_arm.c`

The ARM backend generates 32-bit ARM (ARMv7-A, ARM mode) machine code. All instructions are exactly 4 bytes (fixed-width), stored little-endian.

#### Register Encoding

```c
ARM_REG_ENC[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
// R0-R7 map directly to ARM r0-r7
// Scratch register: r12 (IP) for large immediates
// r13=SP, r14=LR, r15=PC are reserved
```

#### ARM Instruction Encoding

All instructions use the condition field AL (0xE = always execute) in bits [31:28].

| UA Instruction | ARM Encoding | Notes |
|----------------|-------------|-------|
| `LDI Rd, #imm` | `MOVW` + `MOVT` | MOVW for low 16 bits; MOVT for high 16 bits if needed |
| `MOV Rd, Rs` | `MOV Rd, Rm` | Data processing (opcode 0xD) |
| `ADD Rd, Rs` | `ADD Rd, Rd, Rm` | Data processing (opcode 0x4) |
| `SUB Rd, Rs` | `SUB Rd, Rd, Rm` | Data processing (opcode 0x2) |
| `AND Rd, Rs` | `AND Rd, Rd, Rm` | Data processing (opcode 0x0) |
| `OR Rd, Rs` | `ORR Rd, Rd, Rm` | Data processing (opcode 0xC) |
| `XOR Rd, Rs` | `EOR Rd, Rd, Rm` | Data processing (opcode 0x1) |
| `NOT Rd` | `MVN Rd, Rd` | Data processing (opcode 0xF) |
| `MUL Rd, Rs` | `MUL Rd, Rd, Rm` | Multiply instruction |
| `DIV Rd, Rs` | `SDIV Rd, Rd, Rm` | Signed divide (ARMv7VE) |
| `SHL Rd, #n` | `LSL Rd, Rd, #n` | MOV with barrel shift |
| `SHR Rd, #n` | `LSR Rd, Rd, #n` | MOV with barrel shift |
| `CMP Ra, Rb` | `CMP Ra, Rb` | Data processing with S=1 |
| `JMP label` | `B label` | Branch (cond=AL, 24-bit signed offset) |
| `JZ label` | `BEQ label` | Branch (cond=EQ) |
| `JNZ label` | `BNE label` | Branch (cond=NE) |
| `JL label` | `BLT label` | Branch (cond=LT, 0xB) |
| `JG label` | `BGT label` | Branch (cond=GT, 0xC) |
| `CALL label` | `BL label` | Branch with Link |
| `RET` | `BX LR` | Branch and Exchange to return address |
| `PUSH Rs` | `STR Rs, [SP, #-4]!` | Pre-indexed store with writeback |
| `POP Rd` | `LDR Rd, [SP], #4` | Post-indexed load |
| `LOAD Rd, Rs` | `LDR Rd, [Rs]` | Load word |
| `STORE Rs, Rd` | `STR Rs, [Rd]` | Store word |
| `INC Rd` | `ADD Rd, Rd, #1` | Add immediate 1 |
| `DEC Rd` | `SUB Rd, Rd, #1` | Subtract immediate 1 |
| `NOP` | `MOV R0, R0` | Canonical ARM NOP (0xE1A00000) |
| `HLT` | `BX LR` | Return to caller |
| `INT #n` | `SVC #n` | Supervisor Call |

#### Immediate Handling

ARM data-processing instructions can encode an 8-bit immediate rotated right by an even number (0, 2, 4, ..., 30). If a value fits this scheme, it is encoded inline. Otherwise, the backend loads the value into the scratch register r12 (IP) using `MOVW`/`MOVT` and uses the register form.

`LDI` always uses `MOVW` for the low 16 bits, adding `MOVT` for the high 16 bits if non-zero. This means `LDI` is 4 bytes for values 0–65535 and 8 bytes for larger values.

#### Branch Offset Calculation

ARM branches use a 24-bit signed offset (in words, not bytes), relative to PC+8 (due to the ARM pipeline). The backend calculates: `offset24 = (target - (instr_addr + 8)) >> 2`. This gives a range of ±32 MB.

#### Symbol Table

The ARM backend uses an `ARMSymTab` structure with up to 256 symbols and 256 fixups. Branch fixups store the condition code and link flag, allowing the correct branch instruction to be patched after all labels are resolved.

**Files:** `backend_8051.h`, `backend_8051.c`

#### Architecture Constraints

The 8051 is an 8-bit accumulator machine. Most ALU operations can only use the accumulator (A register) as one operand. The backend generates accumulator-routing sequences:

```
ADD R3, R5  →  MOV A, R3    ; load dest into accumulator
               ADD A, R5    ; add source
               MOV R3, A    ; store result back
```

#### Register Encoding

8051 registers R0–R7 map directly to register bank 0 (RAM addresses 0x00–0x07). Instructions encode the register in the opcode's low 3 bits:

```
MOV A, Rn  →  0xE8 + n
MOV Rn, A  →  0xF8 + n
ADD A, Rn  →  0x28 + n
INC Rn     →  0x08 + n
```

#### Indirect Addressing

`LOAD` and `STORE` use indirect addressing (`@R0` or `@R1`). Only registers R0 and R1 support this mode — using R2–R7 as a pointer produces an error.

```
LOAD R3, R0  →  MOV A, @R0    ; 0xE6
                MOV R3, A     ; 0xFB
```

#### Symbol Table

The 8051 backend maintains a symbol table (max 256 entries) mapping label names to 16-bit byte addresses. `LJMP` and `LCALL` use these absolute addresses. `JZ` and `JNZ` compute 8-bit relative offsets from the current PC.

#### INT Polyfill

The 8051 has no native `INT` instruction. The backend generates `LCALL` to the standard interrupt vector address:

```
INT #n  →  LCALL (n * 8) + 3
```

This follows the conventional 8051 interrupt vector table layout where each vector entry starts at address `(n×8)+3`.

### ARM64 (AArch64) Backend

**Files:** `backend_arm64.h`, `backend_arm64.c`

The ARM64 backend generates 64-bit AArch64 machine code. All instructions are exactly 4 bytes (fixed-width), stored little-endian.

#### Register Encoding

```c
A64_REG_ENC[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
// R0-R7 map directly to X0-X7 (64-bit registers)
// Scratch registers: X9 (temporary), X10 (secondary scratch for SET imm)
// X30=LR, X31=SP/XZR are reserved
```

#### Key Instructions

| UA Instruction | AArch64 Encoding | Notes |
|----------------|-----------------|-------|
| `LDI Rd, #imm` | `MOVZ` + `MOVK` | MOVZ for low 16 bits; up to 3× MOVK for higher halfwords |
| `MOV Rd, Rs` | `MOV Xd, Xn` | ORR with XZR encoding |
| `ADD Rd, Rs` | `ADD Xd, Xd, Xm` | 64-bit add |
| `SYS` | `SVC #0` | Supervisor call |
| `HLT` | `RET` (via X30) | Branch to link register |

#### Immediate Handling

`LDI` uses `MOVZ` for the lowest non-zero 16-bit halfword, followed by up to three `MOVK` instructions for the remaining halfwords. Values 0–65535 are a single 4-byte instruction.

#### Branch Offsets

AArch64 branches use a 26-bit signed offset (in words), giving a range of ±128 MB. Conditional branches (`B.EQ`, `B.NE`, `B.LT`, `B.GT`) use a 19-bit signed offset (±1 MB).

### RISC-V (RV64I+M) Backend

**Files:** `backend_risc_v.h`, `backend_risc_v.c`

The RISC-V backend generates 64-bit RV64I machine code with the M (multiply/divide) extension. All instructions are 32 bits (4 bytes), stored little-endian.

#### Register Encoding

```c
RV_REG_ENC[8] = { 10, 11, 12, 13, 14, 15, 16, 17 };
// R0-R7 map to x10-x17 (a0-a7, the argument/return registers)
// Scratch registers: t0 (x5), t1 (x6)
// x0=zero (hardwired), x1=ra, x2=sp are reserved
```

#### Key Instructions

| UA Instruction | RISC-V Encoding | Notes |
|----------------|----------------|-------|
| `LDI Rd, #imm` | `LUI` + `ADDI` | LUI for upper 20 bits, ADDI for lower 12 bits |
| `MOV Rd, Rs` | `ADDI Rd, Rs, 0` | Pseudo-instruction using ADDI with 0 |
| `ADD Rd, Rs` | `ADD Rd, Rd, Rs` | R-type instruction |
| `MUL Rd, Rs` | `MUL Rd, Rd, Rs` | Requires M extension |
| `SYS` | `ECALL` | Environment call (syscall number in a7 = R7) |
| `HLT` | `JALR x0, x1, 0` | Return via RA register |

#### Immediate Handling

RISC-V I-type instructions support 12-bit signed immediates. For values that don't fit, the backend uses `LUI` (load upper immediate, 20 bits) combined with `ADDI`. The scratch register `t0` (x5) is used for large immediates in ALU operations.

---

## Stage 4: Output

### Raw Binary

`write_binary()` in `main.c` writes the `CodeBuffer` contents directly to a file using `fwrite()`. No headers, no padding — just raw machine code bytes.

### PE Emitter

**Files:** `emitter_pe.h`, `emitter_pe.c`

`emit_pe_exe()` constructs a minimal Windows PE/COFF executable in memory, then writes it to disk. The layout:

```
Offset    Content                  Size
────────────────────────────────────────────
0x0000    DOS Header               64 bytes
0x0040    DOS Stub (unused)        —
0x0080    PE Signature "PE\0\0"    4 bytes
0x0084    COFF File Header         20 bytes
0x0098    Optional Header (PE32+)  112 bytes
0x0108    Data Directories         128 bytes (16 entries × 8)
0x0188    Section Header (.text)   40 bytes
0x01C8    Section Header (.idata)  40 bytes  (only when imports exist)
0x01F0    Padding to 0x200         —
0x0200    .text section (code)     code_size (padded to FileAlignment)
0x0200+T  .idata section           IDT + ILT + HintName + DLL name + IAT
```

When `-sys win32` is specified, the x86-64 backend appends runtime dispatcher stubs and a 32-byte Import Address Table (IAT) to the code. The PE emitter detects this (`code->pe_iat_offset > 0`) and generates a `.idata` section containing:

1. **Import Directory Table (IDT)** — one entry for `kernel32.dll` + null terminator
2. **Import Lookup Table (ILT)** — pointers to hint/name entries
3. **Hint/Name entries** — `GetStdHandle`, `WriteFile`, `ExitProcess`
4. **DLL name** — `kernel32.dll`
5. **IAT pre-fill** — matching ILT entries, overwritten by the PE loader at runtime

The Import data directory (index 1) in the optional header points to the IDT. The loader resolves the IAT entries at process start, allowing the dispatcher stubs to call Win32 API functions via `CALL [RIP+disp32]` targeting the IAT.

Key fields:
- **Machine:** `0x8664` (AMD64)
- **ImageBase:** `0x00400000`
- **EntryPoint RVA:** `0x1000` (start of `.text` section)
- **SectionAlignment:** `0x1000` (4 KB pages)
- **FileAlignment:** `0x200` (512 bytes, standard for PE)
- **Subsystem:** `IMAGE_SUBSYSTEM_WINDOWS_CUI` (3 = console)
- **SizeOfHeaders:** `0x200`

The code is appended after the headers. The final file size is `0x200 + code_size + idata_size` (each section rounded up to FileAlignment).

### ELF Emitter

**Files:** `emitter_elf.h`, `emitter_elf.c`

`emit_elf_exe()` constructs a minimal 64-bit Linux ELF executable in memory, then writes it to disk. The layout:

```
Offset    Content                  Size
────────────────────────────────────────────
0x0000    ELF64 Header             64 bytes
0x0040    Program Header (LOAD)    56 bytes
0x0078    CALL stub                5 bytes
0x007D    User machine code        code_size bytes
0x007D+N  Exit stub                12 bytes
```

Key fields:
- **Base address:** `0x00400000`
- **Entry point:** `0x00400078` (immediately after headers)
- **Segment alignment:** 2 MB (`0x200000`)
- **Program header:** Single `PT_LOAD`, flags `PF_R | PF_X`

The emitter wraps user code with two stubs:
1. **Call stub** (5 bytes): `CALL rel32` — calls the user code as a subroutine
2. **Exit stub** (12 bytes): `mov rdi, rax; mov eax, 60; syscall` — calls Linux `sys_exit` with the value from RAX (R0)

When the user’s `HLT` (→ `RET`) executes, control returns from the call stub and falls through to the exit stub, terminating the process with the correct exit code.

### JIT Executor

`execute_jit()` in `main.c`:

1. Allocates a block of read-write-execute memory
   - Windows: `VirtualAlloc(..., MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)`
   - POSIX: `mmap(..., PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, ...)`
2. Copies the machine code into the allocated block
3. Casts the block to a function pointer `int64_t (*)(void)` using a `memcpy` trick (ISO C99 pedantic-safe — avoids direct code-to-function-pointer cast)
4. Calls the function
5. Prints the return value (RAX)
6. Frees the memory

---

## Key Data Structures

### Token

```c
typedef struct {
    UasTokenType type;
    char         lexeme[UAS_MAX_TOKEN_LEN];
    int          line;
    int64_t      num_value;    /* for TOKEN_NUMBER and TOKEN_REGISTER */
} Token;
```

### Instruction (IR)

```c
typedef struct {
    Opcode    opcode;
    int       operand_count;
    Operand   operands[MAX_OPERANDS];
    int       line;
    int       is_label;
    char      label_name[UAS_MAX_LABEL_LEN];
} Instruction;
```

### CodeBuffer

```c
typedef struct {
    uint8_t *bytes;
    int      size;
    int      capacity;
} CodeBuffer;
```

Dynamic array that grows by doubling. `emit_byte()` appends one byte and resizes if needed.

### X64SymTab (x86-64 backend)

```c
typedef struct {
    char names[X64_MAX_SYMBOLS][64];
    int  addrs[X64_MAX_SYMBOLS];
    int  count;
    /* Fixup table for rel32 patching */
    struct { int offset; int symbol_index; } fixups[X64_MAX_FIXUPS];
    int fixup_count;
} X64SymTab;
```

### SymbolTable (8051 backend)

```c
typedef struct {
    Symbol entries[MAX_SYMBOLS];
    int    count;
} SymbolTable;

typedef struct {
    char     name[UAS_MAX_LABEL_LEN];
    uint16_t address;
} Symbol;
```

---

## Source File Map

| File | Lines | Purpose |
|------|-------|---------|
| `main.c` | ~500 | CLI parsing, file I/O, JIT execution, output routing |
| `precompiler.h` | ~50 | Precompiler public API |
| `precompiler.c` | ~470 | `@`-directive preprocessor (conditionals, imports, stubs) |
| `lexer.h` | ~80 | Token type enum, `Token` struct, public API |
| `lexer.c` | ~250 | Tokenizer implementation |
| `parser.h` | ~110 | Opcode/operand enums, `Instruction` struct, public API |
| `parser.c` | ~350 | Shape-table parser, mnemonic lookup |
| `codegen.h` | ~30 | `CodeBuffer` struct, utility function declarations |
| `codegen.c` | ~100 | Code buffer management, hex dump |
| `backend_x86_64.h` | ~15 | `generate_x86_64()` declaration |
| `backend_x86_64.c` | ~700 | Full x86-64 two-pass assembler with 20+ emit helpers |
| `backend_x86_32.h` | ~50 | `generate_x86_32()` declaration, register tables |
| `backend_x86_32.c` | ~700 | Full x86-32 (IA-32) two-pass assembler |
| `backend_arm.h` | ~70 | `generate_arm()` declaration, ARM register tables |
| `backend_arm.c` | ~800 | Full ARM (ARMv7-A) two-pass assembler |
| `backend_arm64.h` | ~80 | `generate_arm64()` declaration, AArch64 register tables |
| `backend_arm64.c` | ~1500 | Full ARM64 (AArch64) two-pass assembler |
| `backend_risc_v.h` | ~80 | `generate_risc_v()` declaration, RISC-V register tables |
| `backend_risc_v.c` | ~1350 | Full RISC-V (RV64I+M) two-pass assembler |
| `backend_8051.h` | ~40 | Symbol types, `generate_8051()` declaration |
| `backend_8051.c` | ~970 | Full 8051 two-pass assembler |
| `emitter_pe.h` | ~15 | `emit_pe_exe()` declaration |
| `emitter_pe.c` | ~350 | PE/COFF builder with optional .idata import table |
| `emitter_elf.h` | ~45 | `emit_elf_exe()` declaration |
| `emitter_elf.c` | ~260 | Minimal ELF64 builder |
| `emitter_macho.h` | ~15 | `emit_macho_exe()` declaration |
| `emitter_macho.c` | ~250 | Minimal Mach-O builder |
| **Total** | **~8,500** | |

---

## Design Decisions

### Why C99?

C99 is available everywhere — every embedded toolchain, every desktop compiler, every CI system. No build system, no package manager, no runtime. A single `gcc` command builds UA on any platform.

### Why Not an AST?

Assembly is inherently linear — there's no nesting, no precedence, no scoping. A flat `Instruction[]` array is the natural representation. An AST would add complexity with no benefit.

### Why Two-Pass Instead of Single-Pass with Backpatching?

Both approaches work. Two-pass was chosen because:
1. Pass 1 provides exact code sizes, which are useful for diagnostics
2. The size mismatch check (pass 1 estimate vs. pass 2 actual) catches encoding bugs
3. The code is easier to reason about — no deferred fixup lists threaded through the emit logic

The x86-64 backend does use a fixup table for `rel32` offsets, but this is a post-emit patch pass, not a backpatch during emission. The distinction matters: all label addresses are already known when fixups are applied.

### Why Shape Tables?

Shape tables decouple syntax validation from code generation. The parser validates that `ADD R0, R1` has the right operand types without knowing anything about x86-64 or 8051 encodings. If a new backend is added, the parser doesn't change.

### Why `HLT` = `RET` on x86-64?

In JIT mode, the generated code runs as a function called from C. `HLT` must return to the caller — `RET` is the correct instruction. For bare PE executables (without `-sys win32`), the OS entry point is a function that returns its exit code in RAX, so `RET` works there too.

When `-sys win32` is specified, the backend replaces `HLT` with a call to the exit dispatcher, which invokes `ExitProcess(0)` via `kernel32.dll`. This ensures clean process termination even without a return address on the stack.

On ARM and ARM64, `HLT` emits `BX LR` / `RET` (branch to link register). On RISC-V, `HLT` emits `JALR x0, ra, 0` (return via RA).

On 8051, `HLT` generates `SJMP $` (infinite self-loop), which is the standard way to halt embedded firmware.

### Why `INT` = `LCALL` on 8051?

The 8051 has no software interrupt instruction. The standard convention is to use `LCALL` to the interrupt vector table at address `(n × 8) + 3`. This allows UA programs to invoke interrupt handlers portably.
