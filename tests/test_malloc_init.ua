; Test malloc initialization only
@IMPORT std_io

BUFFER malloc_heap_buffer, 1024

VAR malloc_heap_ptr
VAR malloc_heap_end
VAR malloc_initialized

start:
    LDS  R0, "Testing malloc init...\n"
    CALL std_io.print
    
    CALL _malloc_init_heap
    
    ; Check if initialized
    GET  R0, malloc_initialized
    LDI  R1, 1
    CMP  R0, R1
    JNZ  init_failed
    
    LDS  R0, "Init successful!\n"
    CALL std_io.print
    JMP  done
    
init_failed:
    LDS  R0, "Init failed!\n"
    CALL std_io.print
    
done:
    HLT

_malloc_init_heap:
    GET  R0, malloc_heap_buffer
    SET  malloc_heap_ptr, R0
    
    LDI  R1, 1024
    ADD  R0, R1
    SET  malloc_heap_end, R0
    
    LDI  R1, 1
    SET  malloc_initialized, R1
    
    RET
