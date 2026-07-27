#ifndef SPARC_TARGET_SYSCALL_H
#define SPARC_TARGET_SYSCALL_H

#ifdef TARGET_SPARC64
# define UNAME_MACHINE "sparc64"
#else
# define UNAME_MACHINE "sparc"
#endif
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS
#define TARGET_MCL_CURRENT 0x2000
#define TARGET_MCL_FUTURE  0x4000
#define TARGET_MCL_ONFAULT 0x8000

#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPUSPARCState *env)
{
#ifdef TARGET_SPARC64
    return MAX(TARGET_PAGE_SIZE, 16 * 1024);
#else
    return 256 * 1024;
#endif
}

#endif
