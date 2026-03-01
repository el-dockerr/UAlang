; Test malloc function with very simple logic
@IMPORT std_io

BUFFER malloc_heap_buffer, 4096

VAR malloc_heap_ptr
VAR malloc_heap_end  
VAR malloc_initialized

start:
    LDS  R0, "Testing malloc...\n"
    CALL std_io.print
    
    ; Allocate 64 bytes
    LDI  R0, 64
    CALL malloc
    
    ; Check result
    LDI  R1, 0
    CMP  R0, R1
    JZ   alloc_failed
    
    LDS  R0, "Malloc succeeded!\n"
    CALL std_io.print
    JMP  done
    
alloc_failed:
    LDS  R0, "Malloc failed!\n"
    CALL std_io.print
    
done:
    HLT

malloc:
    MOV  R3, R0               ; Save size
    
    ; Check if initialized
    GET  R1, malloc_initialized
    LDI  R2, 0
    CMP  R1, R2
    JNZ  ready
    
    ; Init heap
    PUSH R3
    CALL _malloc_init_heap
    POP  R3
    
ready:
    ; Align size (skip for now, just use size as-is)
    MOV  R4, R3
    
    ; Get current pointer
    GET  R5, malloc_heap_ptr
    
    ; Calculate new pointer
    MOV  R6, R5
    ADD  R6, R4
    
    ; Check bounds
    GET  R7, malloc_heap_end
    CMP  R6, R7
    JG   oom
    
    ; Update pointer and return
    SET  malloc_heap_ptr, R6
    MOV  R0, R5
    RET
    
oom:
    LDI  R0, 0
    RET

_malloc_init_heap:
    GET  R0, malloc_heap_buffer
    SET  malloc_heap_ptr, R0
    
    LDI  R1, 4096
    ADD  R0, R1
    SET  malloc_heap_end, R0
    
    LDI  R1, 1
    SET  malloc_initialized, R1
    
    RET
