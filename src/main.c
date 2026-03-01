/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 5: CLI, File I/O, Multi-Backend & JIT Execution
 *
 *  File:    main.c
 *  Purpose: CLI compiler driver with JIT support.
 *
 *   Usage:  ua <input.ua> -arch <arch> [-o output] [-sys system] [--run]
 *
 *   -o      Output file path      (default: a.out)
 *   -arch   Target architecture   (mcs51 | x86 | x86_32 | arm | arm64 | riscv) [mandatory]
 *   -sys    Target OS / system    (baremetal | win32 | linux | macos)            [stored]
 *   --run   JIT-execute the code  (x86 only, skips .bin write)
 *
 *  Pipeline:
 *   Parse Args -> Read File -> Precompiler -> Lexer -> Parser
 *      -> Backend (arch-specific) -> Write .bin  OR  JIT execute -> Cleanup
 *
 *  Build:  gcc -std=c99 -Wall -Wextra -pedantic -o ua.exe \
 *              main.c lexer.c parser.c codegen.c precompiler.c \
 *              backend_8051.c backend_x86_64.c backend_x86_32.c \
 *              backend_arm.c backend_arm64.c backend_risc_v.c \
 *              emitter_pe.c emitter_elf.c emitter_macho.c
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
#include "backend_x86_32.h"
#include "backend_arm.h"
#include "backend_arm64.h"
#include "backend_risc_v.h"
#include "emitter_pe.h"
#include "emitter_elf.h"
#include "emitter_macho.h"
#include "precompiler.h"

#define UA_VERSION "26.0.2-ALPHA"

/* Forward declaration — used by compliance tables before full definition */
static int str_casecmp_portable(const char *a, const char *b);

/* =========================================================================
 *  Opcode Compliance — Architecture / System validation tables
 *
 *  Each opcode carries two bitmasks:
 *    arch_mask  —  which architectures support the instruction
 *    sys_mask   —  which systems/OS targets support the instruction
 *
 *  If an instruction has  arch_mask == UA_AALL  and  sys_mask == UA_SALL,
 *  it is universal and accepted everywhere.  When new architecture-specific
 *  opcodes are added, restrict the masks accordingly.
 *
 *  The validation pass runs after parsing, before backend dispatch.
 * ========================================================================= */

/* Architecture bitmask flags */
#define UA_AX86     0x01u   /* x86-64                  */
#define UA_AX86_32  0x02u   /* x86-32 (IA-32)          */
#define UA_AARM     0x04u   /* ARM (ARMv7-A)           */
#define UA_AARM64   0x08u   /* ARM64 (AArch64)         */
#define UA_ARISCV   0x10u   /* RISC-V (RV64I+M)        */
#define UA_AMCS51   0x20u   /* 8051 / MCS-51           */
#define UA_AALL     0x3Fu   /* All architectures       */

/* System/OS bitmask flags */
#define UA_SBARE    0x01u   /* baremetal (no OS)       */
#define UA_SWIN32   0x02u   /* Windows (win32)         */
#define UA_SLINUX   0x04u   /* Linux                   */
#define UA_SMACOS   0x08u   /* macOS                   */
#define UA_SALL     0x0Fu   /* All systems             */

typedef struct {
    unsigned int arch_mask;     /* supported architectures                   */
    unsigned int sys_mask;      /* supported systems                         */
} OpcodeCompliance;

/*
 * Compliance table — indexed by Opcode enum.
 *
 * All 37 existing opcodes are universal (UA_AALL, UA_SALL).
 * When architecture-specific instructions are introduced, restrict the
 * masks here.  The SYS opcode is restricted to non-baremetal systems
 * on architectures that require an OS for syscall support.
 *
 * NOTE: OP_SYS uses native syscall instructions (SYSCALL, SVC, ECALL)
 *       which are valid even on baremetal firmware (calling into firmware
 *       supervisor mode), so it is kept universal.
 */
