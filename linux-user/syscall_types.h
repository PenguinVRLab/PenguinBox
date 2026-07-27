STRUCT_SPECIAL(termios)

STRUCT(winsize,
       TYPE_SHORT, TYPE_SHORT, TYPE_SHORT, TYPE_SHORT)

STRUCT(serial_multiport_struct,
       TYPE_INT, TYPE_INT, TYPE_CHAR, TYPE_CHAR, TYPE_INT, TYPE_CHAR, TYPE_CHAR,
       TYPE_INT, TYPE_CHAR, TYPE_CHAR, TYPE_INT, TYPE_CHAR, TYPE_CHAR, TYPE_INT,
       MK_ARRAY(TYPE_INT, 32))

STRUCT(serial_icounter_struct,
       TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, MK_ARRAY(TYPE_INT, 16))

STRUCT(sockaddr,
       TYPE_SHORT, MK_ARRAY(TYPE_CHAR, 14))

STRUCT(rtentry,
       TYPE_ULONG, MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr),
       TYPE_SHORT, TYPE_SHORT, TYPE_ULONG, TYPE_PTRVOID, TYPE_SHORT, TYPE_PTRVOID,
       TYPE_ULONG, TYPE_ULONG, TYPE_SHORT)

STRUCT(ifmap,
       TYPE_ULONG, TYPE_ULONG, TYPE_SHORT, TYPE_CHAR, TYPE_CHAR, TYPE_CHAR,
       TYPE_CHAR, TYPE_CHAR, TYPE_CHAR)

STRUCT(sockaddr_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), MK_STRUCT(STRUCT_sockaddr))

STRUCT(short_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), TYPE_SHORT)

STRUCT(int_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), TYPE_INT)

STRUCT(ifmap_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), MK_STRUCT(STRUCT_ifmap))

STRUCT(char_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ),
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ))

STRUCT(ptr_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), TYPE_PTRVOID)

STRUCT(ifconf,
       TYPE_INT, TYPE_PTRVOID)

STRUCT(arpreq,
       MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr), TYPE_INT, MK_STRUCT(STRUCT_sockaddr),
       MK_ARRAY(TYPE_CHAR, 16))

STRUCT(arpreq_old,
       MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr), TYPE_INT, MK_STRUCT(STRUCT_sockaddr))

STRUCT(cdrom_read_audio,
       TYPE_CHAR, TYPE_CHAR, TYPE_CHAR, TYPE_CHAR, TYPE_CHAR, TYPE_INT, TYPE_PTRVOID,
       TYPE_NULL)

STRUCT(hd_geometry,
       TYPE_CHAR, TYPE_CHAR, TYPE_SHORT, TYPE_ULONG)

STRUCT(dirent,
       TYPE_LONG, TYPE_LONG, TYPE_SHORT, MK_ARRAY(TYPE_CHAR, 256))

STRUCT(kbentry,
       TYPE_CHAR, TYPE_CHAR, TYPE_SHORT)

STRUCT(kbsentry,
       TYPE_CHAR, MK_ARRAY(TYPE_CHAR, 512))

STRUCT(audio_buf_info,
       TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT)

STRUCT(count_info,
       TYPE_INT, TYPE_INT, TYPE_INT)

STRUCT(buffmem_desc,
       TYPE_PTRVOID, TYPE_INT)

STRUCT(mixer_info,
       MK_ARRAY(TYPE_CHAR, 16), MK_ARRAY(TYPE_CHAR, 32), TYPE_INT, MK_ARRAY(TYPE_INT, 10))

STRUCT(snd_timer_id,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT)

STRUCT(snd_timer_ginfo,
       MK_STRUCT(STRUCT_snd_timer_id),
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 64),
       MK_ARRAY(TYPE_CHAR, 80),
       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 32))

STRUCT(snd_timer_gparams,
       MK_STRUCT(STRUCT_snd_timer_id),
       TYPE_ULONG,
       TYPE_ULONG,
       MK_ARRAY(TYPE_CHAR, 32))

