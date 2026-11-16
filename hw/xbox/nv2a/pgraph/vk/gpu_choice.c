/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "ui/xemu-settings.h"
#include "ui/xemu-notifications.h"
#include "renderer.h"
#include "vr.h"
#include "gpu_choice.h"

static GMutex gpu_lock;
static XemuGpuStatus gpu_status;

static bool gpu_vr;

GpuSelectChoice xemu_gpu_effective_choice(bool *set_aside)
{
    GpuSelectChoice saved = {
        .uuid_hex = g_config.display.gpu.uuid,
        .name = g_config.display.gpu.name,
        .last_name = g_config.display.vulkan.preferred_physical_device,
    };
    return gpu_select_choice_for(&saved, gpu_vr, set_aside);
}

static void uuid_text(const GpuSelectDevice *d,
                      char out[GPU_SELECT_UUID_HEX_SIZE])
{
    if (d->has_uuid) {
        gpu_select_uuid_to_hex(d->uuid, out);
    } else {
        g_strlcpy(out, "none", GPU_SELECT_UUID_HEX_SIZE);
    }
}

static void log_devices(const GpuSelectDevice *dev, int n)
{
    fprintf(stderr, "gpu: probe: %d device(s)\n", n);
    for (int i = 0; i < n; i++) {
        char line[512], uuid[GPU_SELECT_UUID_HEX_SIZE], pci[48] = "none";
        gpu_select_describe(&dev[i], line, sizeof(line));
        uuid_text(&dev[i], uuid);
        if (dev[i].has_pci) {
            snprintf(pci, sizeof(pci), "%04x:%02x:%02x.%x",
                     (unsigned)dev[i].pci_domain, (unsigned)dev[i].pci_bus,
                     (unsigned)dev[i].pci_device,
                     (unsigned)dev[i].pci_function);
        }
        fprintf(stderr, "gpu:   [%d] %s | uuid %s | pci %s | %s%s\n", i, line,
                uuid, pci, dev[i].usable ? "usable" : "not usable: ",
                dev[i].usable ? "" : dev[i].why_not);
    }
}

static void missing_text(char *out, size_t size, const char *picked)
{
    const char *name = g_config.display.gpu.name;
    snprintf(out, size,
             "The chosen GPU '%s' was not found (or cannot be used); using "
             "'%s'.",
             name && name[0] ? name : "(no name saved)",
             picked ? picked : "no GPU");
}

#ifdef __linux__
static void reexec_self(void)
{
    gchar *buf = NULL;
    gsize len = 0;
    if (!g_file_get_contents("/proc/self/cmdline", &buf, &len, NULL) ||
        len == 0) {
        fprintf(stderr, "gpu: steer: the process arguments could not be read; "
                        "continuing with the environment set here\n");
        g_free(buf);
        return;
    }
    GPtrArray *args = g_ptr_array_new();
    for (gsize off = 0; off < len; off += strlen(buf + off) + 1) {
        g_ptr_array_add(args, buf + off);
    }
    g_ptr_array_add(args, NULL);
    fflush(NULL);
    execv("/proc/self/exe", (char **)args->pdata);
    fprintf(stderr, "gpu: steer: the process could not be restarted (%s); "
                    "continuing with the environment set here\n",
            strerror(errno));
    g_ptr_array_free(args, TRUE);
    g_free(buf);
}

static void steer_gl(const GpuSelectDevice *dev, int n, int pick)
{
    GpuSelectEnvOp ops[GPU_SELECT_MAX_ENV_OPS];
    int k = gpu_select_env_plan(dev, n, pick, ops, GPU_SELECT_MAX_ENV_OPS);
    if (k == 0) {
        fprintf(stderr, "gpu: steer: %d hardware GPU(s), nothing to do\n",
                gpu_select_hardware_count(dev, n));
        return;
    }

    bool differs = false;
    for (int i = 0; i < k; i++) {
        const char *cur = g_getenv(ops[i].name);
        if (ops[i].set ? (!cur || strcmp(cur, ops[i].value)) : cur != NULL) {
            differs = true;
        }
    }
    if (!differs) {
        fprintf(stderr, "gpu: steer: the environment already selects '%s'\n",
                dev[pick].name);
        return;
    }

    char token[GPU_SELECT_UUID_HEX_SIZE];
    uuid_text(&dev[pick], token);
    const char *done = g_getenv("PENGUINBOX_GPU_STEER_DONE");
    if (done && !strcmp(done, token)) {
        fprintf(stderr, "gpu: WARNING: steer: the environment for '%s' did "
                        "not hold across the restart of the process; "
                        "continuing\n", dev[pick].name);
        return;
    }

    for (int i = 0; i < k; i++) {
        if (ops[i].set) {
            g_setenv(ops[i].name, ops[i].value, TRUE);
            fprintf(stderr, "gpu: steer: %s=%s\n", ops[i].name, ops[i].value);
        } else if (g_getenv(ops[i].name)) {
            g_unsetenv(ops[i].name);
            fprintf(stderr, "gpu: steer: %s removed\n", ops[i].name);
        }
    }
    g_setenv("PENGUINBOX_GPU_STEER_DONE", token, TRUE);
    fprintf(stderr, "gpu: steer: restarting the process once so OpenGL starts "
                    "on '%s'\n", dev[pick].name);
    reexec_self();
}
#endif