static const OpcodeCompliance OPCODE_COMPLIANCE[OP_COUNT] = {
    /* Data movement                                                        */
    [OP_MOV]    = { UA_AALL,  UA_SALL  },
    [OP_LDI]    = { UA_AALL,  UA_SALL  },
    [OP_LOAD]   = { UA_AALL,  UA_SALL  },
    [OP_STORE]  = { UA_AALL,  UA_SALL  },

    /* Arithmetic                                                           */
    [OP_ADD]    = { UA_AALL,  UA_SALL  },
    [OP_SUB]    = { UA_AALL,  UA_SALL  },
    [OP_MUL]    = { UA_AALL,  UA_SALL  },
    [OP_DIV]    = { UA_AALL,  UA_SALL  },

    /* Bitwise                                                              */
    [OP_AND]    = { UA_AALL,  UA_SALL  },
    [OP_OR]     = { UA_AALL,  UA_SALL  },
    [OP_XOR]    = { UA_AALL,  UA_SALL  },
    [OP_NOT]    = { UA_AALL,  UA_SALL  },
    [OP_SHL]    = { UA_AALL,  UA_SALL  },
    [OP_SHR]    = { UA_AALL,  UA_SALL  },

    /* Comparison / control flow                                            */
    [OP_CMP]    = { UA_AALL,  UA_SALL  },
    [OP_JMP]    = { UA_AALL,  UA_SALL  },
    [OP_JZ]     = { UA_AALL,  UA_SALL  },
    [OP_JNZ]    = { UA_AALL,  UA_SALL  },
    [OP_JL]     = { UA_AALL,  UA_SALL  },
    [OP_JG]     = { UA_AALL,  UA_SALL  },
    [OP_CALL]   = { UA_AALL,  UA_SALL  },
    [OP_RET]    = { UA_AALL,  UA_SALL  },

    /* Stack                                                                */
    [OP_PUSH]   = { UA_AALL,  UA_SALL  },
    [OP_POP]    = { UA_AALL,  UA_SALL  },

    /* Increment / Decrement                                                */
    [OP_INC]    = { UA_AALL,  UA_SALL  },
    [OP_DEC]    = { UA_AALL,  UA_SALL  },

    /* Software Interrupt                                                   */
    [OP_INT]    = { UA_AALL,  UA_SALL  },

    /* Variables                                                            */
    [OP_VAR]    = { UA_AALL,  UA_SALL  },
    [OP_SET]    = { UA_AALL,  UA_SALL  },
    [OP_GET]    = { UA_AALL,  UA_SALL  },

    /* String / Byte / Syscall                                              */
    [OP_LDS]    = { UA_AALL,  UA_SALL  },
    [OP_LOADB]  = { UA_AALL,  UA_SALL  },
    [OP_STOREB] = { UA_AALL,  UA_SALL  },
    [OP_SYS]    = { UA_AALL,  UA_SALL  },

    /* Buffer allocation                                                    */
    [OP_BUFFER] = { UA_AALL,  UA_SALL  },

    /* Miscellaneous                                                        */
    [OP_NOP]    = { UA_AALL,  UA_SALL  },
    [OP_HLT]    = { UA_AALL,  UA_SALL  },

    /* --- Architecture-specific opcodes ---------------------------------- */
    /* x86 family (x86_32 & x86_64) */
    [OP_CPUID]  = { UA_AX86 | UA_AX86_32,            UA_SALL  },
    [OP_RDTSC]  = { UA_AX86 | UA_AX86_32,            UA_SALL  },
    [OP_BSWAP]  = { UA_AX86 | UA_AX86_32,            UA_SALL  },
    [OP_PUSHA]  = { UA_AX86_32,                       UA_SALL  },
    [OP_POPA]   = { UA_AX86_32,                       UA_SALL  },

    /* 8051 exclusive */
    [OP_DJNZ]   = { UA_AMCS51,                        UA_SALL  },
    [OP_CJNE]   = { UA_AMCS51,                        UA_SALL  },
    [OP_SETB]   = { UA_AMCS51,                        UA_SALL  },
    [OP_CLR]    = { UA_AMCS51,                        UA_SALL  },
    [OP_RETI]   = { UA_AMCS51,                        UA_SALL  },

    /* ARM & ARM64 + RISC-V for WFI */
    [OP_WFI]    = { UA_AARM | UA_AARM64 | UA_ARISCV,  UA_SALL  },
    [OP_DMB]    = { UA_AARM | UA_AARM64,              UA_SALL  },

    /* RISC-V exclusive */
    [OP_EBREAK] = { UA_ARISCV,                        UA_SALL  },
    [OP_FENCE]  = { UA_ARISCV,                        UA_SALL  },

    /* Assembler directives — universal */
    [OP_ORG]    = { UA_AALL,                           UA_SALL  },
};

