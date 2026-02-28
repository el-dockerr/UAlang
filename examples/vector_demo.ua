; =============================================================================
;  vector_demo.ua — std_vector demonstration with console output
;
;  Shows how to use the std_vector library to build a dynamic byte
;  collection: push, pop, resize, iterate, and query state.
;  Results are printed to the console via std_io and std_string.
;
;  Compatible with: x86, x86_32, arm, arm64, riscv
;  (Requires a hosted OS for std_io — not suitable for bare-metal 8051.)
;
;  Build & run:
;    uas examples/vector_demo.ua -arch x86   -sys linux  --run
;    uas examples/vector_demo.ua -arch x86   -sys win32  --run
;    uas examples/vector_demo.ua -arch arm   -sys linux  -o vector_demo.bin
;    uas examples/vector_demo.ua -arch arm64 -sys linux  -o vector_demo.bin
;    uas examples/vector_demo.ua -arch riscv -sys linux  -o vector_demo.bin
; =============================================================================

@ARCH_ONLY x86, x86_32, arm, arm64, riscv
@IMPORT std_io
@IMPORT std_string
@IMPORT std_vector

; ---- Data ----------------------------------------------------------------
    BUFFER my_vec, 64        ; backing store (capacity = 64 bytes)
    BUFFER print_buf, 24     ; scratch buffer for to_string output

; ---- Entry ---------------------------------------------------------------
    JMP main

; --------------------------------------------------------------------------
;  print_num — helper: print the integer in R0, followed by a newline
;  Clobbers: R0-R7 (via std_string/std_io)
; --------------------------------------------------------------------------
print_num:
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print
    RET

; --------------------------------------------------------------------------
;  print_vec — print all vector elements as "[a, b, c, ...]\n"
;  Clobbers: R0-R7
; --------------------------------------------------------------------------
print_vec:
    LDS  R0, "["
    CALL std_io.print

    CALL std_vector.size_of      ; R0 = current size
    PUSH R0                      ; save size on stack
    LDI  R6, 0                   ; R6 = index

print_vec_loop:
    POP  R7                      ; R7 = size
    PUSH R7                      ; keep it on stack
    CMP  R6, R7
    JZ   print_vec_end

    SET  std_vector.index, R6
    CALL std_vector.at           ; R0 = vec[index]
    PUSH R6
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    POP  R6

    ; Separator: comma unless last
    INC  R6
    POP  R7
    PUSH R7
    CMP  R6, R7
    JZ   print_vec_loop          ; skip comma on last iteration
    PUSH R6
    LDS  R0, ", "
    CALL std_io.print
    POP  R6
    JMP  print_vec_loop

print_vec_end:
    POP  R7                      ; clean stack
    LDS  R0, "]\n"
    CALL std_io.print
    RET

; ---- Main ----------------------------------------------------------------
main:
    LDS  R0, "=== std_vector Demo ===\n\n"
    CALL std_io.print

    ; --- Initialise the vector ---
    GET  R0, my_vec
    SET  std_vector.ptr, R0
    SET  std_vector.capacity, 64
    CALL std_vector.clear

    ; === 1. Show capacity ===
    LDS  R0, "Capacity    : "
    CALL std_io.print
    CALL std_vector.capacity_of  ; R0 = 64
    CALL print_num

    ; === 2. Vector starts empty ===
    LDS  R0, "Size (init) : "
    CALL std_io.print
    CALL std_vector.size_of      ; R0 = 0
    CALL print_num

    LDS  R0, "Empty?      : "
    CALL std_io.print
    CALL std_vector.empty        ; R0 = 1
    CALL print_num

    ; === 3. push_back five values: 10, 20, 30, 40, 50 ===
    LDS  R0, "\nPushing 10, 20, 30, 40, 50 ...\n"
    CALL std_io.print

    SET  std_vector.value, 10
    CALL std_vector.push_back
    SET  std_vector.value, 20
    CALL std_vector.push_back
    SET  std_vector.value, 30
    CALL std_vector.push_back
    SET  std_vector.value, 40
    CALL std_vector.push_back
    SET  std_vector.value, 50
    CALL std_vector.push_back

    LDS  R0, "Vector      : "
    CALL std_io.print
    CALL print_vec

    LDS  R0, "Size        : "
    CALL std_io.print
    CALL std_vector.size_of
    CALL print_num

    ; === 4. front / back ===
    LDS  R0, "front()     : "
    CALL std_io.print
    CALL std_vector.front        ; R0 = 10
    CALL print_num

    LDS  R0, "back()      : "
    CALL std_io.print
    CALL std_vector.back         ; R0 = 50
    CALL print_num

    ; === 5. at(2) ===
    LDS  R0, "at(2)       : "
    CALL std_io.print
    SET  std_vector.index, 2
    CALL std_vector.at           ; R0 = 30
    CALL print_num

    ; === 6. set_at(2, 99) ===
    LDS  R0, "\nset_at(2, 99)\n"
    CALL std_io.print
    SET  std_vector.index, 2
    SET  std_vector.value, 99
    CALL std_vector.set_at

    LDS  R0, "Vector      : "
    CALL std_io.print
    CALL print_vec

    ; === 7. pop_back ===
    LDS  R0, "\npop_back()  : "
    CALL std_io.print
    CALL std_vector.pop_back     ; R0 = 50
    CALL print_num

    LDS  R0, "Vector      : "
    CALL std_io.print
    CALL print_vec

    ; === 8. resize (grow to 7 — zero fills new slots) ===
    LDS  R0, "\nresize(7) — grow with zero-fill\n"
    CALL std_io.print
    SET  std_vector.new_size, 7
    CALL std_vector.resize

    LDS  R0, "Vector      : "
    CALL std_io.print
    CALL print_vec

    LDS  R0, "Size        : "
    CALL std_io.print
    CALL std_vector.size_of
    CALL print_num

    ; === 9. resize (shrink to 3) ===
    LDS  R0, "\nresize(3) — shrink\n"
    CALL std_io.print
    SET  std_vector.new_size, 3
    CALL std_vector.resize

    LDS  R0, "Vector      : "
    CALL std_io.print
    CALL print_vec

    ; === 10. clear ===
    LDS  R0, "\nclear()\n"
    CALL std_io.print
    CALL std_vector.clear

    LDS  R0, "Size        : "
    CALL std_io.print
    CALL std_vector.size_of      ; R0 = 0
    CALL print_num

    LDS  R0, "Empty?      : "
    CALL std_io.print
    CALL std_vector.empty        ; R0 = 1
    CALL print_num

    ; === Done ===
    LDS  R0, "\nDone.\n"
    CALL std_io.print
    HLT
