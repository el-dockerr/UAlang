; Minimal malloc tests (no realloc/calloc)
@IMPORT std_malloc
@IMPORT std_io

start:
    LDS  R0, "=== Minimal malloc tests ===\n"
    CALL std_io.print
    
    ; Test 1: Basic malloc
    LDS  R0, "[TEST 1] malloc(100)\n"
    CALL std_io.print
    LDI  R0, 100
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   fail
    LDS  R0, "  PASS\n"
    CALL std_io.print
    
    ; Test 2: Alignment
    LDS  R0, "[TEST 2] Alignment check\n"
    CALL std_io.print
    LDI  R0, 17
    CALL std_malloc.malloc
    MOV  R1, R0
    LDI  R2, 7
    AND  R1, R2
    LDI  R2, 0
    CMP  R1, R2
    JNZ  fail
    LDS  R0, "  PASS\n"
    CALL std_io.print
    
    ; Test 3: free (no-op)
    LDS  R0, "[TEST 3] free (no-op)\n"
    CALL std_io.print
    LDI  R0, 64
    CALL std_malloc.malloc
    CALL std_malloc.free
    LDS  R0, "  PASS\n"
    CALL std_io.print
    
    LDS  R0, "\n=== All tests passed! ===\n"
    CALL std_io.print
    HLT

fail:
    LDS  R0, "  FAIL\n"
    CALL std_io.print
    HLT