STRUCT(snd_timer_gstatus,
       MK_STRUCT(STRUCT_snd_timer_id),
       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_ULONG,
       MK_ARRAY(TYPE_CHAR, 32))

STRUCT(snd_timer_select,
       MK_STRUCT(STRUCT_snd_timer_id),
       MK_ARRAY(TYPE_CHAR, 32))

STRUCT(snd_timer_info,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 64),
       MK_ARRAY(TYPE_CHAR, 80),
       TYPE_ULONG,
       TYPE_ULONG,
       MK_ARRAY(TYPE_CHAR, 64))

STRUCT(snd_timer_params,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 60))

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
STRUCT(timeval,
       TYPE_LONG,
       TYPE_INT)

STRUCT(_kernel_sock_timeval,
       TYPE_LONG,
       TYPE_INT)
#else
STRUCT(timeval,
       TYPE_LONG,
       TYPE_LONG)

STRUCT(_kernel_sock_timeval,
       TYPE_LONGLONG,
       TYPE_LONGLONG)
#endif

STRUCT(timespec,
       TYPE_LONG,
       TYPE_LONG)

STRUCT(_kernel_timespec,
       TYPE_LONGLONG,
       TYPE_LONGLONG)

STRUCT(snd_timer_status,
       MK_STRUCT(STRUCT_timespec),
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 64))

STRUCT(loop_info,
       TYPE_INT,
       TYPE_OLDDEVT,
       TYPE_ULONG,
       TYPE_OLDDEVT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 64),
       MK_ARRAY(TYPE_CHAR, 32),
       MK_ARRAY(TYPE_ULONG, 2),
       MK_ARRAY(TYPE_CHAR, 4))

STRUCT(loop_info64,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 64),
       MK_ARRAY(TYPE_CHAR, 64),
       MK_ARRAY(TYPE_CHAR, 32),
       MK_ARRAY(TYPE_ULONGLONG, 2))

STRUCT(loop_config,
       TYPE_INT,
       TYPE_INT,
       MK_STRUCT(STRUCT_loop_info64),
       MK_ARRAY(TYPE_ULONGLONG, 8))

STRUCT(mtop, TYPE_SHORT, TYPE_INT)
STRUCT(mtget, TYPE_LONG, TYPE_LONG, TYPE_LONG, TYPE_LONG, TYPE_LONG,
       TYPE_INT, TYPE_INT)
STRUCT(mtpos, TYPE_LONG)

STRUCT(fb_fix_screeninfo,
       MK_ARRAY(TYPE_CHAR, 16),
       TYPE_ULONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_SHORT,
       TYPE_SHORT,
       TYPE_SHORT,
       TYPE_INT,
       TYPE_ULONG,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 3))

STRUCT(fb_var_screeninfo,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_INT, 3),
       MK_ARRAY(TYPE_INT, 3),
       MK_ARRAY(TYPE_INT, 3),
       MK_ARRAY(TYPE_INT, 3),
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_INT, 5))

STRUCT(fb_cmap,
       TYPE_INT,
       TYPE_INT,
       TYPE_PTRVOID,
       TYPE_PTRVOID,
       TYPE_PTRVOID,
       TYPE_PTRVOID)

STRUCT(fb_con2fbmap,
       TYPE_INT,
       TYPE_INT)


STRUCT(vt_stat,
       TYPE_SHORT,
       TYPE_SHORT,
       TYPE_SHORT)

STRUCT(vt_mode,
       TYPE_CHAR,
       TYPE_CHAR,
       TYPE_SHORT,
       TYPE_SHORT,
       TYPE_SHORT)

STRUCT(dm_ioctl,
       MK_ARRAY(TYPE_INT, 3),
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_CHAR, 128),
       MK_ARRAY(TYPE_CHAR, 129),
       MK_ARRAY(TYPE_CHAR, 7))

STRUCT(dm_target_spec,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, 16))

STRUCT(dm_target_deps,
       TYPE_INT,
       TYPE_INT)

