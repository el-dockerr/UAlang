; test_set_imm.ua — test SET with immediate value
; Expected: R0 = 99 at the end

    VAR answer

    ; Set variable to immediate value
    SET  answer, 99

    ; Read it back
    GET  R0, answer

    HLT
