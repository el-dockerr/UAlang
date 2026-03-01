# std_malloc Memory Model Documentation

## Overview

The `std_malloc` library provides dynamic memory allocation for UA programs using a simple **bump allocator** strategy. This is the foundation for self-hosting the UA compiler in Version 27 "Amazing Grace".

## Architecture Support

| Architecture | System | Syscall Method | Implementation |
|--------------|--------|----------------|----------------|
| x86-64 | Linux | `mmap` (syscall 9) | ✅ Complete |
| x86-64 | Win32 | `VirtualAlloc` (SYS dispatch code 3) | ✅ Complete |
| x86-32 | Linux | `mmap2` (syscall 192) | ✅ Complete |
| ARM | Linux | `mmap2` (syscall 192) | ✅ Complete |
| ARM64 | Linux | `mmap` (syscall 222) | ✅ Complete |
| ARM64 | macOS | `mmap` (syscall 197) | ✅ Complete |
| RISC-V | Linux | `mmap` (syscall 222) | ✅ Complete |

## Memory Layout

### Heap Structure

```
┌──────────────────────────────────────────────┐
│         Heap (16 MB = 16,777,216 bytes)      │
├──────────────────────────────────────────────┤
│  malloc_heap_base   ───►│                    │ ◄─── Base address (from mmap/VirtualAlloc)
│                          │   Allocated        │
│  malloc_heap_current ──►│   Region           │ ◄─── Next free byte (bump pointer)
│                          │                    │
│                          │   Free             │
│                          │   Region           │
│                          │                    │
│  malloc_heap_limit   ───►│                    │ ◄─── base + HEAP_SIZE
└──────────────────────────────────────────────┘
```

### Global Variables

| Variable | Purpose | Initial Value |
|----------|---------|---------------|
| `malloc_heap_base` | Base address of heap | 0 (set on first malloc) |
| `malloc_heap_current` | Next free byte pointer | 0 (set to base on init) |
| `malloc_heap_limit` | End of heap (base + 16 MB) | 0 (set on init) |

## Allocation Strategy: Bump Allocator

The bump allocator is the simplest memory allocation strategy:

1. **Initialization (lazy):** On the first `malloc()` call, allocate a 16 MB heap using OS syscall
2. **Allocation:** Return current pointer, then advance it by the requested size (aligned)
3. **No deallocation:** `free()` is a no-op; memory is not reclaimed until program exit

### Advantages
- **Simple:** ~300 lines of UA code for all platforms
- **Fast:** O(1) allocation (just pointer arithmetic)
- **Predictable:** No fragmentation, deterministic behavior

### Disadvantages
- **No reclamation:** Memory is never freed during execution
- **Heap exhaustion:** Once 16 MB is used, all allocations fail
- **Not suitable for long-running programs** with dynamic allocation patterns

> **Note:** A full free-list allocator with coalescing is planned for Version 28 "Brilliant Babbage".

## API Functions

### `malloc(size)`

Allocate N bytes from heap.

**Input:**
- `R0` = size in bytes

**Output:**
- `R0` = pointer to allocated block (or 0 on failure)

**Behavior:**
1. If heap not initialized, call `_malloc_init_heap()`
2. Align requested size to 8-byte boundary: `size = (size + 7) & ~7`
3. Check if enough space: `current + size <= limit`
4. If yes: return current pointer, advance current by size
5. If no: return 0 (failure)

**Alignment:** All allocations are 8-byte aligned for optimal performance on 64-bit architectures.

**Example:**
```asm
@IMPORT std_malloc

    LDI  R0, 1024        ; Request 1 KB
    CALL std_malloc.malloc
    CMP  R0, 0
    JZ   allocation_failed
    ; R0 now points to 1 KB of memory
```

---

### `free(ptr)`

Release allocated memory (currently no-op).

**Input:**
- `R0` = pointer to block

**Output:**
- None

**Behavior:**
- Immediately returns (no-op)
- Future versions will track deallocations for validation

**Example:**
```asm
    GET  R0, my_buffer
    CALL std_malloc.free   ; No-op, but shows intent
```

---

### `realloc(ptr, new_size)`

