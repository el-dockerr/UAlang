# Architecture-Specific Opcodes

These instructions extend UA beyond the portable MVIS core. Unlike MVIS opcodes (which work on every target), architecture-specific opcodes are restricted to particular CPU families. Using them on an unsupported target produces a **compile-time compliance error**.

> **When to use these:** Architecture-specific opcodes let you tap into hardware features that have no portable equivalent — CPU identification, timestamp counters, memory barriers, low-power sleep modes, and bit-level I/O. Always guard them with `@IF_ARCH` or `@arch_only` to keep your program compilable across targets.

---

## Table of Contents

1. [Compliance Enforcement](#compliance-enforcement)
2. [Architecture Support Matrix](#architecture-support-matrix)
3. [x86 Family Opcodes](#x86-family-opcodes)
   - [CPUID — CPU Identification](#cpuid--cpu-identification)
   - [RDTSC — Read Timestamp Counter](#rdtsc--read-timestamp-counter)
   - [BSWAP — Byte Swap](#bswap--byte-swap)
   - [PUSHA — Push All Registers](#pusha--push-all-registers)
   - [POPA — Pop All Registers](#popa--pop-all-registers)
4. [8051 (MCS-51) Opcodes](#8051-mcs-51-opcodes)
   - [DJNZ — Decrement and Jump if Not Zero](#djnz--decrement-and-jump-if-not-zero)
   - [CJNE — Compare and Jump if Not Equal](#cjne--compare-and-jump-if-not-equal)
   - [SETB — Set Bit](#setb--set-bit)
   - [CLR — Clear](#clr--clear)
   - [RETI — Return from Interrupt](#reti--return-from-interrupt)
5. [ARM / ARM64 Opcodes](#arm--arm64-opcodes)
   - [WFI — Wait for Interrupt](#wfi--wait-for-interrupt)
   - [DMB — Data Memory Barrier](#dmb--data-memory-barrier)
6. [RISC-V Opcodes](#risc-v-opcodes)
   - [WFI — Wait for Interrupt (RISC-V)](#wfi--wait-for-interrupt-risc-v)
   - [EBREAK — Environment Breakpoint](#ebreak--environment-breakpoint)
   - [FENCE — Memory Ordering Fence](#fence--memory-ordering-fence)
7. [Usage Patterns](#usage-patterns)

---

## Compliance Enforcement

UA enforces architecture compliance at compile time. Every opcode has an **architecture mask** specifying which targets support it. If your code uses an opcode that isn't valid for the current `-arch` target, compilation fails immediately with a clear diagnostic:

```
  UA Compliance Error
  -------------------
  Line 12: opcode 'PUSHA' is not supported on architecture 'x86'
  Supported architectures: x86_32
```

This prevents you from accidentally generating invalid machine code. To use architecture-specific opcodes safely, wrap them in `@IF_ARCH` guards:

```asm
@IF_ARCH x86
    CPUID            ; only compiles for x86-64
    RDTSC            ; only compiles for x86-64
@ENDIF

@IF_ARCH x86_32
    PUSHA            ; only compiles for x86-32
    POPA
@ENDIF
```

Or restrict an entire file with `@arch_only`:

```asm
@arch_only mcs51
; This entire file is 8051-only
    DJNZ R0, loop
    RETI
```

---

## Architecture Support Matrix

| Opcode | x86 | x86_32 | ARM | ARM64 | RISC-V | 8051 | Description |
|--------|:---:|:------:|:---:|:-----:|:------:|:----:|-------------|
| `CPUID` | **Yes** | **Yes** | — | — | — | — | CPU identification |
| `RDTSC` | **Yes** | **Yes** | — | — | — | — | Read timestamp counter |
| `BSWAP` | **Yes** | **Yes** | — | — | — | — | Byte swap (endian) |
| `PUSHA` | — | **Yes** | — | — | — | — | Push all GP registers |
| `POPA` | — | **Yes** | — | — | — | — | Pop all GP registers |
| `DJNZ` | — | — | — | — | — | **Yes** | Decrement & jump if ≠ 0 |
| `CJNE` | — | — | — | — | — | **Yes** | Compare & jump if ≠ |
| `SETB` | — | — | — | — | — | **Yes** | Set bit |
| `CLR` | — | — | — | — | — | **Yes** | Clear bit/accumulator |
| `RETI` | — | — | — | — | — | **Yes** | Return from interrupt |
| `WFI` | — | — | **Yes** | **Yes** | **Yes** | — | Wait for interrupt |
| `DMB` | — | — | **Yes** | **Yes** | — | — | Data memory barrier |
| `EBREAK` | — | — | — | — | **Yes** | — | Debugger breakpoint |
| `FENCE` | — | — | — | — | **Yes** | — | Memory ordering fence |

---

## x86 Family Opcodes

These opcodes are available on the x86-64 and/or x86-32 backends. They expose hardware features specific to the Intel/AMD x86 architecture family.

---

### CPUID — CPU Identification

```
CPUID
```

Executes the x86 `CPUID` instruction, which queries the processor for identification and feature information. Before calling `CPUID`, load the **leaf** number into `R0` (EAX/RAX). After execution, the CPU populates:

| Register | Contains |
|----------|----------|
| R0 (EAX) | Depends on leaf |
| R1 (ECX) | Depends on leaf |
| R2 (EDX) | Depends on leaf |
| R3 (EBX) | Depends on leaf |

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | x86, x86_32 |
| Machine code (x86-64) | `0F A2` (2 bytes) |
| Machine code (x86-32) | `0F A2` (2 bytes) |

**Example — Get CPU vendor string:**

```asm
@IF_ARCH x86
    LDI  R0, 0          ; CPUID leaf 0 — vendor string
    CPUID
    ; R3 (EBX) = first 4 chars, R2 (EDX) = next 4, R1 (ECX) = last 4
    ; For Intel: "GenuineIntel"
    ; For AMD:   "AuthenticAMD"
@ENDIF
```

**Example — Check feature flags:**

```asm
@IF_ARCH x86
    LDI  R0, 1          ; CPUID leaf 1 — processor info & features
    CPUID
    ; R2 (EDX) contains feature flags
    ; Bit 25 = SSE, Bit 26 = SSE2, etc.
    AND  R2, 0x2000000   ; isolate SSE bit (bit 25)
    CMP  R2, 0
    JNZ  has_sse
@ENDIF
```

---

### RDTSC — Read Timestamp Counter

```
RDTSC
```

Reads the processor's 64-bit timestamp counter. The counter increments with every clock cycle (or at a fixed rate on modern CPUs). After execution:

- `R0` (EAX/RAX) = low 32 bits of counter
- `R2` (EDX) = high 32 bits of counter

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | x86, x86_32 |
| Machine code | `0F 31` (2 bytes) |

**Example — Measure execution time:**

```asm
@IF_ARCH x86
    RDTSC
    MOV  R6, R0          ; save start timestamp (low 32 bits)

    ; ... code to benchmark ...

    RDTSC
    SUB  R0, R6          ; R0 = elapsed cycles (approximate)
@ENDIF
```

> **Note:** On modern CPUs, `RDTSC` may not be serializing. For precise benchmarking, pair with `CPUID` as a serializing barrier.

---

### BSWAP — Byte Swap

```
BSWAP  Rd
```

Reverses the byte order of register Rd. Converts between big-endian and little-endian formats.

| Field | Value |
|-------|-------|
| Operands | 1 register |
| Flags affected | None |
| Architectures | x86, x86_32 |
| Machine code (x86-64) | `48 0F C8+rd` (3 bytes, 64-bit swap with REX.W) |
| Machine code (x86-32) | `0F C8+rd` (2 bytes, 32-bit swap) |

**Before/after on x86-32 (32-bit):**

```
R0 = 0x12345678  →  BSWAP R0  →  R0 = 0x78563412
```

**Before/after on x86-64 (64-bit):**

```
R0 = 0x0102030405060708  →  BSWAP R0  →  R0 = 0x0807060504030201
```

**Example — Convert network byte order:**

```asm
@IF_ARCH x86
    ; Assume R0 contains a 32-bit value in network byte order (big-endian)
    BSWAP R0             ; convert to host byte order (little-endian)
@ENDIF
```

---

### PUSHA — Push All Registers

```
PUSHA
```

Pushes all 8 general-purpose 32-bit registers onto the stack in the order: EAX, ECX, EDX, EBX, ESP (original value), EBP, ESI, EDI.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | **x86_32 only** |
| Machine code | `60` (1 byte) |

> **x86-64 note:** `PUSHA` is **not available** on x86-64. The instruction was removed in 64-bit mode. Use individual `PUSH` instructions instead.

**Example — Save/restore all registers around a function call:**

```asm
@IF_ARCH x86_32
    PUSHA                ; save all registers
    CALL some_function   ; function may clobber registers
    POPA                 ; restore all registers
@ENDIF
```

---

### POPA — Pop All Registers

```
POPA
```

Pops all 8 general-purpose 32-bit registers from the stack in reverse order: EDI, ESI, EBP, (ESP skipped), EBX, EDX, ECX, EAX.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | **x86_32 only** |
| Machine code | `61` (1 byte) |

Always used in conjunction with `PUSHA`. The ESP value on the stack is discarded (not restored).

---

## 8051 (MCS-51) Opcodes

These opcodes expose native 8051 features — tight loop control, bit-level I/O, and interrupt handling. They are essential for embedded firmware development on the 8051 microcontroller family.

---

### DJNZ — Decrement and Jump if Not Zero

```
DJNZ  Rn, label
```

Decrements register Rn by 1. If the result is not zero, jumps to label. This is the classic tight-loop instruction on the 8051 — combining a decrement and conditional branch in a single 2-byte instruction.

| Field | Value |
|-------|-------|
| Operands | 1 register, 1 label |
| Flags affected | None (8051 DJNZ does not affect flags) |
| Architectures | **mcs51 only** |
| Machine code | `D8+n, rel8` (2 bytes) |

The relative offset (`rel8`) is computed from the PC after the instruction. Range: -128 to +127 bytes.

**Example — Delay loop:**

```asm
@IF_ARCH mcs51
    LDI  R2, 100         ; loop 100 times
delay:
    NOP                   ; waste time
    DJNZ R2, delay        ; R2-- ; if R2 != 0, repeat
@ENDIF
```

**Example — Countdown:**

```asm
@IF_ARCH mcs51
    LDI  R0, 10
count:
    ; R0 = 10, 9, 8, ... 1
    DJNZ R0, count
    ; R0 = 0 here
@ENDIF
```

> **Comparison with MVIS:** In portable MVIS code, the same pattern requires three instructions:
> ```asm
>     DEC  R0
>     CMP  R0, 0
>     JNZ  loop
> ```
> `DJNZ` accomplishes this in a single instruction (2 bytes vs. ~6 bytes on 8051).

---

### CJNE — Compare and Jump if Not Equal

```
CJNE  Rn, #imm, label
```

Compares Rn with the immediate value. If they are **not equal**, jumps to label. Also sets the carry flag (C=1 if Rn < imm, C=0 if Rn >= imm).

| Field | Value |
|-------|-------|
| Operands | 1 register, 1 immediate, 1 label |
| Flags affected | Carry flag |
| Architectures | **mcs51 only** |
| Machine code | `B4, imm8, rel8` (3 bytes if Rn=R0, 4 bytes otherwise) |

If Rn is not the accumulator (R0), the compiler polyfills with `MOV A, Rn` before the `CJNE A, #imm, rel8` instruction (adding 1 byte).

**Example — Wait for specific value:**

```asm
@IF_ARCH mcs51
wait_for_ready:
    ; Assume R1 receives a status byte
    CJNE R1, 0xFF, wait_for_ready  ; loop until R1 == 0xFF
    ; R1 == 0xFF — proceed
@ENDIF
```

**Example — Range check:**

```asm
@IF_ARCH mcs51
    ; Check if R0 is less than 10
    CJNE R0, 10, not_ten
    JMP  is_ten              ; R0 == 10 exactly

not_ten:
    ; Carry flag tells us: C=1 means R0 < 10, C=0 means R0 > 10
    JMP done

is_ten:
    ; Handle R0 == 10

done:
@ENDIF
```

---

### SETB — Set Bit

```
SETB  bit_addr
```

Sets the specified bit to 1. The operand is a **direct bit address** (0x00–0xFF), which maps to the 8051's bit-addressable memory space.

| Field | Value |
|-------|-------|
| Operands | 1 register (used as bit address) |
| Flags affected | None |
| Architectures | **mcs51 only** |
| Machine code | `D2, bit_addr` (2 bytes) |

**8051 bit-addressable memory:**

| Bit Range | SFR / RAM |
|-----------|----------|
| 0x00–0x7F | Internal RAM bytes 0x20–0x2F (8 bytes × 8 bits) |
| 0x80–0xFF | Bit-addressable SFRs (P0, P1, P2, P3, etc.) |

**Example — Set a port pin high:**

```asm
@IF_ARCH mcs51
    ; P1.0 = bit address 0x90
    LDI  R0, 0x90
    SETB R0              ; set P1.0 = 1 (pin high)
@ENDIF
```

**Example — Set carry flag:**

```asm
@IF_ARCH mcs51
    ; Carry flag = bit address 0xD7 (in PSW register)
    LDI  R0, 0xD7
    SETB R0              ; set carry flag
@ENDIF
```

---

### CLR — Clear

```
CLR  Rn
```

If Rn is `R0` (the accumulator mapping), clears the accumulator to 0 (`CLR A`). Otherwise, treats the register value as a bit address and clears that bit (`CLR bit`).

| Field | Value |
|-------|-------|
| Operands | 1 register |
| Flags affected | None |
| Architectures | **mcs51 only** |
| Machine code (CLR A) | `E4` (1 byte, when Rn = R0) |
| Machine code (CLR bit) | `C2, bit_addr` (2 bytes, when Rn ≠ R0) |

**Example — Clear accumulator:**

```asm
@IF_ARCH mcs51
    CLR  R0              ; CLR A — accumulator = 0
@ENDIF
```

**Example — Clear a port pin:**

```asm
@IF_ARCH mcs51
    LDI  R1, 0x90        ; P1.0 bit address
    CLR  R1               ; clear P1.0 = 0 (pin low)
@ENDIF
```

---

### RETI — Return from Interrupt

```
RETI
```

Returns from an interrupt service routine (ISR). Similar to `RET`, but also re-enables the interrupted priority level in the 8051's interrupt controller.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | Restores interrupt priority level |
| Architectures | **mcs51 only** |
| Machine code | `32` (1 byte) |

**Example — Simple interrupt handler:**

```asm
@IF_ARCH mcs51
    ; Timer 0 ISR (vector at 0x000B)
timer0_isr:
    PUSH R0
    ; ... handle timer event ...
    POP  R0
    RETI                 ; return from interrupt (not RET!)
@ENDIF
```

> **Important:** Always use `RETI` (not `RET`) to return from interrupt handlers. `RET` does not restore the interrupt priority level, which can prevent further interrupts from firing.

---

## ARM / ARM64 Opcodes

These opcodes are available on both the ARM (ARMv7-A) and ARM64 (AArch64) backends, exposing system-level features common to the ARM architecture family.

---

### WFI — Wait for Interrupt

```
WFI
```

Places the processor in a low-power standby state until an interrupt, debug event, or reset occurs. The processor stops executing instructions and reduces power consumption.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | **arm, arm64, riscv** |

| Architecture | Machine Code | Encoding |
|-------------|-------------|----------|
| ARM (ARMv7-A) | `E3 20 F0 03` | Condition=AL, hint WFI |
| ARM64 (AArch64) | `D5 03 20 7F` | System instruction HINT #3 |
| RISC-V | `10 50 00 73` | SYSTEM opcode, WFI function |

**Example — Low-power idle loop:**

```asm
@IF_ARCH arm
idle:
    WFI                  ; sleep until interrupt
    JMP  idle            ; handle interrupt, then sleep again
@ENDIF
```

> **Note:** WFI is also available on RISC-V. See the [RISC-V section](#wfi--wait-for-interrupt-risc-v) for RISC-V-specific details.

---

### DMB — Data Memory Barrier

```
DMB
```

Ensures that all memory accesses before the `DMB` instruction are completed before any memory accesses after it. This is critical for multi-core systems and memory-mapped I/O.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | **arm, arm64** |

| Architecture | Machine Code | Encoding |
|-------------|-------------|----------|
| ARM (ARMv7-A) | `F5 7F F0 5F` | DMB SY (full system barrier) |
| ARM64 (AArch64) | `D5 03 3F BF` | DMB SY (full system barrier) |

The UA compiler always emits `DMB SY` (full-system, all accesses). This is the strongest barrier type.

**Example — Memory-mapped I/O:**

```asm
@IF_ARCH arm
    ; Write to device register
    STORE R1, R0          ; write command to hardware
    DMB                   ; ensure write completes before next access
    LOAD  R2, R0          ; read status — guaranteed to see the write's effect
@ENDIF
```

**Example — Spinlock release:**

```asm
@IF_ARCH arm64
    ; Ensure all writes are visible before releasing lock
    DMB
    LDI  R1, 0
    STORE R1, R0          ; release lock (visible to other cores)
@ENDIF
```

---

## RISC-V Opcodes

These opcodes are specific to the RISC-V (RV64I) backend.

---

### WFI — Wait for Interrupt (RISC-V)

```
WFI
```

Same behavior as the ARM WFI — places the processor in a low-power state until an interrupt occurs.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | **riscv** (also arm, arm64) |
| Machine code (RISC-V) | `10 50 00 73` (4 bytes) |

```asm
@IF_ARCH riscv
    WFI                  ; halt until interrupt
@ENDIF
```

> **Privilege note:** On RISC-V, WFI may be restricted to machine mode or supervisor mode depending on the platform. In user mode, it may be treated as a NOP.

---

### EBREAK — Environment Breakpoint

```
EBREAK
```

Triggers a breakpoint exception. Used for debugging — when execution reaches `EBREAK`, control transfers to the attached debugger (if any) or the debug exception handler.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | Triggers exception |
| Architectures | **riscv only** |
| Machine code | `00 10 00 73` (4 bytes) |

**Example — Debug breakpoint:**

```asm
@IF_ARCH riscv
    ; Insert a breakpoint for debugging
    EBREAK               ; trap to debugger
    ; Execution continues here after debugger resumes
@ENDIF
```

**Example — Assert-like check:**

```asm
@IF_ARCH riscv
    CMP  R0, 0
    JNZ  ok
    EBREAK               ; trap: R0 should not be 0!
ok:
@ENDIF
```

> **Semihosting:** On some RISC-V platforms, `EBREAK` is used as a semihosting trap — the debugger intercepts it and performs I/O operations on behalf of the target.

---

### FENCE — Memory Ordering Fence

```
FENCE
```

Ensures that all memory reads and writes before the `FENCE` are completed before any memory accesses that follow. The RISC-V equivalent of ARM's `DMB`.

| Field | Value |
|-------|-------|
| Operands | None |
| Flags affected | None |
| Architectures | **riscv only** |
| Machine code | `0F F0 0F 00` (4 bytes, FENCE iorw, iorw) |

The UA compiler emits `FENCE iorw, iorw` — the strongest fence ordering all input, output, read, and write operations in both predecessor and successor sets.

**Example — Device register access:**

```asm
@IF_ARCH riscv
    STORE R1, R0          ; write to device register
    FENCE                 ; ensure write is globally visible
    LOAD  R2, R0          ; read status
@ENDIF
```

**Example — Multi-hart synchronization:**

```asm
@IF_ARCH riscv
    ; Publish data, then release flag
    SET   shared_data, R0
    FENCE                 ; ensure data is visible before flag
    SET   ready_flag, R1  ; other harts see data before flag
@ENDIF
```

---

## Usage Patterns

### Cross-Platform with Architecture-Specific Optimization

Use MVIS as the default, with architecture-specific optimizations guarded by `@IF_ARCH`:

```asm
; Portable delay loop (works everywhere)
delay_portable:
    LDI  R2, 100
delay_loop:
    DEC  R2
    CMP  R2, 0
    JNZ  delay_loop
    RET

; 8051-optimized delay loop (fewer bytes, faster)
@IF_ARCH mcs51
delay_8051:
    LDI  R2, 100
delay_loop_8051:
    DJNZ R2, delay_loop_8051
    RET
@ENDIF
```

### Multi-Architecture File

```asm
; main.ua — works on all targets

@IMPORT std_io

    JMP main

; Platform-specific initialization
init:
    @IF_ARCH x86
        CPUID                    ; identify CPU capabilities
    @ENDIF

    @IF_ARCH arm
        DMB                      ; ensure clean memory state
    @ENDIF

    @IF_ARCH riscv
        FENCE                    ; memory fence before starting
    @ENDIF

    RET

main:
    CALL init
    LDS  R0, "Hello from UA!\n"
    CALL std_io.print
    HLT
```

### 8051 Interrupt-Driven Firmware

```asm
@arch_only mcs51

    JMP  main

; Timer 0 interrupt handler
timer0_handler:
    PUSH R0
    PUSH R1

    ; Toggle P1.0
    LDI  R0, 0x90           ; P1.0 bit address
    SETB R0                  ; set pin high
    ; ... do work ...
    CLR  R0                  ; set pin low

    POP  R1
    POP  R0
    RETI                     ; return from interrupt (re-enables interrupts)

main:
    ; ... setup timer, enable interrupts ...

idle:
    NOP
    JMP  idle                ; wait for interrupts
```

### RISC-V Debugging

```asm
@arch_only riscv

    LDI  R0, 42
    EBREAK                   ; debugger stops here — inspect R0

    ; After resuming from debugger:
    ADD  R0, 8               ; R0 = 50
    FENCE                    ; ensure all prior writes are visible
    HLT
```
