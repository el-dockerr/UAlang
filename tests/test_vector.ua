; =============================================================================
;  test_vector.ua — std_vector library test
;
;  Exercises every std_vector function: clear, push_back, pop_back,
;  front, back, at, set_at, empty, size_of, capacity_of, begin, end,
;  data, resize.
;
;  If all checks pass   → R0 = 1  (success)
;  If any check fails   → R0 = 0  (failure) and HLT at the fail point
;
;  Build & run (JIT, x86-64):
;    uas tests/test_vector.ua -arch x86 --run
;
;  Cross-compile (examples):
;    uas tests/test_vector.ua -arch arm   -o test_vector.bin
;    uas tests/test_vector.ua -arch riscv -o test_vector.bin
;    uas tests/test_vector.ua -arch mcs51 -o test_vector.bin
; =============================================================================

@IMPORT std_vector

; ---- Data ----------------------------------------------------------------
    BUFFER my_vec, 32

    JMP main

; ---- Main ----------------------------------------------------------------
main:
    ; -- Initialise the vector ---------------------------------------------
    GET  R0, my_vec
    SET  std_vector.ptr, R0
    SET  std_vector.capacity, 32
    CALL std_vector.clear

    ; -- Test 1: empty after clear should return 1 -------------------------
    CALL std_vector.empty
    CMP  R0, 1
    JZ   test2
    JMP  fail
test2:
    ; -- Test 2: size_of after clear should return 0 -----------------------
    CALL std_vector.size_of
    CMP  R0, 0
    JZ   test3
    JMP  fail
test3:
    ; -- Test 3: capacity_of should return 32 ------------------------------
    CALL std_vector.capacity_of
    CMP  R0, 32
    JZ   test4
    JMP  fail
test4:
    ; -- Test 4: push_back three elements: 10, 20, 30 ---------------------
    SET  std_vector.value, 10
    CALL std_vector.push_back
    SET  std_vector.value, 20
    CALL std_vector.push_back
    SET  std_vector.value, 30
    CALL std_vector.push_back

    ; -- Test 5: size should now be 3 --------------------------------------
    CALL std_vector.size_of
    CMP  R0, 3
    JZ   test6
    JMP  fail
test6:
    ; -- Test 6: empty should return 0 -------------------------------------
    CALL std_vector.empty
    CMP  R0, 0
    JZ   test7
    JMP  fail
test7:
    ; -- Test 7: front should return 10 ------------------------------------
    CALL std_vector.front
    CMP  R0, 10
    JZ   test8
    JMP  fail
test8:
    ; -- Test 8: back should return 30 -------------------------------------
    CALL std_vector.back
    CMP  R0, 30
    JZ   test9
    JMP  fail
test9:
    ; -- Test 9: at index 1 should return 20 -------------------------------
    SET  std_vector.index, 1
    CALL std_vector.at
    CMP  R0, 20
    JZ   test10
    JMP  fail
test10:
    ; -- Test 10: set_at index 1 to 25 ------------------------------------
    SET  std_vector.index, 1
    SET  std_vector.value, 25
    CALL std_vector.set_at

    SET  std_vector.index, 1
    CALL std_vector.at
    CMP  R0, 25
    JZ   test11
    JMP  fail
test11:
    ; -- Test 11: pop_back should return 30 and reduce size to 2 -----------
    CALL std_vector.pop_back
    CMP  R0, 30
    JZ   test11b
    JMP  fail
test11b:
    CALL std_vector.size_of
    CMP  R0, 2
    JZ   test12
    JMP  fail
test12:
    ; -- Test 12: back after pop should return 25 --------------------------
    CALL std_vector.back
    CMP  R0, 25
    JZ   test13
    JMP  fail
test13:
    ; -- Test 13: data returns our buffer address --------------------------
    CALL std_vector.data
    GET  R1, my_vec
    CMP  R0, R1
    JZ   test14
    JMP  fail
test14:
    ; -- Test 14: begin returns same as data -------------------------------
    CALL std_vector.begin
    GET  R1, my_vec
    CMP  R0, R1
    JZ   test15
    JMP  fail
test15:
    ; -- Test 15: end = begin + size (size is 2) ---------------------------
    CALL std_vector.end
    MOV  R6, R0              ; R6 = end
    CALL std_vector.begin
    LDI  R1, 2
    ADD  R0, R1              ; R0 = begin + 2
    CMP  R0, R6
    JZ   test16
    JMP  fail
test16:
    ; -- Test 16: resize to 5 — should zero-fill new elements -------------
    SET  std_vector.new_size, 5
    CALL std_vector.resize

    CALL std_vector.size_of
    CMP  R0, 5
    JZ   test16b
    JMP  fail
test16b:
    ; new elements (index 2, 3, 4) should be 0
    SET  std_vector.index, 3
    CALL std_vector.at
    CMP  R0, 0
    JZ   test16c
    JMP  fail
test16c:
    ; old elements should survive resize
    SET  std_vector.index, 0
    CALL std_vector.at
    CMP  R0, 10
    JZ   test17
    JMP  fail
test17:
    ; -- Test 17: resize to 1 (shrink) ------------------------------------
    SET  std_vector.new_size, 1
    CALL std_vector.resize

    CALL std_vector.size_of
    CMP  R0, 1
    JZ   test17b
    JMP  fail
test17b:
    CALL std_vector.front
    CMP  R0, 10
    JZ   test18
    JMP  fail
test18:
    ; -- Test 18: clear → empty again --------------------------------------
    CALL std_vector.clear
    CALL std_vector.empty
    CMP  R0, 1
    JZ   test18b
    JMP  fail
test18b:
    CALL std_vector.size_of
    CMP  R0, 0
    JZ   test19
    JMP  fail
test19:
    ; -- Test 19: pop_back on empty returns 0 ------------------------------
    CALL std_vector.pop_back
    CMP  R0, 0
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
