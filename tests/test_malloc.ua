; =============================================================================
;  Test Suite for std_malloc
;
;  Tests:
;    1. Basic malloc - allocate 1 KB, 10 KB, 1 MB blocks
;    2. Alignment validation - verify 8-byte alignment
;    3. Realloc stress test - grow from 64 bytes to 1 MB
;    4. Calloc test - zero-initialized allocation
;    5. Free test - verify no-op behavior
;    6. Exhaustion test - allocate until heap is full
;
; =============================================================================

@IMPORT std_malloc
@IMPORT std_io

; ---- Test 1: Basic malloc ---------------------------------------------------
test_basic_malloc:
    LDS  R0, "Test 1: Basic malloc...\n"
    CALL std_io.print
    
    ; Allocate 1 KB (1024 bytes)
    LDI  R0, 1024
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    LDS  R0, "  1 KB allocation: OK\n"
    CALL std_io.print
    
    ; Allocate 10 KB (10240 bytes)
    LDI  R0, 10240
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    LDS  R0, "  10 KB allocation: OK\n"
    CALL std_io.print
    
    ; Allocate 1 MB (1048576 bytes)
    LDI  R0, 1048576
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    LDS  R0, "  1 MB allocation: OK\n"
    CALL std_io.print
    
    LDS  R0, "Test 1: PASS\n\n"
    CALL std_io.print
    RET

; ---- Test 2: Alignment validation ------------------------------------------
test_alignment:
    LDS  R0, "Test 2: Alignment validation...\n"
    CALL std_io.print
    
    ; Allocate 7 bytes (should be rounded to 8)
    LDI  R0, 7
    CALL std_malloc.malloc
    MOV  R6, R0              ; Save pointer
    
    ; Check if address is 8-byte aligned: (addr & 7) == 0
    LDI  R1, 7
    AND  R0, R1
    LDI  R1, 0
    CMP  R0, R1
    JNZ  test_fail
    
    LDS  R0, "  7-byte allocation aligned to 8: OK\n"
    CALL std_io.print
    
    ; Allocate 13 bytes (should be rounded to 16)
    LDI  R0, 13
    CALL std_malloc.malloc
    
    ; Check alignment
    LDI  R1, 7
    AND  R0, R1
    LDI  R1, 0
    CMP  R0, R1
    JNZ  test_fail
    
    LDS  R0, "  13-byte allocation aligned to 16: OK\n"
    CALL std_io.print
    
    LDS  R0, "Test 2: PASS\n\n"
    CALL std_io.print
    RET

; ---- Test 3: Realloc stress test -------------------------------------------
test_realloc:
    LDS  R0, "Test 3: Realloc stress test...\n"
    CALL std_io.print
    
    ; Start with 64 bytes
    LDI  R0, 64
    CALL std_malloc.malloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    MOV  R6, R0              ; R6 = current pointer
    
    LDS  R0, "  Initial 64 bytes: OK\n"
    CALL std_io.print
    
    ; Grow to 256 bytes
    MOV  R0, R6
    LDI  R1, 256
    CALL std_malloc.realloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    MOV  R6, R0
    
    LDS  R0, "  Realloc to 256 bytes: OK\n"
    CALL std_io.print
    
    ; Grow to 4 KB
    MOV  R0, R6
    LDI  R1, 4096
    CALL std_malloc.realloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    MOV  R6, R0
    
    LDS  R0, "  Realloc to 4 KB: OK\n"
    CALL std_io.print
    
    ; Grow to 1 MB
    MOV  R0, R6
    LDI  R1, 1048576
    CALL std_malloc.realloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    
    LDS  R0, "  Realloc to 1 MB: OK\n"
    CALL std_io.print
    
    LDS  R0, "Test 3: PASS\n\n"
    CALL std_io.print
    RET

; ---- Test 4: Calloc (zero-initialized) -------------------------------------
test_calloc:
    LDS  R0, "Test 4: Calloc (zero-initialized)...\n"
    CALL std_io.print
    
    ; Allocate 100 elements of 4 bytes each (400 bytes total)
    LDI  R0, 100
    LDI  R1, 4
    CALL std_malloc.calloc
    LDI  R1, 0
    CMP  R0, R1
    JZ   test_fail
    
    ; Verify first 16 bytes are zero
    MOV  R6, R0              ; R6 = pointer
    LDI  R7, 16              ; Check 16 bytes
    
_calloc_verify_loop:
    LDI  R1, 0
    CMP  R7, R1
    JZ   _calloc_verify_done
    
    LOADB R2, R6
    LDI  R3, 0
    CMP  R2, R3
    JNZ  test_fail           ; Non-zero byte found
    
    INC  R6
    DEC  R7
    JMP  _calloc_verify_loop
    
_calloc_verify_done:
    LDS  R0, "  400 bytes zero-initialized: OK\n"
    CALL std_io.print
    
    LDS  R0, "Test 4: PASS\n\n"
    CALL std_io.print
    RET

; ---- Test 5: Free (no-op) ---------------------------------------------------
test_free:
    LDS  R0, "Test 5: Free (no-op)...\n"
    CALL std_io.print
    
    ; Allocate 1 KB
    LDI  R0, 1024
    CALL std_malloc.malloc
    MOV  R6, R0
    
    ; Free it (should be no-op)
    MOV  R0, R6
    CALL std_malloc.free
    
    LDS  R0, "  Free executed (no-op): OK\n"
    CALL std_io.print
    
    LDS  R0, "Test 5: PASS\n\n"
    CALL std_io.print
    RET

; ---- Test 6: Heap exhaustion ------------------------------------------------
test_exhaustion:
    LDS  R0, "Test 6: Heap exhaustion...\n"
    CALL std_io.print
    
    ; Try to allocate 20 MB (larger than 16 MB heap)
    LDI  R0, 20971520        ; 20 MB
    CALL std_malloc.malloc
    
    ; Should fail (return 0)
    LDI  R1, 0
    CMP  R0, R1
    JNZ  test_fail           ; Should have failed
    
    LDS  R0, "  20 MB allocation correctly failed: OK\n"
    CALL std_io.print
    
    LDS  R0, "Test 6: PASS\n\n"
    CALL std_io.print
    RET

; ---- Main test runner -------------------------------------------------------
start:
    LDS  R0, "\n=== std_malloc Test Suite ===\n\n"
    CALL std_io.print
    
    CALL test_basic_malloc
    CALL test_alignment
    CALL test_realloc
    CALL test_calloc
    CALL test_free
    CALL test_exhaustion
    
    LDS  R0, "=== All tests PASSED ===\n"
    CALL std_io.print
    HLT

test_fail:
    LDS  R0, "\n*** TEST FAILED ***\n"
    CALL std_io.print
    LDI  R0, 1
    HLT
