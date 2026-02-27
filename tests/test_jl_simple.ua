; test_jl_simple.ua — minimal JL test
; Expected: R0 = 10 (JL should trigger since 5 < 10)
    LDI  R0, 5
    LDI  R1, 10
    CMP  R0, R1
    JL   less
    JMP  done
less:
    MOV  R0, R1
done:
    HLT
