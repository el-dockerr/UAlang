; Minimal test - just print a message
@IMPORT std_io

start:
    LDS  R0, "Hello from UA!\n"
    CALL std_io.print
    HLT
