#ifndef QEMU_ELF_H
#define QEMU_ELF_H

typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Word;

typedef uint64_t Elf64_Addr;
typedef uint16_t Elf64_Half;
typedef int16_t  Elf64_SHalf;
typedef uint64_t Elf64_Off;
typedef int32_t  Elf64_Sword;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

#define PT_NULL           0
#define PT_LOAD           1
#define PT_DYNAMIC        2
#define PT_INTERP         3
#define PT_NOTE           4
#define PT_SHLIB          5
#define PT_PHDR           6
#define PT_LOOS           0x60000000
#define PT_HIOS           0x6fffffff
#define PT_LOPROC         0x70000000
#define PT_HIPROC         0x7fffffff

#define PT_GNU_STACK      (PT_LOOS + 0x474e551)
#define PT_GNU_PROPERTY   (PT_LOOS + 0x474e553)

#define PT_MIPS_REGINFO   0x70000000
#define PT_MIPS_RTPROC    0x70000001
#define PT_MIPS_OPTIONS   0x70000002
#define PT_MIPS_ABIFLAGS  0x70000003

#define EF_MIPS_ARCH          0xf0000000

#define EF_MIPS_ARCH_1        0x00000000
#define EF_MIPS_ARCH_2        0x10000000
#define EF_MIPS_ARCH_3        0x20000000
#define EF_MIPS_ARCH_4        0x30000000
#define EF_MIPS_ARCH_5        0x40000000
#define EF_MIPS_ARCH_32       0x50000000
#define EF_MIPS_ARCH_64       0x60000000
#define EF_MIPS_ARCH_32R2     0x70000000
#define EF_MIPS_ARCH_64R2     0x80000000
#define EF_MIPS_ARCH_32R6     0x90000000
#define EF_MIPS_ARCH_64R6     0xa0000000

#define EF_MIPS_ARCH_ASE      0x0f000000

#define EF_MIPS_ARCH_ASE_MICROMIPS 0x02000000
#define EF_MIPS_ARCH_ASE_M16  0x04000000
#define EF_MIPS_ARCH_ASE_MDMX 0x08000000

#define EF_MIPS_ABI_O32       0x00001000
#define EF_MIPS_ABI_O64       0x00002000

#define EF_MIPS_NOREORDER     0x00000001
#define EF_MIPS_PIC           0x00000002
#define EF_MIPS_CPIC          0x00000004
#define EF_MIPS_ABI2          0x00000020
#define EF_MIPS_OPTIONS_FIRST 0x00000080
#define EF_MIPS_32BITMODE     0x00000100
#define EF_MIPS_ABI           0x0000f000
#define EF_MIPS_FP64          0x00000200
#define EF_MIPS_NAN2008       0x00000400

#define EF_MIPS_MACH_NONE     0x00000000
#define EF_MIPS_MACH_3900     0x00810000
#define EF_MIPS_MACH_4010     0x00820000
#define EF_MIPS_MACH_4100     0x00830000
#define EF_MIPS_MACH_4650     0x00850000
#define EF_MIPS_MACH_4120     0x00870000
#define EF_MIPS_MACH_4111     0x00880000
#define EF_MIPS_MACH_SB1      0x008a0000
#define EF_MIPS_MACH_OCTEON   0x008b0000
#define EF_MIPS_MACH_XLR      0x008c0000
#define EF_MIPS_MACH_OCTEON2  0x008d0000
#define EF_MIPS_MACH_OCTEON3  0x008e0000
#define EF_MIPS_MACH_5400     0x00910000
#define EF_MIPS_MACH_5900     0x00920000
#define EF_MIPS_MACH_5500     0x00980000
#define EF_MIPS_MACH_9000     0x00990000
#define EF_MIPS_MACH_LS2E     0x00a00000
#define EF_MIPS_MACH_LS2F     0x00a10000
#define EF_MIPS_MACH_LS3A     0x00a20000
#define EF_MIPS_MACH          0x00ff0000

#define MIPS_ABI_FP_UNKNOWN   (-1)

#define MIPS_ABI_FP_ANY       0x0
#define MIPS_ABI_FP_DOUBLE    0x1
#define MIPS_ABI_FP_SINGLE    0x2
#define MIPS_ABI_FP_SOFT      0x3
#define MIPS_ABI_FP_OLD_64    0x4
#define MIPS_ABI_FP_XX        0x5
#define MIPS_ABI_FP_64        0x6
#define MIPS_ABI_FP_64A       0x7

typedef struct mips_elf_abiflags_v0 {
  uint16_t version;
  uint8_t isa_level;
  uint8_t isa_rev;
  uint8_t gpr_size;
  uint8_t cpr1_size;
  uint8_t cpr2_size;
  uint8_t fp_abi;
  uint32_t isa_ext;
  uint32_t ases;
  uint32_t flags1;
  uint32_t flags2;
} Mips_elf_abiflags_v0;

#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3
#define ET_CORE   4
#define ET_LOPROC 0xff00
#define ET_HIPROC 0xffff

#define EM_NONE             0
#define EM_M32              1
#define EM_SPARC            2
#define EM_386              3
#define EM_68K              4
#define EM_88K              5
#define EM_486              6
#define EM_860              7

#define EM_MIPS             8

#define EM_MIPS_RS4_BE      10

#define EM_PARISC           15

#define EM_SPARC32PLUS      18

#define EM_PPC              20
#define EM_PPC64            21

#define EM_ARM              40

#define EM_SH               42

#define EM_SPARCV9          43

#define EM_TRICORE          44

#define EM_IA_64            50

#define EM_X86_64           62

#define EM_S390             22

#define EM_CRIS             76

#define EM_AVR              83

#define EM_V850             87

#define EM_H8_300H          47
#define EM_H8S              48
#define EM_LATTICEMICO32    138

#define EM_OPENRISC         92

#define EM_HEXAGON          164

#define EM_RX               173

#define EM_RISCV            243

#define EM_NANOMIPS         249

#define EM_LOONGARCH        258

#define EM_ALPHA            0x9026

#define EM_CYGNUS_V850      0x9080

#define EM_S390_OLD         0xA390

#define EM_ALTERA_NIOS2     113

#define EM_MICROBLAZE       189
#define EM_MICROBLAZE_OLD   0xBAAB

#define EM_XTENSA           94

#define EM_AARCH64          183

#define EF_AVR_MACH         0x7F

#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_BINDNOW      24
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH      29
#define DT_FLAGS        30
#define DT_LOOS         0x6000000d
#define DT_HIOS         0x6ffff000
#define DT_LOPROC       0x70000000
#define DT_HIPROC       0x7fffffff

#define DT_VALRNGLO     0x6ffffd00
#define DT_VALRNGHI     0x6ffffdff

#define DT_ADDRRNGLO    0x6ffffe00
#define DT_ADDRRNGHI    0x6ffffeff

#define DT_VERSYM       0x6ffffff0
#define DT_RELACOUNT    0x6ffffff9
#define DT_RELCOUNT     0x6ffffffa
#define DT_FLAGS_1      0x6ffffffb
#define DT_VERDEF       0x6ffffffc
#define DT_VERDEFNUM    0x6ffffffd
#define DT_VERNEED      0x6ffffffe
#define DT_VERNEEDNUM   0x6fffffff

#define DT_MIPS_RLD_VERSION     0x70000001
#define DT_MIPS_TIME_STAMP      0x70000002
#define DT_MIPS_ICHECKSUM       0x70000003
#define DT_MIPS_IVERSION        0x70000004
#define DT_MIPS_FLAGS           0x70000005
  #define RHF_NONE              0
  #define RHF_HARDWAY           1
  #define RHF_NOTPOT            2
#define DT_MIPS_BASE_ADDRESS    0x70000006
#define DT_MIPS_CONFLICT        0x70000008
#define DT_MIPS_LIBLIST         0x70000009
#define DT_MIPS_LOCAL_GOTNO     0x7000000a
#define DT_MIPS_CONFLICTNO      0x7000000b
#define DT_MIPS_LIBLISTNO       0x70000010
#define DT_MIPS_SYMTABNO        0x70000011
#define DT_MIPS_UNREFEXTNO      0x70000012
#define DT_MIPS_GOTSYM          0x70000013
#define DT_MIPS_HIPAGENO        0x70000014
#define DT_MIPS_RLD_MAP         0x70000016

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