/* -------------------------------------------------------------------------
 *  resolve_arch_mask()  —  map an architecture string to its bitmask
 * --------------------------------------------------------------------- */
static unsigned int resolve_arch_mask(const char *arch)
{
    if (str_casecmp_portable(arch, "x86") == 0)        return UA_AX86;
    if (str_casecmp_portable(arch, "x86_32") == 0 ||
        str_casecmp_portable(arch, "ia32") == 0)       return UA_AX86_32;
    if (str_casecmp_portable(arch, "arm") == 0)         return UA_AARM;
    if (str_casecmp_portable(arch, "arm64") == 0 ||
        str_casecmp_portable(arch, "aarch64") == 0)     return UA_AARM64;
    if (str_casecmp_portable(arch, "riscv") == 0 ||
        str_casecmp_portable(arch, "rv64") == 0)        return UA_ARISCV;
    if (str_casecmp_portable(arch, "mcs51") == 0)       return UA_AMCS51;
    return 0;
}

/* -------------------------------------------------------------------------
 *  resolve_sys_mask()  —  map a system string to its bitmask
 *  Returns UA_SBARE when sys is NULL (baremetal target).
 * --------------------------------------------------------------------- */
static unsigned int resolve_sys_mask(const char *sys)
{
    if (!sys)                                           return UA_SBARE;
    if (str_casecmp_portable(sys, "baremetal") == 0)    return UA_SBARE;
    if (str_casecmp_portable(sys, "win32") == 0)        return UA_SWIN32;
    if (str_casecmp_portable(sys, "linux") == 0)        return UA_SLINUX;
    if (str_casecmp_portable(sys, "macos") == 0 ||
        str_casecmp_portable(sys, "darwin") == 0)       return UA_SMACOS;
    return 0;
}

/* -------------------------------------------------------------------------
 *  arch_name_from_mask()  —  human-readable list of supported archs
 * --------------------------------------------------------------------- */
static void arch_names_from_mask(unsigned int mask, char *buf, int size)
{
    buf[0] = '\0';
    int pos = 0;
    struct { unsigned int bit; const char *name; } map[] = {
        { UA_AX86,    "x86"    },
        { UA_AX86_32, "x86_32" },
        { UA_AARM,    "arm"    },
        { UA_AARM64,  "arm64"  },
        { UA_ARISCV,  "riscv"  },
        { UA_AMCS51,  "mcs51"  },
    };
    for (int i = 0; i < 6; i++) {
        if (mask & map[i].bit) {
            if (pos > 0 && pos < size - 2) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
            int nlen = (int)strlen(map[i].name);
            if (pos + nlen < size - 1) {
                memcpy(buf + pos, map[i].name, (size_t)nlen);
                pos += nlen;
            }
        }
    }
    buf[pos] = '\0';
}

/* -------------------------------------------------------------------------
 *  sys_names_from_mask()  —  human-readable list of supported systems
 * --------------------------------------------------------------------- */
static void sys_names_from_mask(unsigned int mask, char *buf, int size)
{
    buf[0] = '\0';
    int pos = 0;
    struct { unsigned int bit; const char *name; } map[] = {
        { UA_SBARE,  "baremetal" },
        { UA_SWIN32, "win32"     },
        { UA_SLINUX, "linux"     },
        { UA_SMACOS, "macos"     },
    };
    for (int i = 0; i < 4; i++) {
        if (mask & map[i].bit) {
            if (pos > 0 && pos < size - 2) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
            int nlen = (int)strlen(map[i].name);
            if (pos + nlen < size - 1) {
                memcpy(buf + pos, map[i].name, (size_t)nlen);
                pos += nlen;
            }
        }
    }
    buf[pos] = '\0';
}

/* -------------------------------------------------------------------------
 *  validate_opcode_compliance()
 *
 *  Walk every IR instruction and verify that its opcode is supported by
 *  the target architecture and system.  Returns 0 on success, -1 on
 *  failure (diagnostics printed to stderr).
 * --------------------------------------------------------------------- */
