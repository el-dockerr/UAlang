
<div align="center">
  <img src="docs/images/icon128.png" alt="UA Logo" width="128"/>

# UA — Unified Assembler
**One assembly language. Every architecture.**
<br>
</div>


UA is an experimental assembler that defines a single, portable assembly language — the **Unified Assembly** instruction set — and compiles it to native machine code for multiple target architectures from a single source file.

## The UA Design Manifesto: The Philosophy of Unified Assembler
1. **Bare Metal, Modern Comfort**

We reject the illusion that developers must choose between high-level ergonomics and low-level control. Unified Assembler (UA) provides the raw, unadulterated power of hardware registers (MOV R0, R1) while offering the structural comforts of modern programming. We do not hide the machine; we simply make it accessible.

2. **The Trojan Horse of Abstraction**

Through a powerful, zero-overhead @IMPORT system, UA seamlessly namespaces files and functions. By providing standard libraries (like std_math or std_string) written entirely in UA, developers can write code that looks and feels like a high-level language, while executing as pure, cycle-accurate assembly. UA doesn't just compete with higher-level languages—it sublimates them.

3. **Zero Dependencies, Absolute Autonomy**

A true systems compiler should not be a bloated behemoth relying on gigabytes of external toolchains. UA is radically self-reliant. Written in pure C99, it requires no external linkers, no package managers, and no runtime environments. It takes raw text and directly emits executable .exe, .elf, or .bin files.

4. **The Universal Baseline (The MVIS Doctrine)**

Whether targeting a 64-bit desktop processor, an ARM mobile chip, or a tiny 8-bit 8051 microcontroller, the core logic remains identical. The Minimum Viable Instruction Set (MVIS) ensures that the foundational building blocks of computation are portable across entirely different hardware architectures. The compiler handles the architectural polyfills behind the scenes, keeping the language pure and unified.

5. **Transparency over Magic**

We do not patronize the developer. If an 8-bit register overflows at 255 on an 8051 chip, it overflows exactly as the silicon dictates. UA introduces no hidden safety nets, garbage collectors, or performance-draining safeguards. The developer is in absolute command. When the code crashes, it crashes honestly.

6. The Ouroboros Target
A language only truly matures when it can create itself. The ultimate destination for UA is absolute self-hosting: a compiler written entirely in UA, translating its own source code into native machine code.

*Swen Kalski aka. El Docker February 2027*

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
ua program.ua -arch arm64 -sys macos  # compile for Apple Silicon
ua program.ua -arch riscv -sys linux  # compile for RISC-V
ua program.ua -arch x86 -sys win32    # build a Windows .exe
ua program.ua -arch x86 -sys linux    # build a Linux ELF binary
```

## Hello World

```asm
@IMPORT std_io
    LDS  R0, "Hello, World!\n"
    CALL std_io.print
    HLT
```

```bash
ua hello.ua -arch x86 -sys linux -o hello
./hello
Hello, World!
```

## Key Features

- **34-instruction MVIS** — a Minimum Viable Instruction Set covering data movement, arithmetic, bitwise logic, control flow, stack operations, byte-granularity memory access, string literals, and system calls
- **Standard Libraries** — `@IMPORT std_io` and `@IMPORT std_string` for console I/O and string operations, written entirely in UA
- **Precompiler** — `@IF_ARCH`, `@IF_SYS`, `@ENDIF` conditional compilation; `@IMPORT` with once-only file inclusion; `@DUMMY` stub markers
- **Six backends** — Intel x86-64 (64-bit), Intel x86-32/IA-32 (32-bit), ARM ARMv7-A (32-bit), ARM64/AArch64 (64-bit, Apple Silicon), RISC-V RV64I+M (64-bit), and Intel 8051/MCS-51 (8-bit embedded)
- **Five output modes** — raw binary, Windows PE executable, Linux ELF executable, macOS Mach-O executable, and JIT execution
- **Two-pass assembly** — full label resolution with forward references
- **Pure C99** — zero dependencies, no external libraries, builds with a single `gcc` command
- **Strict validation** — shape-table-driven operand checking, range validation, duplicate label detection

## Supported Architectures

| Target | Flag | Registers | Output Formats |
|--------|------|-----------|----------------|
| **x86-64** | `-arch x86` | R0–R7 → RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI | Raw binary, PE .exe, ELF, JIT |
| **x86-32 (IA-32)** | `-arch x86_32` | R0–R7 → EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI | Raw binary, PE .exe, ELF |
| **ARM (ARMv7-A)** | `-arch arm` | R0–R7 → r0–r7 | Raw binary, ELF |
| **ARM64 (AArch64)** | `-arch arm64` | R0–R7 → X0–X7 | Raw binary, ELF, Mach-O |
| **RISC-V (RV64I+M)** | `-arch riscv` | R0–R7 → a0–a7 (x10–x17) | Raw binary, ELF |
| **8051/MCS-51** | `-arch mcs51` | R0–R7 → 8051 bank-0 registers | Raw binary |

## Quick Start

### Build

```bash
cd src
gcc -std=c99 -Wall -Wextra -pedantic -o ua.exe \
    main.c lexer.c parser.c codegen.c precompiler.c \
    backend_8051.c backend_x86_64.c backend_x86_32.c \
    backend_arm.c backend_arm64.c backend_risc_v.c \
    emitter_pe.c emitter_elf.c emitter_macho.c
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

# Compile for ARM64 / Apple Silicon
./ua program.ua -arch arm64 -sys macos -o program

# Compile for RISC-V (RV64I)
./ua program.ua -arch riscv -sys linux -o program.elf

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
├── lib/
│   ├── std_io.ua               # Standard I/O library (print)
│   └── std_string.ua           # Standard string library (strlen)
└── src/
    ├── main.c                  # CLI driver, file I/O, JIT executor
    ├── precompiler.h/.c        # Preprocessor (@IF_ARCH, @IMPORT, etc.)
    ├── lexer.h / lexer.c       # Tokenizer
    ├── parser.h / parser.c     # IR generator with shape validation
    ├── codegen.h / codegen.c   # Shared code buffer utilities
    ├── backend_x86_64.h/.c     # x86-64 native code generator
    ├── backend_x86_32.h/.c     # x86-32 (IA-32) native code generator
    ├── backend_arm.h/.c        # ARM (ARMv7-A) native code generator
    ├── backend_arm64.h/.c      # ARM64 (AArch64 / Apple Silicon) code generator
    ├── backend_risc_v.h/.c     # RISC-V (RV64I+M) native code generator
    ├── backend_8051.h/.c       # 8051/MCS-51 native code generator
    ├── emitter_pe.h/.c         # Windows PE executable emitter
    ├── emitter_elf.h/.c        # Linux ELF executable emitter
    └── emitter_macho.h/.c      # macOS Mach-O executable emitter
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