STRUCT(dm_name_list,
       TYPE_ULONGLONG,
       TYPE_INT)

STRUCT(dm_target_versions,
       TYPE_INT,
       MK_ARRAY(TYPE_INT, 3))

STRUCT(dm_target_msg,
       TYPE_ULONGLONG)

STRUCT(drm_version,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONG,
       TYPE_PTRVOID,
       TYPE_ULONG,
       TYPE_PTRVOID,
       TYPE_ULONG,
       TYPE_PTRVOID)

STRUCT(drm_i915_getparam,
       TYPE_INT,
       TYPE_PTRVOID)

STRUCT(file_clone_range,
       TYPE_LONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(fstrim_range,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(fiemap_extent,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_ULONGLONG, 2),
       TYPE_INT,
       MK_ARRAY(TYPE_INT, 3))

STRUCT(fiemap,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT)

STRUCT(blkpg_partition,
       TYPE_LONGLONG,
       TYPE_LONGLONG,
       TYPE_INT,
       MK_ARRAY(TYPE_CHAR, BLKPG_DEVNAMELTH),
       MK_ARRAY(TYPE_CHAR, BLKPG_VOLNAMELTH))

#if defined(BTRFS_IOC_SUBVOL_CREATE) || defined(BTRFS_IOC_SNAP_CREATE) || \
    defined(BTRFS_IOC_SNAP_DESTROY)  || defined(BTRFS_IOC_SCAN_DEV)  || \
    defined(BTRFS_IOC_FORGET_DEV)    || defined(BTRFS_IOC_ADD_DEV) || \
    defined(BTRFS_IOC_RM_DEV)        || defined(BTRFS_IOC_DEV_INFO)
STRUCT(btrfs_ioctl_vol_args,
       TYPE_LONGLONG,
       MK_ARRAY(TYPE_CHAR, BTRFS_PATH_NAME_MAX + 1))
#endif

#ifdef BTRFS_IOC_GET_SUBVOL_INFO
STRUCT(btrfs_ioctl_timespec,
       TYPE_ULONGLONG,
       TYPE_INT)

STRUCT(btrfs_ioctl_get_subvol_info_args,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_CHAR, BTRFS_VOL_NAME_MAX + 1),
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE),
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE),
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE),
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec),
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec),
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec),
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec),
       MK_ARRAY(TYPE_ULONGLONG, 8))
#endif

#ifdef BTRFS_IOC_INO_LOOKUP
STRUCT(btrfs_ioctl_ino_lookup_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_CHAR, BTRFS_INO_LOOKUP_PATH_MAX))
#endif

#ifdef BTRFS_IOC_INO_PATHS
STRUCT(btrfs_ioctl_ino_path_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_ULONGLONG, 4),
       TYPE_ULONGLONG)
#endif

#if defined(BTRFS_IOC_LOGICAL_INO) || defined(BTRFS_IOC_LOGICAL_INO_V2)
STRUCT(btrfs_ioctl_logical_ino_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_ULONGLONG, 3),
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)
#endif

#ifdef BTRFS_IOC_INO_LOOKUP_USER
STRUCT(btrfs_ioctl_ino_lookup_user_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_CHAR, BTRFS_VOL_NAME_MAX + 1),
       MK_ARRAY(TYPE_CHAR, BTRFS_INO_LOOKUP_USER_PATH_MAX))
#endif

#if defined(BTRFS_IOC_SCRUB) || defined(BTRFS_IOC_SCRUB_PROGRESS)
STRUCT(btrfs_scrub_progress,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(btrfs_ioctl_scrub_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_STRUCT(STRUCT_btrfs_scrub_progress),
       MK_ARRAY(TYPE_ULONGLONG,
                (1024 - 32 -
                 sizeof(struct btrfs_scrub_progress)) / 8))
#endif

#ifdef BTRFS_IOC_DEV_INFO
STRUCT(btrfs_ioctl_dev_info_args,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE),
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_ULONGLONG, 379),
       MK_ARRAY(TYPE_CHAR, BTRFS_DEVICE_PATH_NAME_MAX))
