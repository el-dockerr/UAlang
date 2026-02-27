; test_buffer_addr.ua — test BUFFER address retrieval
; Expected: R0 = address of buffer (should be non-zero)
    BUFFER mybuf, 8
    GET  R0, mybuf
    HLT
