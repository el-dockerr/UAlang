; =============================================================================
;  array_demo.ua — std_array demonstration with console output
;
;  Shows how to use the std_array library to manage a fixed-size byte
;  array: fill, read, overwrite, and query size/empty status.
;  Results are printed to the console via std_io and std_string.
;
;  Compatible with: x86, x86_32, arm, arm64, riscv
;  (Requires a hosted OS for std_io — not suitable for bare-metal 8051.)
;
;  Build & run:
;    uas examples/array_demo.ua -arch x86   -sys linux  --run
;    uas examples/array_demo.ua -arch x86   -sys win32  --run
;    uas examples/array_demo.ua -arch arm   -sys linux  -o array_demo.bin
;    uas examples/array_demo.ua -arch arm64 -sys linux  -o array_demo.bin
;    uas examples/array_demo.ua -arch riscv -sys linux  -o array_demo.bin
; =============================================================================

@ARCH_ONLY x86, x86_32, arm, arm64, riscv
@IMPORT std_io
@IMPORT std_string
@IMPORT std_array

; ---- Data ----------------------------------------------------------------
    BUFFER my_arr, 8         ; our 8-byte array
    BUFFER print_buf, 24     ; scratch buffer for to_string output

; ---- Entry ---------------------------------------------------------------
    JMP main

main:
    ; === Initialise ===
    LDS  R0, "=== std_array Demo ===\n"
    CALL std_io.print

    GET  R0, my_arr
    SET  std_array.ptr, R0
    SET  std_array.size, 8

    ; === 1. Show size ===
    LDS  R0, "Array size  : "
    CALL std_io.print
    CALL std_array.size_of       ; R0 = 8
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print

    ; === 2. Show empty status (should be 0 = not empty) ===
    LDS  R0, "Is empty?   : "
    CALL std_io.print
    CALL std_array.empty         ; R0 = 0
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print

    ; === 3. Fill with 42 ===
    LDS  R0, "Filling array with 42 ...\n"
    CALL std_io.print

    SET  std_array.value, 42
    CALL std_array.fill

    ; === 4. Print front and back ===
    LDS  R0, "front()     : "
    CALL std_io.print
    CALL std_array.front         ; R0 = 42
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print

    LDS  R0, "back()      : "
    CALL std_io.print
    CALL std_array.back          ; R0 = 42
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print

    ; === 5. Overwrite index 0 = 10, index 7 = 99 ===
    LDS  R0, "set_at(0, 10) and set_at(7, 99)\n"
    CALL std_io.print

    SET  std_array.index, 0
    SET  std_array.value, 10
    CALL std_array.set_at

    SET  std_array.index, 7
    SET  std_array.value, 99
    CALL std_array.set_at

    ; === 6. Read back modified elements ===
    LDS  R0, "front()     : "
    CALL std_io.print
    CALL std_array.front         ; R0 = 10
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print

    LDS  R0, "back()      : "
    CALL std_io.print
    CALL std_array.back          ; R0 = 99
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print

    ; === 7. Read middle element (index 4 should still be 42) ===
    LDS  R0, "at(4)       : "
    CALL std_io.print
    SET  std_array.index, 4
    CALL std_array.at            ; R0 = 42
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    LDS  R0, "\n"
    CALL std_io.print

    ; === 8. Print all elements ===
    LDS  R0, "Full array  : ["
    CALL std_io.print
    ; Loop through indices 0..7
    LDI  R6, 0               ; R6 = index counter
print_loop:
    LDI  R7, 8
    CMP  R6, R7
    JZ   print_done

    SET  std_array.index, R6
    CALL std_array.at            ; R0 = arr[index]
    PUSH R6                      ; save counter (CALL clobbers)
    GET  R1, print_buf
    CALL std_string.to_string
    GET  R0, print_buf
    CALL std_io.print
    POP  R6                      ; restore counter

    ; Print separator (comma+space) unless last element
    INC  R6
    LDI  R7, 8
    CMP  R6, R7
    JZ   print_done
    PUSH R6
    LDS  R0, ", "
    CALL std_io.print
    POP  R6
    JMP  print_loop

print_done:
    LDS  R0, "]\n"
    CALL std_io.print

    ; === Done ===
    LDS  R0, "Done.\n"
    CALL std_io.print
    HLT