#define ELF_ST_BIND(x)          ((x) >> 4)
#define ELF_ST_TYPE(x)          (((unsigned int) x) & 0xf)
#define ELF_ST_INFO(bind, type) (((bind) << 4) | ((type) & 0xf))
#define ELF32_ST_BIND(x)        ELF_ST_BIND(x)
#define ELF32_ST_TYPE(x)        ELF_ST_TYPE(x)
#define ELF64_ST_BIND(x)        ELF_ST_BIND(x)
#define ELF64_ST_TYPE(x)        ELF_ST_TYPE(x)

#define AT_NULL             0
#define AT_IGNORE           1
#define AT_EXECFD           2
#define AT_PHDR             3
#define AT_PHENT            4
#define AT_PHNUM            5
#define AT_PAGESZ           6
#define AT_BASE             7
#define AT_FLAGS            8
#define AT_ENTRY            9
#define AT_NOTELF           10
#define AT_UID              11
#define AT_EUID             12
#define AT_GID              13
#define AT_EGID             14
#define AT_PLATFORM         15
#define AT_HWCAP            16
#define AT_CLKTCK           17
#define AT_FPUCW            18
#define AT_DCACHEBSIZE      19
#define AT_ICACHEBSIZE      20
#define AT_UCACHEBSIZE      21
#define AT_IGNOREPPC        22
#define AT_SECURE           23
#define AT_BASE_PLATFORM    24
#define AT_RANDOM           25
#define AT_HWCAP2           26
#define AT_EXECFN           31
#define AT_SYSINFO          32
#define AT_SYSINFO_EHDR     33
#define AT_L1I_CACHESHAPE   34
#define AT_L1D_CACHESHAPE   35
#define AT_L2_CACHESHAPE    36
#define AT_L3_CACHESHAPE    37

typedef struct dynamic{
  Elf32_Sword d_tag;
  union{
    Elf32_Sword d_val;
    Elf32_Addr d_ptr;
  } d_un;
} Elf32_Dyn;

typedef struct {
  Elf64_Sxword d_tag;
  union {
    Elf64_Xword d_val;
    Elf64_Addr d_ptr;
  } d_un;
} Elf64_Dyn;

#define ELF32_R_SYM(x) ((x) >> 8)
#define ELF32_R_TYPE(x) ((x) & 0xff)

#define ELF64_R_SYM(i)                  ((i) >> 32)
#define ELF64_R_TYPE(i)                 ((i) & 0xffffffff)
#define ELF64_R_TYPE_DATA(i)            (((ELF64_R_TYPE(i) >> 8) ^ 0x00800000) - 0x00800000)

#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GOT32     3
#define R_386_PLT32     4
#define R_386_COPY      5
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8
#define R_386_GOTOFF    9
#define R_386_GOTPC     10
#define R_386_NUM       11
#define R_386_PC8       23

#define R_MIPS_NONE     0
#define R_MIPS_16       1
#define R_MIPS_32       2
#define R_MIPS_REL32    3
#define R_MIPS_26       4
#define R_MIPS_HI16     5
#define R_MIPS_LO16     6
#define R_MIPS_GPREL16  7
#define R_MIPS_LITERAL  8
#define R_MIPS_GOT16    9
#define R_MIPS_PC16     10
#define R_MIPS_CALL16   11
#define R_MIPS_GPREL32  12
#define R_MIPS_UNUSED1  13
#define R_MIPS_UNUSED2  14
#define R_MIPS_UNUSED3  15
#define R_MIPS_SHIFT5   16
#define R_MIPS_SHIFT6   17
#define R_MIPS_64       18
#define R_MIPS_GOT_DISP 19
#define R_MIPS_GOT_PAGE 20
#define R_MIPS_GOT_OFST 21
#define R_MIPS_GOTHI16  22
#define R_MIPS_GOTLO16  23
#define R_MIPS_SUB      24
#define R_MIPS_INSERT_A 25
#define R_MIPS_INSERT_B 26
#define R_MIPS_DELETE   27
#define R_MIPS_HIGHER   28
#define R_MIPS_HIGHEST  29
#define R_MIPS_CALLHI16 30
#define R_MIPS_CALLLO16 31
#define R_MIPS_LOVENDOR 100
#define R_MIPS_HIVENDOR 127


#define EF_SPARCV9_MM           3
#define EF_SPARCV9_TSO          0
#define EF_SPARCV9_PSO          1
#define EF_SPARCV9_RMO          2
#define EF_SPARC_LEDATA         0x800000
#define EF_SPARC_EXT_MASK       0xFFFF00
#define EF_SPARC_32PLUS         0x000100
#define EF_SPARC_SUN_US1        0x000200
#define EF_SPARC_HAL_R1         0x000400
#define EF_SPARC_SUN_US3        0x000800

#define R_SPARC_NONE        0
#define R_SPARC_8           1
#define R_SPARC_16          2
#define R_SPARC_32          3
#define R_SPARC_DISP8       4
#define R_SPARC_DISP16      5
#define R_SPARC_DISP32      6
#define R_SPARC_WDISP30     7
#define R_SPARC_WDISP22     8
#define R_SPARC_HI22        9
#define R_SPARC_22          10
#define R_SPARC_13          11
#define R_SPARC_LO10        12
#define R_SPARC_GOT10       13
#define R_SPARC_GOT13       14
#define R_SPARC_GOT22       15
#define R_SPARC_PC10        16
#define R_SPARC_PC22        17
#define R_SPARC_WPLT30      18
#define R_SPARC_COPY        19
#define R_SPARC_GLOB_DAT    20
#define R_SPARC_JMP_SLOT    21
#define R_SPARC_RELATIVE    22
#define R_SPARC_UA32        23
#define R_SPARC_PLT32       24
#define R_SPARC_HIPLT22     25
#define R_SPARC_LOPLT10     26
#define R_SPARC_PCPLT32     27
#define R_SPARC_PCPLT22     28
#define R_SPARC_PCPLT10     29
#define R_SPARC_10          30
#define R_SPARC_11          31
#define R_SPARC_64          32
#define R_SPARC_OLO10       33
#define R_SPARC_HH22        34
#define R_SPARC_HM10        35
#define R_SPARC_LM22        36
#define R_SPARC_WDISP16     40
#define R_SPARC_WDISP19     41
#define R_SPARC_7           43
#define R_SPARC_5           44
#define R_SPARC_6           45

#define HWCAP_ARM_SWP           (1 << 0)
#define HWCAP_ARM_HALF          (1 << 1)
#define HWCAP_ARM_THUMB         (1 << 2)
#define HWCAP_ARM_26BIT         (1 << 3)
#define HWCAP_ARM_FAST_MULT     (1 << 4)
#define HWCAP_ARM_FPA           (1 << 5)
#define HWCAP_ARM_VFP           (1 << 6)
#define HWCAP_ARM_EDSP          (1 << 7)
#define HWCAP_ARM_JAVA          (1 << 8)
#define HWCAP_ARM_IWMMXT        (1 << 9)
#define HWCAP_ARM_CRUNCH        (1 << 10)
#define HWCAP_ARM_THUMBEE       (1 << 11)
#define HWCAP_ARM_NEON          (1 << 12)
#define HWCAP_ARM_VFPv3         (1 << 13)
#define HWCAP_ARM_VFPv3D16      (1 << 14)
#define HWCAP_ARM_TLS           (1 << 15)
#define HWCAP_ARM_VFPv4         (1 << 16)
#define HWCAP_ARM_IDIVA         (1 << 17)
#define HWCAP_ARM_IDIVT         (1 << 18)
#define HWCAP_IDIV              (HWCAP_IDIVA | HWCAP_IDIVT)
#define HWCAP_VFPD32            (1 << 19)
#define HWCAP_LPAE              (1 << 20)

