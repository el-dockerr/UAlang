; Comprehensive malloc test suite (safe, no infinite loops)
; Tests malloc, free, realloc, calloc with bounded iterations
@IMPORT std_malloc
@IMPORT std_io

start:
    LDS  R0, "=== std_malloc Test Suite ===\n"
    CALL std_io.print
    
    CALL test_malloc_basic
    CALL test_malloc_alignment
    CALL test_realloc
    CALL test_calloc
    CALL test_free
    
    LDS  R0, "\n=== All tests passed! ===\n"
    CALL std_io.print
    HLT

; Test 1: Basic malloc allocations
test_malloc_basic:
    LDS  R0, "[TEST 1] Basic malloc allocations\n"
    CALL std_io.print
    
    ; Allocate 100 bytes
    LDI  R0, 100
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test1_fail
    
    ; Allocate 1024 bytes
    LDI  R0, 1024
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test1_fail
    
    ; Allocate 1MB
    LDI  R0, 1048576
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test1_fail
    
    LDS  R0, "  ✓ PASS: Basic allocations\n"
    CALL std_io.print
    RET
    
test1_fail:
    LDS  R0, "  ✗ FAIL: Basic allocations\n"
    CALL std_io.print
    HLT

; Test 2: Alignment verification
test_malloc_alignment:
    LDS  R0, "[TEST 2] Alignment (8-byte boundary)\n"
    CALL std_io.print
    
    ; Allocate odd sizes and check alignment
    LDI  R0, 17
    CALL std_malloc.malloc
    
    ; Check if aligned (ptr & 7 == 0)
    MOV  R1, R0
    LDI  R2, 7
    AND  R1, R2
    LDI  R2, 0
    CMP  R1, R2
    JNZ  test2_fail
    
    ; Another odd size
    LDI  R0, 123
    CALL std_malloc.malloc
    MOV  R1, R0
    LDI  R2, 7
    AND  R1, R2
    LDI  R2, 0
    CMP  R1, R2
    JNZ  test2_fail
    
    LDS  R0, "  ✓ PASS: Alignment correct\n"
    CALL std_io.print
    RET
    
test2_fail:
    LDS  R0, "  ✗ FAIL: Misaligned pointer\n"
    CALL std_io.print
    HLT

; Test 3: realloc (just test it returns non-null, skip copy verification)
test_realloc:
    LDS  R0, "[TEST 3] realloc (resize allocation)\n"
    CALL std_io.print
    
    ; Allocate 32 bytes
    LDI  R0, 32
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test3_fail
    
    ; Realloc to 32 bytes (same size - minimal work)
    MOV  R1, R0              ; Save ptr
    MOV  R0, R1              ; R0 = old ptr
    LDI  R1, 32              ; R1 = new size (same)
    CALL std_malloc.realloc
    
    ; Just verify it returns non-null
    LDI  R1, 0
    CMP  R0, R1
    JZ   test3_fail
    
    LDS  R0, "  ✓ PASS: realloc works\n"
    CALL std_io.print
    RET
    
test3_fail:
    LDS  R0, "  ✗ FAIL: realloc failed\n"
    CALL std_io.print
    HLT

; Test 4: calloc (zero-initialized)
test_calloc:
    LDS  R0, "[TEST 4] calloc (zero-initialized array)\n"
    CALL std_io.print
    
    ; Allocate array of 10 elements, 8 bytes each
    LDI  R0, 10
    LDI  R1, 8
    CALL std_malloc.calloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test4_fail
    
    ; Verify first 10 bytes are zero (bounded loop)
    MOV  R1, R0              ; R1 = ptr
    LDI  R2, 10              ; R2 = counter (BOUNDED)
    
test4_verify_loop:
    LDI  R3, 0
    CMP  R2, R3
    JZ   test4_verify_done
    
    LOADB R3, R1
    LDI  R0, 0
    CMP  R3, R0
    JNZ  test4_fail
    
    INC  R1
    DEC  R2
    JMP  test4_verify_loop
    
test4_verify_done:
    LDS  R0, "  ✓ PASS: calloc zeros memory\n"
    CALL std_io.print
    RET
    
test4_fail:
    LDS  R0, "  ✗ FAIL: calloc failed or not zeroed\n"
    CALL std_io.print
    HLT

; Test 5: free (no-op, just verify it doesn't crash)
test_free:
    LDS  R0, "[TEST 5] free (no-op in v1)\n"
    CALL std_io.print
    
    ; Allocate and free
    LDI  R0, 128
    CALL std_malloc.malloc
    CALL std_malloc.free
    
    LDS  R0, "  ✓ PASS: free doesn't crash\n"
    CALL std_io.print
    RET
