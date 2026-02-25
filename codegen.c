/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Shared Code-Generation Utilities
 *
 *  File:    codegen.c
 *  Purpose: Implementations for CodeBuffer management and hexdump.
 *
 *  License: MIT
 * =============================================================================
 */

#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 *  Constants
 * ========================================================================= */
#define INITIAL_CODE_CAPACITY  256

/* =========================================================================
 *  create_code_buffer()
 * ========================================================================= */
CodeBuffer* create_code_buffer(void)
{
    CodeBuffer *buf = (CodeBuffer *)malloc(sizeof(CodeBuffer));
    if (!buf) return NULL;
    buf->bytes    = (uint8_t *)malloc(INITIAL_CODE_CAPACITY);
    buf->size     = 0;
    buf->capacity = INITIAL_CODE_CAPACITY;
    if (!buf->bytes) { free(buf); return NULL; }
    return buf;
}

/* =========================================================================
 *  free_code_buffer()
 * ========================================================================= */
void free_code_buffer(CodeBuffer *buf)
{
    if (!buf) return;
    free(buf->bytes);
    free(buf);
}

/* =========================================================================
 *  emit_byte()
 * ========================================================================= */
void emit_byte(CodeBuffer *buf, uint8_t byte)
{
    if (buf->size >= buf->capacity) {
        int new_cap = buf->capacity * 2;
        uint8_t *tmp = (uint8_t *)realloc(buf->bytes, (size_t)new_cap);
        if (!tmp) {
            fprintf(stderr, "UAS codegen: out of memory\n");
            exit(1);
        }
        buf->bytes    = tmp;
        buf->capacity = new_cap;
    }
    buf->bytes[buf->size++] = byte;
}

/* =========================================================================
 *  hexdump()  —  canonical hex dump of a byte buffer
 * ========================================================================= */
void hexdump(const uint8_t *data, int size)
{
    for (int i = 0; i < size; i += 16) {
        /* Address */
        printf("  %04X: ", i);

        /* Hex bytes */
        for (int j = 0; j < 16; j++) {
            if (i + j < size)
                printf("%02X ", data[i + j]);
            else
                printf("   ");
            if (j == 7) printf(" ");   /* midpoint separator */
        }

        /* ASCII representation */
        printf(" |");
        for (int j = 0; j < 16; j++) {
            if (i + j < size) {
                uint8_t c = data[i + j];
                printf("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
            }
        }
        printf("|\n");
    }
}
