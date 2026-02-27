; =============================================================================
;  UA Standard Library — std_arrays
;
;  Byte-array utility functions for contiguous memory buffers.
;  Use with BUFFER to allocate memory and LDS/GET to obtain addresses.
;
;  Functions:
;    fill_bytes:  Fill a byte buffer with a constant value.
;                 Input:  R0 = destination address
;                         R1 = byte count (length)
;                         R2 = fill value (low byte used)
;                 Output: (none — buffer is filled in-place)
;                 Clobbers: R0 (advanced past end), R1 (zeroed), R3
;
;    copy_bytes:  Copy bytes from source to destination.
;                 Input:  R0 = source address
;                         R1 = destination address
;                         R2 = byte count (length)
;                 Output: (none — bytes are copied in-place)
;                 Clobbers: R0, R1 (both advanced), R2 (zeroed), R3, R4
;
;  Platform support:
;    This library uses only architecture-neutral MVIS instructions
;    (STOREB, LOADB, INC, DEC, CMP, JZ, JMP, LDI, RET) and works
;    on all backends.
;
;  8051 note:
;    LOADB / STOREB use indirect addressing (@R0/@R1).  Only internal
;    RAM (0x00-0xFF) is accessible.  Buffers allocated with BUFFER
;    reside in internal RAM and are valid targets.
; =============================================================================

; ---------------------------------------------------------------------------
;  fill_bytes — fill memory with a constant byte
;
;  Algorithm:  while (count > 0) { *dst++ = val; count--; }
;
;  Entry:  R0 = destination, R1 = count, R2 = fill value
;  Exit:   R0 past end, R1 = 0
;  Clobbers: R0, R1, R3
; ---------------------------------------------------------------------------
fill_bytes:
    LDI R3, 0            ; zero sentinel
fill_bytes_loop:
    CMP R1, R3           ; if count == 0, done
    JZ fill_bytes_done
    STOREB R2, R0        ; store fill value at [R0]
    INC R0               ; dst++
    DEC R1               ; count--
    JMP fill_bytes_loop
fill_bytes_done:
    RET

; ---------------------------------------------------------------------------
;  copy_bytes — copy N bytes from source to destination
;
;  Algorithm:  while (count > 0) { *dst++ = *src++; count--; }
;
;  Entry:  R0 = source, R1 = destination, R2 = count
;  Exit:   R0, R1 advanced past end, R2 = 0
;  Clobbers: R0, R1, R2, R3, R4
; ---------------------------------------------------------------------------
copy_bytes:
    LDI R4, 0            ; zero sentinel
copy_bytes_loop:
    CMP R2, R4           ; if count == 0, done
    JZ copy_bytes_done
    LOADB R3, R0         ; R3 = byte at [src]
    STOREB R3, R1        ; store byte at [dst]
    INC R0               ; src++
    INC R1               ; dst++
    DEC R2               ; count--
    JMP copy_bytes_loop
copy_bytes_done:
    RET
