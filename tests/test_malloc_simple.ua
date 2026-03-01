; Simple malloc test - just allocate and print success
@IMPORT std_malloc
@IMPORT std_io

start:
    LDS  R0, "Simple malloc test...\n"
    CALL std_io.print
    
    ; Try to allocate 1024 bytes
    LDI  R0, 1024
    CALL std_malloc.malloc
    
    ; Check if it succeeded
    LDI  R1, 0
    CMP  R0, R1
    JZ   failed
    
    LDS  R0, "SUCCESS: Allocated 1024 bytes\n"
    CALL std_io.print
    HLT

failed:
    LDS  R0, "FAILED: Could not allocate memory\n"
    CALL std_io.print
    LDI  R0, 1
    HLT
