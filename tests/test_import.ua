; test_import.ua — test @IMPORT namespace feature
@IMPORT "math.ua"

    JMP  start

start:
    LDI  R0, 7
    CALL math.double_it
    ; R0 should now be 14
    HLT
