/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 6+8: Windows PE (Portable Executable) Emitter
 *
 *  File:    emitter_pe.c
 *  Purpose: Build a valid 64-bit Windows PE executable from a raw x86-64
 *           machine-code buffer.  Supports optional Win32 API imports
 *           (kernel32.dll) when the backend targets Win32.
 *
 *  When code->pe_iat_offset > 0 (Win32 target), the emitter:
 *    - Adds a .idata section with import structures
 *    - Pre-fills IAT entries with Import Lookup Table values
 *    - Configures data directories for the Windows loader
 *    - Marks .text as RWX so the loader can patch the IAT
 *
 *  Layout (with imports):
 *
 *    0x0000       Headers (DOS + NT + 2 section headers)
 *    0x0200       .text  (code + vars + strings + stubs + IAT)
 *    0x0200+N     .idata (ILT + IDT + HintName + DLL name)
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
#define PE_SIZE_OF_HEADERS      0x0200
#define PE_TEXT_RVA              0x1000

/* ---- Import data layout within .idata section (fixed offsets) --------- */
/*  0x00  IDT[0]          20 bytes   (kernel32.dll descriptor)             */
/*  0x14  IDT[1]          20 bytes   (null terminator)                     */
/*  0x28  ILT[0]           8 bytes   → HintName GetStdHandle               */
/*  0x30  ILT[1]           8 bytes   → HintName WriteFile                  */
/*  0x38  ILT[2]           8 bytes   → HintName ExitProcess                */
/*  0x40  ILT[3]           8 bytes   (null terminator)                     */
/*  0x48  HN_GetStdHandle 16 bytes   (2 hint + "GetStdHandle\0" + 1 pad)  */
/*  0x58  HN_WriteFile    12 bytes   (2 hint + "WriteFile\0" + 1 pad)     */
/*  0x64  HN_ExitProcess  14 bytes   (2 hint + "ExitProcess\0" + 1 pad)   */
/*  0x72  DLL name        13 bytes   "kernel32.dll\0"                      */
/*  Total: 0x7F = 127 bytes                                               */
#define IDATA_IDT_OFF     0x00
#define IDATA_ILT_OFF     0x28
#define IDATA_HN0_OFF     0x48   /* GetStdHandle */
#define IDATA_HN1_OFF     0x58   /* WriteFile    */
#define IDATA_HN2_OFF     0x64   /* ExitProcess  */
#define IDATA_DLL_OFF     0x72   /* DLL name     */
#define IDATA_RAW_SIZE    0x7F   /* 127 bytes    */

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

    int has_imports = (code->pe_iat_offset > 0);
    int num_sections = has_imports ? 2 : 1;

    /* ---- Compute .text sizes ------------------------------------------ */
    uint32_t text_raw_size     = (uint32_t)code->size;
    uint32_t text_file_size    = align_up(text_raw_size, PE_FILE_ALIGNMENT);
    uint32_t text_virtual_size = align_up(text_raw_size, PE_SECTION_ALIGNMENT);

    /* ---- Compute .idata sizes (if imports present) -------------------- */
    uint32_t idata_rva          = 0;
    uint32_t idata_raw_size     = 0;
    uint32_t idata_file_size    = 0;
    uint32_t idata_virtual_size = 0;
    uint32_t idata_file_off     = 0;

    if (has_imports) {
        idata_rva          = PE_TEXT_RVA + text_virtual_size;
        idata_raw_size     = IDATA_RAW_SIZE;
        idata_file_size    = align_up(idata_raw_size, PE_FILE_ALIGNMENT);
        idata_virtual_size = align_up(idata_raw_size, PE_SECTION_ALIGNMENT);
        idata_file_off     = PE_SIZE_OF_HEADERS + text_file_size;
    }

    /* ---- Compute overall image and file sizes ------------------------- */
    uint32_t image_size;
    if (has_imports) {
        image_size = idata_rva + idata_virtual_size;
    } else {
        image_size = PE_TEXT_RVA + text_virtual_size;
    }
    uint32_t total_file_size = PE_SIZE_OF_HEADERS + text_file_size
                             + (has_imports ? idata_file_size : 0);

    /* ---- Optional header size (always 16 data directories) ------------ */
    uint32_t num_data_dirs  = 16;
    uint32_t opt_hdr_size   = 24 + 88 + num_data_dirs * 8;   /* = 240 */
    uint32_t sect_hdr_off   = 0x58 + opt_hdr_size;           /* = 0x148 */

    fprintf(stderr, "[PE] .text raw size  : %u bytes\n", text_raw_size);
    if (has_imports) {
        fprintf(stderr, "[PE] .idata raw size : %u bytes\n", idata_raw_size);
        fprintf(stderr, "[PE] IAT offset      : %u (RVA 0x%X)\n",
                code->pe_iat_offset,
                PE_TEXT_RVA + (uint32_t)code->pe_iat_offset);
    }
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
    dos[0] = 'M';
    dos[1] = 'Z';
    write_le32(dos + 60, 0x40);                /* e_lfanew -> PE hdr */

    /* ====================================================================
     *  PE Signature  (4 bytes at offset 0x0040)
     * ==================================================================== */
    uint8_t *pe_sig = img + 0x40;
    pe_sig[0] = 'P';  pe_sig[1] = 'E';
    pe_sig[2] = 0;    pe_sig[3] = 0;

    /* ====================================================================
     *  IMAGE_FILE_HEADER  (20 bytes at offset 0x0044)
     * ==================================================================== */
    uint8_t *fh = img + 0x44;
    write_le16(fh +  0, 0x8664);               /* Machine: AMD64        */
    write_le16(fh +  2, (uint16_t)num_sections);
    write_le32(fh +  4, 0);                    /* TimeDateStamp         */
    write_le32(fh +  8, 0);                    /* PointerToSymbolTable  */
    write_le32(fh + 12, 0);                    /* NumberOfSymbols       */
    write_le16(fh + 16, (uint16_t)opt_hdr_size);
    write_le16(fh + 18, 0x0022);               /* EXECUTABLE | LARGE_ADDRESS */

    /* ====================================================================
     *  IMAGE_OPTIONAL_HEADER64  (240 bytes at offset 0x0058)
     *
     *  PE32+ with 16 data directory entries.
     * ==================================================================== */
    uint8_t *oh = img + 0x58;

    /* Standard fields (24 bytes) */
    write_le16(oh +  0, 0x020B);               /* Magic: PE32+ (64-bit) */
    oh[2] = 1;  oh[3] = 0;
    write_le32(oh +  4, text_file_size);       /* SizeOfCode            */
    write_le32(oh +  8, has_imports ? idata_file_size : 0); /* SizeOfInitializedData */
    write_le32(oh + 12, 0);                    /* SizeOfUninitializedData */
    write_le32(oh + 16, PE_TEXT_RVA);          /* AddressOfEntryPoint   */
    write_le32(oh + 20, PE_TEXT_RVA);          /* BaseOfCode            */

    /* Windows-specific fields (88 bytes) */
    write_le64(oh + 24, PE_IMAGE_BASE);
    write_le32(oh + 32, PE_SECTION_ALIGNMENT);
    write_le32(oh + 36, PE_FILE_ALIGNMENT);
    write_le16(oh + 40, 6);  write_le16(oh + 42, 0);   /* OS version 6.0 */
    write_le16(oh + 44, 0);  write_le16(oh + 46, 0);   /* Image version  */
    write_le16(oh + 48, 6);  write_le16(oh + 50, 0);   /* Subsystem ver  */
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
    write_le32(oh +108, num_data_dirs);        /* NumberOfRvaAndSizes   */

    /* Data directories (16 × 8 = 128 bytes at oh+112) ------------------- */
    uint8_t *dd = oh + 112;
    /* By default all zeros.  Fill in Import and IAT if we have imports. */
    if (has_imports) {
        /* DataDirectory[1] — Import Table */
        write_le32(dd + 1*8,     idata_rva + IDATA_IDT_OFF);   /* RVA  */
        write_le32(dd + 1*8 + 4, 40);                          /* Size = 2 entries × 20 */

        /* DataDirectory[12] — Import Address Table */
        uint32_t iat_rva = PE_TEXT_RVA + (uint32_t)code->pe_iat_offset;
        write_le32(dd + 12*8,     iat_rva);                     /* RVA  */
        write_le32(dd + 12*8 + 4, (uint32_t)code->pe_iat_count * 8); /* Size */
    }

    /* ====================================================================
     *  Section Headers  (at offset 0x0148)
     * ==================================================================== */

    /* ---- .text -------------------------------------------------------- */
    uint8_t *sh0 = img + sect_hdr_off;
    memcpy(sh0, ".text\0\0\0", 8);
    write_le32(sh0 +  8, text_raw_size);       /* VirtualSize           */
    write_le32(sh0 + 12, PE_TEXT_RVA);         /* VirtualAddress        */
    write_le32(sh0 + 16, text_file_size);      /* SizeOfRawData         */
    write_le32(sh0 + 20, PE_SIZE_OF_HEADERS);  /* PointerToRawData      */
    write_le32(sh0 + 24, 0);
    write_le32(sh0 + 28, 0);
    write_le16(sh0 + 32, 0);
    write_le16(sh0 + 34, 0);
    /* Characteristics: CODE | EXECUTE | READ (+ WRITE if IAT in .text) */
    write_le32(sh0 + 36, has_imports ? 0xE0000020u : 0x60000020u);

    /* ---- .idata (only when we have imports) --------------------------- */
    if (has_imports) {
        uint8_t *sh1 = img + sect_hdr_off + 40;
        memcpy(sh1, ".idata\0\0", 8);
        write_le32(sh1 +  8, idata_raw_size);
        write_le32(sh1 + 12, idata_rva);
        write_le32(sh1 + 16, idata_file_size);
        write_le32(sh1 + 20, idata_file_off);
        write_le32(sh1 + 24, 0);
        write_le32(sh1 + 28, 0);
        write_le16(sh1 + 32, 0);
        write_le16(sh1 + 34, 0);
        write_le32(sh1 + 36, 0xC0000040u);   /* INITIALIZED_DATA | READ | WRITE */
    }

    /* ====================================================================
     *  .text Section Data  (at file offset PE_SIZE_OF_HEADERS)
     * ==================================================================== */
    memcpy(img + PE_SIZE_OF_HEADERS, code->bytes, text_raw_size);

    /* ====================================================================
     *  .idata Section Data + IAT pre-fill  (only when imports present)
     * ==================================================================== */
    if (has_imports) {
        uint8_t *id = img + idata_file_off;

        /* ---- HintName entries ----------------------------------------- */
        /* GetStdHandle: hint=0, name="GetStdHandle\0" */
        write_le16(id + IDATA_HN0_OFF, 0);
        memcpy(id + IDATA_HN0_OFF + 2, "GetStdHandle", 13);

        /* WriteFile: hint=0, name="WriteFile\0" */
        write_le16(id + IDATA_HN1_OFF, 0);
        memcpy(id + IDATA_HN1_OFF + 2, "WriteFile", 10);

        /* ExitProcess: hint=0, name="ExitProcess\0" */
        write_le16(id + IDATA_HN2_OFF, 0);
        memcpy(id + IDATA_HN2_OFF + 2, "ExitProcess", 12);

        /* DLL name */
        memcpy(id + IDATA_DLL_OFF, "kernel32.dll", 13);

        /* ---- Import Lookup Table (ILT) -------------------------------- */
        write_le64(id + IDATA_ILT_OFF + 0,  (uint64_t)(idata_rva + IDATA_HN0_OFF));
        write_le64(id + IDATA_ILT_OFF + 8,  (uint64_t)(idata_rva + IDATA_HN1_OFF));
        write_le64(id + IDATA_ILT_OFF + 16, (uint64_t)(idata_rva + IDATA_HN2_OFF));
        write_le64(id + IDATA_ILT_OFF + 24, 0);  /* null terminator */

        /* ---- Import Directory Table (IDT) ----------------------------- */
        uint8_t *idt = id + IDATA_IDT_OFF;
        write_le32(idt + 0,  idata_rva + IDATA_ILT_OFF);   /* OriginalFirstThunk (→ ILT) */
        write_le32(idt + 4,  0);                            /* TimeDateStamp */
        write_le32(idt + 8,  0);                            /* ForwarderChain */
        write_le32(idt + 12, idata_rva + IDATA_DLL_OFF);   /* Name (→ "kernel32.dll") */
        write_le32(idt + 16, PE_TEXT_RVA + (uint32_t)code->pe_iat_offset); /* FirstThunk (→ IAT in .text) */
        /* IDT[1] is already zeros (null terminator) */

        /* ---- Pre-fill IAT in .text with same RVAs as ILT -------------- */
        /* The Windows loader will overwrite these with actual addresses,
         * but on disk they must contain the same import lookup values. */
        uint8_t *iat = img + PE_SIZE_OF_HEADERS + code->pe_iat_offset;
        write_le64(iat + 0,  (uint64_t)(idata_rva + IDATA_HN0_OFF));
        write_le64(iat + 8,  (uint64_t)(idata_rva + IDATA_HN1_OFF));
        write_le64(iat + 16, (uint64_t)(idata_rva + IDATA_HN2_OFF));
        write_le64(iat + 24, 0);  /* null terminator */
    }

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