void xemu_gpu_start(void)
{
    bool steer = false;
    (void)steer;
#ifdef __linux__
    const char *sw = g_getenv("PENGUINBOX_GPU_STEER");
    steer = !(sw && !strcmp(sw, "0"));
    if (steer) {
        const char *prime = g_getenv("DRI_PRIME");
        g_unsetenv("__VK_LAYER_NV_optimus");
        if (prime && g_str_has_suffix(prime, "!")) {
            g_autofree char *plain = g_strndup(prime, strlen(prime) - 1);
            g_setenv("DRI_PRIME", plain, TRUE);
        }
    } else {
        fprintf(stderr, "gpu: steer: off (PENGUINBOX_GPU_STEER=0)\n");
    }
#endif

    static GpuSelectDevice dev[GPU_SELECT_MAX_DEVICES];
    int listed = pgraph_vk_gpu_probe(dev, GPU_SELECT_MAX_DEVICES);
    int n = gpu_select_dedup(dev, listed, NULL);
    bool set_aside = false;
    gpu_vr = xemu_vr_wanted();
    GpuSelectChoice choice = xemu_gpu_effective_choice(&set_aside);
    const char *saved_uuid = g_config.display.gpu.uuid;
    const char *saved_name = g_config.display.gpu.name;
    GpuSelectPick pick = gpu_select_pick(dev, n, &choice, NULL);
    const char *picked = pick.index >= 0 ? dev[pick.index].name : NULL;

    g_mutex_lock(&gpu_lock);
    memset(&gpu_status, 0, sizeof(gpu_status));
    gpu_status.n = n;
    memcpy(gpu_status.dev, dev, sizeof(gpu_status.dev));
    gpu_status.start = pick;
    gpu_status.vr = gpu_vr;
    gpu_status.choice_set_aside = set_aside;
    if (n > 0 && pick.choice_missing) {
        missing_text(gpu_status.notice_missing,
                     sizeof(gpu_status.notice_missing), picked);
    }
    g_mutex_unlock(&gpu_lock);

    if (n == 0) {
        fprintf(stderr, "gpu: probe: no Vulkan device list; nothing chosen "
                        "here\n");
        return;
    }
    log_devices(dev, n);
    if (listed != n) {
        fprintf(stderr, "gpu: probe: %d duplicate entr%s of these devices not "
                        "shown\n", listed - n, listed - n == 1 ? "y" : "ies");
    }
    if ((saved_uuid && saved_uuid[0]) || (saved_name && saved_name[0])) {
        fprintf(stderr, "gpu: choice: '%s' (uuid %s)\n",
                saved_name ? saved_name : "",
                saved_uuid && saved_uuid[0] ? saved_uuid : "none");
    } else {
        fprintf(stderr, "gpu: choice: automatic\n");
    }
    if (gpu_vr) {
        fprintf(stderr, "gpu: vr: VR is on, the headset runtime picks the "
                        "GPU: %s\n",
                set_aside ? "the saved choice is set aside and the automatic "
                            "rule applies" :
                            "the automatic rule applies");
    }
    if (pick.choice_missing) {
        char text[400];
        missing_text(text, sizeof(text), picked);
        fprintf(stderr, "gpu: WARNING: %s\n", text);
    }
    if (pick.index < 0) {
        fprintf(stderr, "gpu: pick: no usable GPU\n");
        return;
    }
    fprintf(stderr, "gpu: pick: [%d] %s: %s\n", pick.index, picked,
            gpu_select_reason_text(pick.reason));

#ifdef __linux__
    if (steer) {
        steer_gl(dev, n, pick.index);
    }
#endif
}

