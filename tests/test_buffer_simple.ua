; test_buffer_simple.ua — minimal BUFFER test
; Expected: R0 = 42
    BUFFER mybuf, 8
    GET  R0, mybuf
    LDI  R1, 42
    STOREB R1, R0
    LOADB R0, R0
    HLT
