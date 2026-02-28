; =============================================================================
;  UA Standard Library — std_vector
;
;  Dynamic-size byte vector — modelled after C++ std::vector<uint8_t>.
;
;  A "vector" is a BUFFER with a tracked logical size that can grow up to
;  a fixed capacity.  The user pre-allocates a BUFFER as backing store,
;  then uses push_back / pop_back / resize to manage elements.
;
;  Setup:
;    BUFFER my_vec, 64
;    GET  R0, my_vec
;    SET  std_vector.ptr, R0
;    SET  std_vector.capacity, 64
;    CALL std_vector.clear              ; initialise size to 0
;
;  Functions:
;    clear:       Reset vector to empty (size = 0).
;                 Clobbers: R0
;
;    push_back:   Append one byte.  Ignored silently if at capacity.
;                 Input:  SET std_vector.value, <v>
;                 Clobbers: R0, R1, R2
;
;    pop_back:    Remove and return the last byte.
;                 Output: R0 = removed element (0 if vector was empty)
;                 Clobbers: R0, R1, R2
;
;    front:       Return the first element.
;                 Output: R0 = first byte
;                 Clobbers: R0
;
;    back:        Return the last element.
;                 Output: R0 = last byte
;                 Clobbers: R0, R1
;
;    data:        Return pointer to backing buffer.
;                 Output: R0 = base address
;                 Clobbers: R0
;
;    begin:       Return pointer to first element.
;                 Output: R0 = base address
;                 Clobbers: R0
;
;    end:         Return pointer one past last element.
;                 Output: R0 = ptr + vec_size
;                 Clobbers: R0, R1
;
;    empty:       Check whether vector has zero elements.
;                 Output: R0 = 1 if empty, 0 otherwise
;                 Clobbers: R0, R1
;
;    size_of:     Return the current element count.
;                 Output: R0 = vec_size
;                 Clobbers: R0
;
;    capacity_of: Return the maximum capacity.
;                 Output: R0 = capacity
;                 Clobbers: R0
;
;    at:          Read element at index.
;                 Input:  SET std_vector.index, <i>
;                 Output: R0 = byte at position index
;                 Clobbers: R0, R1
;
;    set_at:      Write a value at index.
;                 Input:  SET std_vector.index, <i>
;                         SET std_vector.value, <v>
;                 Clobbers: R0, R1, R2
;
;    resize:      Change the logical size (clamped to capacity).
;                 New elements are zero-filled when growing.
;                 Input:  SET std_vector.new_size, <n>
;                 Clobbers: R0, R1, R2, R3
;
;  Parameter variables:
;    ptr        — base address of the BUFFER
;    capacity   — maximum number of elements (= BUFFER size)
;    vec_size   — current number of elements   (managed internally)
;    index      — element index (for at / set_at)
;    value      — byte value   (for push_back / set_at)
;    new_size   — target size  (for resize)
;
;  Platform support:
;    Uses only MVIS instructions.  Works on ALL backends.
; =============================================================================

; ---- Parameter / state variables -----------------------------------------
    VAR ptr                  ; base address of backing buffer
    VAR capacity             ; maximum element count
    VAR vec_size             ; current logical size  (managed by the library)
    VAR index                ; element index for at / set_at
    VAR value                ; byte value for push_back / set_at
    VAR new_size             ; target size for resize

; ---------------------------------------------------------------------------
;  clear — reset vector to empty
;
;  Equivalent to C++: vec.clear()
; ---------------------------------------------------------------------------
clear:
    LDI  R0, 0
    SET  vec_size, R0
    RET

; ---------------------------------------------------------------------------
;  push_back — append one byte to the end
;
;  Equivalent to C++: vec.push_back(v)
;  If vec_size == capacity, the call is silently ignored (no crash).
; ---------------------------------------------------------------------------
push_back:
    GET  R0, vec_size
    GET  R1, capacity
    CMP  R0, R1             ; if size == capacity, full
    JZ   push_back_full
    GET  R1, ptr
    ADD  R1, R0              ; R1 = ptr + size = address of new slot
    GET  R2, value
    STOREB R2, R1            ; write the byte
    INC  R0                  ; size++
    SET  vec_size, R0
push_back_full:
    RET

; ---------------------------------------------------------------------------
;  pop_back — remove and return the last byte
;
;  Equivalent to C++: vec.pop_back()  (but also returns the value)
;  Returns 0 if the vector is already empty.
; ---------------------------------------------------------------------------
pop_back:
    GET  R0, vec_size
    LDI  R1, 0
    CMP  R0, R1             ; if size == 0, empty
    JZ   pop_back_empty
    DEC  R0                  ; new size = size - 1
    SET  vec_size, R0        ; store new size
    GET  R1, ptr
    ADD  R1, R0              ; R1 = ptr + new_size = address of removed element
    LOADB R0, R1             ; R0 = popped byte
    RET