static int validate_opcode_compliance(const Instruction *ir, int ir_count,
                                      const char *arch, const char *sys)
{
    unsigned int arch_bit = resolve_arch_mask(arch);
    unsigned int sys_bit  = resolve_sys_mask(sys);
    int errors = 0;

    for (int i = 0; i < ir_count; i++) {
        if (ir[i].is_label) continue;   /* labels have no opcode */

        Opcode op = ir[i].opcode;
        if (op < 0 || op >= OP_COUNT) continue;  /* safety guard */

        const OpcodeCompliance *c = &OPCODE_COMPLIANCE[op];

        /* Check architecture support */
        if (arch_bit != 0 && !(c->arch_mask & arch_bit)) {
            char supported[128];
            arch_names_from_mask(c->arch_mask, supported,
                                (int)sizeof(supported));
            fprintf(stderr,
                    "\n  UA Compliance Error\n"
                    "  -------------------\n"
                    "  Line %d: opcode '%s' is not supported on "
                    "architecture '%s'\n"
                    "  Supported architectures: %s\n\n",
                    ir[i].line, opcode_name(op), arch, supported);
            errors++;
        }

        /* Check system support */
        if (sys_bit != 0 && !(c->sys_mask & sys_bit)) {
            char supported[128];
            sys_names_from_mask(c->sys_mask, supported,
                               (int)sizeof(supported));
            fprintf(stderr,
                    "\n  UA Compliance Error\n"
                    "  -------------------\n"
                    "  Line %d: opcode '%s' is not supported on "
                    "system '%s'\n"
                    "  Supported systems: %s\n\n",
                    ir[i].line, opcode_name(op),
                    sys ? sys : "baremetal", supported);
            errors++;
        }
    }

    if (errors > 0) {
        fprintf(stderr, "[Compliance] %d opcode violation(s) found.\n",
                errors);
        return -1;
    }

    return 0;
}

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
    const char *input_file;     /* Path to .ua source file (mandatory)   */
    const char *output_file;    /* Path to output binary    (default a.out) */
    const char *arch;           /* Target architecture      (mandatory)   */
    const char *sys;            /* Target OS / system       (optional)    */
    int         run;            /* 1 = JIT execute, 0 = write .bin        */
    char        exe_dir[1024];  /* Directory of compiler executable       */
} Config;

/* =========================================================================
 *  usage()  –  print help text to stderr and exit
 * ========================================================================= */