Resize an existing allocation.

**Input:**
- `R0` = pointer to existing block (or 0)
- `R1` = new size in bytes

**Output:**
- `R0` = pointer to resized block (or 0 on failure)

**Behavior:**
1. If `ptr` is 0, equivalent to `malloc(new_size)`
2. Otherwise:
   - Allocate new block of `new_size`
   - Copy `new_size` bytes from old block to new block
   - Free old block (no-op currently)
   - Return new block pointer

**Warning:** This implementation copies the full `new_size` bytes, which may exceed the original allocation size. In practice, the developer must ensure `new_size >= old_size` or track the original size separately.

**Example:**
```asm
    LDI  R0, 64          ; Allocate 64 bytes
    CALL std_malloc.malloc
    MOV  R6, R0          ; Save pointer
    
    ; ... use memory ...
    
    MOV  R0, R6          ; Old pointer
    LDI  R1, 256         ; New size (grow to 256 bytes)
    CALL std_malloc.realloc
    ; R0 now points to 256-byte block with data copied
```

---

### `calloc(count, size)`

Allocate zero-initialized memory.

**Input:**
- `R0` = number of elements
- `R1` = size of each element in bytes

**Output:**
- `R0` = pointer to allocated block (or 0 on failure)

**Behavior:**
1. Calculate total size: `total = count * size`
2. Call `malloc(total)`
3. Zero-fill all bytes in allocated block
4. Return pointer

**Example:**
```asm
    LDI  R0, 100         ; 100 elements
    LDI  R1, 8           ; 8 bytes each
    CALL std_malloc.calloc
    ; R0 points to 800 bytes of zero-initialized memory
```

---

## Heap Initialization

Heap initialization is **lazy** — it occurs on the first `malloc()` call.

### Linux/macOS: `mmap` syscall

```c
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
```

**Parameters:**
- `addr` = 0 (let kernel choose address)
- `length` = 16777216 (16 MB)
- `prot` = 3 (`PROT_READ | PROT_WRITE`)
- `flags` = 34 (`MAP_PRIVATE | MAP_ANONYMOUS`)
- `fd` = -1 (anonymous mapping)
- `offset` = 0

**Return:** Pointer to mapped memory (or -1 on error)

### Windows: `VirtualAlloc`

```c
LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
```

Called via SYS dispatcher code 3:
- `R7` = size (16777216)
- `R0` = 3 (dispatch code)

**Return:** Pointer to allocated memory (or 0 on error)

---

## Alignment Rules

All allocations are aligned to **8-byte boundaries**:

```
requested_size = 7   → aligned_size = 8
requested_size = 9   → aligned_size = 16
requested_size = 100 → aligned_size = 104
```

**Calculation:**
```asm
LDI  R1, 7
ADD  R3, R1              ; size += 7
LDI  R1, -8              ; ~7 in two's complement
AND  R3, R1              ; size &= ~7
```

This ensures:
1. Optimal performance on 64-bit architectures (aligned memory accesses)
2. Compatibility with struct alignment requirements
3. No misaligned pointer issues

---

## Error Handling

### Allocation Failure

`malloc()`, `realloc()`, and `calloc()` return **0** on failure:

```asm
CALL std_malloc.malloc
LDI  R1, 0
CMP  R0, R1
JZ   handle_error        ; Allocation failed
```

**Failure conditions:**
- Heap not initialized and `mmap`/`VirtualAlloc` failed
- Insufficient space in heap (`current + size > limit`)
- Requested size is 0 (implementation-defined, currently proceeds)

### Heap Exhaustion

With a 16 MB heap, once ~16 MB of memory has been allocated, subsequent allocations will fail:

```asm
; Attempt to allocate 20 MB (exceeds 16 MB heap)
LDI  R0, 20971520
CALL std_malloc.malloc
; R0 = 0 (failure)
```

---

## Limitations & Future Work

### Current Limitations (v27)

1. **No memory reclamation:** `free()` is a no-op
2. **Fixed heap size:** 16 MB per process (sufficient for compiler use)
3. **No heap growth:** Cannot expand beyond initial allocation
4. **Single heap:** No support for multiple heaps or threads
5. **Naive `realloc()`:** Copies entire `new_size` bytes (wasteful for shrinking)

