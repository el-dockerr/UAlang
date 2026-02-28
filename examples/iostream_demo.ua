; =============================================================================
;  iostream_demo.ua — std_iostream demonstration with console output
;
;  Shows how to use the std_iostream library to write data to a file,
;  read it back, and display the result on the console.
;
;  The demo:
;    1. Prints a header message
;    2. Creates a file "demo_output.txt" and writes two lines
;    3. Closes and re-opens the file for reading
;    4. Reads the entire content back into a buffer
;    5. Prints the file content to the console
;    6. Cleans up and exits
;
;  Compatible with: x86, x86_32, arm, arm64, riscv
;  (Requires a hosted OS — not suitable for bare-metal 8051.)
;
;  Build & run:
;    ua examples/iostream_demo.ua -arch x86   -sys win32 -o iostream_demo.exe
;    ua examples/iostream_demo.ua -arch x86   -sys linux  -o iostream_demo
;    ua examples/iostream_demo.ua -arch x86_32 -sys linux -o iostream_demo.bin
;    ua examples/iostream_demo.ua -arch arm   -sys linux  -o iostream_demo.bin
;    ua examples/iostream_demo.ua -arch arm64 -sys linux  -o iostream_demo.bin
;    ua examples/iostream_demo.ua -arch riscv -sys linux  -o iostream_demo.bin
; =============================================================================

@ARCH_ONLY x86, x86_32, arm, arm64, riscv
@IMPORT std_io
@IMPORT std_iostream
@IMPORT std_string

; ---- Data ----------------------------------------------------------------
    BUFFER read_buf,       128
    BUFFER num_buf,        16

; ---- Entry ---------------------------------------------------------------
    JMP main

main:
    ; ---- Print header ----
    LDS  R0, "=== UA iostream Demo ===\n"
    CALL std_io.print

    ; ---- Open file for writing ----
    LDS  R0, "Writing to demo_output.txt...\n"
    CALL std_io.print

    LDS  R0, "demo_output.txt"
    SET  std_iostream.iostream_path, R0
    SET  std_iostream.iostream_mode, 1           ; write (create/truncate)
    CALL std_iostream.fopen

    ; ---- Write line 1 (32 bytes) ----
    LDS  R0, "Line 1: Hello from UA iostream!\n"
    SET  std_iostream.iostream_buf, R0
    SET  std_iostream.iostream_count, 32
    CALL std_iostream.fwrite

    ; ---- Write line 2 (24 bytes) ----
    LDS  R0, "Line 2: File I/O works!\n"
    SET  std_iostream.iostream_buf, R0
    SET  std_iostream.iostream_count, 24
    CALL std_iostream.fwrite

    ; ---- Close ----
    CALL std_iostream.fclose
    LDS  R0, "File closed.\n"
    CALL std_io.print

    ; ---- Reopen for reading ----
    LDS  R0, "Reopening for read...\n"
    CALL std_io.print

    LDS  R0, "demo_output.txt"
    SET  std_iostream.iostream_path, R0
    SET  std_iostream.iostream_mode, 0           ; read
    CALL std_iostream.fopen

    ; ---- Read back the content ----
    GET  R0, read_buf
    SET  std_iostream.iostream_buf, R0
    SET  std_iostream.iostream_count, 128
    CALL std_iostream.fread
    ; R0 = bytes actually read — save for later
    PUSH R0

    ; ---- Print read header + content ----
    LDS  R0, "--- File content ---\n"
    CALL std_io.print

    GET  R0, read_buf
    CALL std_io.print

    LDS  R0, "--- End of file ---\n"
    CALL std_io.print

    ; ---- Print byte count ----
    POP  R0
    GET  R1, num_buf
    CALL std_string.to_string
    GET  R0, num_buf
    CALL std_io.print

    LDS  R0, " bytes read.\n"
    CALL std_io.print

    ; ---- Close ----
    CALL std_iostream.fclose

    ; ---- Done ----
    LDS  R0, "Demo complete!\n"
    CALL std_io.print
    HLT
