# Version 27 Roadmap: "Amazing Grace"
## Self-Hosting UA Compiler

> *"The whole framework has been carefully planned with a view to enabling our new 'analytical engine' to perform, on sound principles, the same kind of operations as those now performed by human computers."*  
> — Ada Lovelace, Notes on the Analytical Engine (1843)

**Goal:** Compile the UA compiler using UA itself, eliminating the dependency on C compilers and achieving complete self-hosting autonomy.

**Target Architectures for Self-Hosting:**
- x86-64 (primary development target)
- x86-32 (secondary, broader hardware support)
- ARM64 (modern ARM, mobile/server)
- RISC-V (future-proof, open ISA)

**Excluded Architectures:**
- 8051/MCS-51: Insufficient memory and computational resources for hosting a compiler

---

## Current State Assessment

### What We Have (Version 26 "Awesome Ada")

**Compiler Pipeline:**
- ✅ Precompiler with conditional compilation (`@IF_ARCH`, `@IF_SYS`, `@IMPORT`)
- ✅ Lexer/Tokenizer (~260 lines of C)
- ✅ Parser with full IR generation (~478 lines of C)
- ✅ 6 architecture backends (8051, x86-64, x86-32, ARM, ARM64, RISC-V)
- ✅ 3 output emitters (PE, ELF, Mach-O)
- ✅ Two-pass assembly with label resolution
- ✅ Architecture compliance validation

**Instruction Set:**
- ✅ 37 core MVIS opcodes (data movement, arithmetic, logic, control flow, memory, syscalls)
- ✅ 14 architecture-specific opcodes
- ✅ `VAR`/`SET`/`GET` variable system
- ✅ `BUFFER` memory allocation
- ✅ `LDS` string literal loading
- ✅ `LOADB`/`STOREB` byte-level memory access

**Standard Libraries (all in UA):**
- ✅ `std_io` — Console I/O (print, read)
- ✅ `std_string` — String utilities (strlen, parse_int, to_string)
- ✅ `std_math` — Integer math (pow, factorial, max, abs)
- ✅ `std_arrays` — Byte array helpers
- ✅ `std_array` — Fixed-size arrays
- ✅ `std_vector` — Dynamic vectors
- ✅ `std_iostream` — File I/O (fopen, fread, fwrite, fclose)

### What We're Missing for Self-Hosting

**Critical Missing Components:**
1. **Dynamic Memory Management** — No heap allocator (`malloc`, `realloc`, `free`)
2. **Hash Tables / Symbol Tables** — Compiler needs efficient symbol lookup
3. **Advanced String Operations** — String comparison, concatenation, search, substring
4. **Token Stream Management** — Dynamic token array handling
5. **IR/AST Construction** — Dynamic instruction list building
6. **Binary Output Generation** — PE/ELF/Mach-O structure encoding in UA
7. **Error Reporting** — Formatted error messages with line/column info
8. **Efficient I/O Buffering** — Reading/writing large files efficiently
9. **Path/Filename Manipulation** — Resolving relative imports, directory traversal
10. **Command-Line Argument Handling** — Parsing `-arch`, `-sys`, `-o`, `--run` flags
11. **Numeric to String Conversion** — Hex/binary formatting for diagnostics
12. **Code Buffer Management** — Efficient byte buffer growth and emission
13. **Two-Pass Assembly Logic** — Forward reference resolution
14. **Syscall Wrapper Abstraction** — Unified cross-platform syscall interface

---

## Phase-by-Phase Self-Hosting Roadmap

### Phase 1: Foundation — Dynamic Memory & Core Data Structures
**Objective:** Establish the fundamental building blocks required by any compiler.

#### Task 1.1: Dynamic Memory Allocator (`std_malloc`)
- [ ] Implement `malloc(size)` — Allocate N bytes from heap
  - Use `mmap` (Linux/macOS, syscall 9 on x86-64) or `VirtualAlloc` (Win32)
  - Initial implementation: simple bump allocator with fixed heap size (e.g., 16 MB)
  - Return pointer in R0, or 0 on failure
- [ ] Implement `free(ptr)` — Release allocated memory
  - Initial implementation: no-op (defer real free-list management to v28)
  - Track allocated regions for validation
- [ ] Implement `realloc(ptr, new_size)` — Resize allocation
  - Allocate new block, copy data, free old (simple strategy)
- [ ] Implement `calloc(count, size)` — Zero-initialized allocation
  - `malloc` + zero-fill loop
- [ ] Write test suite in UA:
  - Allocate 1 KB, 10 KB, 1 MB blocks
  - Realloc stress test (grow from 64 bytes to 1 MB in steps)
  - Validate pointer alignment (8-byte alignment on 64-bit architectures)
- [ ] Document memory model and heap layout

**Dependencies:** Core MVIS opcodes (`LDI`, `MOV`, `ADD`, `SUB`, `CMP`, `JZ`, `STORE`, `LOAD`, `SYS`)  
**Estimated Complexity:** Medium (200-300 lines UA)  
**Target Architectures:** x86-64, x86-32, ARM64, RISC-V

---

#### Task 1.2: Hash Table (`std_hashtable`)
- [ ] Design hash table structure:
  - Separate chaining with linked list buckets
  - Key: null-terminated string, Value: 64-bit integer (pointer or value)
  - Bucket count: power of 2 (e.g., 256, 512, 1024)
