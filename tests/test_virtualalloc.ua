; Test VirtualAlloc directly
@IMPORT std_io

start:
    ; Try to allocate 4096 bytes using VirtualAlloc
    ; Parameters: lpAddress=NULL, dwSize=4096, flAllocationType=0x3000, flProtect=0x04
    
    LDI  R0, 0           ; lpAddress = NULL
    LDI  R1, 4096        ; dwSize = 4096
    LDI  R2, 12288       ; flAllocationType = 0x3000 (MEM_COMMIT | MEM_RESERVE)
    LDI  R3, 4           ; flProtect = 0x04 (PAGE_READWRITE)
    LDI  R7, 3           ; Syscall type 3 for VirtualAlloc
    SYS
    
    ; Check if R0 contains valid pointer
    LDI  R1, 0
    CMP  R0, R1
    JZ   alloc_failed
    
    LDS  R0, "VirtualAlloc succeeded!\n"
    CALL std_io.print
    JMP  done
    
alloc_failed:
    LDS  R0, "VirtualAlloc failed!\n"
    CALL std_io.print
    
done:
    HLT
