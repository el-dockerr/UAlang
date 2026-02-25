/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Shared Code-Generation Utilities
 *
 *  File:    codegen.h
 *  Purpose: Common types and helpers used by every back-end:
 *             - CodeBuffer  (dynamic byte buffer for machine code)
 *             - hexdump()   (canonical hex dump to stdout)
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UAS_CODEGEN_H
#define UAS_CODEGEN_H

#include <stdint.h>

/* =========================================================================
 *  Code Buffer
 * =========================================================================
 *  All back-ends emit raw bytes into a CodeBuffer.
 *  The caller must free it with free_code_buffer().
 * ========================================================================= */
typedef struct {
    uint8_t *bytes;         /* Raw machine code bytes                    */
    int      size;          /* Number of valid bytes in `bytes`           */
    int      capacity;      /* Allocated capacity                        */
} CodeBuffer;

/* =========================================================================
 *  Public API
 * ========================================================================= */

/*
 * create_code_buffer()
 *   Allocate an empty CodeBuffer with an initial capacity.
 *   Returns NULL on allocation failure.
 */
CodeBuffer* create_code_buffer(void);

/*
 * free_code_buffer()
 *   Frees a CodeBuffer.  Safe with NULL.
 */
void free_code_buffer(CodeBuffer *buf);

/*
 * emit_byte()
 *   Append a single byte to the buffer, growing if necessary.
 */
void emit_byte(CodeBuffer *buf, uint8_t byte);

/*
 * hexdump()
 *   Pretty-prints `size` bytes from `data` in canonical hex-dump format.
 */
void hexdump(const uint8_t *data, int size);

#endif /* UAS_CODEGEN_H */
