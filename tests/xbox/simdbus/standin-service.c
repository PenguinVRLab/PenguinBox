/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

static const char *INTROSPECTION =
    "<node><interface name='org.freedesktop.Application'>"
    "<method name='Activate'><arg type='a{sv}' direction='in'/></method>"
    "<method name='Open'><arg type='as' direction='in'/><arg type='a{sv}' direction='in'/></method>"
    "<method name='ActivateAction'><arg type='s' direction='in'/><arg type='av' direction='in'/>"
    "<arg type='a{sv}' direction='in'/></method></interface></node>";

static char mode[32] = "saved";
static GMainLoop *loop;
static guint owner_id;
static char result[1024], request_id[128], profiles[1024];
static unsigned long title_id;
static int accepted;

static gboolean finish_cb(gpointer data)
{
    (void)data;
    if (strcmp(mode, "nofile") != 0) {
        char tmp[1100];
        snprintf(tmp, sizeof(tmp), "%s.tmp", result);
        FILE *f = fopen(tmp, "w");
        if (f) {
            const char *outcome = strcmp(mode, "cancel") == 0 ? "cancel_boot" :
                                  strcmp(mode, "quick") == 0 ? "unchanged" : "saved";
            fprintf(f, "schema: sim-result/1\nrequest_id: %s\ntitle_id: %lu\noutcome: %s\nprofiles: %s\n"
                    "ports: %s\nwritten:\n- profile_sbc_real.yaml\npreflight: READY\n",
                    strcmp(mode, "badid") == 0 ? "pb-someone-else" : request_id, title_id, outcome, profiles,
                    strcmp(mode, "cancel") == 0 ? "null" : "ports");
            fclose(f);
            rename(tmp, result);
        }
    }
    g_bus_unown_name(owner_id);
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void method(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *name,
                   GVariant *params, GDBusMethodInvocation *inv, gpointer data)
{
    (void)c, (void)sender, (void)path, (void)iface, (void)data;
    if (strcmp(name, "ActivateAction") != 0) {
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    const char *action = NULL;
    GVariantIter *av = NULL;
    GVariant *pd = NULL;
    g_variant_get(params, "(&sav@a{sv})", &action, &av, &pd);
    if (strcmp(mode, "usage") == 0 || strcmp(action, "configure-title") != 0) {
        g_dbus_method_invocation_return_dbus_error(inv, "com.penguinvrlabs.simconfigurator.Error.Usage",
                                                   "usage: a bad or unknown flag");
        g_idle_add(finish_cb, NULL);
        goto out;
    }
    if (accepted) {
        g_dbus_method_invocation_return_dbus_error(inv, "com.penguinvrlabs.simconfigurator.Error.Busy",
                                                   "this instance is already open");
        goto out;
    }
    if (strcmp(mode, "busy") == 0) {
        g_dbus_method_invocation_return_dbus_error(inv, "com.penguinvrlabs.simconfigurator.Error.Busy",
                                                   "another instance owns the devices");
        strcpy(mode, "nofile");
        g_idle_add(finish_cb, NULL);
        goto out;
    }
    GVariant *v = NULL;
    if (g_variant_iter_next(av, "v", &v)) {
        GVariantIter it;
        const char *k;
        GVariant *val;
        g_variant_iter_init(&it, v);
        while (g_variant_iter_next(&it, "{&sv}", &k, &val)) {
            const char *s = g_variant_is_of_type(val, G_VARIANT_TYPE_STRING) ? g_variant_get_string(val, NULL) : "";
            if (!strcmp(k, "result")) snprintf(result, sizeof(result), "%s", s);
            else if (!strcmp(k, "request-id")) snprintf(request_id, sizeof(request_id), "%s", s);
            else if (!strcmp(k, "profiles")) snprintf(profiles, sizeof(profiles), "%s", s);
            else if (!strcmp(k, "configure-title")) title_id = strtoul(s, NULL, 0);
            g_variant_unref(val);
        }
        g_variant_unref(v);
    }
    char *result_folder = g_path_get_dirname(result);
    gboolean folders = g_file_test(result_folder, G_FILE_TEST_IS_DIR) && g_file_test(profiles, G_FILE_TEST_IS_DIR);
    g_free(result_folder);
    if (!folders) {
        g_dbus_method_invocation_return_dbus_error(inv, "com.penguinvrlabs.simconfigurator.Error.Usage",
                                                   "the result's folder or the profiles folder does not exist");
        strcpy(mode, "nofile");
        g_idle_add(finish_cb, NULL);
        goto out;
    }
    char argsf[1100];
    snprintf(argsf, sizeof(argsf), "%s.args", result);
    FILE *f = fopen(argsf, "w");
    if (f) {
        fprintf(f, "title 0x%08lx\nprofiles %s\nresult %s\nrequest %s\n", title_id, profiles, result, request_id);
        fclose(f);
    }
    accepted = 1;
    g_dbus_method_invocation_return_value(inv, NULL);
    if (strcmp(mode, "slow") != 0) {
        g_timeout_add(strcmp(mode, "quick") == 0 ? 0 : 500, finish_cb, NULL);
    }
out:
    g_variant_iter_free(av);
    g_variant_unref(pd);
}

static const GDBusInterfaceVTable vtable = { method, NULL, NULL, { 0 } };

static void on_bus(GDBusConnection *c, const char *name, gpointer data)
{
    (void)name, (void)data;
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(INTROSPECTION, NULL);
    g_dbus_connection_register_object(c, "/com/penguinvrlabs/simconfigurator", node->interfaces[0], &vtable, NULL,
                                      NULL, NULL);
    g_dbus_node_info_unref(node);
}

static gboolean backstop(gpointer data)
{
    (void)data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void on_lost(GDBusConnection *c, const char *name, gpointer data)
{
    (void)c, (void)name, (void)data;
    g_main_loop_quit(loop);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        if (f) {
            if (fscanf(f, "%31s", mode) != 1) {
                strcpy(mode, "saved");
            }
            fclose(f);
        }
    }
    loop = g_main_loop_new(NULL, FALSE);
    owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, "com.penguinvrlabs.simconfigurator", G_BUS_NAME_OWNER_FLAGS_NONE,
                              on_bus, NULL, on_lost, NULL, NULL);
    g_timeout_add_seconds(60, backstop, NULL);
    g_main_loop_run(loop);
    return 0;
}