#define PPC_FEATURE_32                  0x80000000
#define PPC_FEATURE_64                  0x40000000
#define PPC_FEATURE_601_INSTR           0x20000000
#define PPC_FEATURE_HAS_ALTIVEC         0x10000000
#define PPC_FEATURE_HAS_FPU             0x08000000
#define PPC_FEATURE_HAS_MMU             0x04000000
#define PPC_FEATURE_HAS_4xxMAC          0x02000000
#define PPC_FEATURE_UNIFIED_CACHE       0x01000000
#define PPC_FEATURE_HAS_SPE             0x00800000
#define PPC_FEATURE_HAS_EFP_SINGLE      0x00400000
#define PPC_FEATURE_HAS_EFP_DOUBLE      0x00200000
#define PPC_FEATURE_NO_TB               0x00100000
#define PPC_FEATURE_POWER4              0x00080000
#define PPC_FEATURE_POWER5              0x00040000
#define PPC_FEATURE_POWER5_PLUS         0x00020000
#define PPC_FEATURE_CELL                0x00010000
#define PPC_FEATURE_BOOKE               0x00008000
#define PPC_FEATURE_SMT                 0x00004000
#define PPC_FEATURE_ICACHE_SNOOP        0x00002000
#define PPC_FEATURE_ARCH_2_05           0x00001000
#define PPC_FEATURE_PA6T                0x00000800
#define PPC_FEATURE_HAS_DFP             0x00000400
#define PPC_FEATURE_POWER6_EXT          0x00000200
#define PPC_FEATURE_ARCH_2_06           0x00000100
#define PPC_FEATURE_HAS_VSX             0x00000080

#define PPC_FEATURE_PSERIES_PERFMON_COMPAT \
                                        0x00000040

#define PPC_FEATURE_TRUE_LE             0x00000002
#define PPC_FEATURE_PPC_LE              0x00000001

#define PPC_FEATURE2_ARCH_2_07          0x80000000
#define PPC_FEATURE2_HAS_HTM            0x40000000
#define PPC_FEATURE2_HAS_DSCR           0x20000000
#define PPC_FEATURE2_HAS_EBB            0x10000000
#define PPC_FEATURE2_HAS_ISEL           0x08000000
#define PPC_FEATURE2_HAS_TAR            0x04000000
#define PPC_FEATURE2_HAS_VEC_CRYPTO     0x02000000
#define PPC_FEATURE2_HTM_NOSC           0x01000000
#define PPC_FEATURE2_ARCH_3_00          0x00800000
#define PPC_FEATURE2_HAS_IEEE128        0x00400000
#define PPC_FEATURE2_ARCH_3_10          0x00040000

#define HWCAP_SPARC_FLUSH               0x00000001
#define HWCAP_SPARC_STBAR               0x00000002
#define HWCAP_SPARC_SWAP                0x00000004
#define HWCAP_SPARC_MULDIV              0x00000008
#define HWCAP_SPARC_V9                  0x00000010
#define HWCAP_SPARC_ULTRA3              0x00000020
#define HWCAP_SPARC_BLKINIT             0x00000040
#define HWCAP_SPARC_N2                  0x00000080
#define HWCAP_SPARC_MUL32               0x00000100
#define HWCAP_SPARC_DIV32               0x00000200
#define HWCAP_SPARC_FSMULD              0x00000400
#define HWCAP_SPARC_V8PLUS              0x00000800
#define HWCAP_SPARC_POPC                0x00001000
#define HWCAP_SPARC_VIS                 0x00002000
#define HWCAP_SPARC_VIS2                0x00004000
#define HWCAP_SPARC_ASI_BLK_INIT        0x00008000
#define HWCAP_SPARC_FMAF                0x00010000
#define HWCAP_SPARC_VIS3                0x00020000
#define HWCAP_SPARC_HPC                 0x00040000
#define HWCAP_SPARC_RANDOM              0x00080000
#define HWCAP_SPARC_TRANS               0x00100000
#define HWCAP_SPARC_FJFMAU              0x00200000
#define HWCAP_SPARC_IMA                 0x00400000
#define HWCAP_SPARC_ASI_CACHE_SPARING   0x00800000
#define HWCAP_SPARC_PAUSE               0x01000000
#define HWCAP_SPARC_CBCOND              0x02000000
#define HWCAP_SPARC_CRYPTO              0x04000000

#define HWCAP_S390_NR_ESAN3      0
#define HWCAP_S390_NR_ZARCH      1
#define HWCAP_S390_NR_STFLE      2
#define HWCAP_S390_NR_MSA        3
#define HWCAP_S390_NR_LDISP      4
#define HWCAP_S390_NR_EIMM       5
#define HWCAP_S390_NR_DFP        6
#define HWCAP_S390_NR_HPAGE      7
#define HWCAP_S390_NR_ETF3EH     8
#define HWCAP_S390_NR_HIGH_GPRS  9
#define HWCAP_S390_NR_TE        10
#define HWCAP_S390_NR_VXRS      11
#define HWCAP_S390_NR_VXRS_BCD  12
#define HWCAP_S390_NR_VXRS_EXT  13
#define HWCAP_S390_NR_GS        14
#define HWCAP_S390_NR_VXRS_EXT2 15
#define HWCAP_S390_NR_VXRS_PDE  16
#define HWCAP_S390_NR_SORT      17
#define HWCAP_S390_NR_DFLT      18
#define HWCAP_S390_NR_VXRS_PDE2 19
#define HWCAP_S390_NR_NNPA      20
#define HWCAP_S390_NR_PCI_MIO   21
#define HWCAP_S390_NR_SIE       22

#define HWCAP_S390_ESAN3     (1 << HWCAP_S390_NR_ESAN3)
#define HWCAP_S390_ZARCH     (1 << HWCAP_S390_NR_ZARCH)
#define HWCAP_S390_STFLE     (1 << HWCAP_S390_NR_STFLE)
#define HWCAP_S390_MSA       (1 << HWCAP_S390_NR_MSA)
#define HWCAP_S390_LDISP     (1 << HWCAP_S390_NR_LDISP)
#define HWCAP_S390_EIMM      (1 << HWCAP_S390_NR_EIMM)
#define HWCAP_S390_DFP       (1 << HWCAP_S390_NR_DFP)
#define HWCAP_S390_HPAGE     (1 << HWCAP_S390_NR_HPAGE)
#define HWCAP_S390_ETF3EH    (1 << HWCAP_S390_NR_ETF3EH)
#define HWCAP_S390_HIGH_GPRS (1 << HWCAP_S390_NR_HIGH_GPRS)
#define HWCAP_S390_TE        (1 << HWCAP_S390_NR_TE)
#define HWCAP_S390_VXRS      (1 << HWCAP_S390_NR_VXRS)
#define HWCAP_S390_VXRS_BCD  (1 << HWCAP_S390_NR_VXRS_BCD)
#define HWCAP_S390_VXRS_EXT  (1 << HWCAP_S390_NR_VXRS_EXT)
#define HWCAP_S390_GS        (1 << HWCAP_S390_NR_GS)
#define HWCAP_S390_VXRS_EXT2 (1 << HWCAP_S390_NR_VXRS_EXT2)
#define HWCAP_S390_VXRS_PDE  (1 << HWCAP_S390_NR_VXRS_PDE)
#define HWCAP_S390_SORT      (1 << HWCAP_S390_NR_SORT)
#define HWCAP_S390_DFLT      (1 << HWCAP_S390_NR_DFLT)
#define HWCAP_S390_VXRS_PDE2 (1 << HWCAP_S390_NR_VXRS_PDE2)
#define HWCAP_S390_NNPA      (1 << HWCAP_S390_NR_NNPA)
#define HWCAP_S390_PCI_MIO   (1 << HWCAP_S390_NR_PCI_MIO)
#define HWCAP_S390_SIE       (1 << HWCAP_S390_NR_SIE)

#define EF_M68K_CPU32    0x00810000
#define EF_M68K_M68000   0x01000000
#define EF_M68K_CFV4E    0x00008000
#define EF_M68K_FIDO     0x02000000
#define EF_M68K_ARCH_MASK                                               \
  (EF_M68K_M68000 | EF_M68K_CPU32 | EF_M68K_CFV4E | EF_M68K_FIDO)

#define EF_M68K_CF_ISA_MASK     0x0F
#define EF_M68K_CF_ISA_A_NODIV  0x01
#define EF_M68K_CF_ISA_A        0x02
#define EF_M68K_CF_ISA_A_PLUS   0x03
#define EF_M68K_CF_ISA_B_NOUSP  0x04
#define EF_M68K_CF_ISA_B        0x05
#define EF_M68K_CF_ISA_C        0x06
#define EF_M68K_CF_ISA_C_NODIV  0x07
#define EF_M68K_CF_MAC_MASK     0x30
#define EF_M68K_CF_MAC          0x10
#define EF_M68K_CF_EMAC         0x20
#define EF_M68K_CF_EMAC_B       0x30
#define EF_M68K_CF_FLOAT        0x40
#define EF_M68K_CF_MASK         0xFF

