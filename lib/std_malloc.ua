; std_malloc.ua - Dynamic Memory Allocator for UA
; Version 1.0 - Bump Allocator (Static Buffer, R0-R3 only for x86-64)
; Part of Phase 1: Foundation (Task 1.1)
;
; IMPORTANT: Only uses R0-R3 for x86-64 compatibility (R4=RSP, R5=RBP cannot be used)
;
; API Functions:
;   malloc(size)           → Allocate 'size' bytes, return pointer or 0 on failure
;   free(ptr)              → Free memory (no-op in bump allocator v1)
;   realloc(ptr, new_size) → Resize allocation (naive copy implementation)
;   calloc(count, size)    → Allocate zero-initialized array
;
; Memory Model:
;   - 16 MB static heap buffer
;   - 8-byte alignment for all allocations
;   - No reclamation (free is no-op)

; ============================================================================
; STATIC HEAP BUFFER (16 MB)
; ============================================================================

BUFFER malloc_heap_buffer, 16777216

; ============================================================================
; VARIABLES
; ============================================================================

VAR malloc_heap_ptr      ; Current allocation pointer
VAR malloc_heap_end      ; End of heap
VAR malloc_initialized   ; 1 if initialized

; ============================================================================
; malloc(size) → ptr
; ============================================================================
; Parameters: R0 = size
; Returns: R0 = pointer or 0

malloc:
    ; R0 = size (preserve it across init check)
    GET  R1, malloc_initialized
    LDI  R2, 0
    CMP  R1, R2
    JNZ  _malloc_ready
    
    ; Need to init - save R0
    PUSH R0
    CALL _malloc_init_heap
    POP  R0
    
_malloc_ready:
    ; R0 = requested size
    ; Align: (size + 7) & ~7
    LDI  R1, 7
    ADD  R0, R1          ; size + 7
    NOT  R1              ; ~7
    AND  R0, R1          ; aligned size now in R0
    
    ; R0 = aligned_size
    ; Get heap_ptr into R1
    GET  R1, malloc_heap_ptr
    
    ; Calculate new_ptr = heap_ptr + aligned_size
    ; R2 = new_ptr
    MOV  R2, R1
    ADD  R2, R0
    
    ; Check bounds: new_ptr <= heap_end
    GET  R3, malloc_heap_end
    CMP  R2, R3
    JG   _malloc_oom
    
    ; Success: update heap_ptr, return old ptr
    SET  malloc_heap_ptr, R2
    MOV  R0, R1          ; Return old heap_ptr
    RET
    
_malloc_oom:
    LDI  R0, 0
    RET

; ============================================================================
; free(ptr)
; ============================================================================
; No-op in v1
; Parameters: R0 = pointer

free:
    RET

; ============================================================================
; realloc(ptr, new_size) → new_ptr
; ============================================================================
; Parameters: R0 = old pointer, R1 = new size
; Returns: R0 = new pointer or 0 on failure

realloc:
    ; Save parameters in memory (only have R0-R3 available)
    PUSH R0              ; Save old_ptr
    PUSH R1              ; Save new_size
    
    ; Allocate new block: malloc(new_size)
    MOV  R0, R1
    CALL malloc
    
    ; R0 = new_ptr (or 0 if failed)
    LDI  R1, 0
    CMP  R0, R1
    JZ   _realloc_failed
    
    ; Copy data: R0=new_ptr (save it), top of stack=new_size, next=old_ptr
    PUSH R0              ; Save new_ptr
    
    ; Stack: [new_ptr] [new_size] [old_ptr]
    ; Pop all three
    POP  R0              ; R0 = new_ptr
    POP  R1              ; R1 = new_size (bytes to copy)
    POP  R2              ; R2 = old_ptr
    
    ; Now copy: src=R2 (old), dst=R0 (new), count=R1
    ; Need to preserve new_ptr for return
    PUSH R0              ; Save new_ptr for return
    
_realloc_copy_loop:
    LDI  R3, 0
    CMP  R1, R3
    JZ   _realloc_done
    
    ; Copy one byte: *dst++ = *src++
    LOADB R3, R2
    STOREB R0, R3
    INC  R0
    INC  R2
    DEC  R1
    JMP  _realloc_copy_loop
    
_realloc_done:
    POP  R0              ; Restore new_ptr
    RET
    
_realloc_failed:
    POP  R1              ; Clean stack (new_size)
    POP  R1              ; Clean stack (old_ptr)
    LDI  R0, 0
    RET

; ============================================================================
; calloc(count, size) → ptr
; ============================================================================
; Parameters: R0 = count, R1 = size
; Returns: R0 = pointer or 0

calloc:
    ; Calculate total = count * size (R0 * R1)
    ; Only have R0-R3, need to preserve R0,R1
    PUSH R0              ; Save count
    PUSH R1              ; Save size
    
    ; Multiply using repeated addition
    LDI  R2, 0           ; R2 = result
    MOV  R3, R0          ; R3 = count (loop counter)
    MOV  R0, R1          ; R0 = size (value to add)
    
_calloc_mul:
    LDI  R1, 0
    CMP  R3, R1
    JZ   _calloc_mul_done
    ADD  R2, R0
    DEC  R3
    JMP  _calloc_mul
    
_calloc_mul_done:
    ; R2 = total size
    MOV  R0, R2
    CALL malloc
    
    ; R0 = ptr or 0
    LDI  R1, 0
    CMP  R0, R1
    JZ   _calloc_failed
    
    ; Zero-initialize: ptr in R0, size in R2
    MOV  R1, R0          ; R1 = ptr to fill
    MOV  R2, R2          ; R2 = bytes remaining (already set)
    LDI  R3, 0           ; R3 = zero byte
    
    PUSH R0              ; Save ptr for return
    
_calloc_zero:
    LDI  R0, 0
    CMP  R2, R0
    JZ   _calloc_zero_done
    
    MOV  R0, R1          ; R0 = current ptr
    STOREB R0, R3
    INC  R1
    DEC  R2
    JMP  _calloc_zero
    
_calloc_zero_done:
    POP  R0              ; Restore ptr
    POP  R1              ; Clean stack (size)
    POP  R1              ; Clean stack (count)
    RET
    
_calloc_failed:
    POP  R1              ; Clean stack (size)
    POP  R1              ; Clean stack (count)
    LDI  R0, 0
    RET

; ============================================================================
; _malloc_init_heap (internal)
; ============================================================================
; Initializes heap pointers

_malloc_init_heap:
    ; Get buffer base address
    GET  R0, malloc_heap_buffer
    
    ; Align buffer base to 8-byte boundary: (addr + 7) & ~7
    LDI  R1, 7
    ADD  R0, R1
    NOT  R1              ; ~7
    AND  R0, R1          ; R0 now aligned
    
    SET  malloc_heap_ptr, R0
    
    ; Calculate end = base + 16MB
    LDI  R1, 16777216
    ADD  R0, R1
    SET  malloc_heap_end, R0
    
    ; Mark as initialized
    LDI  R0, 1
    SET  malloc_initialized, R0
    
    RET
