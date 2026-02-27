; test_jg_simple.ua — minimal JG test
; Expected: R0 = 10 (JG should trigger since 20 > 10)
    LDI  R0, 20
    LDI  R1, 10
    CMP  R0, R1
    JG   greater
    JMP  done
greater:
    MOV  R0, R1
done:
    HLT
