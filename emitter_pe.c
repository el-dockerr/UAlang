/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Phase 6: Windows PE (Portable Executable) Emitter
 *
 *  File:    emitter_pe.c
 *  Purpose: Build a minimal but valid 64-bit Windows PE executable from
 *           a raw x86-64 machine-code buffer.  Zero external dependencies —
 *           all PE/COFF structures are defined inline with <stdint.h>.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  PE Layout (as generated)                                          │
 *  │                                                                    │
 *  │  File offset  Size   Content                                       │
 *  │  ──────────── ────── ──────────────────────────────────────         │
 *  │  0x0000       64     IMAGE_DOS_HEADER  (MZ stub)                   │
 *  │  0x0040       24+20  PE signature + IMAGE_FILE_HEADER              │
 *  │   +0x0058     112    IMAGE_OPTIONAL_HEADER64 (no data dirs)        │
 *  │  0x00C8       40     IMAGE_SECTION_HEADER  (.text)                 │
 *  │  0x00F0       pad    Zero-fill up to FileAlignment (0x200)         │
 *  │  0x0200       code   .text section (raw machine code)              │
 *  │   + pad       zeros  Pad to FileAlignment boundary                 │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  Constants:
 *    ImageBase         = 0x0040_0000
 *    SectionAlignment  = 0x1000  (4 KiB)
 *    FileAlignment     = 0x0200  (512 B)
 *    SizeOfHeaders     = 0x0200  (headers fit in one aligned block)
 *    EntryPoint RVA    = 0x1000  (start of .text)
 *
 *  License: MIT
 * =============================================================================
 */

#include "emitter_pe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 *  PE Constants
 * ========================================================================= */
#define PE_IMAGE_BASE           0x00400000ULL
#define PE_SECTION_ALIGNMENT    0x1000
#define PE_FILE_ALIGNMENT       0x0200
#define PE_SIZE_OF_HEADERS      0x0200   /* must be >= actual header bytes */
#define PE_TEXT_RVA              0x1000   /* .text virtual address (RVA)    */

/* =========================================================================
 *  Align helper
 * ========================================================================= */
static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/* =========================================================================
 *  write_le16 / write_le32 / write_le64  —  little-endian serialisers
 * ========================================================================= */
