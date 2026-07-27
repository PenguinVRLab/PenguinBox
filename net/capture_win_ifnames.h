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

#ifndef CAPTURE_WIN_IFNAMES_H
#define CAPTURE_WIN_IFNAMES_H

extern gboolean parse_as_guid(const char *guid_text, GUID *guid);

extern char *get_interface_friendly_name_from_device_guid(GUID *guid);

extern char *get_windows_interface_friendly_name(const char *interface_devicename);

#endif
