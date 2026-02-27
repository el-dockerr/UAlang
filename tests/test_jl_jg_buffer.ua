; test_jl_jg_buffer.ua — Test JL, JG, and BUFFER instructions
; Expected result: R0 = 42

    JMP main

    VAR result, 0

; --- Test JL: clamp from below ---
; R0 = max(R0, 10) — if R0 < 10, set R0 = 10
clamp_low(result):
    GET  R0, result
    LDI  R1, 10
    CMP  R0, R1
    JL   set_low
    JMP  clamp_low_done
set_low:
    MOV  R0, R1
clamp_low_done:
    SET  result, R0
    RET

; --- Test JG: clamp from above ---
; R0 = min(R0, 100) — if R0 > 100, set R0 = 100
clamp_high(result):
    GET  R0, result
    LDI  R1, 100
    CMP  R0, R1
    JG   set_high
    JMP  clamp_high_done
set_high:
    MOV  R0, R1
clamp_high_done:
    SET  result, R0
    RET

main:
    ; Test 1: JL should trigger (5 < 10 → clamped to 10)
    SET  result, 5
    CALL clamp_low
    GET  R0, result
    ; R0 should be 10

    ; Test 2: JG should trigger (200 > 100 → clamped to 100)
    SET  result, 200
    CALL clamp_high
    GET  R0, result
    ; R0 should be 100

    ; Test 3: JL should NOT trigger (50 >= 10)
    SET  result, 50
    CALL clamp_low
    GET  R0, result
    ; R0 should still be 50

    ; Test 4: JG should NOT trigger (50 <= 100)
    CALL clamp_high
    GET  R0, result
    ; R0 should still be 50

    ; Test 5: BUFFER — write 42 to a buffer, read it back
    BUFFER test_buf, 8
    GET  R0, test_buf
    LDI  R1, 42
    STOREB R1, R0
    LOADB R0, R0
    ; R0 should be 42

    HLT