#define R_68K_NONE      0
#define R_68K_32        1
#define R_68K_16        2
#define R_68K_8         3
#define R_68K_PC32      4
#define R_68K_PC16      5
#define R_68K_PC8       6
#define R_68K_GOT32     7
#define R_68K_GOT16     8
#define R_68K_GOT8      9
#define R_68K_GOT32O    10
#define R_68K_GOT16O    11
#define R_68K_GOT8O     12
#define R_68K_PLT32     13
#define R_68K_PLT16     14
#define R_68K_PLT8      15
#define R_68K_PLT32O    16
#define R_68K_PLT16O    17
#define R_68K_PLT8O     18
#define R_68K_COPY      19
#define R_68K_GLOB_DAT  20
#define R_68K_JMP_SLOT  21
#define R_68K_RELATIVE  22

#define R_ALPHA_NONE            0
#define R_ALPHA_REFLONG         1
#define R_ALPHA_REFQUAD         2
#define R_ALPHA_GPREL32         3
#define R_ALPHA_LITERAL         4
#define R_ALPHA_LITUSE          5
#define R_ALPHA_GPDISP          6
#define R_ALPHA_BRADDR          7
#define R_ALPHA_HINT            8
#define R_ALPHA_SREL16          9
#define R_ALPHA_SREL32          10
#define R_ALPHA_SREL64          11
#define R_ALPHA_GPRELHIGH       17
#define R_ALPHA_GPRELLOW        18
#define R_ALPHA_GPREL16         19
#define R_ALPHA_COPY            24
#define R_ALPHA_GLOB_DAT        25
#define R_ALPHA_JMP_SLOT        26
#define R_ALPHA_RELATIVE        27
#define R_ALPHA_BRSGP           28
#define R_ALPHA_TLSGD           29
#define R_ALPHA_TLS_LDM         30
#define R_ALPHA_DTPMOD64        31
#define R_ALPHA_GOTDTPREL       32
#define R_ALPHA_DTPREL64        33
#define R_ALPHA_DTPRELHI        34
#define R_ALPHA_DTPRELLO        35
#define R_ALPHA_DTPREL16        36
#define R_ALPHA_GOTTPREL        37
#define R_ALPHA_TPREL64         38
#define R_ALPHA_TPRELHI         39
#define R_ALPHA_TPRELLO         40
#define R_ALPHA_TPREL16         41

#define SHF_ALPHA_GPREL         0x10000000


#define EF_PPC64_ABI            0x3

#define R_PPC_NONE              0
#define R_PPC_ADDR32            1
#define R_PPC_ADDR24            2
#define R_PPC_ADDR16            3
#define R_PPC_ADDR16_LO         4
#define R_PPC_ADDR16_HI         5
#define R_PPC_ADDR16_HA         6
#define R_PPC_ADDR14            7
#define R_PPC_ADDR14_BRTAKEN    8
#define R_PPC_ADDR14_BRNTAKEN   9
#define R_PPC_REL24             10
#define R_PPC_REL14             11
#define R_PPC_REL14_BRTAKEN     12
#define R_PPC_REL14_BRNTAKEN    13
#define R_PPC_GOT16             14
#define R_PPC_GOT16_LO          15
#define R_PPC_GOT16_HI          16
#define R_PPC_GOT16_HA          17
#define R_PPC_PLTREL24          18
#define R_PPC_COPY              19
#define R_PPC_GLOB_DAT          20
#define R_PPC_JMP_SLOT          21
#define R_PPC_RELATIVE          22
#define R_PPC_LOCAL24PC         23
#define R_PPC_UADDR32           24
#define R_PPC_UADDR16           25
#define R_PPC_REL32             26
#define R_PPC_PLT32             27
#define R_PPC_PLTREL32          28
#define R_PPC_PLT16_LO          29
#define R_PPC_PLT16_HI          30
#define R_PPC_PLT16_HA          31
#define R_PPC_SDAREL16          32
#define R_PPC_SECTOFF           33
#define R_PPC_SECTOFF_LO        34
#define R_PPC_SECTOFF_HI        35
#define R_PPC_SECTOFF_HA        36
#ifndef R_PPC_NUM
#define R_PPC_NUM               37
#endif

#define EF_ARM_RELEXEC        0x01
#define EF_ARM_HASENTRY       0x02
#define EF_ARM_INTERWORK      0x04
#define EF_ARM_APCS_26        0x08
#define EF_ARM_APCS_FLOAT     0x10
#define EF_ARM_PIC            0x20
#define EF_ALIGN8             0x40
#define EF_NEW_ABI            0x80
#define EF_OLD_ABI            0x100
#define EF_ARM_SOFT_FLOAT     0x200
#define EF_ARM_VFP_FLOAT      0x400
#define EF_ARM_MAVERICK_FLOAT 0x800

#define EF_ARM_SYMSARESORTED    0x04
#define EF_ARM_DYNSYMSUSESEGIDX 0x08
#define EF_ARM_MAPSYMSFIRST     0x10
#define EF_ARM_EABIMASK         0xFF000000

#define EF_ARM_BE8          0x00800000
#define EF_ARM_LE8          0x00400000

#define EF_ARM_EABI_VERSION(flags) ((flags) & EF_ARM_EABIMASK)
#define EF_ARM_EABI_UNKNOWN  0x00000000
#define EF_ARM_EABI_VER1     0x01000000
#define EF_ARM_EABI_VER2     0x02000000
#define EF_ARM_EABI_VER3     0x03000000
#define EF_ARM_EABI_VER4     0x04000000
#define EF_ARM_EABI_VER5     0x05000000

#define STT_ARM_TFUNC      0xd

#define SHF_ARM_ENTRYSECT  0x10000000
#define SHF_ARM_COMDEF     0x80000000

#define PF_ARM_SB          0x10000000

#define R_ARM_NONE              0
#define R_ARM_PC24              1
#define R_ARM_ABS32             2
#define R_ARM_REL32             3
#define R_ARM_PC13              4
#define R_ARM_ABS16             5
#define R_ARM_ABS12             6
#define R_ARM_THM_ABS5          7
#define R_ARM_ABS8              8
#define R_ARM_SBREL32           9
#define R_ARM_THM_PC22          10
#define R_ARM_THM_PC8           11
#define R_ARM_AMP_VCALL9        12
#define R_ARM_SWI24             13
#define R_ARM_THM_SWI8          14
#define R_ARM_XPC25             15
#define R_ARM_THM_XPC22         16
#define R_ARM_COPY              20
#define R_ARM_GLOB_DAT          21
#define R_ARM_JUMP_SLOT         22
#define R_ARM_RELATIVE          23
#define R_ARM_GOTOFF            24
#define R_ARM_GOTPC             25
#define R_ARM_GOT32             26
#define R_ARM_PLT32             27
#define R_ARM_CALL              28
#define R_ARM_JUMP24            29
#define R_ARM_GNU_VTENTRY       100
#define R_ARM_GNU_VTINHERIT     101
#define R_ARM_THM_PC11          102
#define R_ARM_THM_PC9           103
#define R_ARM_RXPC25            249
#define R_ARM_RSBREL32          250
#define R_ARM_THM_RPC22         251
#define R_ARM_RREL32            252
#define R_ARM_RABS22            253
#define R_ARM_RPC24             254
#define R_ARM_RBASE             255
#define R_ARM_NUM               256

