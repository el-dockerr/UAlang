# UA — Unified Assembler

**One assembly language. Every architecture.**

UA is an experimental assembler that defines a single, portable assembly language — the **Unified Assembly** instruction set — and compiles it to native machine code for multiple target architectures from a single source file.

## The Problem

Assembly languages are inherently hardware-specific. An `ADD` instruction on an 8051 microcontroller has completely different encoding, operand rules, and semantics than `ADD` on x86-64. Anyone working across architectures must learn, write, and maintain separate codebases for each target — even when the *intent* of the code is identical.

## The Vision

UA introduces a **hardware-neutral assembly dialect** with a clean, consistent syntax. You write your program once using the UA instruction set, then compile it to any supported backend:

```
; Same source file — runs on both architectures
    LDI  R0, 10       ; load 10 into R0
    LDI  R1, 5        ; load 5 into R1
    ADD  R0, R1       ; R0 = R0 + R1
    HLT               ; stop
```

```bash
ua program.ua -arch x86 --run        # JIT-execute on your PC
ua program.ua -arch mcs51 -o fw.bin   # cross-compile for 8051
ua program.ua -arch x86_32 -o fw.bin  # compile for IA-32 (32-bit x86)
ua program.ua -arch arm -o fw.bin     # compile for ARM (ARMv7-A)
ua program.ua -arch x86 -sys win32    # build a Windows .exe
ua program.ua -arch x86 -sys linux    # build a Linux ELF binary
```

## Key Features

- **27-instruction MVIS** — a Minimum Viable Instruction Set covering data movement, arithmetic, bitwise logic, control flow, stack operations, and software interrupts
- **Precompiler** — `@IF_ARCH`, `@IF_SYS`, `@ENDIF` conditional compilation; `@IMPORT` with once-only file inclusion; `@DUMMY` stub markers
- **Four backends** — Intel x86-64 (64-bit), Intel x86-32/IA-32 (32-bit), ARM ARMv7-A (32-bit), and Intel 8051/MCS-51 (8-bit embedded)
- **Four output modes** — raw binary, Windows PE executable, Linux ELF executable, and JIT execution
- **Two-pass assembly** — full label resolution with forward references
- **Pure C99** — zero dependencies, no external libraries, builds with a single `gcc` command
- **Strict validation** — shape-table-driven operand checking, range validation, duplicate label detection

## Supported Architectures

| Target | Flag | Registers | Output Formats |
|--------|------|-----------|----------------|
| **x86-64** | `-arch x86` | R0–R7 → RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI | Raw binary, PE .exe, ELF, JIT |
| **x86-32 (IA-32)** | `-arch x86_32` | R0–R7 → EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI | Raw binary, PE .exe, ELF |
| **ARM (ARMv7-A)** | `-arch arm` | R0–R7 → r0–r7 | Raw binary, ELF |
| **8051/MCS-51** | `-arch mcs51` | R0–R7 → 8051 bank-0 registers | Raw binary |

## Quick Start

### Build

```bash
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o ua.exe \
    main.c lexer.c parser.c codegen.c precompiler.c \
    backend_8051.c backend_x86_64.c backend_x86_32.c backend_arm.c \
    emitter_pe.c emitter_elf.c
```

### Run

```bash
# JIT — assemble and execute immediately (x86-64 host)
./ua program.ua -arch x86 --run

# Cross-compile for 8051
./ua firmware.ua -arch mcs51 -o firmware.bin

# Compile for 32-bit x86 (IA-32)
./ua program.ua -arch x86_32 -o program.bin

# Compile for ARM (ARMv7-A)
./ua firmware.ua -arch arm -o firmware.bin

# Build a standalone Windows executable
./ua program.ua -arch x86 -sys win32 -o program.exe

# Build a standalone Linux executable
./ua program.ua -arch x86 -sys linux -o program.elf
```

## Project Structure

```
UA/
├── README.md
├── docs/
│   ├── language-reference.md   # Full instruction set documentation
│   ├── compiler-usage.md       # CLI reference and examples
│   └── architecture.md         # Internal design and pipeline
└── src/
    ├── main.c                  # CLI driver, file I/O, JIT executor
    ├── precompiler.h/.c        # Preprocessor (@IF_ARCH, @IMPORT, etc.)
    ├── lexer.h / lexer.c       # Tokenizer
    ├── parser.h / parser.c     # IR generator with shape validation
    ├── codegen.h / codegen.c   # Shared code buffer utilities
    ├── backend_x86_64.h/.c     # x86-64 native code generator
    ├── backend_x86_32.h/.c     # x86-32 (IA-32) native code generator
    ├── backend_arm.h/.c        # ARM (ARMv7-A) native code generator
    ├── backend_8051.h/.c       # 8051/MCS-51 native code generator
    ├── emitter_pe.h/.c         # Windows PE executable emitter
    └── emitter_elf.h/.c        # Linux ELF executable emitter
```

## Documentation

- [Language Reference](docs/language-reference.md) — complete instruction set, register model, syntax, and operand rules
- [Compiler Usage](docs/compiler-usage.md) — CLI flags, output formats, and usage examples
- [Architecture](docs/architecture.md) — internal pipeline, two-pass assembly, and backend design

## Design Principles

1. **Portability over performance** — UA programs express *intent*, not micro-optimizations. Each backend maps instructions to idiomatic native sequences.
2. **Simplicity** — the entire compiler is under 3,000 lines of C99 with no allocator tricks, no third-party code, and no build system beyond a single compiler invocation.
3. **Correctness** — two-pass assembly ensures all forward references resolve. Shape tables enforce operand grammar at parse time. Backends validate register and immediate ranges.

## Requirements

- **Build**: Any C99-conformant compiler (GCC, Clang, MSVC)
- **JIT execution**: Windows (uses `VirtualAlloc`) or POSIX (uses `mmap`)
- **PE/ELF output**: No runtime dependency — the emitters construct executables in-memory

## License

MIT