void xemu_gpu_note_gl(void)
{
    uint8_t uuid[GPU_SELECT_UUID_SIZE] = { 0 };
    bool known = false;
    const char *renderer = (const char *)glGetString(GL_RENDERER);

    if (epoxy_has_gl_extension("GL_EXT_memory_object")) {
        GLint count = 0;
        glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &count);
        if (count > 0) {
            glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, uuid);
            known = !gpu_select_uuid_is_zero(uuid);
        }
    }
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {
    }

    GpuSelectChoice choice = xemu_gpu_effective_choice(NULL);
    char missing[400] = "", mismatch[900] = "";

    g_mutex_lock(&gpu_lock);
    gpu_status.gl_known = known;
    memcpy(gpu_status.gl_uuid, uuid, sizeof(uuid));
    g_strlcpy(gpu_status.gl_renderer, renderer ? renderer : "",
              sizeof(gpu_status.gl_renderer));
    int n = gpu_status.n;
    GpuSelectPick pick =
        gpu_select_pick(gpu_status.dev, n, &choice, known ? uuid : NULL);
    const GpuSelectDevice *want =
        pick.wanted >= 0 ? &gpu_status.dev[pick.wanted] : NULL;
    char want_name[256] = "", want_uuid[GPU_SELECT_UUID_HEX_SIZE] = "none";
    char gl_uuid[GPU_SELECT_UUID_HEX_SIZE];
    if (want) {
        g_strlcpy(want_name, want->name, sizeof(want_name));
        uuid_text(want, want_uuid);
    }
    gpu_select_uuid_to_hex(uuid, gl_uuid);
    if (want && pick.gl == GPU_SELECT_GL_MISMATCH) {
        snprintf(gpu_status.notice_gl, sizeof(gpu_status.notice_gl),
                 "The display's OpenGL runs on '%s', not on the GPU picked "
                 "for PenguinBox, '%s'.",
                 gpu_status.gl_renderer, want_name);
        g_strlcpy(mismatch, gpu_status.notice_gl, sizeof(mismatch));
    }
    g_strlcpy(missing, gpu_status.notice_missing, sizeof(missing));
    g_mutex_unlock(&gpu_lock);

    if (n == 0 || !want) {
        return;
    }
    if (pick.gl == GPU_SELECT_GL_MATCH) {
        fprintf(stderr, "gpu: gl: OpenGL is on '%s' (device uuid %s): matches "
                        "the picked GPU '%s'\n",
                renderer ? renderer : "", gl_uuid, want_name);
    } else if (pick.gl == GPU_SELECT_GL_MISMATCH) {
        fprintf(stderr,
                "\n"
                "==================== GPU MISMATCH ====================\n"
                "gpu: MISMATCH: OpenGL is on '%s' (device uuid %s)\n"
                "gpu: MISMATCH: the picked GPU is '%s' (device uuid %s)\n"
                "gpu: MISMATCH: the display needs both on one GPU. Start "
                "PenguinBox with the picked GPU's environment, or choose the "
                "other GPU in Settings > Display.\n"
                "======================================================\n\n",
                renderer ? renderer : "", gl_uuid, want_name, want_uuid);
        xemu_queue_notification(mismatch);
    } else {
        fprintf(stderr, "gpu: gl: OpenGL is on '%s': its device cannot be "
                        "compared with the picked GPU '%s' (OpenGL device "
                        "uuid %s, GPU uuid %s)\n",
                renderer ? renderer : "", want_name, known ? gl_uuid : "none",
                want_uuid);
    }
    if (missing[0]) {
        xemu_queue_notification(missing);
    }
}

bool xemu_gpu_gl_uuid(uint8_t out[GPU_SELECT_UUID_SIZE])
{
    g_mutex_lock(&gpu_lock);
    bool known = gpu_status.gl_known;
    memcpy(out, gpu_status.gl_uuid, GPU_SELECT_UUID_SIZE);
    g_mutex_unlock(&gpu_lock);
    return known;
}

