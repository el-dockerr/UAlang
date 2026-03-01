; Test buffer access only (no malloc function calls)
@IMPORT std_io

BUFFER test_buf, 1024

VAR my_var

start:
    ; Get buffer address
    GET  R0, test_buf
    
    ; Store address in variable
    SET  my_var, R0
    
    ; Retrieve and compare
    GET  R1, my_var
    CMP  R0, R1
    JZ   success
    
    LDS  R0, "Buffer address mismatch!\n"
    CALL std_io.print
    JMP  done
    
success:
    LDS  R0, "Buffer access OK!\n"
    CALL std_io.print
    
done:
    HLT