- [ ] Implement `ht_create(bucket_count)` — Allocate and initialize table
- [ ] Implement `ht_hash(key_string)` — Hash function (DJB2 or FNV-1a)
- [ ] Implement `ht_insert(table, key, value)` — Insert or update entry
- [ ] Implement `ht_lookup(table, key)` — Return value or 0 if not found
- [ ] Implement `ht_delete(table, key)` — Remove entry
- [ ] Implement `ht_destroy(table)` — Free all memory
- [ ] Write test suite:
  - Insert 1000 key-value pairs
  - Lookup performance test (all inserted keys)
  - Collision handling test (keys with identical hash % bucket_count)
- [ ] **Use case:** Symbol table for labels, variables, function names

**Dependencies:** `std_malloc`, `std_string` (strlen, string comparison)  
**Estimated Complexity:** Medium-High (300-500 lines UA)  
**Critical for:** Parser, precompiler import tracking

---

#### Task 1.3: Dynamic Array / Vector (`std_dynarray`)
- [ ] Extend `std_vector` to support:
  - Generic pointer storage (not just bytes)
  - Auto-resize with growth factor (e.g., 1.5x or 2x)
- [ ] Implement `da_create(initial_capacity, element_size)` — Allocate vector
- [ ] Implement `da_push(array, element_ptr)` — Append element, resize if needed
- [ ] Implement `da_get(array, index)` — Return pointer to element at index
- [ ] Implement `da_set(array, index, element_ptr)` — Overwrite element
- [ ] Implement `da_size(array)` — Return current element count
- [ ] Implement `da_capacity(array)` — Return allocated capacity
- [ ] Implement `da_destroy(array)` — Free memory
- [ ] **Use case:** Token array, IR instruction array, string pool

**Dependencies:** `std_malloc`  
**Estimated Complexity:** Low-Medium (150-250 lines UA)

---

### Phase 2: Advanced String & I/O Libraries
**Objective:** Provide robust string processing and file I/O required for compiler operations.

#### Task 2.1: Advanced String Operations (`std_string` extensions)
- [ ] Implement `strcmp(str1, str2)` — Compare strings, return -1/0/+1
- [ ] Implement `strcasecmp(str1, str2)` — Case-insensitive comparison
- [ ] Implement `strcat(dest, src)` — Concatenate (assumes dest has space)
- [ ] Implement `strcpy(dest, src)` — Copy string
- [ ] Implement `strncpy(dest, src, n)` — Copy up to N characters
- [ ] Implement `strstr(haystack, needle)` — Find substring
- [ ] Implement `strchr(str, char)` — Find first occurrence of char
- [ ] Implement `strdup(str)` — Allocate and copy string (uses malloc)
- [ ] Implement `trim_whitespace(str)` — Remove leading/trailing spaces, tabs, newlines
- [ ] Implement `split_string(str, delimiter)` — Split into array of substrings
- [ ] Write comprehensive test suite

**Dependencies:** `std_malloc`  
**Estimated Complexity:** Medium (200-300 lines UA)  
**Critical for:** Lexer, precompiler, argument parsing

---

#### Task 2.2: Formatted String Output (`std_format`)
- [ ] Implement `sprintf(buffer, format_string, ...)` — Basic printf-style formatting
  - Support: `%d` (decimal), `%x` (hex), `%s` (string), `%c` (char), `%%` (literal %)
  - No floating point (not needed for compiler)
  - Simple implementation: parse format string, interpolate args from registers
- [ ] Implement `format_error(line, col, message)` — Generate compiler error message
  - Format: `"Error at line {line}, column {col}: {message}\n"`
- [ ] Write test suite:
  - Format integers in decimal and hex
  - Multi-argument formatting
  - Buffer overflow protection

**Dependencies:** `std_string`, numeric conversion utilities  
**Estimated Complexity:** Medium (200-300 lines UA)  
**Critical for:** Error reporting, diagnostics, output generation

---

#### Task 2.3: Buffered File I/O (`std_bufio`)
- [ ] Implement `buf_fopen(path, mode)` — Open file with internal read/write buffer
  - Wrap `std_iostream.fopen`
  - Allocate 4 KB read/write buffer
- [ ] Implement `buf_fread_line(fd)` — Read one line (up to newline), return pointer
  - Buffer refill logic when buffer exhausted
- [ ] Implement `buf_fread_all(fd)` — Read entire file into dynamically-allocated buffer
  - Return pointer and size
- [ ] Implement `buf_fwrite(fd, data, size)` — Buffered write
  - Flush buffer when full
- [ ] Implement `buf_fflush(fd)` — Force buffer write to disk
- [ ] Implement `buf_fclose(fd)` — Flush and close
- [ ] **Use case:** Reading .ua source files line-by-line, writing output binaries

**Dependencies:** `std_iostream`, `std_malloc`  
**Estimated Complexity:** Medium (250-400 lines UA)

---

### Phase 3: Compiler Infrastructure — Lexer in UA
**Objective:** Rewrite the lexer (tokenizer) entirely in UA.

