/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) $Header: /tcpdump/master/libpcap/pcap/pcap.h,v 1.4.2.11 2008-10-06 15:38:39 gianluca Exp $ (LBL)
 */

#ifndef lib_pcap_pcap_h
#define lib_pcap_pcap_h

#if defined(WIN32)
  #include <pcap-stdinc.h>
#elif defined(MSDOS)
  #include <sys/types.h>
  #include <sys/socket.h>
#else
  #include <sys/types.h>
  #include <sys/time.h>
#endif

#ifndef PCAP_DONT_INCLUDE_PCAP_BPF_H
#include <pcap/bpf.h>
#endif

#include <stdio.h>

#ifdef HAVE_REMOTE
	#ifndef SOCKET
		#ifdef WIN32
			#define SOCKET unsigned int
		#else
			#define SOCKET int
		#endif
	#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

#define PCAP_ERRBUF_SIZE 256

#if BPF_RELEASE - 0 < 199406
typedef	int bpf_int32;
typedef	u_int bpf_u_int32;
#endif

typedef struct pcap pcap_t;
typedef struct pcap_dumper pcap_dumper_t;
typedef struct pcap_if pcap_if_t;
typedef struct pcap_addr pcap_addr_t;

struct pcap_file_header {
	bpf_u_int32 magic;
	u_short version_major;
	u_short version_minor;
	bpf_int32 thiszone;
	bpf_u_int32 sigfigs;
	bpf_u_int32 snaplen;
	bpf_u_int32 linktype;
};

#define LT_FCS_LENGTH_PRESENT(x)	((x) & 0x04000000)
#define LT_FCS_LENGTH(x)		(((x) & 0xF0000000) >> 28)
#define LT_FCS_DATALINK_EXT(x)		((((x) & 0xF) << 28) | 0x04000000)

typedef enum {
       PCAP_D_INOUT = 0,
       PCAP_D_IN,
       PCAP_D_OUT
} pcap_direction_t;

struct pcap_pkthdr {
	struct timeval ts;
	bpf_u_int32 caplen;
	bpf_u_int32 len;
};

struct pcap_stat {
	u_int ps_recv;
	u_int ps_drop;
	u_int ps_ifdrop;
#ifdef HAVE_REMOTE
	u_int ps_capt;
	u_int ps_sent;
	u_int ps_netdrop;
#endif
};

#ifdef MSDOS
struct pcap_stat_ex {
       u_long  rx_packets;
       u_long  tx_packets;
       u_long  rx_bytes;
       u_long  tx_bytes;
       u_long  rx_errors;
       u_long  tx_errors;
       u_long  rx_dropped;
       u_long  tx_dropped;
       u_long  multicast;
       u_long  collisions;

       u_long  rx_length_errors;
       u_long  rx_over_errors;
       u_long  rx_crc_errors;
       u_long  rx_frame_errors;
       u_long  rx_fifo_errors;
       u_long  rx_missed_errors;

       u_long  tx_aborted_errors;
       u_long  tx_carrier_errors;
       u_long  tx_fifo_errors;
       u_long  tx_heartbeat_errors;
       u_long  tx_window_errors;
     };
#endif

struct pcap_if {
	struct pcap_if *next;
	char *name;
	char *description;
	struct pcap_addr *addresses;
	bpf_u_int32 flags;
};

#define PCAP_IF_LOOPBACK	0x00000001

struct pcap_addr {
	struct pcap_addr *next;
	struct sockaddr *addr;
	struct sockaddr *netmask;
	struct sockaddr *broadaddr;
	struct sockaddr *dstaddr;
};

typedef void (*pcap_handler)(u_char *, const struct pcap_pkthdr *,
			     const u_char *);

#define PCAP_ERROR			-1
#define PCAP_ERROR_BREAK		-2
#define PCAP_ERROR_NOT_ACTIVATED	-3
#define PCAP_ERROR_ACTIVATED		-4
#define PCAP_ERROR_NO_SUCH_DEVICE	-5
#define PCAP_ERROR_RFMON_NOTSUP		-6
#define PCAP_ERROR_NOT_RFMON		-7
#define PCAP_ERROR_PERM_DENIED		-8
#define PCAP_ERROR_IFACE_NOT_UP		-9

#define PCAP_WARNING			1
#define PCAP_WARNING_PROMISC_NOTSUP	2

char	*pcap_lookupdev(char *);
int	pcap_lookupnet(const char *, bpf_u_int32 *, bpf_u_int32 *, char *);

pcap_t	*pcap_create(const char *, char *);
int	pcap_set_snaplen(pcap_t *, int);
int	pcap_set_promisc(pcap_t *, int);
int	pcap_can_set_rfmon(pcap_t *);
int	pcap_set_rfmon(pcap_t *, int);
int	pcap_set_timeout(pcap_t *, int);
int	pcap_set_buffer_size(pcap_t *, int);
int	pcap_activate(pcap_t *);