#endif

#ifdef BTRFS_IOC_GET_SUBVOL_ROOTREF
STRUCT(rootref,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(btrfs_ioctl_get_subvol_rootref_args,
       TYPE_ULONGLONG,
       MK_ARRAY(MK_STRUCT(STRUCT_rootref),
                BTRFS_MAX_ROOTREF_BUFFER_NUM),
       TYPE_CHAR,
       MK_ARRAY(TYPE_CHAR, 7))
#endif

#ifdef BTRFS_IOC_GET_DEV_STATS
STRUCT(btrfs_ioctl_get_dev_stats,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_ULONGLONG, BTRFS_DEV_STAT_VALUES_MAX),
       MK_ARRAY(TYPE_ULONGLONG,
                128 - 2 - BTRFS_DEV_STAT_VALUES_MAX))
#endif

STRUCT(btrfs_ioctl_quota_ctl_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(btrfs_ioctl_quota_rescan_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       MK_ARRAY(TYPE_ULONGLONG, 6))

STRUCT(btrfs_ioctl_qgroup_assign_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(btrfs_ioctl_qgroup_create_args,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(btrfs_qgroup_limit,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(btrfs_ioctl_qgroup_limit_args,
       TYPE_ULONGLONG,
       MK_STRUCT(STRUCT_btrfs_qgroup_limit))

STRUCT(btrfs_ioctl_feature_flags,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG)

STRUCT(rtc_time,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT)

STRUCT(rtc_wkalrm,
       TYPE_CHAR,
       TYPE_CHAR,
       MK_STRUCT(STRUCT_rtc_time))

STRUCT(rtc_pll_info,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_LONG)

STRUCT(blkpg_ioctl_arg,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_PTRVOID)

STRUCT(format_descr,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT)

STRUCT(floppy_max_errors,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT)

#if defined(CONFIG_USBFS)
STRUCT(usbdevfs_ctrltransfer,
        TYPE_CHAR,
        TYPE_CHAR,
        TYPE_SHORT,
        TYPE_SHORT,
        TYPE_SHORT,
        TYPE_INT,
        TYPE_PTRVOID)

STRUCT(usbdevfs_bulktransfer,
        TYPE_INT,
        TYPE_INT,
        TYPE_INT,
        TYPE_PTRVOID)

STRUCT(usbdevfs_setinterface,
        TYPE_INT,
        TYPE_INT)

STRUCT(usbdevfs_disconnectsignal,
        TYPE_INT,
        TYPE_PTRVOID)

STRUCT(usbdevfs_getdriver,
        TYPE_INT,
        MK_ARRAY(TYPE_CHAR, USBDEVFS_MAXDRIVERNAME + 1))

STRUCT(usbdevfs_connectinfo,
        TYPE_INT,
        TYPE_CHAR)

STRUCT(usbdevfs_iso_packet_desc,
        TYPE_INT,
        TYPE_INT,
        TYPE_INT)

STRUCT(usbdevfs_urb,
        TYPE_CHAR,
        TYPE_CHAR,
        TYPE_INT,
        TYPE_INT,
        TYPE_PTRVOID,
        TYPE_INT,
        TYPE_INT,
        TYPE_INT,
        TYPE_INT,
        TYPE_INT,
        TYPE_INT,
        TYPE_PTRVOID,
        MK_ARRAY(MK_STRUCT(STRUCT_usbdevfs_iso_packet_desc), 0))

STRUCT(usbdevfs_ioctl,
        TYPE_INT,
        TYPE_INT,
        TYPE_PTRVOID)

STRUCT(usbdevfs_hub_portinfo,
        TYPE_CHAR,
        MK_ARRAY(TYPE_CHAR, 127))

STRUCT(usbdevfs_disconnect_claim,
        TYPE_INT,
        TYPE_INT,
        MK_ARRAY(TYPE_CHAR, USBDEVFS_MAXDRIVERNAME + 1))
#endif
