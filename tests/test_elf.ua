; test_elf.ua — compute (10+5)*3-2 = 43, return in R0
    LDI  R0, 10
    LDI  R1, 5
    ADD  R0, R1       ; R0 = 15
    LDI  R1, 3
    MUL  R0, R1       ; R0 = 45
    LDI  R1, 2
    SUB  R0, R1       ; R0 = 43
    HLT