static void usage(const char *progname)
{
    fprintf(stderr,
        "UA - Unified Assembler\n\n"
        "Usage:\n"
        "  %s <input.ua> -arch <architecture> [-o <output>] [-sys <system>] [--run]\n\n"
        "Required:\n"
        "  <input.ua>       Path to the UA source file\n"
        "  -arch <arch>      Target architecture: mcs51, x86, x86_32, arm, arm64, riscv\n\n"
        "Optional:\n"
        "  -o <output>       Output file path (default: a.out)\n"
        "  -sys <system>     Target system:  baremetal, win32, linux, macos\n"
        "  --run             JIT-execute the generated code (x86 only)\n"
        "  -v, --version     Print version information and exit\n\n"
        "Example:\n"
        "  %s program.ua -arch x86 --run\n"
        "  %s program.ua -arch mcs51 -o program.bin\n"
        "  %s program.ua -arch arm64 -sys macos -o program\n"
        "  %s program.ua -arch riscv -sys linux -o program.elf\n",
        progname, progname, progname, progname, progname);
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
    cfg->exe_dir[0]  = '\0';

    if (argc < 2) {
        usage(argv[0]);
    }

    /* Compute exe_dir from argv[0] */
    {
        const char *last_sep = NULL;
        for (const char *p = argv[0]; *p; p++) {
            if (*p == '/' || *p == '\\') last_sep = p;
        }
        if (last_sep) {
            int dlen = (int)(last_sep - argv[0]);
            if (dlen >= (int)sizeof(cfg->exe_dir))
                dlen = (int)sizeof(cfg->exe_dir) - 1;
            memcpy(cfg->exe_dir, argv[0], (size_t)dlen);
            cfg->exe_dir[dlen] = '\0';
        } else {
            cfg->exe_dir[0] = '.';
            cfg->exe_dir[1] = '\0';
        }
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
        else if (strcmp(argv[i], "-v") == 0 ||
                 strcmp(argv[i], "--version") == 0) {
            printf("UA - Unified Assembler v%s\n", UA_VERSION);
            exit(EXIT_SUCCESS);
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

    fprintf(stderr, "UA - Unified Assembler\n");
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

    /* --- 2b. Precompiler ----------------------------------------------- */
    char base_dir[1024];
    {
        const char *last_sep = NULL;
        for (const char *p = cfg.input_file; *p; p++) {
            if (*p == '/' || *p == '\\') last_sep = p;
        }
        if (last_sep) {
            int dlen = (int)(last_sep - cfg.input_file + 1);
            if (dlen >= (int)sizeof(base_dir)) dlen = (int)sizeof(base_dir) - 1;
            memcpy(base_dir, cfg.input_file, (size_t)dlen);
            base_dir[dlen] = '\0';
        } else {
            base_dir[0] = '.'; base_dir[1] = '\0';
        }
    }
    char *preprocessed = preprocess(source, cfg.arch, cfg.sys,
                                    base_dir, cfg.input_file,
                                    cfg.exe_dir);
    if (!preprocessed) {
        fprintf(stderr, "Error: preprocessing failed.\n");
        free(source);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[Precompiler] Done\n");

    /* --- 3. Lexer ------------------------------------------------------ */
    int token_count = 0;
    Token *tokens = tokenize(preprocessed, &token_count);
    if (!tokens) {
        fprintf(stderr, "Error: tokenization failed.\n");
        free(preprocessed);
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
        free(preprocessed);
        free(source);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[Parser] %d IR instructions\n", ir_count);

    /* --- 4b. Opcode compliance validation ------------------------------ */
    if (validate_opcode_compliance(ir, ir_count, cfg.arch, cfg.sys) != 0) {
        fprintf(stderr, "Error: opcode compliance check failed.\n");
        free_instructions(ir);
        free(tokens);
        free(preprocessed);
        free(source);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[Compliance] All opcodes valid for %s", cfg.arch);
    if (cfg.sys) fprintf(stderr, " / %s", cfg.sys);
    fprintf(stderr, "\n");

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
        CodeBuffer *code = generate_x86_64(ir, ir_count, cfg.sys);
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
            else if (cfg.sys != NULL &&
                     str_casecmp_portable(cfg.sys, "linux") == 0) {
                /* Emit ELF executable */
                const char *elf_out = cfg.output_file;
                if (strcmp(elf_out, "a.out") == 0) {
                    elf_out = "a.elf";
                }
                if (emit_elf_exe(elf_out, code) != 0) {
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
    else if (str_casecmp_portable(cfg.arch, "x86_32") == 0 ||
             str_casecmp_portable(cfg.arch, "ia32") == 0) {
        /* ---- x86-32 (IA-32) backend ---------------------------------- */
        if (cfg.run) {
            fprintf(stderr, "Error: --run is only supported for -arch x86.\n");
            rc = EXIT_FAILURE;
        } else {
            CodeBuffer *code = generate_x86_32(ir, ir_count);
            if (!code) {
                fprintf(stderr, "Error: x86-32 code generation failed.\n");
                rc = EXIT_FAILURE;
            } else {
                fprintf(stderr, "\n");
                hexdump(code->bytes, code->size);

                if (cfg.sys != NULL &&
                    str_casecmp_portable(cfg.sys, "win32") == 0) {
                    const char *pe_out = cfg.output_file;
                    if (strcmp(pe_out, "a.out") == 0) {
                        pe_out = "a.exe";
                    }
                    if (emit_pe_exe(pe_out, code) != 0) {
                        rc = EXIT_FAILURE;
                    }
                }
                else if (cfg.sys != NULL &&
                         str_casecmp_portable(cfg.sys, "linux") == 0) {
                    const char *elf_out = cfg.output_file;
                    if (strcmp(elf_out, "a.out") == 0) {
                        elf_out = "a.elf";
                    }
                    if (emit_elf_exe(elf_out, code) != 0) {
                        rc = EXIT_FAILURE;
                    }
                }
                else {
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
    }
    else if (str_casecmp_portable(cfg.arch, "arm") == 0) {
        /* ---- ARM (ARMv7-A) backend ------------------------------------ */
        if (cfg.run) {
            fprintf(stderr, "Error: --run is only supported for -arch x86.\n");
            rc = EXIT_FAILURE;
        } else {
            CodeBuffer *code = generate_arm(ir, ir_count);
            if (!code) {
                fprintf(stderr, "Error: ARM code generation failed.\n");
                rc = EXIT_FAILURE;
            } else {
                fprintf(stderr, "\n");
                hexdump(code->bytes, code->size);

                if (cfg.sys != NULL &&
                    str_casecmp_portable(cfg.sys, "linux") == 0) {
                    const char *elf_out = cfg.output_file;
                    if (strcmp(elf_out, "a.out") == 0) {
                        elf_out = "a.elf";
                    }
                    if (emit_elf_exe(elf_out, code) != 0) {
                        rc = EXIT_FAILURE;
                    }
                }
                else {
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
    }
    else if (str_casecmp_portable(cfg.arch, "arm64") == 0 ||
             str_casecmp_portable(cfg.arch, "aarch64") == 0) {
        /* ---- ARM64 / AArch64 backend --------------------------------- */
        if (cfg.run) {
            fprintf(stderr, "Error: --run is only supported for -arch x86.\n");
            rc = EXIT_FAILURE;
        } else {
            CodeBuffer *code = generate_arm64(ir, ir_count);
            if (!code) {
                fprintf(stderr, "Error: ARM64 code generation failed.\n");
                rc = EXIT_FAILURE;
            } else {
                fprintf(stderr, "\n");
                hexdump(code->bytes, code->size);

                if (cfg.sys != NULL &&
                    (str_casecmp_portable(cfg.sys, "macos") == 0 ||
                     str_casecmp_portable(cfg.sys, "darwin") == 0)) {
                    /* Emit Mach-O executable */
                    const char *macho_out = cfg.output_file;
                    if (strcmp(macho_out, "a.out") == 0) {
                        macho_out = "a.macho";
                    }
                    if (emit_macho_exe(macho_out, code) != 0) {
                        rc = EXIT_FAILURE;
                    }
                }
                else if (cfg.sys != NULL &&
                         str_casecmp_portable(cfg.sys, "linux") == 0) {
                    /* Emit ELF executable */
                    const char *elf_out = cfg.output_file;
                    if (strcmp(elf_out, "a.out") == 0) {
                        elf_out = "a.elf";
                    }
                    if (emit_elf_exe(elf_out, code) != 0) {
                        rc = EXIT_FAILURE;
                    }
                }
                else {
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
    }
    else if (str_casecmp_portable(cfg.arch, "riscv") == 0 ||
             str_casecmp_portable(cfg.arch, "rv64") == 0) {
        /* ---- RISC-V (RV64I+M) backend -------------------------------- */
        if (cfg.run) {
            fprintf(stderr, "Error: --run is only supported for -arch x86.\n");
            rc = EXIT_FAILURE;
        } else {
            CodeBuffer *code = generate_risc_v(ir, ir_count);
            if (!code) {
                fprintf(stderr, "Error: RISC-V code generation failed.\n");
                rc = EXIT_FAILURE;
            } else {
                fprintf(stderr, "\n");
                hexdump(code->bytes, code->size);

                if (cfg.sys != NULL &&
                    str_casecmp_portable(cfg.sys, "linux") == 0) {
                    /* Emit ELF executable */
                    const char *elf_out = cfg.output_file;
                    if (strcmp(elf_out, "a.out") == 0) {
                        elf_out = "a.elf";
                    }
                    if (emit_elf_exe(elf_out, code) != 0) {
                        rc = EXIT_FAILURE;
                    }
                }
                else {
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
    }
    else {
        fprintf(stderr, "Error: unknown architecture '%s'.\n", cfg.arch);
        fprintf(stderr, "Supported architectures: mcs51, x86, x86_32, arm, arm64, riscv\n");
        rc = EXIT_FAILURE;
    }

    /* --- 6. Cleanup ---------------------------------------------------- */
    free_instructions(ir);
    free(tokens);
    free(preprocessed);
    free(source);

    return rc;
}
