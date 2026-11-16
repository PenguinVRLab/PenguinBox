/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#ifndef XEMU_SIM_DBUS_STANDALONE
#include "qemu/osdep.h"
#endif
#include <stdio.h>
#include <string.h>

#include <gio/gio.h>

#include "xemu-sim-dbus.h"

static struct {
    GMutex lock;
    GThread *thread;
    GVariant *flags;
    volatile gint cancel;
    volatile gint end;
    char why[320];
} g_dbus;

bool xemu_sim_dbus_available(void)
{
    return true;
}

static void finish(SimDbusEnd e, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void finish(SimDbusEnd e, const char *fmt, ...)
{
    va_list ap;
    g_mutex_lock(&g_dbus.lock);
    va_start(ap, fmt);
    vsnprintf(g_dbus.why, sizeof(g_dbus.why), fmt, ap);
    va_end(ap);
    g_mutex_unlock(&g_dbus.lock);
    g_atomic_int_set(&g_dbus.end, e);
}

static bool has_owner(GDBusConnection *bus, bool *owned)
{
    GError *err = NULL;
    GVariant *r = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus", "NameHasOwner",
                                              g_variant_new("(s)", SIM_DBUS_NAME), G_VARIANT_TYPE("(b)"),
                                              G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (!r) {
        g_error_free(err);
        return false;
    }
    gboolean b = FALSE;
    g_variant_get(r, "(b)", &b);
    g_variant_unref(r);
    *owned = b;
    return true;
}

static gpointer run(gpointer data)
{
    GVariant *flags = data;
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        finish(SIM_DBUS_FAILED, "no session bus: %s", err->message);
        g_error_free(err);
        g_variant_unref(flags);
        return NULL;
    }
    GVariantBuilder pd;
    g_variant_builder_init(&pd, G_VARIANT_TYPE("a{sv}"));
    GVariantBuilder av;
    g_variant_builder_init(&av, G_VARIANT_TYPE("av"));
    g_variant_builder_add(&av, "v", flags);
    GVariant *r = g_dbus_connection_call_sync(bus, SIM_DBUS_NAME, SIM_DBUS_PATH, "org.freedesktop.Application",
                                              "ActivateAction",
                                              g_variant_new("(sava{sv})", "configure-title", &av, &pd), NULL,
                                              G_DBUS_CALL_FLAGS_NONE, 120000, NULL, &err);
    g_variant_unref(flags);
    if (!r) {
        char *remote = g_dbus_error_get_remote_error(err);
        g_dbus_error_strip_remote_error(err);
        SimDbusEnd e = SIM_DBUS_FAILED;
        if (remote && strcmp(remote, SIM_DBUS_NAME ".Error.Usage") == 0) {
            e = SIM_DBUS_USAGE;
        } else if (remote && strcmp(remote, SIM_DBUS_NAME ".Error.Busy") == 0) {
#ifdef SIM_DBUS_DRILL_BUSY_ACCEPTED
            e = SIM_DBUS_FAILED;
#else
            e = SIM_DBUS_BUSY;
#endif
        } else if (remote && (strcmp(remote, "org.freedesktop.DBus.Error.ServiceUnknown") == 0 ||
                              strcmp(remote, "org.freedesktop.DBus.Error.NameHasNoOwner") == 0 ||
                              strncmp(remote, "org.freedesktop.DBus.Error.Spawn.", 33) == 0)) {
            e = SIM_DBUS_UNKNOWN;
        }
        finish(e, "%s: %s", remote ? remote : "the call failed", err->message);
        g_free(remote);
        g_error_free(err);
        g_object_unref(bus);
        return NULL;
    }
    g_variant_unref(r);
    for (;;) {
        if (g_atomic_int_get(&g_dbus.cancel)) {
            finish(SIM_DBUS_CANCELLED, "the wait was given up from PenguinBox (the Sim Configurator window stays "
                   "open)");
            break;
        }
        bool owned = true;
        if (!has_owner(bus, &owned)) {
            finish(SIM_DBUS_FAILED, "the session bus stopped answering");
            break;
        }
        if (!owned) {
            finish(SIM_DBUS_EXITED, "the Sim Configurator exited");
            break;
        }
        g_usleep(200 * 1000);
    }
    g_object_unref(bus);
    return NULL;
}

bool xemu_sim_dbus_start(const char *const *keys, const char *const *values, int n, char *why, size_t whylen)
{
    if (g_dbus.thread) {
        snprintf(why, whylen, "a Sim Configurator hand-off is already running");
        return false;
    }
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    for (int i = 0; i < n; i++) {
        g_variant_builder_add(&b, "{sv}", keys[i], g_variant_new_string(values[i]));
    }
    g_atomic_int_set(&g_dbus.cancel, 0);
    g_atomic_int_set(&g_dbus.end, SIM_DBUS_RUNNING);
    g_dbus.why[0] = '\0';
    g_dbus.thread = g_thread_new("sim-dbus", run, g_variant_ref_sink(g_variant_builder_end(&b)));
    return true;
}

SimDbusEnd xemu_sim_dbus_poll(char *why, size_t whylen)
{
    SimDbusEnd e = (SimDbusEnd)g_atomic_int_get(&g_dbus.end);
    if (e == SIM_DBUS_RUNNING || !g_dbus.thread) {
        return g_dbus.thread ? SIM_DBUS_RUNNING : SIM_DBUS_FAILED;
    }
    g_thread_join(g_dbus.thread);
    g_dbus.thread = NULL;
    if (why) {
        g_mutex_lock(&g_dbus.lock);
        snprintf(why, whylen, "%s", g_dbus.why);
        g_mutex_unlock(&g_dbus.lock);
    }
    return e;
}

void xemu_sim_dbus_cancel(void)
{
    g_atomic_int_set(&g_dbus.cancel, 1);
}

SimDbusEnd xemu_sim_dbus_wait(char *why, size_t whylen)
{
    SimDbusEnd e;
    while ((e = xemu_sim_dbus_poll(why, whylen)) == SIM_DBUS_RUNNING) {
        g_usleep(50 * 1000);
    }
    return e;
}
