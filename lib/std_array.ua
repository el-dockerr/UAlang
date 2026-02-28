; =============================================================================
;  UA Standard Library — std_array
;
;  Fixed-size byte array — modelled after C++ std::array<uint8_t, N>.
;
;  Setup:
;    In your program, allocate a BUFFER and pass its address and size
;    to this library via the parameter variables:
;
;      BUFFER my_arr, 10
;      GET  R0, my_arr
;      SET  std_array.ptr, R0
;      SET  std_array.size, 10
;
;  Functions:
;    front:    Return the first element.
;              Output: R0 = value of first byte
;              Clobbers: R0
;
;    back:     Return the last element.
;              Output: R0 = value of last byte
;              Clobbers: R0, R1
;
;    data:     Return pointer to underlying buffer.
;              Output: R0 = base address
;              Clobbers: R0
;
;    begin:    Return pointer to first element (same as data).
;              Output: R0 = base address
;              Clobbers: R0
;
;    end:      Return pointer one past the last element.
;              Output: R0 = base address + size
;              Clobbers: R0, R1
;
;    empty:    Check whether the array has zero size.
;              Output: R0 = 1 if size == 0, else 0
;              Clobbers: R0, R1
;
;    size_of:  Return the element count.
;              Output: R0 = size
;              Clobbers: R0
;
;    at:       Read element at a given index.
;              Input:  SET std_array.index, <i>
;              Output: R0 = value of byte at position index
;              Clobbers: R0, R1
;
;    set_at:   Write a value to a given index.
;              Input:  SET std_array.index, <i>
;                      SET std_array.value, <v>
;              Clobbers: R0, R1, R2
;
;    fill:     Fill every element with a constant byte.
;              Input:  SET std_array.value, <v>
;              Clobbers: R0, R1, R2, R3
;
;  Parameter variables (set before calling functions):
;    ptr    — base address of the BUFFER  (GET Rx, myBuf; SET std_array.ptr, Rx)
;    size   — number of elements
;    index  — element index for at / set_at
;    value  — byte value for fill / set_at
;
;  Platform support:
;    Uses only MVIS instructions (LOADB, STOREB, ADD, CMP, INC, DEC,
;    JZ, JNZ, JMP, LDI, MOV, GET, SET, RET).
;    Works on ALL backends: x86, x86_32, arm, arm64, riscv, mcs51.
; =============================================================================

; ---- Parameter variables ------------------------------------------------
    VAR ptr                  ; base address of the array buffer
    VAR size                 ; number of elements (fixed at creation)
    VAR index                ; element index (for at / set_at)
    VAR value                ; byte value   (for fill / set_at)

; ---------------------------------------------------------------------------
;  front — return the first element
;
;  Equivalent to C++: arr.front()
;  Entry:  ptr set to buffer address
;  Exit:   R0 = first byte value
; ---------------------------------------------------------------------------
front:
    GET  R0, ptr             ; R0 = base address
    LOADB R0, R0             ; R0 = byte at [ptr]
    RET

; ---------------------------------------------------------------------------
;  back — return the last element
;
;  Equivalent to C++: arr.back()
;  Entry:  ptr and size set
;  Exit:   R0 = last byte value
; ---------------------------------------------------------------------------
back:
    GET  R0, ptr             ; R0 = base address
    GET  R1, size            ; R1 = element count
    DEC  R1                  ; R1 = size - 1  (last valid index)
    ADD  R0, R1              ; R0 = ptr + size - 1
    LOADB R0, R0             ; R0 = byte at last position
    RET

; ---------------------------------------------------------------------------
;  data — return pointer to underlying storage
;
;  Equivalent to C++: arr.data()
;  Exit:   R0 = base address
; ---------------------------------------------------------------------------
data:
    GET  R0, ptr
    RET

; ---------------------------------------------------------------------------
;  begin — return pointer to the first element
;
;  Equivalent to C++: arr.begin()
;  Exit:   R0 = base address
; ---------------------------------------------------------------------------
begin:
    GET  R0, ptr
    RET

; ---------------------------------------------------------------------------
;  end — return pointer one past the last element
;
;  Equivalent to C++: arr.end()
;  Exit:   R0 = ptr + size
; ---------------------------------------------------------------------------
end:
    GET  R0, ptr
    GET  R1, size
    ADD  R0, R1              ; R0 = ptr + size
    RET

; ---------------------------------------------------------------------------
;  empty — check whether array has zero size
;
;  Equivalent to C++: arr.empty()
;  Exit:   R0 = 1 if empty, 0 if not
; ---------------------------------------------------------------------------
empty:
    GET  R0, size
    LDI  R1, 0
    CMP  R0, R1
    JZ   empty_yes
    LDI  R0, 0              ; not empty
    RET
empty_yes:
    LDI  R0, 1              ; empty
    RET

; ---------------------------------------------------------------------------
;  size_of — return the element count
;
;  Equivalent to C++: arr.size()
;  Exit:   R0 = size
; ---------------------------------------------------------------------------
size_of:
    GET  R0, size
    RET

; ---------------------------------------------------------------------------
;  at — read element at a given index
;
;  Equivalent to C++: arr.at(i)  /  arr[i]
;  Entry:  ptr, size, index set
;  Exit:   R0 = byte at position index
; ---------------------------------------------------------------------------
at:
    GET  R0, ptr
    GET  R1, index
    ADD  R0, R1              ; R0 = ptr + index
    LOADB R0, R0             ; R0 = byte at that address
    RET

; ---------------------------------------------------------------------------
;  set_at — write a value to a given index
;
;  Equivalent to C++: arr[i] = v
;  Entry:  ptr, index, value set
; ---------------------------------------------------------------------------
set_at:
    GET  R0, ptr
    GET  R1, index
    ADD  R0, R1              ; R0 = ptr + index
    GET  R2, value
    STOREB R2, R0            ; store value at address
    RET

; ---------------------------------------------------------------------------
;  fill — set every element to the same byte value
;
;  Equivalent to C++: arr.fill(v)
;  Entry:  ptr, size, value set
;  Clobbers: R0, R1, R2, R3
; ---------------------------------------------------------------------------
fill:
    GET  R0, ptr             ; R0 = destination address
    GET  R1, size            ; R1 = remaining count
    GET  R2, value           ; R2 = fill byte
    LDI  R3, 0              ; zero sentinel
fill_loop:
    CMP  R1, R3             ; if count == 0, done
    JZ   fill_done
    STOREB R2, R0            ; store value at [R0]
    INC  R0                  ; advance pointer
    DEC  R1                  ; decrement count
    JMP  fill_loop
fill_done:
    RET
