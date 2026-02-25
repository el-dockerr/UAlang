/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Phase 5: CLI, File I/O, x86-64 Backend & JIT Execution
 *
 *  File:    main.c
 *  Purpose: CLI compiler driver with JIT support.
 *
 *   Usage:  uas <input.uas> -arch <arch> [-o output] [-sys system] [--run]
 *
 *   -o      Output file path      (default: a.out)
 *   -arch   Target architecture   (mcs51 | x86)        [mandatory]
 *   -sys    Target OS / system    (baremetal | win32)   [stored]
 *   --run   JIT-execute the code  (x86 only, skips .bin write)
 *
 *  Pipeline:
 *   Parse Args -> Read File -> Lexer -> Parser
 *      -> Backend (arch-specific) -> Write .bin  OR  JIT execute -> Cleanup
 *
 *  Build:  gcc -std=c99 -Wall -Wextra -pedantic -o uas.exe \
 *              main.c lexer.c parser.c codegen.c backend_8051.c \
 *              backend_x86_64.c emitter_pe.c
 *
 *  License: MIT
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "backend_8051.h"
#include "backend_x86_64.h"
#include "emitter_pe.h"

/* =========================================================================
 *  Platform-specific JIT headers
 * ========================================================================= */
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
#endif

/* =========================================================================
 *  Configuration – populated by argument parsing
 * ========================================================================= */
typedef struct {
    const char *input_file;     /* Path to .uas source file (mandatory)   */
    const char *output_file;    /* Path to output binary    (default a.out) */
    const char *arch;           /* Target architecture      (mandatory)   */
    const char *sys;            /* Target OS / system       (optional)    */
    int         run;            /* 1 = JIT execute, 0 = write .bin        */
} Config;

/* =========================================================================
 *  usage()  –  print help text to stderr and exit
 * ========================================================================= */
static void usage(const char *progname)
{
    fprintf(stderr,
        "UAS - Unified Assembler System\n\n"
        "Usage:\n"
        "  %s <input.uas> -arch <architecture> [-o <output>] [-sys <system>] [--run]\n\n"
        "Required:\n"
        "  <input.uas>       Path to the UAS source file\n"
        "  -arch <arch>      Target architecture: mcs51, x86\n\n"
        "Optional:\n"
        "  -o <output>       Output file path (default: a.out)\n"
        "  -sys <system>     Target system:  baremetal, win32\n"
        "  --run             JIT-execute the generated code (x86 only)\n\n"
        "Example:\n"
        "  %s program.uas -arch x86 --run\n"
        "  %s program.uas -arch mcs51 -o program.bin\n",
        progname, progname, progname);
    exit(EXIT_FAILURE);
}

/* =========================================================================
 *  parse_args()  –  parse argc/argv into a Config struct
 *
 *  Returns 0 on success, prints diagnostics to stderr and calls usage()
 *  on failure (which does not return).
 * ========================================================================= */
static int parse_args(int argc, char *argv[], Config *cfg)
{
    /* Defaults */
    cfg->input_file  = NULL;
    cfg->output_file = "a.out";
    cfg->arch        = NULL;
    cfg->sys         = NULL;
    cfg->run         = 0;

    if (argc < 2) {
        usage(argv[0]);
    }

    int i = 1;
    while (i < argc) {
        /* ---- flags ---------------------------------------------------- */
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires a file path.\n");
                usage(argv[0]);
            }
            cfg->output_file = argv[++i];
        }
        else if (strcmp(argv[i], "-arch") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -arch requires an architecture name.\n");
                usage(argv[0]);
            }
            cfg->arch = argv[++i];
        }
        else if (strcmp(argv[i], "-sys") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -sys requires a system name.\n");
                usage(argv[0]);
            }
            cfg->sys = argv[++i];
        }
        else if (strcmp(argv[i], "--run") == 0) {
            cfg->run = 1;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'.\n", argv[i]);
            usage(argv[0]);
        }
        /* ---- positional: input file ----------------------------------- */
        else {
            if (cfg->input_file != NULL) {
                fprintf(stderr,
                    "Error: multiple input files specified ('%s' and '%s').\n",
                    cfg->input_file, argv[i]);
                usage(argv[0]);
            }
            cfg->input_file = argv[i];
        }
        i++;
    }

    /* ---- validate mandatory fields ------------------------------------ */
    if (cfg->input_file == NULL) {
        fprintf(stderr, "Error: no input file specified.\n");
        usage(argv[0]);
    }
    if (cfg->arch == NULL) {
        fprintf(stderr, "Error: -arch is required.\n");
        usage(argv[0]);
    }

    return 0;
}

