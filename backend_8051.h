/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Phase 3: 8051 Back-End (Code Generation)
 *
 *  File:    backend_8051.h
 *  Purpose: Public interface for the Intel 8051 / MCS-51 code generator.
 *
 *  The back-end performs a classic two-pass assembly:
 *
 *    Pass 1 — Address resolution:
 *             Walk the IR, compute the byte-size of each instruction's
 *             8051 translation, and record every label's byte offset in
 *             a symbol table.
 *
 *    Pass 2 — Code emission:
 *             Walk the IR again, emit raw 8051 machine bytes into a
 *             flat buffer, resolving label references using the symbol
 *             table built in Pass 1.
 *
 *  Target:  Intel MCS-51 / 8051 / STC89C52RC  (Harvard architecture)
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UAS_BACKEND_8051_H
#define UAS_BACKEND_8051_H

#include "parser.h"
#include "codegen.h"    /* CodeBuffer, free_code_buffer, hexdump */
#include <stdint.h>

/* =========================================================================
 *  Symbol Table Entry
 * ========================================================================= */
#define MAX_SYMBOLS  256

typedef struct {
    char    name[UAS_MAX_LABEL_LEN];   /* Label name                     */
    int     address;                   /* Byte offset from start of code */
} Symbol;

typedef struct {
    Symbol  entries[MAX_SYMBOLS];
    int     count;
} SymbolTable;

/* =========================================================================
 *  Public API
 * ========================================================================= */

/*
 * generate_8051()
 *   Two-pass assembler: builds symbol table, emits 8051 machine code,
 *   and returns the code buffer.
 *
 *   The caller must free the result with free_code_buffer().
 */
CodeBuffer* generate_8051(const Instruction *ir, int ir_count);

#endif /* UAS_BACKEND_8051_H */
