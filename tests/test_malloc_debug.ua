; Test malloc with detailed debugging
@IMPORT std_io

BUFFER malloc_heap_buffer, 4096

VAR malloc_heap_ptr
VAR malloc_heap_end
VAR malloc_initialized

start:
    LDS  R0, "1. Starting\n"
    CALL std_io.print
    
    LDI  R0, 64
    LDS  R1, "2. Calling malloc\n"
    PUSH R0
    MOV  R0, R1
    CALL std_io.print
    POP  R0
    
    CALL malloc
    
    LDS  R0, "3. Malloc returned\n"
    CALL std_io.print
    
    HLT

malloc:
    LDS  R0, "  3a. In malloc\n"
    PUSH R3
    CALL std_io.print
    POP  R3
    
    MOV  R3, R0
    
    LDS  R0, "  3b. Checking init\n"
    PUSH R3
    CALL std_io.print
    POP  R3
    
    GET  R1, malloc_initialized
    LDI  R2, 0
    CMP  R1, R2
    JNZ  ready
    
    LDS  R0, "  3c. Calling init\n"
    PUSH R3
    CALL std_io.print
    POP  R3
    
    PUSH R3
    CALL _malloc_init_heap
    POP  R3
    
ready:
    LDS  R0, "  3d. Getting pointer\n"
    PUSH R3
    CALL std_io.print
    POP  R3
    
    GET  R5, malloc_heap_ptr
    
    LDS  R0, "  3e. Calculating new ptr\n"
    PUSH R3
    PUSH R5
    CALL std_io.print
    POP  R5
    POP  R3
    
    MOV  R6, R5
    ADD  R6, R3
    
    LDS  R0, "  3f. Checking bounds\n"
    PUSH R5
    PUSH R6
    CALL std_io.print
    POP  R6
    POP  R5
    
    GET  R7, malloc_heap_end
    CMP  R6, R7
    JG   oom
    
    LDS  R0, "  3g. Updating pointer\n"
    PUSH R5
    CALL std_io.print
    POP  R5
    
    SET  malloc_heap_ptr, R6
    MOV  R0, R5
    
    LDS  R1, "  3h. Returning\n"
    PUSH R0
    MOV  R0, R1
    CALL std_io.print
    POP  R0
    
    RET
    
oom:
    LDS  R0, "  3x. OOM\n"
    CALL std_io.print
    LDI  R0, 0
    RET

_malloc_init_heap:
    LDS  R0, "    Init: start\n"
    CALL std_io.print
    
    GET  R0, malloc_heap_buffer
    SET  malloc_heap_ptr, R0
    
    LDS  R0, "    Init: set ptr\n"
    CALL std_io.print
    
    GET  R0, malloc_heap_buffer
    LDI  R1, 4096
    ADD  R0, R1
    SET  malloc_heap_end, R0
    
    LDS  R0, "    Init: set end\n"
    CALL std_io.print
    
    LDI  R1, 1
    SET  malloc_initialized, R1
    
    LDS  R0, "    Init: done\n"
    CALL std_io.print
    
    RET