pcap_t	*pcap_open_live(const char *, int, int, int, char *);
pcap_t	*pcap_open_dead(int, int);
pcap_t	*pcap_open_offline(const char *, char *);
#if defined(WIN32)
pcap_t  *pcap_hopen_offline(intptr_t, char *);
#if !defined(LIBPCAP_EXPORTS)
#define pcap_fopen_offline(f,b) \
	pcap_hopen_offline(_get_osfhandle(_fileno(f)), b)
#else
static pcap_t *pcap_fopen_offline(FILE *, char *);
#endif
#else
pcap_t	*pcap_fopen_offline(FILE *, char *);
#endif

void	pcap_close(pcap_t *);
int	pcap_loop(pcap_t *, int, pcap_handler, u_char *);
int	pcap_dispatch(pcap_t *, int, pcap_handler, u_char *);
const u_char*
	pcap_next(pcap_t *, struct pcap_pkthdr *);
int 	pcap_next_ex(pcap_t *, struct pcap_pkthdr **, const u_char **);
void	pcap_breakloop(pcap_t *);
int	pcap_stats(pcap_t *, struct pcap_stat *);
int	pcap_setfilter(pcap_t *, struct bpf_program *);
int 	pcap_setdirection(pcap_t *, pcap_direction_t);
int	pcap_getnonblock(pcap_t *, char *);
int	pcap_setnonblock(pcap_t *, int, char *);
int	pcap_inject(pcap_t *, const void *, size_t);
int	pcap_sendpacket(pcap_t *, const u_char *, int);
const char *pcap_statustostr(int);
const char *pcap_strerror(int);
char	*pcap_geterr(pcap_t *);
void	pcap_perror(pcap_t *, char *);
int	pcap_compile(pcap_t *, struct bpf_program *, const char *, int,
	    bpf_u_int32);
int	pcap_compile_nopcap(int, int, struct bpf_program *,
	    const char *, int, bpf_u_int32);
void	pcap_freecode(struct bpf_program *);
int	pcap_offline_filter(struct bpf_program *, const struct pcap_pkthdr *,
	    const u_char *);
int	pcap_datalink(pcap_t *);
int	pcap_datalink_ext(pcap_t *);
int	pcap_list_datalinks(pcap_t *, int **);
int	pcap_set_datalink(pcap_t *, int);
void	pcap_free_datalinks(int *);
int	pcap_datalink_name_to_val(const char *);
const char *pcap_datalink_val_to_name(int);
const char *pcap_datalink_val_to_description(int);
int	pcap_snapshot(pcap_t *);
int	pcap_is_swapped(pcap_t *);
int	pcap_major_version(pcap_t *);
int	pcap_minor_version(pcap_t *);

FILE	*pcap_file(pcap_t *);
int	pcap_fileno(pcap_t *);

pcap_dumper_t *pcap_dump_open(pcap_t *, const char *);
pcap_dumper_t *pcap_dump_fopen(pcap_t *, FILE *fp);
FILE	*pcap_dump_file(pcap_dumper_t *);
long	pcap_dump_ftell(pcap_dumper_t *);
int	pcap_dump_flush(pcap_dumper_t *);
void	pcap_dump_close(pcap_dumper_t *);
void	pcap_dump(u_char *, const struct pcap_pkthdr *, const u_char *);

int	pcap_findalldevs(pcap_if_t **, char *);
void	pcap_freealldevs(pcap_if_t *);

const char *pcap_lib_version(void);

u_int	bpf_filter(const struct bpf_insn *, const u_char *, u_int, u_int);
int	bpf_validate(const struct bpf_insn *f, int len);
char	*bpf_image(const struct bpf_insn *, int);
void	bpf_dump(const struct bpf_program *, int);

#if defined(WIN32)

int pcap_setbuff(pcap_t *p, int dim);
int pcap_setmode(pcap_t *p, int mode);
int pcap_setmintocopy(pcap_t *p, int size);

#include "Win32-Extensions.h"

#define MODE_CAPT 0
#define MODE_STAT 1
#define MODE_MON 2

#elif defined(MSDOS)

int  pcap_stats_ex (pcap_t *, struct pcap_stat_ex *);
void pcap_set_wait (pcap_t *p, void (*yield)(void), int wait);
u_long pcap_mac_packets (void);

#else

int	pcap_get_selectable_fd(pcap_t *);

#endif

#ifdef HAVE_REMOTE
#include <remote-ext.h>
#endif

#ifdef __cplusplus
}
#endif

#endif
