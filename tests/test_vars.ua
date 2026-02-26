; test_vars.ua — test VAR/SET/GET on all architectures
; Expected: R0 = 42 at the end

    VAR counter
    VAR result, 10

    ; Store 32 into counter
    LDI  R1, 32
    SET  counter, R1

    ; Load counter into R0
    GET  R0, counter

    ; Load result into R2
    GET  R2, result

    ; R0 = counter + result = 32 + 10 = 42
    ADD  R0, R2

    ; Store final answer back
    SET  result, R0

    ; Verify: GET it back
    GET  R0, result

    HLT