#### Task 3.1: Token Structure & Token Array Management
- [ ] Define token structure layout in UA:
  ```
  Token (128 bytes):
    +0:  type (8 bytes)          ; TOKEN_OPCODE, TOKEN_REGISTER, etc.
    +8:  text (64 bytes)         ; lexeme string
    +72: value (8 bytes)         ; numeric value
    +80: line (8 bytes)          ; source line
    +88: column (8 bytes)        ; source column
  ```
- [ ] Implement `token_create(type, text, value, line, col)` — Allocate token
- [ ] Implement `token_array_create()` — Create dynamic token array
- [ ] Implement `token_array_push(arr, token)` — Append token to array
- [ ] Write test: Create 1000 tokens, verify storage and retrieval

**Dependencies:** `std_malloc`, `std_dynarray`  
**Estimated Complexity:** Low (100-150 lines UA)

---

#### Task 3.2: Character Classification & Scanning
- [ ] Implement `is_digit(char)` — Returns 1 if '0'-'9'
- [ ] Implement `is_alpha(char)` — Returns 1 if 'a'-'z', 'A'-'Z'
- [ ] Implement `is_alnum(char)` — Returns 1 if alpha or digit
- [ ] Implement `is_whitespace(char)` — Returns 1 if space, tab, CR
- [ ] Implement `is_hex_digit(char)` — Returns 1 if '0'-'9', 'a'-'f', 'A'-'F'
- [ ] Implement `to_upper(char)` — Convert to uppercase
- [ ] Implement `to_lower(char)` — Convert to lowercase

**Dependencies:** None (pure MVIS logic)  
**Estimated Complexity:** Low (50-100 lines UA)

---

#### Task 3.3: Lexer Core — Tokenization Logic
- [ ] Implement `tokenize(source_code_ptr, source_length)` — Main entry point
  - Returns token array pointer
- [ ] Implement scanning state machine:
  - Whitespace skipping
  - Comment detection (`;` to end-of-line)
  - Identifier/label/opcode recognition
  - Register recognition (`R0`-`R15`)
  - Number parsing (decimal, `0x` hex, `0b` binary)
  - String literal parsing (`"..."` with escape sequences)
  - Punctuation (`,`, `:`, `(`, `)`, `#`)
- [ ] Implement opcode table lookup — map string → opcode enum
  - Use hash table for O(1) lookup (37 core + 14 arch-specific opcodes)
- [ ] Implement label vs. label_ref disambiguation (`:` suffix detection)
- [ ] Error handling:
  - Unknown character → TOKEN_UNKNOWN with diagnostic
  - Unterminated string → error message with line/column
- [ ] Write test suite:
  - Tokenize `tests/hello.ua`, validate token sequence
  - Tokenize `tests/calc.ua`, validate numeric literals
  - Tokenize malformed input, verify error reporting

**Dependencies:** `token_array`, `std_hashtable`, `std_string`, `std_bufio`  
**Estimated Complexity:** High (600-800 lines UA)  
**Critical Success Factor:** Lexer must match C version's behavior exactly

---

### Phase 4: Compiler Infrastructure — Parser in UA
**Objective:** Rewrite the parser to generate IR from tokens, entirely in UA.

#### Task 4.1: Instruction Structure & IR Array Management
- [ ] Define Instruction structure layout:
  ```
  Instruction (256 bytes):
    +0:   is_label (8 bytes)
    +8:   label_name (128 bytes)
    +136: is_function (8 bytes)
    +144: param_count (8 bytes)
    +152: param_names (8 * 8 = 64 bytes, pointers to strings)
    +216: opcode (8 bytes)
    +224: operands (3 * 16 = 48 bytes, each operand = type + data union)
    +272: operand_count (8 bytes)
    +280: line (8 bytes)
    +288: column (8 bytes)
  ```
- [ ] Implement `instr_create_label(name, line, col)` — Create label IR entry
- [ ] Implement `instr_create_opcode(opcode, operands, count, line, col)` — Create instruction
- [ ] Implement `ir_array_create()` — Dynamic IR instruction array
- [ ] Implement `ir_array_push(arr, instr)` — Append instruction to IR

**Dependencies:** `std_malloc`, `std_dynarray`  
**Estimated Complexity:** Low (100-150 lines UA)

---

#### Task 4.2: Operand Parsing
- [ ] Implement `parse_operand(token)` — Convert token to Operand struct
  - TOKEN_REGISTER → OPERAND_REGISTER (extract R0-R15 number)
  - TOKEN_NUMBER → OPERAND_IMMEDIATE (extract int64 value)
  - TOKEN_LABEL_REF / TOKEN_IDENTIFIER → OPERAND_LABEL_REF
  - TOKEN_STRING → OPERAND_STRING
- [ ] Implement operand validation (register range, immediate size)
- [ ] Write test: Parse operands from token stream, validate types

**Dependencies:** Token structures  
**Estimated Complexity:** Low (100-150 lines UA)

---

#### Task 4.3: Parser Core — IR Generation
- [ ] Implement `parse(token_array)` — Main entry point, returns IR array
- [ ] Implement instruction grammar validation:
  - Load operand shape table (e.g., `MOV Rd, Rs` → register, register)
  - Validate operand count and types per opcode
- [ ] Implement two-pass label resolution:
  - **Pass 1:** Build symbol table (label name → IR index/address)
  - **Pass 2:** Resolve all `OPERAND_LABEL_REF` to addresses