/* =========================================================================
 *  read_file()  –  read an entire file into a heap-allocated string
 *
 *  Returns a null-terminated string on success.
 *  Returns NULL on failure (diagnostic printed to stderr).
 *  The caller must free() the returned pointer.
 * ========================================================================= */
static char* read_file(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s': ", filename);
        perror(NULL);
        return NULL;
    }

    /* Determine file size */
    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: fseek failed on '%s'.\n", filename);
        fclose(fp);
        return NULL;
    }
    long length = ftell(fp);
    if (length < 0) {
        fprintf(stderr, "Error: ftell failed on '%s'.\n", filename);
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    /* Allocate buffer (+1 for null terminator) */
    char *buffer = (char *)malloc((size_t)length + 1);
    if (!buffer) {
        fprintf(stderr, "Error: out of memory reading '%s'.\n", filename);
        fclose(fp);
        return NULL;
    }

    /* Read entire file */
    size_t read_count = fread(buffer, 1, (size_t)length, fp);
    if ((long)read_count != length) {
        fprintf(stderr, "Error: short read on '%s' (got %zu of %ld bytes).\n",
                filename, read_count, length);
        free(buffer);
        fclose(fp);
        return NULL;
    }

    buffer[length] = '\0';
    fclose(fp);
    return buffer;
}

/* =========================================================================
 *  write_binary()  –  write raw bytes to a file in binary mode
 *
 *  Returns 0 on success, non-zero on failure.
 * ========================================================================= */
static int write_binary(const char *filename, const uint8_t *data, int size)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s' for writing: ", filename);
        perror(NULL);
        return 1;
    }

    size_t written = fwrite(data, 1, (size_t)size, fp);
    if ((int)written != size) {
        fprintf(stderr, "Error: short write to '%s' (%zu of %d bytes).\n",
                filename, written, size);
        fclose(fp);
        return 1;
    }

    fclose(fp);
    return 0;
}

/* =========================================================================
 *  str_casecmp_portable()  –  case-insensitive string compare (ANSI C)
 * ========================================================================= */