void xemu_gpu_note_vulkan_pick(const GpuSelectDevice *dev, int n,
                               const GpuSelectPick *pick)
{
    if (pick->index < 0 || pick->index >= n) {
        return;
    }
    const GpuSelectDevice *use = &dev[pick->index];
    const GpuSelectDevice *want = &dev[pick->wanted];

    fprintf(stderr, "gpu: vulkan: '%s': %s\n", want->name,
            gpu_select_reason_text(pick->reason));
    if (pick->choice_missing) {
        char text[400];
        missing_text(text, sizeof(text), want->name);
        fprintf(stderr, "gpu: WARNING: %s\n", text);
    }

    g_mutex_lock(&gpu_lock);
    gpu_status.vulkan_known = true;
    g_strlcpy(gpu_status.vulkan_name, use->name,
              sizeof(gpu_status.vulkan_name));
    if (pick->choice_missing) {
        missing_text(gpu_status.notice_missing,
                     sizeof(gpu_status.notice_missing), want->name);
    }
    if (pick->gl == GPU_SELECT_GL_MISMATCH && pick->followed_gl) {
        snprintf(gpu_status.notice_gl, sizeof(gpu_status.notice_gl),
                 "The display's OpenGL runs on '%s', not on '%s'. PenguinBox "
                 "is using '%s' for Vulkan as well, so the picture works.",
                 gpu_status.gl_renderer, want->name, use->name);
    } else if (pick->gl == GPU_SELECT_GL_MISMATCH) {
        snprintf(gpu_status.notice_gl, sizeof(gpu_status.notice_gl),
                 "The display's OpenGL runs on '%s' and Vulkan on '%s': two "
                 "GPUs. The picture may fail.",
                 gpu_status.gl_renderer, use->name);
    } else if (pick->gl == GPU_SELECT_GL_MATCH) {
        gpu_status.notice_gl[0] = '\0';
    }
    g_mutex_unlock(&gpu_lock);

    if (pick->gl == GPU_SELECT_GL_MISMATCH) {
        fprintf(stderr,
                "gpu: MISMATCH: vulkan: OpenGL is not on '%s'; %s '%s' for "
                "Vulkan\n",
                want->name,
                pick->followed_gl ? "using the GPU OpenGL is on," :
                                    "no Vulkan device is the one OpenGL is "
                                    "on, keeping",
                use->name);
    }
}

void xemu_gpu_note_vulkan_xr(const GpuSelectDevice *dev)
{
    uint8_t gl[GPU_SELECT_UUID_SIZE];
    bool gl_known = xemu_gpu_gl_uuid(gl);
    bool differs = gl_known && dev->has_uuid &&
                   memcmp(gl, dev->uuid, GPU_SELECT_UUID_SIZE);

    g_mutex_lock(&gpu_lock);
    gpu_status.vulkan_known = true;
    g_strlcpy(gpu_status.vulkan_name, dev->name,
              sizeof(gpu_status.vulkan_name));
    if (differs) {
        snprintf(gpu_status.notice_gl, sizeof(gpu_status.notice_gl),
                 "The display's OpenGL runs on '%s' and the OpenXR runtime "
                 "chose '%s' for Vulkan: two GPUs. The picture may fail.",
                 gpu_status.gl_renderer, dev->name);
    }
    g_mutex_unlock(&gpu_lock);

    if (differs) {
        fprintf(stderr, "gpu: MISMATCH: vulkan: the OpenXR runtime chose '%s' "
                        "and OpenGL is on another GPU\n", dev->name);
    }
}

void xemu_gpu_note_vr_refused(const char *runtime_name)
{
    char text[900];

    g_mutex_lock(&gpu_lock);
    snprintf(gpu_status.notice_vr, sizeof(gpu_status.notice_vr),
             "VR is off this session: the headset runtime runs on '%s' and "
             "the display's OpenGL on '%s', two GPUs. Start the headset "
             "runtime on the GPU OpenGL is on, then start PenguinBox again.",
             runtime_name, gpu_status.gl_renderer);
    g_strlcpy(text, gpu_status.notice_vr, sizeof(text));
    g_mutex_unlock(&gpu_lock);

    fprintf(stderr,
            "\n"
            "==================== VR REFUSED ====================\n"
            "gpu: vr: %s\n"
            "====================================================\n\n",
            text);
    xemu_queue_notification(text);
}

void xemu_gpu_get_status(XemuGpuStatus *out)
{
    g_mutex_lock(&gpu_lock);
    *out = gpu_status;
    g_mutex_unlock(&gpu_lock);
}
