; Final verification test for std_malloc v1.0
; Tests: malloc(), free(), alignment
; Note: realloc() and calloc() are implemented but need additional testing
@IMPORT std_malloc
@IMPORT std_io

start:
    LDS  R0, "std_malloc v1.0 - Final Verification\n"
    CALL std_io.print
    LDS  R0, "====================================\n\n"
    CALL std_io.print
    
    ; Test 1: malloc() with various sizes
    LDS  R0, "[1] malloc() - Small (64 bytes)\n"
    CALL std_io.print
    LDI  R0, 64
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   fail
    LDS  R0, "    PASS\n"
    CALL std_io.print
    
    LDS  R0, "[2] malloc() - Medium (4096 bytes)\n"
    CALL std_io.print
    LDI  R0, 4096
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   fail
    LDS  R0, "    PASS\n"
    CALL std_io.print
    
    LDS  R0, "[3] malloc() - Large (1 MB)\n"
    CALL std_io.print
    LDI  R0, 1048576
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   fail
    LDS  R0, "    PASS\n"
    CALL std_io.print
    
    ; Test 2: 8-byte alignment
    LDS  R0, "[4] Alignment - Odd size (17 bytes)\n"
    CALL std_io.print
    LDI  R0, 17
    CALL std_malloc.malloc
    MOV  R1, R0
    LDI  R2, 7
    AND  R1, R2
    LDI  R2, 0
    CMP  R1, R2
    JNZ  fail
    LDS  R0, "    PASS (8-byte aligned)\n"
    CALL std_io.print
    
    LDS  R0, "[5] Alignment - Odd size (123 bytes)\n"
    CALL std_io.print
    LDI  R0, 123
    CALL std_malloc.malloc
    MOV  R1, R0
    LDI  R2, 7
    AND  R1, R2
    LDI  R2, 0
    CMP  R1, R2
    JNZ  fail
    LDS  R0, "    PASS (8-byte aligned)\n"
    CALL std_io.print
    
    ; Test 3: free() (no-op, just verify no crash)
    LDS  R0, "[6] free() - No-op verification\n"
    CALL std_io.print
    LDI  R0, 256
    CALL std_malloc.malloc
    CALL std_malloc.free
    LDS  R0, "    PASS (no crash)\n"
    CALL std_io.print
    
    ; Success
    LDS  R0, "\n====================================\n"
    CALL std_io.print
    LDS  R0, "RESULT: All core tests PASSED\n"
    CALL std_io.print
    LDS  R0, "malloc() and free() verified OK\n"
    CALL std_io.print
    HLT

fail:
    LDS  R0, "    FAIL\n\n"
    CALL std_io.print
    LDS  R0, "RESULT: Test FAILED\n"
    CALL std_io.print
    LDI  R0, 1
    HLT
