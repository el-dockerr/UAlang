# UA Standard Libraries Reference

The UA standard libraries are written entirely in UA assembly and provide reusable functions for console I/O, string handling, math, data structures, and file I/O. Import them with `@IMPORT`:

```asm
@IMPORT std_io
@IMPORT std_string
@IMPORT std_math
@IMPORT std_arrays
@IMPORT std_array
@IMPORT std_vector
@IMPORT std_iostream
```

All functions follow the UA calling convention: parameters are passed via registers (R0–R7) or shared `VAR`s (set with `SET`, read with `GET`). Return values come back in R0.

---

## Table of Contents

1. [std_io — Console I/O](#std_io--console-io)
2. [std_string — String Utilities](#std_string--string-utilities)
3. [std_math — Integer Math](#std_math--integer-math)
4. [std_arrays — Byte Array Helpers](#std_arrays--byte-array-helpers)
5. [std_array — Fixed-Size Byte Array](#std_array--fixed-size-byte-array)
6. [std_vector — Dynamic Byte Vector](#std_vector--dynamic-byte-vector)
7. [std_iostream — File I/O Streams](#std_iostream--file-io-streams)
8. [Platform Support Matrix](#platform-support-matrix)

---

## std_io — Console I/O

**Import:** `@IMPORT std_io`
**Source:** [lib/std_io.ua](../lib/std_io.ua)

Read from stdin and write to stdout using OS system calls.

### Functions

#### `print`

Write a null-terminated string to stdout.

| | |
|---|---|
| **Input** | R0 = pointer to null-terminated string |
| **Output** | *(none)* |
| **Clobbers** | R0, R1, R2, R3, R6, R7 |

The function computes `strlen` internally, then issues a write syscall. On Win32, the SYS dispatcher routes through `WriteFile` via kernel32.

```asm
@IMPORT std_io

    LDS  R0, "Hello, World!\n"
    CALL std_io.print
    HLT
```

#### `read`

Read up to N bytes from stdin into a buffer.

| | |
|---|---|
| **Input** | R0 = pointer to buffer, R1 = max bytes to read |
| **Output** | R0 = number of bytes actually read (Linux); undefined on Win32 |
| **Clobbers** | R0, R1, R2, R3, R6, R7 |

> **Note:** The buffer is **not** null-terminated by the OS. Add the null byte yourself after reading:
> ```asm
> CALL std_io.read          ; R0 = bytes read
> GET  R3, my_buffer
> ADD  R3, R0               ; R3 → one past last byte
> LDI  R1, 0
> STOREB R1, R3             ; null-terminate
> ```

```asm
@IMPORT std_io

    BUFFER input, 64
    GET  R0, input
    LDI  R1, 63              ; leave room for null
    CALL std_io.read
    ; R0 = bytes read
```

---

## std_string — String Utilities

**Import:** `@IMPORT std_string`
**Source:** [lib/std_string.ua](../lib/std_string.ua)

String operations on null-terminated byte strings. Uses only architecture-neutral MVIS instructions — works on **all** backends including 8051.

### Functions

#### `strlen`

Compute the length of a null-terminated string.

| | |
|---|---|
| **Input** | R0 = pointer to string |
| **Output** | R1 = length (bytes, excluding null terminator) |
| **Clobbers** | R0, R2, R3 |

```asm
@IMPORT std_string

    LDS  R0, "Hello"
    CALL std_string.strlen
    ; R1 = 5
```

#### `parse_int`

Parse an ASCII decimal string into an unsigned integer.

| | |
|---|---|
| **Input** | R0 = pointer to null-terminated digit string (e.g. `"42\n"` or `"123\0"`) |
| **Output** | R0 = parsed integer value |
| **Clobbers** | R1, R2, R3, R6, R7 |

Stops at null (`\0`), newline (`\n`), or carriage return (`\r`). Handles unsigned values only.

```asm
@IMPORT std_string

    LDS  R0, "256"
    CALL std_string.parse_int
    ; R0 = 256
```

#### `to_string`

Convert a signed integer to a null-terminated ASCII decimal string.

| | |
|---|---|
| **Input** | R0 = integer value (signed), R1 = output buffer pointer |
| **Output** | Buffer at R1 filled with ASCII representation + null terminator |
| **Clobbers** | R0, R1, R2, R3, R6, R7 |

Handles negative numbers (writes leading `'-'`). Buffer must be at least 12 bytes (32-bit) or 22 bytes (64-bit).

```asm
@IMPORT std_string

    BUFFER num_buf, 16
    LDI  R0, -42
    GET  R1, num_buf
    CALL std_string.to_string
    ; num_buf now contains "-42\0"
```

---

## std_math — Integer Math

**Import:** `@IMPORT std_math`
**Source:** [lib/std_math.ua](../lib/std_math.ua)

Integer math utility functions. Uses only architecture-neutral MVIS instructions — works on **all** backends.

> **8051 note:** `MUL` uses the hardware 8×8 multiplier (`MUL AB`). Results are limited to 8-bit operands; overflow wraps silently.

### Functions

#### `pow`

Integer exponentiation: $\text{base}^{\text{exp}}$.

| | |
|---|---|
| **Input** | R0 = base, R1 = exponent (≥ 0) |
| **Output** | R0 = result |
| **Clobbers** | R1, R2, R3 |

```asm
@IMPORT std_math

    LDI  R0, 2
    LDI  R1, 10
    CALL std_math.pow
    ; R0 = 1024
```

#### `factorial`

Compute $n!$ (n factorial).

| | |
|---|---|
| **Input** | R0 = n (≥ 0) |
| **Output** | R0 = n! |
| **Clobbers** | R1, R2 |

```asm
@IMPORT std_math

    LDI  R0, 5
    CALL std_math.factorial
    ; R0 = 120
```

#### `max`

Return the larger of two signed integers.

| | |
|---|---|
| **Input** | R0 = a, R1 = b |
| **Output** | R0 = max(a, b) |
| **Clobbers** | *(none besides R0)* |

```asm
@IMPORT std_math

    LDI  R0, 3
    LDI  R1, 7
    CALL std_math.max
    ; R0 = 7
```

#### `abs`

Absolute value of a signed integer.

| | |
|---|---|
| **Input** | R0 = signed value |
| **Output** | R0 = \|value\| |
| **Clobbers** | R1, R2 |

```asm
@IMPORT std_math

    LDI  R0, -15
    CALL std_math.abs
    ; R0 = 15
```

---

## std_arrays — Byte Array Helpers

**Import:** `@IMPORT std_arrays`
**Source:** [lib/std_arrays.ua](../lib/std_arrays.ua)

Low-level byte-array primitives for contiguous memory buffers. Works on **all** backends.

> **8051 note:** `LOADB` / `STOREB` use indirect addressing — only internal RAM (0x00–0xFF) is accessible.

### Functions

#### `fill_bytes`

Fill a byte buffer with a constant value.

| | |
|---|---|
| **Input** | R0 = destination address, R1 = byte count, R2 = fill value (low byte) |
| **Output** | *(none — buffer filled in-place)* |
| **Clobbers** | R0 (advanced past end), R1 (zeroed), R3 |

```asm
@IMPORT std_arrays

    BUFFER buf, 32
    GET  R0, buf
    LDI  R1, 32
    LDI  R2, 0xFF
    CALL std_arrays.fill_bytes
    ; buf now contains 32 bytes of 0xFF
```

#### `copy_bytes`

Copy bytes from source to destination.

| | |
|---|---|
| **Input** | R0 = source address, R1 = destination address, R2 = byte count |
| **Output** | *(none — bytes copied in-place)* |
| **Clobbers** | R0, R1 (both advanced), R2 (zeroed), R3, R4 |

```asm
@IMPORT std_arrays

    BUFFER src, 16
    BUFFER dst, 16
    GET  R0, src
    GET  R1, dst
    LDI  R2, 16
    CALL std_arrays.copy_bytes
```

---

## std_array — Fixed-Size Byte Array

**Import:** `@IMPORT std_array`
**Source:** [lib/std_array.ua](../lib/std_array.ua)

Fixed-size byte array modelled after C++ `std::array<uint8_t, N>`. Works on **all** backends including 8051.

### Setup

Allocate a `BUFFER`, then pass its address and size to the library via shared variables:

```asm
@IMPORT std_array

    BUFFER my_arr, 10
    GET  R0, my_arr
    SET  std_array.ptr, R0
    SET  std_array.size, 10
```

### Parameter Variables

| Variable | Type | Description |
|---|---|---|
| `std_array.ptr` | address | Base address of the BUFFER |
| `std_array.size` | integer | Number of elements (fixed at creation) |
| `std_array.index` | integer | Element index for `at` / `set_at` |
| `std_array.value` | byte | Byte value for `fill` / `set_at` |

### Functions

#### `front`

Return the first element. Equivalent to C++ `arr.front()`.

| | |
|---|---|
| **Output** | R0 = value of first byte |
| **Clobbers** | R0 |

#### `back`

Return the last element. Equivalent to C++ `arr.back()`.

| | |
|---|---|
| **Output** | R0 = value of last byte |
| **Clobbers** | R0, R1 |

#### `data`

Return pointer to underlying buffer. Equivalent to C++ `arr.data()`.

| | |
|---|---|
| **Output** | R0 = base address |
| **Clobbers** | R0 |

#### `begin`

Return pointer to first element. Equivalent to C++ `arr.begin()`.

| | |
|---|---|
| **Output** | R0 = base address |
| **Clobbers** | R0 |

#### `end`

Return pointer one past the last element. Equivalent to C++ `arr.end()`.

| | |
|---|---|
| **Output** | R0 = base address + size |
| **Clobbers** | R0, R1 |

#### `empty`

Check whether the array has zero size. Equivalent to C++ `arr.empty()`.

| | |
|---|---|
| **Output** | R0 = 1 if empty, 0 otherwise |
| **Clobbers** | R0, R1 |

#### `size_of`

Return the element count. Equivalent to C++ `arr.size()`.

| | |
|---|---|
| **Output** | R0 = size |
| **Clobbers** | R0 |

#### `at`

Read element at a given index. Equivalent to C++ `arr[i]`.

| | |
|---|---|
| **Input** | `SET std_array.index, <i>` |
| **Output** | R0 = byte value at position *index* |
| **Clobbers** | R0, R1 |

#### `set_at`

Write a value to a given index. Equivalent to C++ `arr[i] = v`.

| | |
|---|---|
| **Input** | `SET std_array.index, <i>`, `SET std_array.value, <v>` |
| **Clobbers** | R0, R1, R2 |

#### `fill`

Fill every element with a constant byte. Equivalent to C++ `arr.fill(v)`.

| | |
|---|---|
| **Input** | `SET std_array.value, <v>` |
| **Clobbers** | R0, R1, R2, R3 |

### Example

```asm
@IMPORT std_io
@IMPORT std_array

    BUFFER my_arr, 5

    ; Initialise
    GET  R0, my_arr
    SET  std_array.ptr, R0
    SET  std_array.size, 5

    ; Fill with 'A' (65)
    LDI  R0, 65
    SET  std_array.value, R0
    CALL std_array.fill

    ; Read element 2
    LDI  R0, 2
    SET  std_array.index, R0
    CALL std_array.at
    ; R0 = 65
```

---

## std_vector — Dynamic Byte Vector

**Import:** `@IMPORT std_vector`
**Source:** [lib/std_vector.ua](../lib/std_vector.ua)

Dynamic-size byte vector modelled after C++ `std::vector<uint8_t>`. A vector is a `BUFFER` with a tracked logical size that can grow up to a fixed capacity. Works on **all** backends.

### Setup

```asm
@IMPORT std_vector

    BUFFER my_vec, 64
    GET  R0, my_vec
    SET  std_vector.ptr, R0
    SET  std_vector.capacity, 64
    CALL std_vector.clear           ; initialise size to 0
```

### Parameter Variables

| Variable | Type | Description |
|---|---|---|
| `std_vector.ptr` | address | Base address of the backing BUFFER |
| `std_vector.capacity` | integer | Maximum element count (= BUFFER size) |
| `std_vector.vec_size` | integer | Current logical size (managed internally) |
| `std_vector.index` | integer | Element index for `at` / `set_at` |
| `std_vector.value` | byte | Byte value for `push_back` / `set_at` |
| `std_vector.new_size` | integer | Target size for `resize` |

### Functions

#### `clear`

Reset vector to empty (`vec_size = 0`). Equivalent to C++ `vec.clear()`.

| | |
|---|---|
| **Clobbers** | R0 |

#### `push_back`

Append one byte to the end. Silently ignored if at capacity. Equivalent to C++ `vec.push_back(v)`.

| | |
|---|---|
| **Input** | `SET std_vector.value, <v>` |
| **Clobbers** | R0, R1, R2 |

#### `pop_back`

Remove and return the last byte. Equivalent to C++ `vec.pop_back()` (but also returns the value).

| | |
|---|---|
| **Output** | R0 = removed element (0 if vector was empty) |
| **Clobbers** | R0, R1, R2 |

#### `front`

Return the first element. Equivalent to C++ `vec.front()`.

| | |
|---|---|
| **Output** | R0 = first byte |
| **Clobbers** | R0 |

#### `back`

Return the last element. Equivalent to C++ `vec.back()`.

| | |
|---|---|
| **Output** | R0 = last byte |
| **Clobbers** | R0, R1 |

#### `data`

Return pointer to backing buffer. Equivalent to C++ `vec.data()`.

| | |
|---|---|
| **Output** | R0 = base address |
| **Clobbers** | R0 |

#### `begin`

Return pointer to first element. Equivalent to C++ `vec.begin()`.

| | |
|---|---|
| **Output** | R0 = base address |
| **Clobbers** | R0 |

#### `end`

Return pointer one past last element. Equivalent to C++ `vec.end()`.

| | |
|---|---|
| **Output** | R0 = ptr + vec\_size |
| **Clobbers** | R0, R1 |

#### `empty`

Check whether vector has zero elements. Equivalent to C++ `vec.empty()`.

| | |
|---|---|
| **Output** | R0 = 1 if empty, 0 otherwise |
| **Clobbers** | R0, R1 |

#### `size_of`

Return the current element count. Equivalent to C++ `vec.size()`.

| | |
|---|---|
| **Output** | R0 = vec\_size |
| **Clobbers** | R0 |

#### `capacity_of`

Return the maximum capacity. Equivalent to C++ `vec.capacity()`.

| | |
|---|---|
| **Output** | R0 = capacity |
| **Clobbers** | R0 |

#### `at`

Read element at index. Equivalent to C++ `vec[i]`.

| | |
|---|---|
| **Input** | `SET std_vector.index, <i>` |
| **Output** | R0 = byte at position *index* |
| **Clobbers** | R0, R1 |

#### `set_at`

Write a value at index. Equivalent to C++ `vec[i] = v`.

| | |
|---|---|
| **Input** | `SET std_vector.index, <i>`, `SET std_vector.value, <v>` |
| **Clobbers** | R0, R1, R2 |

#### `resize`

Change the logical size (clamped to capacity). New elements are zero-filled when growing. Equivalent to C++ `vec.resize(n)`.

| | |
|---|---|
| **Input** | `SET std_vector.new_size, <n>` |
| **Clobbers** | R0, R1, R2, R3 |

### Example

```asm
@IMPORT std_vector

    BUFFER my_vec, 64
    GET  R0, my_vec
    SET  std_vector.ptr, R0
    SET  std_vector.capacity, 64
    CALL std_vector.clear

    ; Push three bytes
    LDI  R0, 10
    SET  std_vector.value, R0
    CALL std_vector.push_back
    LDI  R0, 20
    SET  std_vector.value, R0
    CALL std_vector.push_back
    LDI  R0, 30
    SET  std_vector.value, R0
    CALL std_vector.push_back

    ; Size is now 3
    CALL std_vector.size_of       ; R0 = 3

    ; Pop the last element
    CALL std_vector.pop_back      ; R0 = 30
```

---

## std_iostream — File I/O Streams

**Import:** `@IMPORT std_iostream`
**Source:** [lib/std_iostream.ua](../lib/std_iostream.ua)

File stream I/O: open, read, write, and close files using OS system calls.

> **Supported architectures:** x86, x86\_32, arm, arm64, riscv.
> Not available on 8051/MCS-51 (no OS / file system).
> The library is guarded with `@ARCH_ONLY x86, x86_32, arm, arm64, riscv`.

### Parameter Variables

All functions communicate through shared variables set before each call:

| Variable | Type | Description |
|---|---|---|
| `std_iostream.iostream_fd` | integer | File descriptor (Linux) or HANDLE (Win32). Set automatically by `fopen`. |
| `std_iostream.iostream_path` | address | Pointer to a null-terminated file path string. |
| `std_iostream.iostream_mode` | integer | Open mode: `0` = read, `1` = write (create/truncate). |
| `std_iostream.iostream_buf` | address | Pointer to source (write) or destination (read) buffer. |
| `std_iostream.iostream_count` | integer | Number of bytes to read or write. |

### Open Modes

| Mode | Value | Linux flags | Win32 mapping |
|---|---|---|---|
| Read | `0` | `O_RDONLY` (0) | `GENERIC_READ` + `OPEN_EXISTING` |
| Write | `1` | `O_WRONLY\|O_CREAT\|O_TRUNC` (577), mode 0666 | `GENERIC_WRITE` + `CREATE_ALWAYS` |

### Functions

#### `fopen`

Open a file for reading or writing.

| | |
|---|---|
| **Input** | `SET std_iostream.iostream_path, R0` (path), `SET std_iostream.iostream_mode, 0\|1` |
| **Output** | R0 = file descriptor / handle (−1 on error). `iostream_fd` is set automatically. |
| **Clobbers** | R0, R2, R3, R6, R7 |

On ARM64 and RISC-V, `fopen` uses the `openat` syscall with `AT_FDCWD` (−100) as the directory file descriptor, so relative paths work correctly.

#### `fread`

Read up to N bytes from a file into a buffer.

| | |
|---|---|
| **Input** | `SET std_iostream.iostream_buf, R0` (destination), `SET std_iostream.iostream_count, <n>`. `iostream_fd` must be set (by `fopen`). |
| **Output** | R0 = number of bytes actually read |
| **Clobbers** | R0, R1, R2, R6, R7 |

#### `fwrite`

Write N bytes from a buffer or string to a file.

| | |
|---|---|
| **Input** | `SET std_iostream.iostream_buf, R0` (source), `SET std_iostream.iostream_count, <n>`. `iostream_fd` must be set. |
| **Output** | R0 = number of bytes actually written |
| **Clobbers** | R0, R1, R2, R6, R7 |

#### `fclose`

Close a previously opened file.

| | |
|---|---|
| **Input** | `iostream_fd` must be set. |
| **Output** | R0 = 0 on success (Linux), nonzero on success (Win32) |
| **Clobbers** | R0, R7 |

### Example — Write and Read Back

```asm
@IMPORT std_io
@IMPORT std_iostream

    BUFFER read_buf, 128

    ; --- Write to a file ---
    LDS  R0, "output.txt"
    SET  std_iostream.iostream_path, R0
    LDI  R0, 1
    SET  std_iostream.iostream_mode, R0     ; write mode
    CALL std_iostream.fopen

    LDS  R0, "Hello, file!\n"
    SET  std_iostream.iostream_buf, R0
    LDI  R0, 13
    SET  std_iostream.iostream_count, R0
    CALL std_iostream.fwrite

    CALL std_iostream.fclose

    ; --- Read it back ---
    LDS  R0, "output.txt"
    SET  std_iostream.iostream_path, R0
    LDI  R0, 0
    SET  std_iostream.iostream_mode, R0     ; read mode
    CALL std_iostream.fopen

    GET  R0, read_buf
    SET  std_iostream.iostream_buf, R0
    LDI  R0, 128
    SET  std_iostream.iostream_count, R0
    CALL std_iostream.fread                 ; R0 = bytes read

    GET  R0, read_buf
    CALL std_io.print                       ; prints "Hello, file!"

    CALL std_iostream.fclose
    HLT
```

### Syscall Numbers by Architecture

| Operation | x86-64 | x86-32 | ARM | ARM64 | RISC-V |
|---|---|---|---|---|---|
| open / openat | 2 | 5 | 5 | 56 | 56 |
| read | 0 | 3 | 3 | 63 | 63 |
| write | 1 | 4 | 4 | 64 | 64 |
| close | 3 | 6 | 6 | 57 | 57 |

On Win32, the SYS dispatcher translates dispatch codes `0`–`3` to `ReadFile`, `WriteFile`, `CreateFileA`, and `CloseHandle` via kernel32.dll.

---

## Platform Support Matrix

| Library | x86 | x86\_32 | ARM | ARM64 | RISC-V | 8051 |
|---|---|---|---|---|---|---|
| **std\_io** | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| **std\_string** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ ¹ |
| **std\_math** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ ² |
| **std\_arrays** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ ¹ |
| **std\_array** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ ¹ |
| **std\_vector** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ ¹ |
| **std\_iostream** | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |

¹ 8051: `LOADB`/`STOREB` use indirect addressing (`@R0`/`@R1`) — only internal RAM (0x00–0xFF) is addressable.
² 8051: `MUL` uses the 8×8 hardware multiplier; overflow wraps silently.

---

*See also: [Language Reference](language-reference.md) · [Beginner's Guide](beginners-guide.md) · [Compiler Usage](compiler-usage.md)*
