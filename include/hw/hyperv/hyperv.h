/*
 * Hyper-V guest/hypervisor interaction
 *
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_HYPERV_H
#define HW_HYPERV_HYPERV_H

#include "exec/hwaddr.h"
#include "hw/core/cpu.h"
#include "hw/hyperv/hyperv-proto.h"

typedef struct HvSintRoute HvSintRoute;

typedef void (*HvSintMsgCb)(void *data, int status);

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintMsgCb cb, void *cb_data);
void hyperv_sint_route_ref(HvSintRoute *sint_route);
void hyperv_sint_route_unref(HvSintRoute *sint_route);

int hyperv_sint_route_set_sint(HvSintRoute *sint_route);

int hyperv_post_msg(HvSintRoute *sint_route, struct hyperv_message *msg);
int hyperv_set_event_flag(HvSintRoute *sint_route, unsigned eventno);

typedef uint16_t (*HvMsgHandler)(const struct hyperv_post_message_input *msg,
                                 void *data);
int hyperv_set_msg_handler(uint32_t conn_id, HvMsgHandler handler, void *data);
int hyperv_set_event_flag_handler(uint32_t conn_id, EventNotifier *notifier);

uint16_t hyperv_hcall_post_message(uint64_t param, bool fast);
uint16_t hyperv_hcall_signal_event(uint64_t param, bool fast);

static inline uint32_t hyperv_vp_index(CPUState *cs)
{
    return cs->cpu_index;
}

void hyperv_synic_add(CPUState *cs);
void hyperv_synic_reset(CPUState *cs);
void hyperv_synic_update(CPUState *cs, bool enable,
                         hwaddr msg_page_addr, hwaddr event_page_addr);
bool hyperv_is_synic_enabled(void);

uint16_t hyperv_hcall_reset_dbg_session(uint64_t outgpa);
uint16_t hyperv_hcall_retreive_dbg_data(uint64_t ingpa, uint64_t outgpa,
                                        bool fast);
uint16_t hyperv_hcall_post_dbg_data(uint64_t ingpa, uint64_t outgpa, bool fast);

uint32_t hyperv_syndbg_send(uint64_t ingpa, uint32_t count);
uint32_t hyperv_syndbg_recv(uint64_t ingpa, uint32_t count);
void hyperv_syndbg_set_pending_page(uint64_t ingpa);
uint64_t hyperv_syndbg_query_options(void);

typedef enum HvSynthDbgMsgType {
    HV_SYNDBG_MSG_CONNECTION_INFO,
    HV_SYNDBG_MSG_SEND,
    HV_SYNDBG_MSG_RECV,
    HV_SYNDBG_MSG_SET_PENDING_PAGE,
    HV_SYNDBG_MSG_QUERY_OPTIONS
} HvDbgSynthMsgType;

typedef struct HvSynDbgMsg {
    HvDbgSynthMsgType type;
    union {
        struct {
            uint32_t host_ip;
            uint16_t host_port;
        } connection_info;
        struct {
            uint64_t buf_gpa;
            uint32_t count;
            uint32_t pending_count;
            bool is_raw;
        } send;
        struct {
            uint64_t buf_gpa;
            uint32_t count;
            uint32_t options;
            uint64_t timeout;
            uint32_t retrieved_count;
            bool is_raw;
        } recv;
        struct {
            uint64_t buf_gpa;
        } pending_page;
        struct {
            uint64_t options;
        } query_options;
    } u;
} HvSynDbgMsg;
typedef uint16_t (*HvSynDbgHandler)(void *context, HvSynDbgMsg *msg);
void hyperv_set_syndbg_handler(HvSynDbgHandler handler, void *context);

bool hyperv_are_vmbus_recommended_features_enabled(void);
void hyperv_set_vmbus_recommended_features_enabled(void);

#endif