pop_back_empty:
    LDI  R0, 0
    RET

; ---------------------------------------------------------------------------
;  front — return the first element
;
;  Equivalent to C++: vec.front()
; ---------------------------------------------------------------------------
front:
    GET  R0, ptr
    LOADB R0, R0             ; R0 = byte at [ptr]
    RET

; ---------------------------------------------------------------------------
;  back — return the last element
;
;  Equivalent to C++: vec.back()
; ---------------------------------------------------------------------------
back:
    GET  R0, ptr
    GET  R1, vec_size
    DEC  R1                  ; last index = size - 1
    ADD  R0, R1              ; R0 = ptr + size - 1
    LOADB R0, R0             ; R0 = byte at last position
    RET

; ---------------------------------------------------------------------------
;  data — return pointer to backing buffer
;
;  Equivalent to C++: vec.data()
; ---------------------------------------------------------------------------
data:
    GET  R0, ptr
    RET

; ---------------------------------------------------------------------------
;  begin — return pointer to first element
;
;  Equivalent to C++: vec.begin()
; ---------------------------------------------------------------------------
begin:
    GET  R0, ptr
    RET

; ---------------------------------------------------------------------------
;  end — return pointer one past last element
;
;  Equivalent to C++: vec.end()
; ---------------------------------------------------------------------------
end:
    GET  R0, ptr
    GET  R1, vec_size
    ADD  R0, R1              ; R0 = ptr + vec_size
    RET

; ---------------------------------------------------------------------------
;  empty — check whether vector has zero elements
;
;  Equivalent to C++: vec.empty()
;  Exit:   R0 = 1 if empty, 0 otherwise
; ---------------------------------------------------------------------------
empty:
    GET  R0, vec_size
    LDI  R1, 0
    CMP  R0, R1
    JZ   empty_yes
    LDI  R0, 0              ; not empty
    RET
empty_yes:
    LDI  R0, 1              ; empty
    RET

; ---------------------------------------------------------------------------
;  size_of — return current element count
;
;  Equivalent to C++: vec.size()
; ---------------------------------------------------------------------------
size_of:
    GET  R0, vec_size
    RET

; ---------------------------------------------------------------------------
;  capacity_of — return maximum capacity
;
;  Equivalent to C++: vec.capacity()
; ---------------------------------------------------------------------------
capacity_of:
    GET  R0, capacity
    RET

; ---------------------------------------------------------------------------
;  at — read element at index
;
;  Equivalent to C++: vec.at(i)  /  vec[i]
;  Entry:  ptr and index set
;  Exit:   R0 = byte at position index
; ---------------------------------------------------------------------------
at:
    GET  R0, ptr
    GET  R1, index
    ADD  R0, R1              ; R0 = ptr + index
    LOADB R0, R0             ; R0 = byte at that address
    RET

; ---------------------------------------------------------------------------
;  set_at — write a value at index
;
;  Equivalent to C++: vec[i] = v
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
;  resize — change logical size (clamped to capacity)
;
;  Equivalent to C++: vec.resize(n)  (with zero-fill for growth)
;  Entry:  new_size set
;  Clobbers: R0, R1, R2, R3
; ---------------------------------------------------------------------------
resize:
    GET  R0, new_size        ; R0 = requested size
    GET  R1, capacity
    CMP  R0, R1             ; clamp to capacity
    JG   resize_clamp
    JMP  resize_check
resize_clamp:
    MOV  R0, R1              ; R0 = capacity (clamped)
resize_check:
    GET  R1, vec_size        ; R1 = current size
    CMP  R0, R1
    JZ   resize_done         ; same size, nothing to do
    JL   resize_shrink       ; shrinking — just set size
    ; Growing — zero-fill from old_size to new_size
    PUSH R0                  ; save target size
    MOV  R2, R1              ; R2 = old_size (counter)
    GET  R0, ptr
    ADD  R0, R1              ; R0 = ptr + old_size (write addr — R0 is 8051-safe)
    LDI  R3, 0              ; fill value = 0
    POP  R1                  ; R1 = target size
resize_fill:
    CMP  R2, R1             ; while counter < target
    JZ   resize_grow_done
    STOREB R3, R0            ; zero the byte  (R0 = address, 8051-compatible)
    INC  R0                  ; next address
    INC  R2                  ; counter++
    JMP  resize_fill
resize_grow_done:
    MOV  R0, R1              ; R0 = target size for SET below
resize_shrink:
    SET  vec_size, R0        ; update logical size
resize_done:
    RET
