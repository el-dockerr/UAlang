; =============================================================================
;  test_iostream.ua — test for std_iostream (file I/O library)
;
;  This test:
;    1. Opens a file for writing ("_test_io.tmp")
;    2. Writes a known string to it
;    3. Closes the file
;    4. Reopens for reading
;    5. Reads the content back into a buffer
;    6. Prints the content to stdout (should match the original string)
;    7. Closes the file
;
;  Expected output:   Hello from iostream!
;
;  Build & run (Win32):
;    ua tests/test_iostream.ua -arch x86 -sys win32 -o tests/test_iostream.exe
;    tests\test_iostream.exe
;
;  Build (Linux):
;    ua tests/test_iostream.ua -arch x86 -sys linux -o tests/test_iostream
;    chmod +x tests/test_iostream && ./tests/test_iostream
; =============================================================================

@ARCH_ONLY x86, x86_32, arm, arm64, riscv
@IMPORT std_io
@IMPORT std_iostream

; ---- Data ----------------------------------------------------------------
    BUFFER read_buf,  64

; ---- Entry ---------------------------------------------------------------
    JMP main

main:
    ; --- Step 1: Open file for writing ---
    LDS  R0, "_test_io.tmp"
    SET  std_iostream.iostream_path, R0
    SET  std_iostream.iostream_mode, 1        ; 1 = write (create / truncate)
    CALL std_iostream.fopen

    ; --- Step 2: Write the test message ---
    LDS  R0, "Hello from iostream!\n"
    SET  std_iostream.iostream_buf, R0
    SET  std_iostream.iostream_count, 21      ; 21 chars including newline
    CALL std_iostream.fwrite

    ; --- Step 3: Close the file ---
    CALL std_iostream.fclose

    ; --- Step 4: Reopen file for reading ---
    LDS  R0, "_test_io.tmp"
    SET  std_iostream.iostream_path, R0
    SET  std_iostream.iostream_mode, 0        ; 0 = read
    CALL std_iostream.fopen

    ; --- Step 5: Read content back ---
    GET  R0, read_buf
    SET  std_iostream.iostream_buf, R0
    SET  std_iostream.iostream_count, 64      ; read up to 64 bytes
    CALL std_iostream.fread

    ; --- Step 6: Print what we read to console ---
    GET  R0, read_buf
    CALL std_io.print

    ; --- Step 7: Close the file ---
    CALL std_iostream.fclose

    ; --- Done ---
    HLT
