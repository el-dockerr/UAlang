/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 1: Lexer / Tokenizer
 *
 *  File:    lexer.h
 *  Purpose: Public interface for the UA lexical analyzer.
 *           Defines token types, the token structure, and the tokenizer API.
 *
 *  Design:  Architecture-agnostic.  The lexer operates on a universal register
 *           model (R0-R15) and a minimal opcode set that will be expanded in
 *           later phases to emit machine code for Intel x86, ARM, RISC-V,
 *           and Harvard-architecture MCUs (8051 / STC89C52RC).
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_LEXER_H
#define UA_LEXER_H

#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Token Types
 * -------------------------------------------------------------------------
 * Every lexeme produced by the tokenizer is classified into exactly one of
 * the categories below.
 * ------------------------------------------------------------------------- */
typedef enum {
    /* --- Operands & identifiers ----------------------------------------- */
    TOKEN_OPCODE,       /* Mnemonic: MOV, ADD, SUB, LDI, LOAD, STORE, ...   */
    TOKEN_REGISTER,     /* Virtual register R0 .. R15                        */
    TOKEN_NUMBER,       /* Decimal, hex (0x..), or binary (0b..) literal     */
    TOKEN_LABEL,        /* Label definition   e.g.  "loop:"                  */
    TOKEN_LABEL_REF,    /* Label reference    e.g.  "loop" used as operand   */
    TOKEN_IDENTIFIER,   /* Generic identifier (future directives / macros)   */

    /* --- Punctuation ---------------------------------------------------- */
    TOKEN_COMMA,        /* ','  operand separator                            */
    TOKEN_COLON,        /* ':'  label terminator (consumed during labeling)  */
    TOKEN_NEWLINE,      /* '\n' statement terminator                         */

    /* --- Meta ----------------------------------------------------------- */
    TOKEN_COMMENT,      /* '; ...' kept for diagnostics, ignored by parser   */
    TOKEN_EOF,          /* End-of-input sentinel                             */
    TOKEN_UNKNOWN       /* Unrecognised character — reported as error        */
} UaTokenType;

/* -------------------------------------------------------------------------
 * Token Structure
 * -------------------------------------------------------------------------
 * Each token carries:
 *   - its type
 *   - a copy of the source lexeme (null-terminated)
 *   - its numeric value (meaningful only for TOKEN_NUMBER / TOKEN_REGISTER)
 *   - source location for error reporting
 * ------------------------------------------------------------------------- */
#define UA_MAX_TOKEN_LEN  64   /* Maximum characters in a single lexeme */

typedef struct {
    UaTokenType type;
    char        text[UA_MAX_TOKEN_LEN];  /* Human-readable lexeme           */
    int64_t     value;                    /* Numeric payload (if applicable) */
    int         line;                     /* 1-based source line             */
    int         column;                   /* 1-based source column           */
} Token;

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------
 *
 * tokenize()
 *   Scans `source_code` and produces a dynamically-allocated array of Token
 *   structs.  The caller is responsible for freeing the returned pointer
 *   with `free()`.  The sentinel TOKEN_EOF is always the last element.
 *
 *   Parameters:
 *     source_code  – null-terminated assembly source text.
 *     token_count  – [out] receives the number of tokens (including EOF).
 *
 *   Returns:
 *     Pointer to a heap-allocated Token array, or NULL on allocation failure.
 * ------------------------------------------------------------------------- */
Token* tokenize(const char *source_code, int *token_count);

/* -------------------------------------------------------------------------
 * token_type_name()
 *   Returns a human-readable string for a given UaTokenType (for debugging).
 * ------------------------------------------------------------------------- */
const char* token_type_name(UaTokenType type);

#endif /* UA_LEXER_H */
