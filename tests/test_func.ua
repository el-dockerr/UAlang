; test_func.ua — test function definitions with labels
; Expected: R0 = 25 at the end

    JMP  main

add_five(x):
    GET  R0, x
    LDI  R1, 5
    ADD  R0, R1
    RET

main:
    VAR x, 20
    CALL add_five
    ; R0 should be 25
    HLT