#define R_AARCH64_NONE                256
#define R_AARCH64_ABS64               257
#define R_AARCH64_ABS32               258
#define R_AARCH64_ABS16               259
#define R_AARCH64_PREL64              260
#define R_AARCH64_PREL32              261
#define R_AARCH64_PREL16              262
#define R_AARCH64_MOVW_UABS_G0        263
#define R_AARCH64_MOVW_UABS_G0_NC     264
#define R_AARCH64_MOVW_UABS_G1        265
#define R_AARCH64_MOVW_UABS_G1_NC     266
#define R_AARCH64_MOVW_UABS_G2        267
#define R_AARCH64_MOVW_UABS_G2_NC     268
#define R_AARCH64_MOVW_UABS_G3        269
#define R_AARCH64_MOVW_SABS_G0        270
#define R_AARCH64_MOVW_SABS_G1        271
#define R_AARCH64_MOVW_SABS_G2        272
#define R_AARCH64_LD_PREL_LO19        273
#define R_AARCH64_ADR_PREL_LO21       274
#define R_AARCH64_ADR_PREL_PG_HI21    275
#define R_AARCH64_ADR_PREL_PG_HI21_NC 276
#define R_AARCH64_ADD_ABS_LO12_NC     277
#define R_AARCH64_LDST8_ABS_LO12_NC   278
#define R_AARCH64_LDST16_ABS_LO12_NC  284
#define R_AARCH64_LDST32_ABS_LO12_NC  285
#define R_AARCH64_LDST64_ABS_LO12_NC  286
#define R_AARCH64_LDST128_ABS_LO12_NC 299
#define R_AARCH64_TSTBR14             279
#define R_AARCH64_CONDBR19            280
#define R_AARCH64_JUMP26              282
#define R_AARCH64_CALL26              283
#define R_AARCH64_MOVW_PREL_G0        287
#define R_AARCH64_MOVW_PREL_G0_NC     288
#define R_AARCH64_MOVW_PREL_G1        289
#define R_AARCH64_MOVW_PREL_G1_NC     290
#define R_AARCH64_MOVW_PREL_G2        291
#define R_AARCH64_MOVW_PREL_G2_NC     292
#define R_AARCH64_MOVW_PREL_G3        293
#define R_AARCH64_MOVW_GOTOFF_G0      300
#define R_AARCH64_MOVW_GOTOFF_G0_NC   301
#define R_AARCH64_MOVW_GOTOFF_G1      302
#define R_AARCH64_MOVW_GOTOFF_G1_NC   303
#define R_AARCH64_MOVW_GOTOFF_G2      304
#define R_AARCH64_MOVW_GOTOFF_G2_NC   305
#define R_AARCH64_MOVW_GOTOFF_G3      306
#define R_AARCH64_GOTREL64            307
#define R_AARCH64_GOTREL32            308
#define R_AARCH64_GOT_LD_PREL19       309
#define R_AARCH64_LD64_GOTOFF_LO15    310
#define R_AARCH64_ADR_GOT_PAGE        311
#define R_AARCH64_LD64_GOT_LO12_NC    312
#define R_AARCH64_LD64_GOTPAGE_LO15   313
#define R_AARCH64_TLSGD_ADR_PREL21            512
#define R_AARCH64_TLSGD_ADR_PAGE21            513
#define R_AARCH64_TLSGD_ADD_LO12_NC           514
#define R_AARCH64_TLSGD_MOVW_G1               515
#define R_AARCH64_TLSGD_MOVW_G0_NC            516
#define R_AARCH64_TLSLD_ADR_PREL21            517
#define R_AARCH64_TLSLD_ADR_PAGE21            518
#define R_AARCH64_TLSLD_ADD_LO12_NC           519
#define R_AARCH64_TLSLD_MOVW_G1               520
#define R_AARCH64_TLSLD_MOVW_G0_NC            521
#define R_AARCH64_TLSLD_LD_PREL19             522
#define R_AARCH64_TLSLD_MOVW_DTPREL_G2        523
#define R_AARCH64_TLSLD_MOVW_DTPREL_G1        524
#define R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC     525
#define R_AARCH64_TLSLD_MOVW_DTPREL_G0        526
#define R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC     527
#define R_AARCH64_TLSLD_ADD_DTPREL_HI12       528
#define R_AARCH64_TLSLD_ADD_DTPREL_LO12       529
#define R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC    530
#define R_AARCH64_TLSLD_LDST8_DTPREL_LO12     531
#define R_AARCH64_TLSLD_LDST8_DTPREL_LO12_NC  532
#define R_AARCH64_TLSLD_LDST16_DTPREL_LO12    533
#define R_AARCH64_TLSLD_LDST16_DTPREL_LO12_NC 534
#define R_AARCH64_TLSLD_LDST32_DTPREL_LO12    535
#define R_AARCH64_TLSLD_LDST32_DTPREL_LO12_NC 536
#define R_AARCH64_TLSLD_LDST64_DTPREL_LO12    537
#define R_AARCH64_TLSLD_LDST64_DTPREL_LO12_NC 538
#define R_AARCH64_TLSIE_MOVW_GOTTPREL_G1      539
#define R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC   540
#define R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21   541
#define R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC 542
#define R_AARCH64_TLSIE_LD_GOTTPREL_PREL19    543
#define R_AARCH64_TLSLE_MOVW_TPREL_G2         544
#define R_AARCH64_TLSLE_MOVW_TPREL_G1         545
#define R_AARCH64_TLSLE_MOVW_TPREL_G1_NC      546
#define R_AARCH64_TLSLE_MOVW_TPREL_G0         547
#define R_AARCH64_TLSLE_MOVW_TPREL_G0_NC      548
#define R_AARCH64_TLSLE_ADD_TPREL_HI12        549
#define R_AARCH64_TLSLE_ADD_TPREL_LO12        550
#define R_AARCH64_TLSLE_ADD_TPREL_LO12_NC     551
#define R_AARCH64_TLSLE_LDST8_TPREL_LO12      552
#define R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC   553
#define R_AARCH64_TLSLE_LDST16_TPREL_LO12     554
#define R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC  555
#define R_AARCH64_TLSLE_LDST32_TPREL_LO12     556
#define R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC  557
#define R_AARCH64_TLSLE_LDST64_TPREL_LO12     558
#define R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC  559
#define R_AARCH64_COPY         1024
#define R_AARCH64_GLOB_DAT     1025
#define R_AARCH64_JUMP_SLOT    1026
#define R_AARCH64_RELATIVE     1027
#define R_AARCH64_TLS_DTPREL64 1028
#define R_AARCH64_TLS_DTPMOD64 1029
#define R_AARCH64_TLS_TPREL64  1030
#define R_AARCH64_TLS_DTPREL32 1031
#define R_AARCH64_TLS_DTPMOD32 1032
#define R_AARCH64_TLS_TPREL32  1033

#define R_390_NONE              0
#define R_390_8                 1
#define R_390_12                2
#define R_390_16                3
#define R_390_32                4
#define R_390_PC32              5
#define R_390_GOT12             6
#define R_390_GOT32             7
#define R_390_PLT32             8
#define R_390_COPY              9
#define R_390_GLOB_DAT          10
#define R_390_JMP_SLOT          11
#define R_390_RELATIVE          12
#define R_390_GOTOFF32          13
#define R_390_GOTPC             14
#define R_390_GOT16             15
#define R_390_PC16              16
#define R_390_PC16DBL           17
#define R_390_PLT16DBL          18
#define R_390_PC32DBL           19
#define R_390_PLT32DBL          20
#define R_390_GOTPCDBL          21
#define R_390_64                22
#define R_390_PC64              23
#define R_390_GOT64             24
#define R_390_PLT64             25
#define R_390_GOTENT            26
#define R_390_GOTOFF16          27
#define R_390_GOTOFF64          28
#define R_390_GOTPLT12          29
#define R_390_GOTPLT16          30
#define R_390_GOTPLT32          31
#define R_390_GOTPLT64          32
#define R_390_GOTPLTENT         33
#define R_390_PLTOFF16          34
#define R_390_PLTOFF32          35
#define R_390_PLTOFF64          36
#define R_390_TLS_LOAD          37
#define R_390_TLS_GDCALL        38
#define R_390_TLS_LDCALL        39
#define R_390_TLS_GD32          40
#define R_390_TLS_GD64          41
#define R_390_TLS_GOTIE12       42
#define R_390_TLS_GOTIE32       43
#define R_390_TLS_GOTIE64       44
#define R_390_TLS_LDM32         45
#define R_390_TLS_LDM64         46
#define R_390_TLS_IE32          47
#define R_390_TLS_IE64          48
#define R_390_TLS_IEENT         49
#define R_390_TLS_LE32          50
#define R_390_TLS_LE64          51
#define R_390_TLS_LDO32         52
#define R_390_TLS_LDO64         53
#define R_390_TLS_DTPMOD        54
#define R_390_TLS_DTPOFF        55
#define R_390_TLS_TPOFF         56
#define R_390_20                57
#define R_390_NUM               58

