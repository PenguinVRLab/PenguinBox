/*
 *  sys/user.h definitions
 *
 *  Copyright (c) 2015 Stacey D. Son (sson at FreeBSD)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_OS_USER_H
#define TARGET_OS_USER_H

struct target_priority {
    uint8_t     pri_class;
    uint8_t     pri_level;
    uint8_t     pri_native;
    uint8_t     pri_user;
};

#define TARGET_CAP_RIGHTS_VERSION  0

typedef struct target_cap_rights {
    uint64_t    cr_rights[TARGET_CAP_RIGHTS_VERSION + 2];
} target_cap_rights_t;

#define TARGET_SS_MAXSIZE     128U
#define TARGET_SS_ALIGNSIZE   (sizeof(__int64_t))
#define TARGET_SS_PAD1SIZE    (TARGET_SS_ALIGNSIZE - sizeof(unsigned char) - \
        sizeof(uint8_t))
#define TARGET_SS_PAD2SIZE    (TARGET_SS_MAXSIZE - sizeof(unsigned char) - \
        sizeof(uint8_t) - TARGET_SS_PAD1SIZE - TARGET_SS_ALIGNSIZE)

struct target_sockaddr_storage {
    unsigned char   ss_len;
    uint8_t         ss_family;
    char            __ss_pad1[TARGET_SS_PAD1SIZE];
    __int64_t       __ss_align;
    char            __ss_pad2[TARGET_SS_PAD2SIZE];
};

#define TARGET_KI_NSPARE_INT        2
#define TARGET_KI_NSPARE_LONG       12
#define TARGET_KI_NSPARE_PTR        6

#define TARGET_WMESGLEN             8
#define TARGET_LOCKNAMELEN          8
#define TARGET_TDNAMLEN             16
#define TARGET_COMMLEN              19
#define TARGET_KI_EMULNAMELEN       16
#define TARGET_KI_NGROUPS           16
#define TARGET_LOGNAMELEN           17
#define TARGET_LOGINCLASSLEN        17

#define TARGET_KF_TYPE_NONE         0
#define TARGET_KF_TYPE_VNODE        1
#define TARGET_KF_TYPE_SOCKET       2
#define TARGET_KF_TYPE_PIPE         3
#define TARGET_KF_TYPE_FIFO         4
#define TARGET_KF_TYPE_KQUEUE       5
#define TARGET_KF_TYPE_CRYPTO       6
#define TARGET_KF_TYPE_MQUEUE       7
#define TARGET_KF_TYPE_SHM          8
#define TARGET_KF_TYPE_SEM          9
#define TARGET_KF_TYPE_PTS          10
#define TARGET_KF_TYPE_PROCDESC     11
#define TARGET_KF_TYPE_DEV          12
#define TARGET_KF_TYPE_UNKNOWN      255

struct target_kinfo_proc {
    int32_t     ki_structsize;
    int32_t     ki_layout;
    abi_ulong   ki_args;
    abi_ulong   ki_paddr;
    abi_ulong   ki_addr;
    abi_ulong   ki_tracep;
    abi_ulong   ki_textvp;
    abi_ulong   ki_fd;
    abi_ulong   ki_vmspace;
    abi_ulong   ki_wchan;
    int32_t     ki_pid;
    int32_t     ki_ppid;
    int32_t     ki_pgid;
    int32_t     ki_tpgid;
    int32_t     ki_sid;
    int32_t     ki_tsid;
    int16_t     ki_jobc;
    int16_t     ki_spare_short1;
    int32_t     ki_tdev__freebsd11;
    target_sigset_t ki_siglist;
    target_sigset_t ki_sigmask;
    target_sigset_t ki_sigignore;
    target_sigset_t ki_sigcatch;

    int32_t     ki_uid;
    int32_t     ki_ruid;
    int32_t     ki_svuid;
    int32_t     ki_rgid;
    int32_t     ki_svgid;
    int16_t     ki_ngroups;
    int16_t     ki_spare_short2;
    int32_t     ki_groups[TARGET_KI_NGROUPS];

    abi_long    ki_size;

    abi_long    ki_rssize;
    abi_long    ki_swrss;
    abi_long    ki_tsize;
    abi_long    ki_dsize;
    abi_long    ki_ssize;

    uint16_t    ki_xstat;
    uint16_t    ki_acflag;

    uint32_t    ki_pctcpu;

    uint32_t    ki_estcpu;
    uint32_t    ki_slptime;
    uint32_t    ki_swtime;
    uint32_t    ki_cow;
    uint64_t    ki_runtime;

    struct  target_freebsd_timeval ki_start;
    struct  target_freebsd_timeval ki_childtime;

    abi_long    ki_flag;
    abi_long    ki_kiflag;
    int32_t     ki_traceflag;
    char        ki_stat;
    int8_t      ki_nice;
    char        ki_lock;
    char        ki_rqindex;
    u_char      ki_oncpu_old;
    u_char      ki_lastcpu_old;
    char        ki_tdname[TARGET_TDNAMLEN + 1];
    char        ki_wmesg[TARGET_WMESGLEN + 1];
    char        ki_login[TARGET_LOGNAMELEN + 1];
    char        ki_lockname[TARGET_LOCKNAMELEN + 1];
    char        ki_comm[TARGET_COMMLEN + 1];
    char        ki_emul[TARGET_KI_EMULNAMELEN + 1];
    char        ki_loginclass[TARGET_LOGINCLASSLEN + 1];

    char        ki_sparestrings[50];
    int32_t     ki_spareints[TARGET_KI_NSPARE_INT];
    uint64_t ki_tdev;
    int32_t     ki_oncpu;
    int32_t     ki_lastcpu;
    int32_t     ki_tracer;
    int32_t     ki_flag2;
    int32_t     ki_fibnum;
    uint32_t    ki_cr_flags;
    int32_t     ki_jid;
    int32_t     ki_numthreads;

    int32_t     ki_tid;

    struct  target_priority ki_pri;
    struct  target_freebsd_rusage ki_rusage;
    struct  target_freebsd_rusage ki_rusage_ch;


    abi_ulong   ki_pcb;
    abi_ulong   ki_kstack;
    abi_ulong   ki_udata;
    abi_ulong   ki_tdaddr;

    abi_ulong   ki_spareptrs[TARGET_KI_NSPARE_PTR];
    abi_long    ki_sparelongs[TARGET_KI_NSPARE_LONG];
    abi_long    ki_sflag;
    abi_long    ki_tdflags;
};

struct target_kinfo_file {
    int32_t  kf_structsize;
    int32_t  kf_type;
    int32_t  kf_fd;
    int32_t  kf_ref_count;
    int32_t  kf_flags;
    int32_t  kf_pad0;
    int64_t  kf_offset;
    union {
        struct {
            uint32_t kf_spareint;
            int  kf_sock_domain0;
            int  kf_sock_type0;
            int  kf_sock_protocol0;
            struct sockaddr_storage kf_sa_local;
            struct sockaddr_storage kf_sa_peer;
            uint64_t kf_sock_pcb;
            uint64_t kf_sock_inpcb;
            uint64_t kf_sock_unpconn;
            uint16_t kf_sock_snd_sb_state;
            uint16_t kf_sock_rcv_sb_state;
            uint32_t kf_sock_pad0;
        } kf_sock;
        struct {
            int  kf_file_type;
            int  kf_spareint[3];
            uint64_t kf_spareint64[30];
            uint64_t kf_file_fsid;
            uint64_t kf_file_rdev;
            uint64_t kf_file_fileid;
            uint64_t kf_file_size;
            uint32_t kf_file_fsid_freebsd11;
            uint32_t kf_file_rdev_freebsd11;
            uint16_t kf_file_mode;
            uint16_t kf_file_pad0;
            uint32_t kf_file_pad1;
        } kf_file;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            uint32_t kf_sem_value;
            uint16_t kf_sem_mode;
        } kf_sem;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            uint64_t kf_pipe_addr;
            uint64_t kf_pipe_peer;
            uint32_t kf_pipe_buffer_cnt;
            uint32_t kf_pipe_pad0[3];
        } kf_pipe;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            uint32_t kf_pts_dev_freebsd11;
            uint32_t kf_pts_pad0;
            uint64_t kf_pts_dev;
            uint32_t kf_pts_pad1[4];
        } kf_pts;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            int32_t  kf_pid;
        } kf_proc;
    } kf_un;
    uint16_t kf_status;
    uint16_t kf_pad1;
    int32_t  _kf_ispare0;
    target_cap_rights_t kf_cap_rights;
    uint64_t _kf_cap_spare;
    char  kf_path[PATH_MAX];
};

struct target_kinfo_vmentry {
    int32_t  kve_structsize;
    int32_t  kve_type;
    uint64_t kve_start;
    uint64_t kve_end;
    uint64_t kve_offset;
    uint64_t kve_vn_fileid;
    uint32_t kve_vn_fsid_freebsd11;
    int32_t  kve_flags;
    int32_t  kve_resident;
    int32_t  kve_private_resident;
    int32_t  kve_protection;
    int32_t  kve_ref_count;
    int32_t  kve_shadow_count;
    int32_t  kve_vn_type;
    uint64_t kve_vn_size;
    uint32_t kve_vn_rdev_freebsd11;
    uint16_t kve_vn_mode;
    uint16_t kve_status;
#if (__FreeBSD_version >= 1300501 && __FreeBSD_version < 1400000) ||    \
    __FreeBSD_version >= 1400009
    union {
        uint64_t _kve_vn_fsid;
        uint64_t _kve_obj;
    } kve_type_spec;
#define kve_vn_fsid kve_type_spec._kve_vn_fsid
#define kve_obj  kve_type_spec._kve_obj
#else
    uint64_t kve_vn_fsid;
#endif
    uint64_t kve_vn_rdev;
    int  _kve_ispare[8];
    char  kve_path[PATH_MAX];
};

#endif