- [ ] Implement function parameter parsing:
  - Detect `label(param1, param2):` syntax
  - Store parameter names in Instruction.param_names
- [ ] Error handling:
  - Wrong operand count → "Expected N operands, got M" at line X
  - Invalid operand type → "Expected register, got immediate" at line Y
  - Undefined label → "Undefined label 'foo' referenced at line Z"
  - Duplicate labels → "Duplicate label 'bar' defined at lines W and Z"
- [ ] Write test suite:
  - Parse `tests/test_func.ua`, validate function parameter capture
  - Parse `tests/test_jl_simple.ua`, validate forward label references
  - Parse file with undefined label, verify error
  - Parse file with duplicate labels, verify error

**Dependencies:** `ir_array`, `parse_operand`, `std_hashtable`, `std_string`  
**Estimated Complexity:** High (700-1000 lines UA)  
**Critical Success Factor:** IR must be identical to C parser output

---

### Phase 5: Compiler Infrastructure — Precompiler in UA
**Objective:** Rewrite precompiler (directive processing, file inclusion) in UA.

#### Task 5.1: Directive Detection & Parsing
- [ ] Implement `is_directive(line)` — Returns 1 if line starts with `@`
- [ ] Implement `parse_directive(line)` — Extract directive name and arguments
  - `@IF_ARCH x86` → directive=IF_ARCH, arg=x86
  - `@IMPORT "lib/std_io.ua"` → directive=IMPORT, arg=lib/std_io.ua
  - `@DEFINE FOO 42` → directive=DEFINE, name=FOO, value=42
- [ ] Write test: Parse all directive types from `tests/test_precompiler.ua`

**Dependencies:** `std_string`  
**Estimated Complexity:** Low (100-150 lines UA)

---

#### Task 5.2: Conditional Compilation Logic
- [ ] Implement conditional stack:
  - `total_depth` — nesting level counter
  - `active_depth` — how many levels have satisfied conditions
- [ ] Implement `@IF_ARCH` / `@IF_SYS` evaluation:
  - Compare argument against current `-arch` / `-sys`
  - Push/pop depth counters
- [ ] Implement `@ENDIF` handling
- [ ] Implement active region detection: `active_depth == total_depth`
- [ ] Write test:
  - Preprocess file with nested conditionals
  - Verify correct blocks included/excluded based on target arch

**Dependencies:** `strcmp`, `strcasecmp`  
**Estimated Complexity:** Medium (200-300 lines UA)

---

#### Task 5.3: File Import & Macro Expansion
- [ ] Implement `@IMPORT` handling:
  - Resolve path relative to importing file's directory
  - Track imported files in hash table (de-duplication)
  - Recursively preprocess imported file
  - Prefix all labels/functions/vars with filename (namespace logic)