#define R_X86_64_NONE       0
#define R_X86_64_64         1
#define R_X86_64_PC32       2
#define R_X86_64_GOT32      3
#define R_X86_64_PLT32      4
#define R_X86_64_COPY       5
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8
#define R_X86_64_GOTPCREL   9
#define R_X86_64_32         10
#define R_X86_64_32S        11
#define R_X86_64_16         12
#define R_X86_64_PC16       13
#define R_X86_64_8          14
#define R_X86_64_PC8        15

#define R_X86_64_NUM        16

#define EF_ALPHA_32BIT      1

#define EF_PARISC_TRAPNIL   0x00010000
#define EF_PARISC_EXT       0x00020000
#define EF_PARISC_LSB       0x00040000
#define EF_PARISC_WIDE      0x00080000
#define EF_PARISC_NO_KABP   0x00100000
#define EF_PARISC_LAZYSWAP  0x00400000
#define EF_PARISC_ARCH      0x0000ffff

#define EFA_PARISC_1_0      0x020b
#define EFA_PARISC_1_1      0x0210
#define EFA_PARISC_2_0      0x0214

#define SHN_PARISC_ANSI_COMMON  0xff00
#define SHN_PARISC_HUGE_COMMON  0xff01

#define SHT_PARISC_EXT      0x70000000
#define SHT_PARISC_UNWIND   0x70000001
#define SHT_PARISC_DOC      0x70000002

#define SHF_PARISC_SHORT    0x20000000
#define SHF_PARISC_HUGE     0x40000000
#define SHF_PARISC_SBP      0x80000000

#define STT_PARISC_MILLICODE    13

#define STT_HP_OPAQUE           (STT_LOOS + 0x1)
#define STT_HP_STUB             (STT_LOOS + 0x2)

#define R_PARISC_NONE           0
#define R_PARISC_DIR32          1
#define R_PARISC_DIR21L         2
#define R_PARISC_DIR17R         3
#define R_PARISC_DIR17F         4
#define R_PARISC_DIR14R         6
#define R_PARISC_PCREL32        9
#define R_PARISC_PCREL21L       10
#define R_PARISC_PCREL17R       11
#define R_PARISC_PCREL17F       12
#define R_PARISC_PCREL14R       14
#define R_PARISC_DPREL21L       18
#define R_PARISC_DPREL14R       22
#define R_PARISC_GPREL21L       26
#define R_PARISC_GPREL14R       30
#define R_PARISC_LTOFF21L       34
#define R_PARISC_LTOFF14R       38
#define R_PARISC_SECREL32       41
#define R_PARISC_SEGBASE        48
#define R_PARISC_SEGREL32       49
#define R_PARISC_PLTOFF21L      50
#define R_PARISC_PLTOFF14R      54
#define R_PARISC_LTOFF_FPTR32   57
#define R_PARISC_LTOFF_FPTR21L  58
#define R_PARISC_LTOFF_FPTR14R  62
#define R_PARISC_FPTR64         64
#define R_PARISC_PLABEL32       65
#define R_PARISC_PCREL64        72
#define R_PARISC_PCREL22F       74
#define R_PARISC_PCREL14WR      75
#define R_PARISC_PCREL14DR      76
#define R_PARISC_PCREL16F       77
#define R_PARISC_PCREL16WF      78
#define R_PARISC_PCREL16DF      79
#define R_PARISC_DIR64          80
#define R_PARISC_DIR14WR        83
#define R_PARISC_DIR14DR        84
#define R_PARISC_DIR16F         85
#define R_PARISC_DIR16WF        86
#define R_PARISC_DIR16DF        87
#define R_PARISC_GPREL64        88
#define R_PARISC_GPREL14WR      91
#define R_PARISC_GPREL14DR      92
#define R_PARISC_GPREL16F       93
#define R_PARISC_GPREL16WF      94
#define R_PARISC_GPREL16DF      95
#define R_PARISC_LTOFF64        96
#define R_PARISC_LTOFF14WR      99
#define R_PARISC_LTOFF14DR      100
#define R_PARISC_LTOFF16F       101
#define R_PARISC_LTOFF16WF      102
#define R_PARISC_LTOFF16DF      103
#define R_PARISC_SECREL64       104
#define R_PARISC_SEGREL64       112
#define R_PARISC_PLTOFF14WR     115
#define R_PARISC_PLTOFF14DR     116
#define R_PARISC_PLTOFF16F      117
#define R_PARISC_PLTOFF16WF     118
#define R_PARISC_PLTOFF16DF     119
#define R_PARISC_LTOFF_FPTR64   120
#define R_PARISC_LTOFF_FPTR14WR 123
#define R_PARISC_LTOFF_FPTR14DR 124
#define R_PARISC_LTOFF_FPTR16F  125
#define R_PARISC_LTOFF_FPTR16WF 126
#define R_PARISC_LTOFF_FPTR16DF 127
#define R_PARISC_LORESERVE      128
#define R_PARISC_COPY           128
#define R_PARISC_IPLT           129
#define R_PARISC_EPLT           130
#define R_PARISC_TPREL32        153
#define R_PARISC_TPREL21L       154
#define R_PARISC_TPREL14R       158
#define R_PARISC_LTOFF_TP21L    162
#define R_PARISC_LTOFF_TP14R    166
#define R_PARISC_LTOFF_TP14F    167
#define R_PARISC_TPREL64        216
#define R_PARISC_TPREL14WR      219
#define R_PARISC_TPREL14DR      220
#define R_PARISC_TPREL16F       221
#define R_PARISC_TPREL16WF      222
#define R_PARISC_TPREL16DF      223
#define R_PARISC_LTOFF_TP64     224
#define R_PARISC_LTOFF_TP14WR   227
#define R_PARISC_LTOFF_TP14DR   228
#define R_PARISC_LTOFF_TP16F    229
#define R_PARISC_LTOFF_TP16WF   230
#define R_PARISC_LTOFF_TP16DF   231
#define R_PARISC_HIRESERVE      255

#define PT_HP_TLS               (PT_LOOS + 0x0)
#define PT_HP_CORE_NONE         (PT_LOOS + 0x1)
#define PT_HP_CORE_VERSION      (PT_LOOS + 0x2)
#define PT_HP_CORE_KERNEL       (PT_LOOS + 0x3)
#define PT_HP_CORE_COMM         (PT_LOOS + 0x4)
#define PT_HP_CORE_PROC         (PT_LOOS + 0x5)
#define PT_HP_CORE_LOADABLE     (PT_LOOS + 0x6)
#define PT_HP_CORE_STACK        (PT_LOOS + 0x7)
#define PT_HP_CORE_SHM          (PT_LOOS + 0x8)
#define PT_HP_CORE_MMF          (PT_LOOS + 0x9)
#define PT_HP_PARALLEL          (PT_LOOS + 0x10)
#define PT_HP_FASTBIND          (PT_LOOS + 0x11)
#define PT_HP_OPT_ANNOT         (PT_LOOS + 0x12)
#define PT_HP_HSL_ANNOT         (PT_LOOS + 0x13)
#define PT_HP_STACK             (PT_LOOS + 0x14)

#define PT_PARISC_ARCHEXT       0x70000000
#define PT_PARISC_UNWIND        0x70000001

#define PF_PARISC_SBP           0x08000000

#define PF_HP_PAGE_SIZE         0x00100000
#define PF_HP_FAR_SHARED        0x00200000
#define PF_HP_NEAR_SHARED       0x00400000
#define PF_HP_CODE              0x01000000
#define PF_HP_MODIFY            0x02000000
#define PF_HP_LAZYSWAP          0x04000000
#define PF_HP_SBP               0x08000000

#define EF_IA_64_MASKOS         0x0000000f
#define EF_IA_64_ABI64          0x00000010
#define EF_IA_64_ARCH           0xff000000

#define PT_IA_64_ARCHEXT        (PT_LOPROC + 0)
#define PT_IA_64_UNWIND         (PT_LOPROC + 1)

#define PF_IA_64_NORECOV        0x80000000

#define SHT_IA_64_EXT           (SHT_LOPROC + 0)
#define SHT_IA_64_UNWIND        (SHT_LOPROC + 1)

#define SHF_IA_64_SHORT         0x10000000
#define SHF_IA_64_NORECOV       0x20000000

#define DT_IA_64_PLT_RESERVE    (DT_LOPROC + 0)
#define DT_IA_64_NUM            1

