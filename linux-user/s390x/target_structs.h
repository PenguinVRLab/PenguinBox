/*
 * S/390 specific structures for linux-user
 *
 * Copyright (c) 2013 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef S390X_TARGET_STRUCTS_H
#define S390X_TARGET_STRUCTS_H


struct target_ipc_perm {
    abi_int __key;
    abi_uint uid;
    abi_uint gid;
    abi_uint cuid;
    abi_uint cgid;
#if TARGET_ABI_BITS == 64
    abi_uint mode;
#else
    abi_ushort mode;
    abi_ushort __pad1;
#endif
    abi_ushort __seq;
    abi_ushort __pad2;
    abi_ulong __unused1;
    abi_ulong __unused2;
};

struct target_shmid_ds {
    struct target_ipc_perm shm_perm;
    abi_long shm_segsz;
    abi_ulong shm_atime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused1;
#endif
    abi_ulong shm_dtime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused2;
#endif
    abi_ulong shm_ctime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused3;
#endif
    abi_int shm_cpid;
    abi_int shm_lpid;
    abi_ulong shm_nattch;
    abi_ulong __unused4;
    abi_ulong __unused5;
};

#endif
