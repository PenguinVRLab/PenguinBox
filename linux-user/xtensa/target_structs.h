#ifndef XTENSA_TARGET_STRUCTS_H
#define XTENSA_TARGET_STRUCTS_H

struct target_ipc_perm {
    abi_int __key;
    abi_uint uid;
    abi_uint gid;
    abi_uint cuid;
    abi_uint cgid;
    abi_uint mode;
    abi_ulong __seq;
    abi_ulong __unused1;
    abi_ulong __unused2;
};

struct target_semid64_ds {
  struct target_ipc_perm sem_perm;
#if TARGET_BIG_ENDIAN
  abi_ulong __unused1;
  abi_ulong sem_otime;
  abi_ulong __unused2;
  abi_ulong sem_ctime;
#else
  abi_ulong sem_otime;
  abi_ulong __unused1;
  abi_ulong sem_ctime;
  abi_ulong __unused2;
#endif
  abi_ulong sem_nsems;
  abi_ulong __unused3;
  abi_ulong __unused4;
};
#define TARGET_SEMID64_DS

struct target_shmid_ds {
    struct target_ipc_perm shm_perm;
    abi_long shm_segsz;
    abi_long shm_atime;
    abi_ulong __unused1;
    abi_long shm_dtime;
    abi_ulong __unused2;
    abi_long shm_ctime;
    abi_ulong __unused3;
    abi_uint shm_cpid;
    abi_uint shm_lpid;
    abi_ulong shm_nattch;
    abi_ulong __unused4;
    abi_ulong __unused5;
};

#endif