#define R_IA64_NONE             0x00
#define R_IA64_IMM14            0x21
#define R_IA64_IMM22            0x22
#define R_IA64_IMM64            0x23
#define R_IA64_DIR32MSB         0x24
#define R_IA64_DIR32LSB         0x25
#define R_IA64_DIR64MSB         0x26
#define R_IA64_DIR64LSB         0x27
#define R_IA64_GPREL22          0x2a
#define R_IA64_GPREL64I         0x2b
#define R_IA64_GPREL32MSB       0x2c
#define R_IA64_GPREL32LSB       0x2d
#define R_IA64_GPREL64MSB       0x2e
#define R_IA64_GPREL64LSB       0x2f
#define R_IA64_LTOFF22          0x32
#define R_IA64_LTOFF64I         0x33
#define R_IA64_PLTOFF22         0x3a
#define R_IA64_PLTOFF64I        0x3b
#define R_IA64_PLTOFF64MSB      0x3e
#define R_IA64_PLTOFF64LSB      0x3f
#define R_IA64_FPTR64I          0x43
#define R_IA64_FPTR32MSB        0x44
#define R_IA64_FPTR32LSB        0x45
#define R_IA64_FPTR64MSB        0x46
#define R_IA64_FPTR64LSB        0x47
#define R_IA64_PCREL60B         0x48
#define R_IA64_PCREL21B         0x49
#define R_IA64_PCREL21M         0x4a
#define R_IA64_PCREL21F         0x4b
#define R_IA64_PCREL32MSB       0x4c
#define R_IA64_PCREL32LSB       0x4d
#define R_IA64_PCREL64MSB       0x4e
#define R_IA64_PCREL64LSB       0x4f
#define R_IA64_LTOFF_FPTR22     0x52
#define R_IA64_LTOFF_FPTR64I    0x53
#define R_IA64_LTOFF_FPTR32MSB  0x54
#define R_IA64_LTOFF_FPTR32LSB  0x55
#define R_IA64_LTOFF_FPTR64MSB  0x56
#define R_IA64_LTOFF_FPTR64LSB  0x57
#define R_IA64_SEGREL32MSB      0x5c
#define R_IA64_SEGREL32LSB      0x5d
#define R_IA64_SEGREL64MSB      0x5e
#define R_IA64_SEGREL64LSB      0x5f
#define R_IA64_SECREL32MSB      0x64
#define R_IA64_SECREL32LSB      0x65
#define R_IA64_SECREL64MSB      0x66
#define R_IA64_SECREL64LSB      0x67
#define R_IA64_REL32MSB         0x6c
#define R_IA64_REL32LSB         0x6d
#define R_IA64_REL64MSB         0x6e
#define R_IA64_REL64LSB         0x6f
#define R_IA64_LTV32MSB         0x74
#define R_IA64_LTV32LSB         0x75
#define R_IA64_LTV64MSB         0x76
#define R_IA64_LTV64LSB         0x77
#define R_IA64_PCREL21BI        0x79
#define R_IA64_PCREL22          0x7a
#define R_IA64_PCREL64I         0x7b
#define R_IA64_IPLTMSB          0x80
#define R_IA64_IPLTLSB          0x81
#define R_IA64_COPY             0x84
#define R_IA64_SUB              0x85
#define R_IA64_LTOFF22X         0x86
#define R_IA64_LDXMOV           0x87
#define R_IA64_TPREL14          0x91
#define R_IA64_TPREL22          0x92
#define R_IA64_TPREL64I         0x93
#define R_IA64_TPREL64MSB       0x96
#define R_IA64_TPREL64LSB       0x97
#define R_IA64_LTOFF_TPREL22    0x9a
#define R_IA64_DTPMOD64MSB      0xa6
#define R_IA64_DTPMOD64LSB      0xa7
#define R_IA64_LTOFF_DTPMOD22   0xaa
#define R_IA64_DTPREL14         0xb1
#define R_IA64_DTPREL22         0xb2
#define R_IA64_DTPREL64I        0xb3
#define R_IA64_DTPREL32MSB      0xb4
#define R_IA64_DTPREL32LSB      0xb5
#define R_IA64_DTPREL64MSB      0xb6
#define R_IA64_DTPREL64LSB      0xb7
#define R_IA64_LTOFF_DTPREL22   0xba

#define R_RISCV_NONE          0
#define R_RISCV_32            1
#define R_RISCV_64            2
#define R_RISCV_RELATIVE      3
#define R_RISCV_COPY          4
#define R_RISCV_JUMP_SLOT     5
#define R_RISCV_TLS_DTPMOD32  6
#define R_RISCV_TLS_DTPMOD64  7
#define R_RISCV_TLS_DTPREL32  8
#define R_RISCV_TLS_DTPREL64  9
#define R_RISCV_TLS_TPREL32   10
#define R_RISCV_TLS_TPREL64   11
#define R_RISCV_BRANCH        16
#define R_RISCV_JAL           17
#define R_RISCV_CALL          18
#define R_RISCV_CALL_PLT      19
#define R_RISCV_GOT_HI20      20
#define R_RISCV_TLS_GOT_HI20  21
#define R_RISCV_TLS_GD_HI20   22
#define R_RISCV_PCREL_HI20    23
#define R_RISCV_PCREL_LO12_I  24
#define R_RISCV_PCREL_LO12_S  25
#define R_RISCV_HI20          26
#define R_RISCV_LO12_I        27
#define R_RISCV_LO12_S        28
#define R_RISCV_TPREL_HI20    29
#define R_RISCV_TPREL_LO12_I  30
#define R_RISCV_TPREL_LO12_S  31
#define R_RISCV_TPREL_ADD     32
#define R_RISCV_ADD8          33
#define R_RISCV_ADD16         34
#define R_RISCV_ADD32         35
#define R_RISCV_ADD64         36
#define R_RISCV_SUB8          37
#define R_RISCV_SUB16         38
#define R_RISCV_SUB32         39
#define R_RISCV_SUB64         40
#define R_RISCV_GNU_VTINHERIT 41
#define R_RISCV_GNU_VTENTRY   42
#define R_RISCV_ALIGN         43
#define R_RISCV_RVC_BRANCH    44
#define R_RISCV_RVC_JUMP      45
#define R_RISCV_RVC_LUI       46
#define R_RISCV_GPREL_I       47
#define R_RISCV_GPREL_S       48
#define R_RISCV_TPREL_I       49
#define R_RISCV_TPREL_S       50
#define R_RISCV_RELAX         51
#define R_RISCV_SUB6          52
#define R_RISCV_SET6          53
#define R_RISCV_SET8          54
#define R_RISCV_SET16         55
#define R_RISCV_SET32         56

#define EF_RISCV_RVC              0x0001
#define EF_RISCV_FLOAT_ABI        0x0006
#define EF_RISCV_FLOAT_ABI_SOFT   0x0000
#define EF_RISCV_FLOAT_ABI_SINGLE 0x0002
#define EF_RISCV_FLOAT_ABI_DOUBLE 0x0004
#define EF_RISCV_FLOAT_ABI_QUAD   0x0006
#define EF_RISCV_RVE              0x0008
#define EF_RISCV_TSO              0x0010

typedef struct elf32_rel {
  Elf32_Addr r_offset;
  Elf32_Word r_info;
} Elf32_Rel;

typedef struct elf64_rel {
  Elf64_Addr r_offset;
  Elf64_Xword r_info;
} Elf64_Rel;

typedef struct elf32_rela{
  Elf32_Addr r_offset;
  Elf32_Word r_info;
  Elf32_Sword r_addend;
} Elf32_Rela;

typedef struct elf64_rela {
  Elf64_Addr r_offset;
  Elf64_Xword r_info;
  Elf64_Sxword r_addend;
} Elf64_Rela;

typedef struct elf32_sym{
  Elf32_Word st_name;
  Elf32_Addr st_value;
  Elf32_Word st_size;
  unsigned char st_info;
  unsigned char st_other;
  Elf32_Half st_shndx;
} Elf32_Sym;

typedef struct elf64_sym {
  Elf64_Word st_name;
  unsigned char st_info;
  unsigned char st_other;
  Elf64_Half st_shndx;
  Elf64_Addr st_value;
  Elf64_Xword st_size;
} Elf64_Sym;


#define EI_NIDENT       16

