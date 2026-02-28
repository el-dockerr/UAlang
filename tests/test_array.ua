; =============================================================================
;  test_array.ua — std_array library test
;
;  Exercises every std_array function: fill, front, back, at, set_at,
;  data, begin, end, empty, size_of.
;
;  The program fills a 10-byte array with 0xAA, overwrites index 0
;  with 0x11 and index 9 with 0x99, then verifies front, back, at,
;  empty, size_of, begin, end, and data.
;
;  If all checks pass   → R0 = 1  (success)
;  If any check fails   → R0 = 0  (failure) and HLT at the fail point
;
;  Build & run (JIT, x86-64):
;    uas tests/test_array.ua -arch x86 --run
;
;  Cross-compile (examples):
;    uas tests/test_array.ua -arch arm   -o test_array.bin
;    uas tests/test_array.ua -arch riscv -o test_array.bin
;    uas tests/test_array.ua -arch mcs51 -o test_array.bin
; =============================================================================

@IMPORT std_array

; ---- Data ----------------------------------------------------------------
    BUFFER my_arr, 10

    JMP main

; ---- Main ----------------------------------------------------------------
main:
    ; -- Initialise std_array parameters -----------------------------------
    GET  R0, my_arr
    SET  std_array.ptr, R0
    SET  std_array.size, 10

    ; -- Test 1: empty on a sized array should return 0 --------------------
    CALL std_array.empty
    CMP  R0, 0
    JZ   test2
    JMP  fail
test2:
    ; -- Test 2: size_of should return 10 ----------------------------------
    CALL std_array.size_of
    CMP  R0, 10
    JZ   test3
    JMP  fail
test3:
    ; -- Test 3: fill the array with 0xAA ---------------------------------
    SET  std_array.value, 0xAA
    CALL std_array.fill

    ; -- Test 4: front should return 0xAA ----------------------------------
    CALL std_array.front
    CMP  R0, 0xAA
    JZ   test5
    JMP  fail
test5:
    ; -- Test 5: back should return 0xAA -----------------------------------
    CALL std_array.back
    CMP  R0, 0xAA
    JZ   test6
    JMP  fail
test6:
    ; -- Test 6: set_at index 0 to 0x11 -----------------------------------
    SET  std_array.index, 0
    SET  std_array.value, 0x11
    CALL std_array.set_at

    ; -- Test 7: set_at index 9 to 0x99 -----------------------------------
    SET  std_array.index, 9
    SET  std_array.value, 0x99
    CALL std_array.set_at

    ; -- Test 8: front should now return 0x11 ------------------------------
    CALL std_array.front
    CMP  R0, 0x11
    JZ   test9
    JMP  fail
test9:
    ; -- Test 9: back should now return 0x99 -------------------------------
    CALL std_array.back
    CMP  R0, 0x99
    JZ   test10
    JMP  fail
test10:
    ; -- Test 10: at index 5 should still be 0xAA -------------------------
    SET  std_array.index, 5
    CALL std_array.at
    CMP  R0, 0xAA
    JZ   test11
    JMP  fail
test11:
    ; -- Test 11: at index 0 should be 0x11 -------------------------------
    SET  std_array.index, 0
    CALL std_array.at
    CMP  R0, 0x11
    JZ   test12
    JMP  fail
test12:
    ; -- Test 12: data returns same address as our buffer ------------------
    CALL std_array.data
    GET  R1, my_arr
    CMP  R0, R1
    JZ   test13
    JMP  fail
test13:
    ; -- Test 13: begin returns same as data -------------------------------
    CALL std_array.begin
    GET  R1, my_arr
    CMP  R0, R1
    JZ   test14
    JMP  fail
test14:
    ; -- Test 14: end = begin + size ---------------------------------------
    CALL std_array.end
    MOV  R6, R0              ; R6 = end
    CALL std_array.begin
    LDI  R1, 10
    ADD  R0, R1              ; R0 = begin + 10
    CMP  R0, R6
    JZ   pass
    JMP  fail

    ; -- All tests passed --------------------------------------------------
pass:
    LDI  R0, 1
    HLT

; ---- fail → R0=0, halt --------------------------------------------------
fail:
    LDI  R0, 0
    HLT