### Planned Improvements (v28+)

1. **Free-list allocator:**
   - Maintain linked list of free blocks
   - Coalesce adjacent free blocks
   - Best-fit or first-fit allocation strategy

2. **Metadata headers:**
   - Store allocation size before each block
   - Enable safe `realloc()` with size tracking
   - Support heap validation/debugging

3. **Heap statistics:**
   - Track total allocated, total freed, peak usage
   - Memory leak detection

4. **Multiple heaps:**
   - Separate heaps for different allocation sizes
   - Reduce fragmentation

---

## Performance Characteristics

| Operation | Time Complexity | Space Overhead |
|-----------|-----------------|----------------|
| `malloc(n)` | O(1) | 0-7 bytes (alignment padding) |
| `free(ptr)` | O(1) (no-op) | 0 bytes |
| `realloc(ptr, n)` | O(n) (copy) | 0-7 bytes (alignment) |
| `calloc(c, s)` | O(c*s) (zero-fill) | 0-7 bytes (alignment) |
| Heap init | O(1) (syscall) | 16 MB (one-time) |

**Memory overhead:** Minimal — no per-allocation metadata in bump allocator.

---

## Example: Complete Usage

```asm
@IMPORT std_malloc
@IMPORT std_io

start:
    ; Allocate 256 bytes for a buffer
    LDI  R0, 256
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   alloc_failed
    MOV  R6, R0              ; Save buffer pointer
    
    ; Write some data to buffer
    LDI  R1, 65              ; ASCII 'A'
    STOREB R1, R6
    
    ; Allocate another 1 KB block
    LDI  R0, 1024
    CALL std_malloc.malloc
    MOV  R7, R0
    
    ; Free both (no-op, but good practice)
    MOV  R0, R6
    CALL std_malloc.free
    MOV  R0, R7
    CALL std_malloc.free
    
    LDS  R0, "Allocations successful!\n"
    CALL std_io.print
    HLT

alloc_failed:
    LDS  R0, "Allocation failed!\n"
    CALL std_io.print
    LDI  R0, 1
    HLT
```

---

## Testing

See [`tests/test_malloc.ua`](../tests/test_malloc.ua) for comprehensive test suite covering:

1. Basic allocations (1 KB, 10 KB, 1 MB)
2. Alignment validation
3. Realloc stress test (64 bytes → 1 MB)
4. Calloc zero-initialization
5. Free no-op behavior
6. Heap exhaustion (20 MB allocation failure)

**Run tests:**
```bash
# Linux x86-64
./ua tests/test_malloc.ua -arch x86 -sys linux --run

# Windows x86-64
ua.exe tests/test_malloc.ua -arch x86 -sys win32 --run

# macOS ARM64
./ua tests/test_malloc.ua -arch arm64 -sys macos --run
```

Expected output:
```
=== std_malloc Test Suite ===

Test 1: Basic malloc...
  1 KB allocation: OK
  10 KB allocation: OK
  1 MB allocation: OK
Test 1: PASS

Test 2: Alignment validation...
  7-byte allocation aligned to 8: OK
  13-byte allocation aligned to 16: OK
Test 2: PASS

Test 3: Realloc stress test...
  Initial 64 bytes: OK
  Realloc to 256 bytes: OK
  Realloc to 4 KB: OK
  Realloc to 1 MB: OK
Test 3: PASS

Test 4: Calloc (zero-initialized)...
  400 bytes zero-initialized: OK
Test 4: PASS

Test 5: Free (no-op)...
  Free executed (no-op): OK
Test 5: PASS

Test 6: Heap exhaustion...
  20 MB allocation correctly failed: OK
Test 6: PASS

=== All tests PASSED ===
```

---

## Conclusion

The `std_malloc` library provides a **simple, fast, and portable** dynamic memory allocation system suitable for bootstrapping the UA compiler. While it lacks advanced features like memory reclamation, it is sufficient for the compiler's needs and will be enhanced in future versions.

**Status:** ✅ Task 1.1 Complete (March 1, 2026)