static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)( v        & 0xFF);
    p[1] = (uint8_t)((v >>  8) & 0xFF);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)( v        & 0xFF);
    p[1] = (uint8_t)((v >>  8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void write_le64(uint8_t *p, uint64_t v)
{
    write_le32(p,     (uint32_t)(v & 0xFFFFFFFF));
    write_le32(p + 4, (uint32_t)(v >> 32));
}

/* =========================================================================
 *  emit_pe_exe()
 * ========================================================================= */
int emit_pe_exe(const char *filename, const CodeBuffer *code)
{
    if (!code || code->size == 0) {
        fprintf(stderr, "PE emitter: no code to emit.\n");
        return 1;
    }

    /* ---- Compute sizes ------------------------------------------------ */
    uint32_t raw_code_size     = (uint32_t)code->size;
    uint32_t text_file_size    = align_up(raw_code_size, PE_FILE_ALIGNMENT);
    uint32_t text_virtual_size = align_up(raw_code_size, PE_SECTION_ALIGNMENT);
    uint32_t image_size        = PE_TEXT_RVA + text_virtual_size;
    uint32_t total_file_size   = PE_SIZE_OF_HEADERS + text_file_size;

    fprintf(stderr, "[PE] Code size       : %u bytes\n", raw_code_size);
    fprintf(stderr, "[PE] .text raw size  : %u bytes (file-aligned)\n",
            text_file_size);
    fprintf(stderr, "[PE] Image size      : 0x%X\n", image_size);
    fprintf(stderr, "[PE] Total file size : %u bytes\n", total_file_size);

    /* ---- Allocate a zero-filled file image ----------------------------- */
    uint8_t *img = (uint8_t *)calloc(1, total_file_size);
    if (!img) {
        fprintf(stderr, "PE emitter: out of memory.\n");
        return 1;
    }

    /* ====================================================================
     *  DOS Header  (64 bytes at offset 0x0000)
     * ==================================================================== */
    uint8_t *dos = img;
    dos[0] = 'M';                              /* e_magic           */
    dos[1] = 'Z';
    write_le32(dos + 60, 0x40);                /* e_lfanew -> PE hdr */

    /* ====================================================================
     *  PE Signature  (4 bytes at offset 0x0040)
     * ==================================================================== */
    uint8_t *pe_sig = img + 0x40;
    pe_sig[0] = 'P';
    pe_sig[1] = 'E';
    pe_sig[2] = 0;
    pe_sig[3] = 0;

    /* ====================================================================
     *  IMAGE_FILE_HEADER  (20 bytes at offset 0x0044)
     *
     *  struct {
     *    uint16_t Machine;              // 0x8664 = AMD64
     *    uint16_t NumberOfSections;     // 1
     *    uint32_t TimeDateStamp;        // 0
     *    uint32_t PointerToSymbolTable; // 0
     *    uint32_t NumberOfSymbols;      // 0
     *    uint16_t SizeOfOptionalHeader; // 112 (PE32+ minimal, 0 data dirs)
     *    uint16_t Characteristics;      // 0x0022 (EXECUTABLE | LARGE_ADDRESS)
     *  };
     * ==================================================================== */
    uint8_t *fh = img + 0x44;
    write_le16(fh +  0, 0x8664);               /* Machine: AMD64    */
    write_le16(fh +  2, 1);                    /* NumberOfSections  */
    write_le32(fh +  4, 0);                    /* TimeDateStamp     */
    write_le32(fh +  8, 0);                    /* PointerToSymbolTable */
    write_le32(fh + 12, 0);                    /* NumberOfSymbols   */
    write_le16(fh + 16, 112);                  /* SizeOfOptionalHeader */
    write_le16(fh + 18, 0x0022);               /* Characteristics   */

    /* ====================================================================
     *  IMAGE_OPTIONAL_HEADER64  (112 bytes at offset 0x0058)
     *
     *  PE32+ optional header with 0 data directory entries.
     *
     *  Standard fields (24 bytes):
     *    Magic, MajorLinker, MinorLinker, SizeOfCode, SizeOfInitializedData,
     *    SizeOfUninitializedData, AddressOfEntryPoint, BaseOfCode
     *
     *  Windows-specific fields (88 bytes):
     *    ImageBase, SectionAlignment, FileAlignment, OS version,
     *    Image version, Subsystem version, Win32VersionValue,
     *    SizeOfImage, SizeOfHeaders, CheckSum, Subsystem,
     *    DllCharacteristics, Stack/Heap sizes, LoaderFlags,
     *    NumberOfRvaAndSizes
     * ==================================================================== */
    uint8_t *oh = img + 0x58;

    /* Standard fields */
    write_le16(oh +  0, 0x020B);               /* Magic: PE32+ (64-bit) */
    oh[2] = 1;                                 /* MajorLinkerVersion    */
    oh[3] = 0;                                 /* MinorLinkerVersion    */
    write_le32(oh +  4, text_file_size);       /* SizeOfCode            */
    write_le32(oh +  8, 0);                    /* SizeOfInitializedData */
    write_le32(oh + 12, 0);                    /* SizeOfUninitializedData */
    write_le32(oh + 16, PE_TEXT_RVA);          /* AddressOfEntryPoint   */
    write_le32(oh + 20, PE_TEXT_RVA);          /* BaseOfCode            */

    /* Windows-specific fields */
    write_le64(oh + 24, PE_IMAGE_BASE);        /* ImageBase             */
    write_le32(oh + 32, PE_SECTION_ALIGNMENT); /* SectionAlignment      */
    write_le32(oh + 36, PE_FILE_ALIGNMENT);    /* FileAlignment         */
    write_le16(oh + 40, 6);                    /* MajorOperatingSystemVersion */
    write_le16(oh + 42, 0);                    /* MinorOperatingSystemVersion */
    write_le16(oh + 44, 0);                    /* MajorImageVersion     */
    write_le16(oh + 46, 0);                    /* MinorImageVersion     */
    write_le16(oh + 48, 6);                    /* MajorSubsystemVersion */
    write_le16(oh + 50, 0);                    /* MinorSubsystemVersion */
    write_le32(oh + 52, 0);                    /* Win32VersionValue     */
    write_le32(oh + 56, image_size);           /* SizeOfImage           */
    write_le32(oh + 60, PE_SIZE_OF_HEADERS);   /* SizeOfHeaders         */
    write_le32(oh + 64, 0);                    /* CheckSum              */
    write_le16(oh + 68, 3);                    /* Subsystem: CONSOLE    */
    write_le16(oh + 70, 0x0000);               /* DllCharacteristics    */
    write_le64(oh + 72, 0x100000);             /* SizeOfStackReserve    */
    write_le64(oh + 80, 0x1000);               /* SizeOfStackCommit     */
    write_le64(oh + 88, 0x100000);             /* SizeOfHeapReserve     */
    write_le64(oh + 96, 0x1000);               /* SizeOfHeapCommit      */
    write_le32(oh +104, 0);                    /* LoaderFlags           */
    write_le32(oh +108, 0);                    /* NumberOfRvaAndSizes   */
    /* No data directory entries (NumberOfRvaAndSizes = 0) */

    /* ====================================================================
     *  IMAGE_SECTION_HEADER  (.text)  (40 bytes at offset 0x00C8)
     *
     *  struct {
     *    char     Name[8];
     *    uint32_t VirtualSize;
     *    uint32_t VirtualAddress;
     *    uint32_t SizeOfRawData;
     *    uint32_t PointerToRawData;
     *    uint32_t PointerToRelocations;  // 0
     *    uint32_t PointerToLineNumbers;  // 0
     *    uint16_t NumberOfRelocations;   // 0
     *    uint16_t NumberOfLineNumbers;   // 0
     *    uint32_t Characteristics;
     *  };
     * ==================================================================== */
    uint8_t *sh = img + 0xC8;
    memcpy(sh, ".text\0\0\0", 8);              /* Name                  */
    write_le32(sh +  8, raw_code_size);        /* VirtualSize           */
    write_le32(sh + 12, PE_TEXT_RVA);          /* VirtualAddress        */
    write_le32(sh + 16, text_file_size);       /* SizeOfRawData         */
    write_le32(sh + 20, PE_SIZE_OF_HEADERS);   /* PointerToRawData      */
    write_le32(sh + 24, 0);                    /* PointerToRelocations  */
    write_le32(sh + 28, 0);                    /* PointerToLineNumbers  */
    write_le16(sh + 32, 0);                    /* NumberOfRelocations   */
    write_le16(sh + 34, 0);                    /* NumberOfLineNumbers   */
    write_le32(sh + 36, 0x60000020);           /* Characteristics:
                                                  CODE | EXECUTE | READ */

    /* ====================================================================
     *  .text Section Data  (at file offset 0x0200)
     * ==================================================================== */
    memcpy(img + PE_SIZE_OF_HEADERS, code->bytes, raw_code_size);

    /* ====================================================================
     *  Write file
     * ==================================================================== */
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "PE emitter: cannot open '%s' for writing: ", filename);
        perror(NULL);
        free(img);
        return 1;
    }

    size_t written = fwrite(img, 1, total_file_size, fp);
    fclose(fp);
    free(img);

    if (written != total_file_size) {
        fprintf(stderr, "PE emitter: short write (%zu of %u bytes).\n",
                written, total_file_size);
        return 1;
    }

    fprintf(stderr, "[PE] Wrote %u bytes to %s\n", total_file_size, filename);
    return 0;
}
