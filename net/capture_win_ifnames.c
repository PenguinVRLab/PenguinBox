/*
 * Adapted from the Wireshark project
 *
 * Routines supporting the use of Windows friendly interface names within Wireshark
 * Copyright 2011-2012, Mike Garratt <wireshark@evn.co.nz>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include <ntddndis.h>
#include <netioapi.h>

#ifndef NDIS_IF_MAX_STRING_SIZE
#define NDIS_IF_MAX_STRING_SIZE IF_MAX_STRING_SIZE
#endif

#ifndef NETIO_STATUS
#define NETIO_STATUS DWORD
#endif

#include "capture_win_ifnames.h"

static int gethexdigit(const char *p)
{
    if(*p >= '0' && *p <= '9'){
        return *p - '0';
    }else if(*p >= 'A' && *p <= 'F'){
        return *p - 'A' + 0xA;
    }else if(*p >= 'a' && *p <= 'f'){
        return *p - 'a' + 0xa;
    }else{
        return -1;
    }
}

static gboolean get8hexdigits(const char *p, DWORD *d)
{
    int digit;
    DWORD val;
    int i;

    val = 0;
    for(i = 0; i < 8; i++){
        digit = gethexdigit(p++);
        if(digit == -1){
            return FALSE;
        }
        val = (val << 4) | digit;
    }
    *d = val;
    return TRUE;
}

static gboolean get4hexdigits(const char *p, WORD *w)
{
    int digit;
    WORD val;
    int i;

    val = 0;
    for(i = 0; i < 4; i++){
        digit = gethexdigit(p++);
        if(digit == -1){
            return FALSE;
        }
        val = (val << 4) | digit;
    }
    *w = val;
    return TRUE;
}

gboolean
parse_as_guid(const char *guid_text, GUID *guid)
{
    int i;
    int digit1, digit2;

    if(*guid_text != '{'){
        return FALSE;
    }
    guid_text++;
    if(!get8hexdigits(guid_text, &guid->Data1)){
        return FALSE;
    }
    guid_text += 8;
    if(*guid_text != '-'){
        return FALSE;
    }
    guid_text++;
    if(!get4hexdigits(guid_text, &guid->Data2)){
        return FALSE;
    }
    guid_text += 4;
    if(*guid_text != '-'){
        return FALSE;
    }
    guid_text++;
    if(!get4hexdigits(guid_text, &guid->Data3)){
        return FALSE;
    }
    guid_text += 4;
    if(*guid_text != '-'){
        return FALSE;
    }
    guid_text++;
    for(i = 0; i < 2; i++){
        digit1 = gethexdigit(guid_text);
        if(digit1 == -1){
            return FALSE;
        }
        guid_text++;
        digit2 = gethexdigit(guid_text);
        if(digit2 == -1){
            return FALSE;
        }
        guid_text++;
        guid->Data4[i] = (digit1 << 4)|(digit2);
    }
    if(*guid_text != '-'){
        return FALSE;
    }
    guid_text++;
    for(i = 0; i < 6; i++){
        digit1 = gethexdigit(guid_text);
        if(digit1 == -1){
            return FALSE;
        }
        guid_text++;
        digit2 = gethexdigit(guid_text);
        if(digit2 == -1){
            return FALSE;
        }
        guid_text++;
        guid->Data4[i+2] = (digit1 << 4)|(digit2);
    }
    if(*guid_text != '}'){
        return FALSE;
    }
    guid_text++;
    if(*guid_text != '\0'){
        return FALSE;
    }
    return TRUE;
}

char *
get_interface_friendly_name_from_device_guid(__in GUID *guid)
{
    HRESULT hr;

    NET_LUID InterfaceLuid;
    hr = ConvertInterfaceGuidToLuid(guid, &InterfaceLuid);
    if(hr == NO_ERROR) {
        WCHAR wName[NDIS_IF_MAX_STRING_SIZE + 1];
        hr = ConvertInterfaceLuidToAlias(&InterfaceLuid, wName, NDIS_IF_MAX_STRING_SIZE+1);
        if(hr == NO_ERROR) {

            int size;
            char *name;
            size = WideCharToMultiByte(CP_UTF8, 0, wName, -1, NULL, 0, NULL, NULL);
            if(size != 0) {
                name = (char *) g_malloc(size);
                if (name != NULL) {
                    size = WideCharToMultiByte(CP_UTF8, 0, wName, -1, name, size, NULL, NULL);
                    if(size != 0) {
                        return name;
                    }
                    g_free(name);
                }
            }
        }
    }

    return NULL;
}

char *
get_windows_interface_friendly_name(const char *interface_devicename)
{
    const char* guid_text;
    GUID guid;

    if(strncmp("\\Device\\NPF_", interface_devicename, 12)==0){
        guid_text=interface_devicename+12;
    }else{
        guid_text=interface_devicename;
    }

    if (!parse_as_guid(guid_text, &guid)){
        return NULL;
    }

    return get_interface_friendly_name_from_device_guid(&guid);
}

#endif