#define PN_XNUM         0xffff

typedef struct elf32_hdr{
  unsigned char e_ident[EI_NIDENT];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
} Elf32_Ehdr;

typedef struct elf64_hdr {
  unsigned char e_ident[16];
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;
  Elf64_Off e_phoff;
  Elf64_Off e_shoff;
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
} Elf64_Ehdr;

#define PF_R 0x4
#define PF_W 0x2
#define PF_X 0x1

typedef struct elf32_phdr{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
} Elf32_Phdr;

typedef struct elf64_phdr {
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;
  Elf64_Addr p_vaddr;
  Elf64_Addr p_paddr;
  Elf64_Xword p_filesz;
  Elf64_Xword p_memsz;
  Elf64_Xword p_align;
} Elf64_Phdr;

#define SHT_NULL            0
#define SHT_PROGBITS        1
#define SHT_SYMTAB          2
#define SHT_STRTAB          3
#define SHT_RELA            4
#define SHT_HASH            5
#define SHT_DYNAMIC         6
#define SHT_NOTE            7
#define SHT_NOBITS          8
#define SHT_REL             9
#define SHT_SHLIB           10
#define SHT_DYNSYM          11
#define SHT_NUM             12
#define SHT_LOPROC          0x70000000
#define SHT_HIPROC          0x7fffffff
#define SHT_LOUSER          0x80000000
#define SHT_HIUSER          0xffffffff
#define SHT_MIPS_LIST       0x70000000
#define SHT_MIPS_CONFLICT   0x70000002
#define SHT_MIPS_GPTAB      0x70000003
#define SHT_MIPS_UCODE      0x70000004

#define SHF_WRITE       0x1
#define SHF_ALLOC       0x2
#define SHF_EXECINSTR   0x4
#define SHF_MASKPROC    0xf0000000
#define SHF_MIPS_GPREL  0x10000000

#define SHN_UNDEF           0
#define SHN_LORESERVE       0xff00
#define SHN_LOPROC          0xff00
#define SHN_HIPROC          0xff1f
#define SHN_ABS             0xfff1
#define SHN_COMMON          0xfff2
#define SHN_HIRESERVE       0xffff
#define SHN_MIPS_ACCOMON    0xff00

typedef struct elf32_shdr {
  Elf32_Word sh_name;
  Elf32_Word sh_type;
  Elf32_Word sh_flags;
  Elf32_Addr sh_addr;
  Elf32_Off sh_offset;
  Elf32_Word sh_size;
  Elf32_Word sh_link;
  Elf32_Word sh_info;
  Elf32_Word sh_addralign;
  Elf32_Word sh_entsize;
} Elf32_Shdr;

typedef struct elf64_shdr {
  Elf64_Word sh_name;
  Elf64_Word sh_type;
  Elf64_Xword sh_flags;
  Elf64_Addr sh_addr;
  Elf64_Off sh_offset;
  Elf64_Xword sh_size;
  Elf64_Word sh_link;
  Elf64_Word sh_info;
  Elf64_Xword sh_addralign;
  Elf64_Xword sh_entsize;
} Elf64_Shdr;

#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_OSABI    7
#define EI_PAD      8

#define ELFOSABI_NONE           0
#define ELFOSABI_SYSV           0
#define ELFOSABI_HPUX           1
#define ELFOSABI_NETBSD         2
#define ELFOSABI_LINUX          3
#define ELFOSABI_SOLARIS        6
#define ELFOSABI_AIX            7
#define ELFOSABI_IRIX           8
#define ELFOSABI_FREEBSD        9
#define ELFOSABI_TRU64          10
#define ELFOSABI_MODESTO        11
#define ELFOSABI_OPENBSD        12
#define ELFOSABI_ARM_FDPIC      65
#define ELFOSABI_XTENSA_FDPIC   65
#define ELFOSABI_ARM            97
#define ELFOSABI_STANDALONE     255

#define ELFMAG0         0x7f
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'
#define ELFMAG          "\177ELF"
#define SELFMAG         4

#define ELFCLASSNONE    0
#define ELFCLASS32      1
#define ELFCLASS64      2
#define ELFCLASSNUM     3

#define ELFDATANONE     0
#define ELFDATA2LSB     1
#define ELFDATA2MSB     2

#define EV_NONE         0
#define EV_CURRENT      1
#define EV_NUM          2

#define NT_PRSTATUS         1
#define NT_FPREGSET         2
#define NT_PRFPREG          2
#define NT_PRPSINFO         3
#define NT_TASKSTRUCT       4
#define NT_AUXV             6
#define NT_PRXFPREG         0x46e62b7f  /* copied from gdb5.1/include/elf/common.h */
#define NT_S390_PV_CPU_DATA 0x30e
#define NT_S390_RI_CB       0x30d
#define NT_S390_GS_CB       0x30b
#define NT_S390_VXRS_HIGH   0x30a
#define NT_S390_VXRS_LOW    0x309
#define NT_S390_PREFIX      0x305
#define NT_S390_CTRS        0x304
#define NT_S390_TODPREG     0x303
#define NT_S390_TODCMP      0x302
#define NT_S390_TIMER       0x301
#define NT_PPC_VMX          0x100
#define NT_PPC_SPE          0x101
#define NT_PPC_VSX          0x102
#define NT_ARM_VFP          0x400
#define NT_ARM_TLS          0x401
#define NT_ARM_HW_BREAK     0x402
#define NT_ARM_HW_WATCH     0x403
#define NT_ARM_SYSTEM_CALL  0x404
#define NT_ARM_SVE          0x405

#define NT_GNU_PROPERTY_TYPE_0  5

#define GNU_PROPERTY_STACK_SIZE                 1
#define GNU_PROPERTY_NO_COPY_ON_PROTECTED       2

#define GNU_PROPERTY_LOPROC                     0xc0000000
#define GNU_PROPERTY_HIPROC                     0xdfffffff
#define GNU_PROPERTY_LOUSER                     0xe0000000
#define GNU_PROPERTY_HIUSER                     0xffffffff

#define GNU_PROPERTY_AARCH64_FEATURE_1_AND      0xc0000000
#define GNU_PROPERTY_AARCH64_FEATURE_1_BTI      (1u << 0)
#define GNU_PROPERTY_AARCH64_FEATURE_1_PAC      (1u << 1)

#define XEN_ELFNOTE_PHYS32_ENTRY    18

typedef struct elf32_note {
  Elf32_Word n_namesz;
  Elf32_Word n_descsz;
  Elf32_Word n_type;
} Elf32_Nhdr;

typedef struct elf64_note {
  Elf64_Word n_namesz;
  Elf64_Word n_descsz;
  Elf64_Word n_type;
} Elf64_Nhdr;


struct elf32_fdpic_loadseg {
  Elf32_Addr addr;
  Elf32_Addr p_vaddr;
  Elf32_Word p_memsz;
};
struct elf32_fdpic_loadmap {
  Elf32_Half version;
  Elf32_Half nsegs;
  struct elf32_fdpic_loadseg segs[ ];
};

#ifdef ELF_CLASS
#if ELF_CLASS == ELFCLASS32

#define elfhdr      elf32_hdr
#define elf_phdr    elf32_phdr
#define elf_note    elf32_note
#define elf_shdr    elf32_shdr
#define elf_sym     elf32_sym
#define elf_addr_t  Elf32_Off
#define elf_rela    elf32_rela

#ifdef ELF_USES_RELOCA
# define ELF_RELOC      Elf32_Rela
#else
# define ELF_RELOC      Elf32_Rel
#endif

#else

#define elfhdr      elf64_hdr
#define elf_phdr    elf64_phdr
#define elf_note    elf64_note
#define elf_shdr    elf64_shdr
#define elf_sym     elf64_sym
#define elf_addr_t  Elf64_Off
#define elf_rela    elf64_rela

#ifdef ELF_USES_RELOCA
# define ELF_RELOC      Elf64_Rela
#else
# define ELF_RELOC      Elf64_Rel
#endif

#endif

#ifndef ElfW
# if ELF_CLASS == ELFCLASS32
#  define ElfW(x)  Elf32_ ## x
#  define ELFW(x)  ELF32_ ## x
# else
#  define ElfW(x)  Elf64_ ## x
#  define ELFW(x)  ELF64_ ## x
# endif
#endif

#endif


#endif
