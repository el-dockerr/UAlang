; math.ua — a small math helper library

    VAR  a
    VAR  b

add_values(a, b):
    GET  R0, a
    GET  R1, b
    ADD  R0, R1
    RET

double_it:
    ; doubles R0
    ADD  R0, R0
    RET