static int str_casecmp_portable(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* =========================================================================
 *  JIT Execution  –  allocate RWX memory, copy code, call as function
 *
 *  The generated x86-64 code ends with RET (C3), so calling the buffer
 *  as a void->int64 function is safe.  RAX holds the return value.
 *
 *  Platform:
 *    Windows — VirtualAlloc  (PAGE_EXECUTE_READWRITE)
 *    POSIX   — mmap          (PROT_READ | PROT_WRITE | PROT_EXEC)
 * ========================================================================= */
typedef int64_t (*JitFunc)(void);

static int execute_jit(const CodeBuffer *code)
{
    if (!code || code->size == 0) {
        fprintf(stderr, "Error: no code to execute.\n");
        return 1;
    }

#ifdef _WIN32
    /* ---------- Windows ------------------------------------------------ */
    void *exec_mem = VirtualAlloc(
        NULL,
        (SIZE_T)code->size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);

    if (!exec_mem) {
        fprintf(stderr, "Error: VirtualAlloc failed (err %lu).\n",
                GetLastError());
        return 1;
    }

    memcpy(exec_mem, code->bytes, (size_t)code->size);

    fprintf(stderr,
        "\n  ┌──────────────────────────────────────┐\n"
        "  │  JIT: Entering generated x86-64 code │\n"
        "  └──────────────────────────────────────┘\n\n");

    /* memcpy avoids ISO C object->function-pointer cast warning */
    JitFunc func;
    memcpy(&func, &exec_mem, sizeof(func));
    int64_t result = func();

    fprintf(stderr,
        "\n  ┌──────────────────────────────────────┐\n"
        "  │  JIT: Returned from generated code   │\n"
        "  └──────────────────────────────────────┘\n");
    fprintf(stderr, "  RAX (R0) = %lld  (0x%llX)\n\n",
            (long long)result, (unsigned long long)result);

    VirtualFree(exec_mem, 0, MEM_RELEASE);

#else
    /* ---------- POSIX (Linux / macOS) ---------------------------------- */
    void *exec_mem = mmap(
        NULL,
        (size_t)code->size,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0);

    if (exec_mem == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    memcpy(exec_mem, code->bytes, (size_t)code->size);

    fprintf(stderr,
        "\n  ┌──────────────────────────────────────┐\n"
        "  │  JIT: Entering generated x86-64 code │\n"
        "  └──────────────────────────────────────┘\n\n");

    /* memcpy avoids ISO C object->function-pointer cast warning */
    JitFunc func;
    memcpy(&func, &exec_mem, sizeof(func));
    int64_t result = func();

    fprintf(stderr,
        "\n  ┌──────────────────────────────────────┐\n"
        "  │  JIT: Returned from generated code   │\n"
        "  └──────────────────────────────────────┘\n");
    fprintf(stderr, "  RAX (R0) = %lld  (0x%llX)\n\n",
            (long long)result, (unsigned long long)result);

    munmap(exec_mem, (size_t)code->size);
#endif

    return 0;
}

/* =========================================================================
 *  main()
 * ========================================================================= */
int main(int argc, char *argv[])
{
    /* --- 1. Parse command-line arguments ------------------------------- */
    Config cfg;
    parse_args(argc, argv, &cfg);

    fprintf(stderr, "UAS - Unified Assembler System\n");
    fprintf(stderr, "  Input  : %s\n", cfg.input_file);
    fprintf(stderr, "  Output : %s\n", cfg.output_file);
    fprintf(stderr, "  Arch   : %s\n", cfg.arch);
    if (cfg.sys)
        fprintf(stderr, "  System : %s\n", cfg.sys);
    if (cfg.run)
        fprintf(stderr, "  Mode   : JIT execute\n");
    fprintf(stderr, "\n");

    /* --- 2. Read source file ------------------------------------------- */
    char *source = read_file(cfg.input_file);
    if (!source) {
        return EXIT_FAILURE;
    }

    /* --- 3. Lexer ------------------------------------------------------ */
    int token_count = 0;
    Token *tokens = tokenize(source, &token_count);
    if (!tokens) {
        fprintf(stderr, "Error: tokenization failed.\n");
        free(source);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[Lexer]  %d tokens\n", token_count);

    /* --- 4. Parser ----------------------------------------------------- */
    int ir_count = 0;
    Instruction *ir = parse(tokens, token_count, &ir_count);
    if (!ir) {
        fprintf(stderr, "Error: parsing failed.\n");
        free(tokens);
        free(source);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[Parser] %d IR instructions\n", ir_count);

    /* --- 5. Backend (architecture-specific code generation) ------------- */
    int rc = EXIT_SUCCESS;

    if (str_casecmp_portable(cfg.arch, "mcs51") == 0) {
        /* ---- MCS-51 / 8051 backend ------------------------------------ */
        if (cfg.run) {
            fprintf(stderr, "Error: --run is only supported for -arch x86.\n");
            rc = EXIT_FAILURE;
        } else {
            CodeBuffer *code = generate_8051(ir, ir_count);
            if (!code) {
                fprintf(stderr, "Error: 8051 code generation failed.\n");
                rc = EXIT_FAILURE;
            } else {
                fprintf(stderr, "\n");
                hexdump(code->bytes, code->size);

                if (write_binary(cfg.output_file, code->bytes, code->size) != 0) {
                    rc = EXIT_FAILURE;
                } else {
                    fprintf(stderr, "\nWrote %d bytes to %s\n",
                            code->size, cfg.output_file);
                }
                free_code_buffer(code);
            }
        }
    }
    else if (str_casecmp_portable(cfg.arch, "x86") == 0) {
        /* ---- x86-64 backend ------------------------------------------- */
        CodeBuffer *code = generate_x86_64(ir, ir_count);
        if (!code) {
            fprintf(stderr, "Error: x86-64 code generation failed.\n");
            rc = EXIT_FAILURE;
        } else {
            fprintf(stderr, "\n");
            hexdump(code->bytes, code->size);

            if (cfg.run) {
                /* JIT execute */
                if (execute_jit(code) != 0) {
                    rc = EXIT_FAILURE;
                }
            }
            else if (cfg.sys != NULL &&
                     str_casecmp_portable(cfg.sys, "win32") == 0) {
                /* Emit PE executable */
                const char *pe_out = cfg.output_file;
                if (strcmp(pe_out, "a.out") == 0) {
                    pe_out = "a.exe";
                }
                if (emit_pe_exe(pe_out, code) != 0) {
                    rc = EXIT_FAILURE;
                }
            }
            else {
                /* Write raw binary */
                if (write_binary(cfg.output_file, code->bytes, code->size) != 0) {
                    rc = EXIT_FAILURE;
                } else {
                    fprintf(stderr, "\nWrote %d bytes to %s\n",
                            code->size, cfg.output_file);
                }
            }
            free_code_buffer(code);
        }
    }
    else {
        fprintf(stderr, "Error: unknown architecture '%s'.\n", cfg.arch);
        fprintf(stderr, "Supported architectures: mcs51, x86\n");
        rc = EXIT_FAILURE;
    }

    /* --- 6. Cleanup ---------------------------------------------------- */
    free_instructions(ir);
    free(tokens);
    free(source);

    return rc;
}