- [ ] Implement `@DEFINE` macro table:
  - Hash table: macro_name → replacement_value
  - Scan every non-directive line for token replacement
  - Respect token boundaries (don't replace substrings)
- [ ] Implement `@ARCH_ONLY` / `@SYS_ONLY` guards:
  - Parse comma-separated list
  - Abort compilation with error if no match
- [ ] Implement `@DUMMY` stub diagnostic
- [ ] Write test:
  - Import chain: A imports B, B imports C
  - Verify correct namespace prefixing (B.label, C.label)
  - Verify macro expansion

**Dependencies:** `std_bufio`, `std_hashtable`, `std_string`, path utilities  
**Estimated Complexity:** High (500-700 lines UA)  
**Critical Success Factor:** Must handle recursive imports and namespace prefixing identically to C version

---

### Phase 6: Backend Code Generation in UA (Single Architecture)
**Objective:** Rewrite one backend (x86-64) entirely in UA as proof-of-concept.

#### Task 6.1: Code Buffer Management (`std_codebuffer`)
- [ ] Implement `cb_create(initial_capacity)` — Allocate code buffer (e.g., 4 KB)
- [ ] Implement `cb_emit_byte(buf, byte)` — Append byte, resize if needed
- [ ] Implement `cb_emit_word(buf, word)` — Append 16-bit value (little-endian)
- [ ] Implement `cb_emit_dword(buf, dword)` — Append 32-bit value
- [ ] Implement `cb_emit_qword(buf, qword)` — Append 64-bit value
- [ ] Implement `cb_size(buf)` — Return current size
- [ ] Implement `cb_get_bytes(buf)` — Return pointer to byte array
- [ ] Implement `cb_destroy(buf)` — Free memory

**Dependencies:** `std_malloc`  
**Estimated Complexity:** Low (150-200 lines UA)

---

#### Task 6.2: x86-64 Backend — Register Mapping & Encoding
- [ ] Implement register mapping table:
  - R0→RAX, R1→RCX, R2→RDX, R3→RBX, R4→RSP, R5→RBP, R6→RSI, R7→RDI
- [ ] Implement ModR/M byte encoding function
- [ ] Implement REX prefix generation (64-bit operand size)
- [ ] Implement SIB byte encoding (for indexed addressing, if needed)
- [ ] Write test: Encode MOV RAX, RCX → bytes `48 89 C8`

**Dependencies:** None (pure logic)  
**Estimated Complexity:** Medium (200-300 lines UA)

---

#### Task 6.3: x86-64 Backend — Instruction Encoding
- [ ] Implement encoding for each MVIS opcode:
  - [ ] `MOV Rd, Rs` → `MOV r64, r64`
  - [ ] `LDI Rd, imm` → `MOV r64, imm64`
  - [ ] `ADD Rd, Rs` → `ADD r64, r64`
  - [ ] `SUB Rd, Rs` → `SUB r64, r64`
  - [ ] `MUL Rd, Rs` → `IMUL r64, r64`
  - [ ] `DIV` → complex (needs RDX:RAX setup, IDIV)
  - [ ] `LOAD Rd, Rs` → `MOV r64, [r64]`
  - [ ] `STORE Rs, Rd` → `MOV [r64], r64`
  - [ ] `LOADB Rd, Rs` → `MOVZX r64, BYTE PTR [r64]`
  - [ ] `STOREB Rs, Rd` → `MOV BYTE PTR [r64], r8`
  - [ ] `JMP label` → `JMP rel32` (two-pass: compute offset)
  - [ ] `JZ label` → `JE rel32`
  - [ ] `JNZ label` → `JNE rel32`
  - [ ] `JL label` → `JL rel32`
  - [ ] `JG label` → `JG rel32`
  - [ ] `CALL label` → `CALL rel32`
  - [ ] `RET` → `RET`
  - [ ] `PUSH Rs` → `PUSH r64`
  - [ ] `POP Rd` → `POP r64`
  - [ ] `CMP Ra, Rb` → `CMP r64, r64`
  - [ ] `AND`, `OR`, `XOR`, `NOT`, `SHL`, `SHR`
  - [ ] `INC`, `DEC`
  - [ ] `INT #imm` → `INT imm8`
  - [ ] `SYS` → `SYSCALL`
  - [ ] `NOP` → `NOP` (0x90)
  - [ ] `HLT` → `HLT` (0xF4)
  - [ ] `LDS Rd, "string"` → allocate string in .rodata, `LEA r64, [RIP+offset]`
  - [ ] `VAR name, init` → allocate in .data section
  - [ ] `SET name, Rs` → `MOV [name], r64`
  - [ ] `GET Rd, name` → `MOV r64, [name]`
  - [ ] `BUFFER name, size` → allocate N bytes in .bss
- [ ] Implement two-pass assembly:
  - **Pass 1:** Emit code, track label addresses (instruction offset in CodeBuffer)
  - **Pass 2:** Resolve label references, patch jump/call offsets
- [ ] Implement `.text` / `.data` / `.bss` / `.rodata` section tracking
- [ ] Write test:
  - Generate code for `tests/calc.ua`
  - Compare binary output to C compiler's x86-64 backend

**Dependencies:** `cb_emit_*`, label symbol table, IR structures  
**Estimated Complexity:** Very High (1500-2500 lines UA)  
**Critical Success Factor:** Generated x86-64 code must be byte-identical to C backend

---

### Phase 7: Output Emitters in UA
**Objective:** Generate PE/ELF/Mach-O executable files in UA.

#### Task 7.1: ELF Emitter for Linux x86-64
- [ ] Define ELF64 header structure (64 bytes)
- [ ] Define ELF64 program header (56 bytes, for PT_LOAD segment)
- [ ] Implement `emit_elf_executable(code_buf, output_path, entry_point, arch)`:
  - Write ELF header (magic, class, endian, machine type)
  - Write program header (load address, file offset, mem size, permissions)
  - Write `.text` section (code bytes)
  - Write `.data` section (initialized variables)
  - Write `.rodata` section (string literals)
- [ ] Handle relocations (if needed for PIC code)
- [ ] Write test:
  - Generate ELF for `tests/hello.ua`
  - Verify with `readelf -h output`
  - Execute on Linux x86-64, confirm output

**Dependencies:** `std_bufio`, `std_codebuffer`, binary struct packing  
**Estimated Complexity:** High (600-900 lines UA)  
**Reference:** Current C implementation in `emitter_elf.c`

---

#### Task 7.2: PE Emitter for Windows x86-64
- [ ] Define PE/COFF structures:
  - DOS header + stub
  - PE signature
  - COFF file header
  - Optional header (PE32+)
  - Section headers (`.text`, `.data`, `.idata` for imports)
- [ ] Implement `emit_pe_executable(code_buf, output_path, entry_point)`:
  - Write DOS header with "MZ" signature
  - Write PE headers
  - Write section data
  - Build Import Address Table (IAT) for kernel32.dll (if needed)
- [ ] Write test:
  - Generate PE for `tests/hello.ua` targeting Win32
  - Verify with `dumpbin /headers output.exe` (or `objdump` on Linux)
  - Execute on Windows x86-64

**Dependencies:** `std_bufio`, `std_codebuffer`  
**Estimated Complexity:** High (700-1100 lines UA)  
**Reference:** Current C implementation in `emitter_pe.c`

---

#### Task 7.3: Mach-O Emitter for macOS ARM64/x86-64
- [ ] Define Mach-O structures:
  - Mach header (`mach_header_64`)
  - Load commands (`LC_SEGMENT_64`, `LC_MAIN`, `LC_DYSYMTAB`)
  - Segment structures (for `__TEXT`, `__DATA` segments)
- [ ] Implement `emit_macho_executable(code_buf, output_path, entry_point, arch)`:
  - Handle both ARM64 and x86-64 (`CPU_TYPE_ARM64`, `CPU_TYPE_X86_64`)
  - Write Mach-O header
  - Write load commands
  - Write segment data
- [ ] Write test:
  - Generate Mach-O for `tests/hello.ua` on macOS ARM64
  - Verify with `otool -h output`
  - Execute on macOS

**Dependencies:** `std_bufio`, `std_codebuffer`  
**Estimated Complexity:** High (600-900 lines UA)  
**Reference:** Current C implementation in `emitter_macho.c`

---

### Phase 8: Integration & Command-Line Driver
**Objective:** Build the UA compiler's main driver that ties all stages together.

#### Task 8.1: Command-Line Argument Parser (`std_args`)
- [ ] Implement `args_parse(argc, argv)` — Parse arguments into Config struct:
  ```
  Config:
    input_file:  pointer to .ua source path
    output_file: pointer to output path (default: "a.out")
    arch:        pointer to architecture string (mcs51|x86|x86_32|arm|arm64|riscv)
    sys:         pointer to system string (baremetal|win32|linux|macos)
    run:         JIT flag (0 or 1)
  ```
- [ ] Support flags:
  - `<input.ua>` — positional argument
  - `-arch <arch>` — mandatory
  - `-o <output>` — optional
  - `-sys <system>` — optional
  - `--run` — boolean flag
  - `-v`, `--version` — print version and exit
- [ ] Error handling:
  - Missing required flags → print usage and exit
  - Unknown flag → error message
- [ ] Write test: Parse various argument combinations, validate Config struct

**Dependencies:** `std_string`, `split_string`  
**Estimated Complexity:** Medium (200-300 lines UA)

---

#### Task 8.2: Compiler Pipeline Integration (`main.ua`)
- [ ] Implement `main(argc, argv)`:
  1. Parse arguments → Config
  2. Read source file → `source_code` string
  3. Preprocess → `preprocessed_code`
  4. Tokenize → `token_array`
  5. Parse → `ir_array`
  6. Validate opcode compliance (arch/sys masks) → pass/fail
  7. Dispatch to backend (based on Config.arch):
     - `generate_x86_64(ir_array)` → CodeBuffer
     - `generate_x86_32(ir_array)` → CodeBuffer
     - `generate_arm64(ir_array)` → CodeBuffer
     - `generate_riscv(ir_array)` → CodeBuffer
  8. Emit output file (based on Config.sys):
     - Linux → `emit_elf_executable(code_buf, output_path)`
     - Win32 → `emit_pe_executable(code_buf, output_path)`
     - macOS → `emit_macho_executable(code_buf, output_path)`
     - baremetal → write raw binary to file
  9. Cleanup: free all allocated memory
  10. Exit with status code (0 = success, 1 = error)
- [ ] Error handling:
  - File not found → "Error: could not open input file 'foo.ua'"
  - Parse error → exit after error message
  - Compliance error → exit after diagnostic
  - Output write failure → "Error: could not write output file"
- [ ] Write test:
  - Compile `tests/hello.ua` with UA compiler (self-hosted!)
  - Compile `tests/calc.ua`, verify output matches C compiler's output
  - Compile invalid .ua file, verify error reporting

**Dependencies:** All previous components  
**Estimated Complexity:** Medium (300-500 lines UA)  
**Milestone:** When this works, UA is self-hosting for one architecture (x86-64)!

---

### Phase 9: Multi-Architecture Support
**Objective:** Extend self-hosting to all target architectures (x86-32, ARM64, RISC-V).

#### Task 9.1: x86-32 Backend in UA
- [ ] Port `backend_x86_32.c` logic to UA
- [ ] Handle 32-bit register mapping (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
- [ ] Encode 32-bit instructions (no REX prefix)
- [ ] Implement syscall via `INT 0x80` (Linux) or kernel32 dispatcher (Win32)
- [ ] Write test: Compile and run `tests/hello.ua` as 32-bit ELF

**Dependencies:** x86-64 backend in UA (leveraging similar logic)  
**Estimated Complexity:** High (1200-1800 lines UA)

---

#### Task 9.2: ARM64 Backend in UA
- [ ] Port `backend_arm64.c` logic to UA
- [ ] Handle ARM64 instruction encoding (32-bit fixed-width instructions)
- [ ] Implement register mapping (X0-X7)
- [ ] Encode branch instructions (B, BL, B.cond) with PC-relative offsets
- [ ] Encode load/store (LDR, STR)
- [ ] Encode arithmetic (ADD, SUB, MUL, SDIV)
- [ ] Implement syscall via `SVC #0`
- [ ] Write test: Compile and run `tests/hello.ua` on ARM64 Linux

**Dependencies:** ARM architecture knowledge  
**Estimated Complexity:** Very High (1500-2200 lines UA)

---

#### Task 9.3: RISC-V Backend in UA
- [ ] Port `backend_risc_v.c` logic to UA
- [ ] Handle RISC-V RV64I+M instruction encoding (32-bit fixed-width)
- [ ] Implement register mapping (a0-a7 = x10-x17)
- [ ] Encode I-type, R-type, S-type, B-type, U-type, J-type formats
- [ ] Encode branch instructions (BEQ, BNE, BLT, BGE)
- [ ] Encode load/store (LD, SD, LB, SB)
- [ ] Encode arithmetic (ADD, SUB, MUL, DIV)
- [ ] Implement syscall via `ECALL`
- [ ] Write test: Compile and run `tests/hello.ua` on RISC-V Linux

**Dependencies:** RISC-V architecture knowledge  
**Estimated Complexity:** Very High (1300-2000 lines UA)

---

### Phase 10: Testing, Validation & Documentation
**Objective:** Ensure the self-hosted compiler is production-ready.

#### Task 10.1: Comprehensive Test Suite
- [ ] Create test matrix:
  - All architectures (x86, x86_32, arm64, riscv) ✕ all systems (linux, win32, macos, baremetal)
  - All MVIS opcodes
  - All architecture-specific opcodes (on their respective targets)
  - Edge cases: forward labels, nested imports, macro expansion
- [ ] Automated testing script:
  - Compile each `.ua` file in `tests/` with both C compiler and UA compiler
  - Compare binary outputs (or execution outputs for JIT tests)
  - Report pass/fail for each test
- [ ] Performance benchmarking:
  - Measure compilation time for large projects
  - Compare C compiler vs. UA compiler (expect UA to be slower initially)
- [ ] Validate binary compatibility:
  - Same `.ua` source compiled by C version and UA version → identical binary output

**Dependencies:** Full self-hosted compiler  
**Estimated Complexity:** High (testing infrastructure + test cases)

---

#### Task 10.2: Bootstrap Process Documentation
- [ ] Document the bootstrap sequence:
  1. Compile UA compiler (written in C) with GCC → `ua_c`
  2. Compile UA compiler (written in UA) with `ua_c` → `ua_ua_gen1`
  3. Compile UA compiler (written in UA) with `ua_ua_gen1` → `ua_ua_gen2`
  4. Compare binaries: if `ua_ua_gen1` == `ua_ua_gen2`, bootstrap complete ✅
- [ ] Write `bootstrap.sh` script:
  ```bash
  #!/bin/bash
  # Stage 1: Build C version
  gcc -o ua_c src/*.c
  
  # Stage 2: Build UA version with C compiler
  ./ua_c main.ua -arch x86 -sys linux -o ua_stage1
  
  # Stage 3: Build UA version with Stage 1 UA compiler
  ./ua_stage1 main.ua -arch x86 -sys linux -o ua_stage2
  
  # Stage 4: Verify binary stability
  cmp ua_stage1 ua_stage2 && echo "Bootstrap SUCCESS!" || echo "Bootstrap FAILED"
  ```
- [ ] Document expected build times and system requirements

---

#### Task 10.3: Self-Hosting User Guide
- [ ] Update `README.md`:
  - Section: "Building UA from Source (Self-Hosted)"
  - Prerequisites: none (just the UA compiler binary!)
  - Build command: `ua ua.ua -arch x86 -sys linux -o ua`
- [ ] Create `docs/self-hosting.md`:
  - Architecture overview of the UA compiler written in UA
  - File structure (`src_ua/` directory layout)
  - How to contribute to the UA-in-UA codebase
  - Debug tips (compiling with debug symbols, using GDB/LLDB)
- [ ] Create `docs/stdlib-reference.md`:
  - Complete API documentation for all new std libraries:
    - `std_malloc`, `std_hashtable`, `std_dynarray`, `std_format`, `std_bufio`, `std_codebuffer`, `std_args`
  - Usage examples for each library

---

### Phase 11: Performance Optimization & Refinement (Post-Self-Hosting)
**Objective:** Improve compiler speed and output quality.

#### Task 11.1: Optimized Memory Allocator
- [ ] Replace bump allocator with free-list allocator
- [ ] Implement block coalescing for `free()`
- [ ] Add allocation statistics tracking

---

#### Task 11.2: Register Allocation Optimization
- [ ] Implement register usage analysis in IR
- [ ] Implement register spilling (allocate stack slots for exceeding R0-R7)
- [ ] Minimize MOV instructions in output

---

#### Task 11.3: Peephole Optimization
- [ ] Detect and eliminate:
  - Redundant MOV (e.g., `MOV RAX, RAX`)
  - Dead code after unconditional JMP
  - Unnecessary PUSH/POP pairs
- [ ] Strength reduction (e.g., MUL by power-of-2 → SHL)

---

#### Task 11.4: Parallel Compilation Support
- [ ] Implement multi-file compilation (compile multiple `.ua` files in parallel)
- [ ] Linking phase: merge multiple CodeBuffers into one executable

---

## Release Checklist for Version 27

**Pre-Release:**
- [ ] All Phase 1-10 tasks completed
- [ ] Bootstrap test passes on x86-64 Linux
- [ ] Bootstrap test passes on ARM64 macOS
- [ ] Bootstrap test passes on x86 Windows (if applicable)
- [ ] All existing `tests/*.ua` programs compile and run correctly with self-hosted compiler
- [ ] Documentation complete (`README.md`, `docs/self-hosting.md`, `docs/stdlib-reference.md`)
- [ ] Changelog updated with all new features
- [ ] Performance: Self-hosted compiler builds itself in < 10 seconds on modern hardware

**Release Artifacts:**
- [ ] `ua` binary (x86-64 Linux)
- [ ] `ua.exe` binary (x86-64 Windows)
- [ ] `ua` binary (ARM64 macOS)
- [ ] Source code: `main.ua` + all `std_*.ua` libraries + backend UA files
- [ ] Test suite: `tests/` directory
- [ ] Documentation: `docs/` directory

**Announcement:**
- [ ] Blog post: "UA Version 27 'Amazing Grace' — A Self-Hosting Compiler"
- [ ] Demonstrate bootstrap process in video/GIF
- [ ] Highlight key achievement: **No dependency on C compilers to build or modify UA**

---

## Risk Assessment

| Risk | Impact | Mitigation Strategy |
|------|--------|---------------------|
| **Memory bugs in UA-written allocator** | High — crashes, leaks | Extensive testing with valgrind-equivalent debugging; start with simple bump allocator |
| **Binary output mismatch between C and UA compilers** | High — incorrect code | Byte-by-byte comparison tests; incremental validation during development |
| **Performance: UA compiler too slow** | Medium — poor UX | Acceptable for v27; optimize in v28; initial target: < 10sec for self-compile |
| **Incomplete backend coverage (missing opcodes)** | High — compiler can't compile itself | Thorough audit: ensure every MVIS opcode used by compiler is correctly encoded |
| **File I/O errors (import resolution, path handling)** | Medium — compiler crashes on complex projects | Robust error handling; test with deeply nested imports |
| **Platform-specific syscall bugs** | Medium — compiler fails on untested OS | Test on all target platforms (Linux, Windows, macOS); maintain syscall compatibility layer |
| **Hash table collisions cause symbol lookup failures** | High — linker errors | Use well-tested hash function (FNV-1a); validate with large symbol tables (1000+ labels) |
| **Insufficient testing before release** | Critical — buggy compiler | Allocate 20% of development time to testing; automated CI pipeline |

---

## Estimated Effort

| Phase | Tasks | Complexity | Est. Lines of UA | Est. Time |
|-------|-------|------------|------------------|-----------|
| 1. Foundation (malloc, hashtable, dynarray) | 3 | Medium-High | 800 | 3-4 weeks |
| 2. Advanced String & I/O | 3 | Medium | 700 | 2-3 weeks |
| 3. Lexer in UA | 3 | High | 900 | 3-4 weeks |
| 4. Parser in UA | 3 | High | 1100 | 4-5 weeks |
| 5. Precompiler in UA | 3 | High | 900 | 3-4 weeks |
| 6. x86-64 Backend in UA | 3 | Very High | 2200 | 6-8 weeks |
| 7. Output Emitters in UA | 3 | High | 2200 | 4-6 weeks |
| 8. CLI Driver & Integration | 2 | Medium | 600 | 2-3 weeks |
| 9. Multi-Arch Backends | 3 | Very High | 4000 | 8-12 weeks |
| 10. Testing & Documentation | 3 | High | N/A | 4-6 weeks |
| **Total** | **29 tasks** | **—** | **~13,400 lines** | **39-55 weeks** |

**Note:** Estimates assume one experienced developer working full-time. Parallelizable work (e.g., multiple backends, testing) can reduce calendar time.

---

## Success Metrics for Version 27

1. **Self-Compilation:** UA compiler (written in UA) successfully compiles itself on x86-64 Linux, producing a binary identical to the previous generation.
2. **Binary Stability:** Three-generation bootstrap (C → UA gen1 → UA gen2 → UA gen3), with gen2 == gen3 byte-for-byte.
3. **Feature Completeness:** All 37 MVIS opcodes + 14 arch-specific opcodes correctly implemented in UA backends.
4. **Test Coverage:** 100% of existing `tests/*.ua` programs compile and execute correctly.
5. **Documentation:** Complete self-hosting guide, standard library reference, and bootstrap instructions.
6. **Performance:** Self-hosted compiler completes full compilation (preprocess → parse → codegen → emit) in < 10 seconds for 10,000-line source file on modern hardware.

---

## Post-Version 27 Roadmap (Future Work)

**Version 28 "Brilliant Babbage"** (Optimization & Usability):
- Advanced optimizations (dead code elimination, constant folding, loop unrolling)
- Incremental compilation & caching
- IDE integration (LSP server in UA)
- Package manager (`ua install <package>`)

**Version 29 "Clever Curry"** (Concurrency & Parallelism):
- Multi-threaded compilation
- Atomic operations & memory barriers in MVIS
- Concurrency primitives in standard library

**Version 30 "Daring Dijkstra"** (Tooling Ecosystem):
- Debugger written in UA (dwarf/pdb debug info generation)
- Profiler & performance analysis tools
- Assembler disassembler (`ua-objdump`)

---

## Closing Statement

Version 27 "Amazing Grace" represents the **ultimate validation** of the UA project: a compiler that needs no external dependencies, no C toolchain, no complex build system. Just UA compiling UA, creating a perfectly autonomous, self-sustaining ecosystem.

When complete, a developer can take **a single UA binary** and a text editor to any supported platform, modify the compiler's source code in UA assembly, and rebuild the compiler **using itself**. This is the Ouroboros realized — a language that creates itself, cycles through itself, and emerges stronger with each generation.

Let us honor Ada Lovelace's vision of an analytical engine capable of self-directed computation. **Let UA compile itself.** and hand it over to the most awesome woman Grace Hopper.

---

*Document Version: 1.0*  
*Created: March 1, 2026*  
*Target Release: Q4 2026*
