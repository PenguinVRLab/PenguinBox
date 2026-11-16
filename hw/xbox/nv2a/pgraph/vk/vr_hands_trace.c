/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr_hands_trace.h"

static const char *const k_names[VR_HANDS_JOINTS] = {
    "PALM", "WRIST",
    "THUMB_METACARPAL", "THUMB_PROXIMAL", "THUMB_DISTAL", "THUMB_TIP",
    "INDEX_METACARPAL", "INDEX_PROXIMAL", "INDEX_INTERMEDIATE", "INDEX_DISTAL", "INDEX_TIP",
    "MIDDLE_METACARPAL", "MIDDLE_PROXIMAL", "MIDDLE_INTERMEDIATE", "MIDDLE_DISTAL", "MIDDLE_TIP",
    "RING_METACARPAL", "RING_PROXIMAL", "RING_INTERMEDIATE", "RING_DISTAL", "RING_TIP",
    "LITTLE_METACARPAL", "LITTLE_PROXIMAL", "LITTLE_INTERMEDIATE", "LITTLE_DISTAL", "LITTLE_TIP",
};

#define TRACE_MAX_SEGMENTS 64
#define TRACE_ID_LEN 48
#define TRACE_PROMPT_LEN 256
#define TRACE_SCRIPT_MAX 65536
#define TRACE_PATH_LEN 1024
#define TRACE_WRITTEN_MAX 64

typedef struct TraceSegment {
    char id[TRACE_ID_LEN];
    char prompt[TRACE_PROMPT_LEN];
    int64_t dur_us;
} TraceSegment;

static struct {
    FILE *f;
    bool any;
    bool have_t0;
    int64_t t0;
    int64_t dur_us;
    uint32_t frame;
    int marker_n;
    int seg_n;
    int seg_cur;
    char pose[TRACE_ID_LEN];
    int part;
    char path[TRACE_PATH_LEN];
    char previous[TRACE_PATH_LEN];
    VrHandsTraceSabotage sab;
    TraceSegment seg[TRACE_MAX_SEGMENTS];
    char line[26 * 400 + 4096];
} s_t;

static int s_mark_req;
static int s_recording;
static char s_script_buf[TRACE_SCRIPT_MAX];
static char s_written[TRACE_WRITTEN_MAX][TRACE_PATH_LEN];
static int s_written_n;

const char *vr_hands_trace_session_name(int state)
{
    switch (state) {
    case XR_SESSION_STATE_IDLE:         return "IDLE";
    case XR_SESSION_STATE_READY:        return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE:      return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED:      return "FOCUSED";
    case XR_SESSION_STATE_STOPPING:     return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING:      return "EXITING";
    default:                            return "UNKNOWN";
    }
}

bool vr_hands_trace_active(void)
{
    return s_t.f != NULL;
}

int vr_hands_trace_segment_count(void)
{
    return s_t.f ? s_t.seg_n : 0;
}

void vr_hands_trace_mark(void)
{
    if (__atomic_load_n(&s_recording, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&s_mark_req, 1, __ATOMIC_ACQ_REL);
    }
}

typedef struct Out {
    char *p, *end;
} Out;

static void out_f(Out *o, const char *fmt, ...)
{
    if (o->p >= o->end) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->p, (size_t)(o->end - o->p), fmt, ap);
    va_end(ap);
    o->p = (n < 0 || n >= o->end - o->p) ? o->end : o->p + n;
}

static void out_str(Out *o, const char *s)
{
    if (s == NULL) {
        out_f(o, "null");
        return;
    }
    out_f(o, "\"");
    for (const unsigned char *c = (const unsigned char *)s; *c && o->p < o->end; c++) {
        if (*c == '"' || *c == '\\') {
            out_f(o, "\\%c", *c);
        } else if (*c < 0x20) {
            out_f(o, "\\u%04x", *c);
        } else {
            *o->p++ = (char)*c;
        }
    }
    out_f(o, "\"");
}

static void out_num(Out *o, double v)
{
    if (!isfinite(v) && s_t.sab != VR_HANDS_TRACE_SAB_RAWFLOAT) {
        out_f(o, "null");
    } else {
        out_f(o, "%.9g", v);
    }
}

static void out_vec(Out *o, const float *v, int n)
{
    out_f(o, "[");
    for (int i = 0; i < n; i++) {
        out_f(o, i ? ", " : "");
        out_num(o, v[i]);
    }
    out_f(o, "]");
}

static void out_flag(Out *o, int8_t v)
{
    out_f(o, v < 0 ? "null" : v ? "true" : "false");
}

static void utc_iso(int64_t utc_us, char *buf, size_t n)
{
    int64_t s = utc_us / 1000000, us = utc_us % 1000000;
    if (us < 0) {
        us += 1000000;
        s--;
    }
    int64_t days = s / 86400, sod = s % 86400;
    if (sod < 0) {
        sod += 86400;
        days--;
    }
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp + (mp < 10 ? 3 : -9);
    y += m <= 2;
    snprintf(buf, n, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%06lldZ", (long long)y, (long long)m,
             (long long)d, (long long)(sod / 3600), (long long)(sod / 60 % 60), (long long)(sod % 60),
             (long long)us);
}

typedef struct Js {
    const char *p;
} Js;

static void js_ws(Js *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') {
        j->p++;
    }
}

static bool js_str(Js *j, char *dst, size_t n)
{
    size_t k = 0;
    js_ws(j);
    if (*j->p != '"') {
        return false;
    }
    j->p++;
    while (*j->p && *j->p != '"') {
        unsigned cp = (unsigned char)*j->p++;
        if (cp != '\\') {
            if (k + 1 < n) {
                dst[k++] = (char)cp;
            }
            continue;
        }
        if (*j->p == '\0') {
            return false;
        }
        char e = *j->p++;
        switch (e) {
        case '"': case '\\': case '/': cp = (unsigned char)e; break;
        case 'n': cp = '\n'; break;
        case 't': cp = '\t'; break;
        case 'r': cp = '\r'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'u': {
            char hex[5] = { 0 };
            for (int i = 0; i < 4; i++) {
                if (!j->p[i]) {
                    return false;
                }
                hex[i] = j->p[i];
            }
            j->p += 4;
            cp = (unsigned)strtoul(hex, NULL, 16);
            break;
        }
        default:
            return false;
        }
        char u[3];
        int ul = 1;
        if (cp < 0x80) {
            u[0] = (char)cp;
        } else if (cp < 0x800) {
            u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3F)); ul = 2;
        } else {
            u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            u[2] = (char)(0x80 | (cp & 0x3F)); ul = 3;
        }
        for (int i = 0; i < ul && k + 1 < n; i++) {
            dst[k++] = u[i];
        }
    }
    if (*j->p != '"') {
        return false;
    }
    j->p++;
    if (n) {
        dst[k] = '\0';
    }
    return true;
}

static bool js_skip(Js *j)
{
    char tmp[8];
    js_ws(j);
    if (*j->p == '"') {
        return js_str(j, tmp, sizeof(tmp));
    }
    const char *s = j->p;
    while (*j->p && *j->p != ',' && *j->p != '}' && *j->p != ']' && *j->p != ' ' && *j->p != '\n' &&
           *j->p != '\r' && *j->p != '\t' && *j->p != '{' && *j->p != '[') {
        j->p++;
    }
    return j->p > s;
}

static bool script_parse(const char *text, const char *path)
{
    Js j = { text };
    const char *k = strstr(text, "\"segments\"");
    if (k == NULL) {
        fprintf(stderr, "PenguinBox: hands trace: script %s has no \"segments\"\n", path);
        return false;
    }
    j.p = k + strlen("\"segments\"");
    js_ws(&j);
    if (*j.p++ != ':') {
        return false;
    }
    js_ws(&j);
    if (*j.p++ != '[') {
        return false;
    }
    s_t.seg_n = 0;
    for (;;) {
        js_ws(&j);
        if (*j.p == ']') {
            break;
        }
        if (*j.p == ',') {
            j.p++;
            continue;
        }
        if (*j.p++ != '{' || s_t.seg_n >= TRACE_MAX_SEGMENTS) {
            fprintf(stderr, "PenguinBox: hands trace: script %s: a segment is not an object (or more than %d)\n",
                    path, TRACE_MAX_SEGMENTS);
            return false;
        }
        TraceSegment *sg = &s_t.seg[s_t.seg_n];
        memset(sg, 0, sizeof(*sg));
        double secs = -1;
        for (;;) {
            char key[32];
            js_ws(&j);
            if (*j.p == '}') {
                j.p++;
                break;
            }
            if (*j.p == ',') {
                j.p++;
                continue;
            }
            if (!js_str(&j, key, sizeof(key))) {
                return false;
            }
            js_ws(&j);
            if (*j.p++ != ':') {
                return false;
            }
            bool ok;
            if (!strcmp(key, "id")) {
                ok = js_str(&j, sg->id, sizeof(sg->id));
            } else if (!strcmp(key, "prompt")) {
                ok = js_str(&j, sg->prompt, sizeof(sg->prompt));
            } else if (!strcmp(key, "seconds")) {
                js_ws(&j);
                char *e;
                secs = strtod(j.p, &e);
                ok = e > j.p;
                j.p = e;
            } else {
                ok = js_skip(&j);
            }
            if (!ok) {
                fprintf(stderr, "PenguinBox: hands trace: script %s: bad value for \"%s\"\n", path, key);
                return false;
            }
        }
        if (sg->id[0] == '\0' || !(secs > 0)) {
            fprintf(stderr, "PenguinBox: hands trace: script %s: segment %d needs an \"id\" and \"seconds\" > 0\n",
                    path, s_t.seg_n + 1);
            return false;
        }
        sg->dur_us = (int64_t)(secs * 1e6);
        s_t.seg_n++;
    }
    if (s_t.seg_n == 0) {
        fprintf(stderr, "PenguinBox: hands trace: script %s has no segments\n", path);
        return false;
    }
    return true;
}

static bool script_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "PenguinBox: hands trace: cannot read the script %s\n", path);
        return false;
    }
    size_t n = fread(s_script_buf, 1, sizeof(s_script_buf) - 1, f);
    bool whole = feof(f) != 0;
    fclose(f);
    s_script_buf[n] = '\0';
    if (!whole) {
        fprintf(stderr, "PenguinBox: hands trace: the script %s is over %d bytes\n", path, TRACE_SCRIPT_MAX - 1);
        return false;
    }
    return script_parse(s_script_buf, path);
}

static bool written_before(const char *path)
{
    for (int i = 0; i < s_written_n; i++) {
        if (!strcmp(s_written[i], path)) {
            return true;
        }
    }
    return false;
}

static bool part_path(const char *path, int n, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(slash ? slash + 1 : path, '.');
    if (dot == NULL || dot == (slash ? slash + 1 : path)) {
        dot = path + strlen(path);
    }
    const int r = snprintf(out, cap, "%.*s.%d%s", (int)(dot - path), path, n, dot);
    return r > 0 && (size_t)r < cap;
}

static bool pick_part(const char *path, char *file, int *part, char *prev)
{
    prev[0] = '\0';
    *part = 1;
    if (strlen(path) >= TRACE_PATH_LEN) {
        return false;
    }
    snprintf(file, TRACE_PATH_LEN, "%s", path);
    if (s_t.sab == VR_HANDS_TRACE_SAB_REOPEN) {
        return true;
    }
    for (int n = 2; written_before(file); n++) {
        snprintf(prev, TRACE_PATH_LEN, "%s", file);
        if (n > TRACE_WRITTEN_MAX + 1 || !part_path(path, n, file, TRACE_PATH_LEN)) {
            return false;
        }
        *part = n;
    }
    return true;
}
static void write_header(Out *o, const char *script, const VrHandsTraceHeader *hdr)
{
    static const VrHandsTraceHeader none;
    const VrHandsTraceHeader *h = hdr ? hdr : &none;
    char iso[64];
    utc_iso(h->utc_us_at_start, iso, sizeof(iso));
    out_f(o, "{\"format\": 2, \"source\": \"xemu_vr XR_EXT_hand_tracking recorder\", \"header\": {\"recorder\": "
             "\"xemu_vr hw/xbox/nv2a/pgraph/vk/vr_hands_trace.c\", \"core\": ");
    out_str(o, h->core);
    out_f(o, ", \"build\": ");
    out_str(o, h->build);
    out_f(o, ", \"runtime\": {\"name\": ");
    out_str(o, h->runtime_name);
    out_f(o, ", \"version\": ");
    out_str(o, h->runtime_version);
    out_f(o, "}, \"system\": ");
    out_str(o, h->system);
    out_f(o, ", \"started_utc\": ");
    out_str(o, iso);
    out_f(o, ", \"extensions\": [");
    for (int i = 0; i < h->n_extensions && h->extensions; i++) {
        out_f(o, i ? ", " : "");
        out_str(o, h->extensions[i]);
    }
    out_f(o, "], \"script\": ");
    out_str(o, (script && script[0]) ? script : NULL);
    out_f(o, ", \"clock\": {\"t_us_at_start\": %lld, \"utc_us_at_start\": %lld}", (long long)h->t_us_at_start,
          (long long)h->utc_us_at_start);
    out_f(o, ", \"part\": %d, \"previous\": ", s_t.part);
    out_str(o, s_t.previous[0] ? s_t.previous : NULL);
    out_f(o, ", \"segments\": [");
    for (int i = 0; i < s_t.seg_n; i++) {
        out_f(o, "%s{\"id\": ", i ? ", " : "");
        out_str(o, s_t.seg[i].id);
        out_f(o, ", \"prompt\": ");
        out_str(o, s_t.seg[i].prompt);
        out_f(o, ", \"seconds\": ");
        out_num(o, s_t.seg[i].dur_us / 1e6);
        out_f(o, "}");
    }
    out_f(o, "]}, \"frames\": [\n]}\n");
}

bool vr_hands_trace_start(const char *path, double seconds, const char *pose, const char *script,
                          const VrHandsTraceHeader *hdr, VrHandsTraceSabotage sab)
{
    if (s_t.f != NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    memset(&s_t, 0, sizeof(s_t));
    s_t.seg_cur = -1;
    if (script && script[0] && !script_load(script)) {
        memset(&s_t, 0, sizeof(s_t));
        return false;
    }
    s_t.sab = sab;
    if (s_written_n >= TRACE_WRITTEN_MAX || !pick_part(path, s_t.path, &s_t.part, s_t.previous)) {
        fprintf(stderr, "PenguinBox: hands trace: NOT RECORDING to %s: this process has written %d trace files or the "
                        "path is too long, and no file it wrote is ever reopened\n", path, s_written_n);
        memset(&s_t, 0, sizeof(s_t));
        return false;
    }
    FILE *f = fopen(s_t.path, "wb");
    if (f == NULL) {
        fprintf(stderr, "PenguinBox: hands trace: cannot open %s\n", s_t.path);
        memset(&s_t, 0, sizeof(s_t));
        return false;
    }
    if (!written_before(s_t.path)) {
        snprintf(s_written[s_written_n++], TRACE_PATH_LEN, "%s", s_t.path);
    }
    s_t.f = f;
    s_t.seg_cur = -1;
    int64_t total = 0;
    for (int i = 0; i < s_t.seg_n; i++) {
        total += s_t.seg[i].dur_us;
    }
    s_t.dur_us = s_t.seg_n ? total : (int64_t)((seconds > 0 ? seconds : 20.0) * 1e6);
    snprintf(s_t.pose, sizeof(s_t.pose), "%s", (pose && pose[0]) ? pose : "open");
    Out o = { s_t.line, s_t.line + sizeof(s_t.line) };
    write_header(&o, script, hdr);
    if (o.p >= o.end) {
        fprintf(stderr, "PenguinBox: hands trace: the header does not fit; not recording\n");
        fclose(f);
        remove(s_t.path);
        memset(&s_t, 0, sizeof(s_t));
        return false;
    }
    fwrite(s_t.line, 1, (size_t)(o.p - s_t.line), f);
    fflush(f);
    __atomic_store_n(&s_mark_req, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recording, 1, __ATOMIC_RELEASE);
    if (s_t.part > 1) {
        fprintf(stderr, "PenguinBox: hands trace: NEW PART %d: this process already wrote %s, so this recording goes "
                        "to %s (the earlier file is kept)\n", s_t.part, s_t.previous, s_t.path);
    }
    path = s_t.path;
    if (s_t.seg_n) {
        fprintf(stderr, "PenguinBox: hands trace: recording to %s, script %s: %d segments, %.1f s\n", path, script,
                s_t.seg_n, s_t.dur_us / 1e6);
    } else {
        fprintf(stderr, "PenguinBox: hands trace: recording to %s for %.1f s (pose label \"%s\")\n",
                path, s_t.dur_us / 1e6, s_t.pose);
    }
    return true;
}

bool vr_hands_trace_start_from_env(const VrHandsTraceHeader *hdr)
{
    const char *p = getenv("XEMU_VR_HANDS_TRACE");
    if (p == NULL || p[0] == '\0') {
        return false;
    }
    const char *s = getenv("XEMU_VR_HANDS_TRACE_SECONDS");
    return vr_hands_trace_start(p, s ? atof(s) : 0.0, getenv("XEMU_VR_HANDS_TRACE_POSE"),
                                getenv("XEMU_VR_HANDS_TRACE_SCRIPT"), hdr, VR_HANDS_TRACE_SAB_NONE);
}

static bool s_warned_not_started;
static VrHandsTraceSabotage s_warn_sab;

bool vr_hands_trace_requested(void)
{
    const char *p = getenv("XEMU_VR_HANDS_TRACE");
    return p != NULL && p[0] != '\0';
}

void vr_hands_trace_session_begin(void)
{
    s_warned_not_started = false;
}

void vr_hands_trace_warn_seed_fault(VrHandsTraceSabotage sab)
{
    s_warn_sab = sab;
}

bool vr_hands_trace_warn_not_started(const char *reason_fmt, ...)
{
    if (!vr_hands_trace_requested() || vr_hands_trace_active() || s_warned_not_started) {
        return false;
    }
    s_warned_not_started = true;
    if (s_warn_sab == VR_HANDS_TRACE_SAB_NOWARN) {
        return false;
    }
    char reason[256];
    va_list ap;
    va_start(ap, reason_fmt);
    vsnprintf(reason, sizeof(reason), reason_fmt, ap);
    va_end(ap);
    fprintf(stderr, "PenguinBox: hands trace: WARNING: XEMU_VR_HANDS_TRACE=%s was requested but no trace is "
                    "recording: %s\n", getenv("XEMU_VR_HANDS_TRACE"), reason);
    return true;
}

void vr_hands_trace_stop(void)
{
    __atomic_store_n(&s_recording, 0, __ATOMIC_RELEASE);
    if (s_t.f != NULL) {
        fflush(s_t.f);
        fclose(s_t.f);
        fprintf(stderr, "PenguinBox: hands trace: closed (%u frames, %d markers)\n", s_t.frame, s_t.marker_n);
    }
    memset(&s_t, 0, sizeof(s_t));
}

static void out_head(Out *o, const VrHandsTraceFrame *fx)
{
    XrPosef h = fx->head;
    if (s_t.sab == VR_HANDS_TRACE_SAB_HEADSPACE) {
        const XrQuaternionf q = h.orientation;
        const XrVector3f p = h.position;
        const float x = -q.x, y = -q.y, z = -q.z, w = q.w;
        const float tx = 2.f * (y * p.z - z * p.y), ty = 2.f * (z * p.x - x * p.z), tz = 2.f * (x * p.y - y * p.x);
        const XrVector3f r = { p.x + w * tx + (y * tz - z * ty), p.y + w * ty + (z * tx - x * tz),
                               p.z + w * tz + (x * ty - y * tx) };
        h.orientation = (XrQuaternionf){ x, y, z, w };
        h.position = (XrVector3f){ -r.x, -r.y, -r.z };
    }
    const float pos[3] = { h.position.x, h.position.y, h.position.z };
    const float ori[4] = { h.orientation.x, h.orientation.y, h.orientation.z, h.orientation.w };
    out_f(o, "\"head\": {\"position\": ");
    out_vec(o, pos, 3);
    out_f(o, ", \"orientation\": ");
    out_vec(o, ori, 4);
    out_f(o, ", \"valid\": %s}", fx->head_valid ? "true" : "false");
}

static int write_record(const VrHandJoints *hj, int side, int label, const VrHandsTraceFrame *fx, const char *seg,
                        const char *marker, const char *skipped)
{
    Out o = { s_t.line, s_t.line + sizeof(s_t.line) };
    out_f(&o, "%s{\"hand\": %s, \"frame\": %u, \"pose\": ", s_t.any ? ",\n" : "",
          label < 0 ? "null" : label ? "\"right\"" : "\"left\"", s_t.frame);
    out_str(&o, seg ? seg : s_t.pose);
    out_f(&o, ", \"space\": ");
    out_str(&o, fx->space ? fx->space : "LOCAL");
    out_f(&o, ", \"t_us\": %lld", (long long)fx->t_us);
    if (s_t.sab != VR_HANDS_TRACE_SAB_NOUTC) {
        out_f(&o, ", \"utc_us\": %lld", (long long)fx->utc_us);
    }
    if (skipped) {
        out_f(&o, ", \"skipped\": ");
        out_str(&o, skipped);
    } else {
        out_f(&o, ", ");
        out_head(&o, fx);
    }
    out_f(&o, ", \"session\": ");
    out_str(&o, fx->session);
    out_f(&o, ", \"segment\": ");
    out_str(&o, seg);
    if (marker) {
        out_f(&o, ", \"marker\": ");
        out_str(&o, marker);
    }
    if (side >= 0) {
        const VrHandsTraceInteraction *hi = &fx->hi[side];
        out_f(&o, ", \"profile\": ");
        out_str(&o, fx->profile[side]);
        out_f(&o, ", \"hand_interaction\": ");
        if (!hi->present) {
            out_f(&o, "null");
        } else {
            out_f(&o, "{\"pinch\": ");
            out_num(&o, hi->pinch);
            out_f(&o, ", \"pinch_ready\": ");
            out_flag(&o, hi->pinch_ready);
            out_f(&o, ", \"grasp\": ");
            out_num(&o, hi->grasp);
            out_f(&o, ", \"grasp_ready\": ");
            out_flag(&o, hi->grasp_ready);
            out_f(&o, ", \"aim_valid\": ");
            out_flag(&o, hi->aim_valid);
            out_f(&o, ", \"poke_valid\": ");
            out_flag(&o, hi->poke_valid);
            out_f(&o, "}");
        }
        out_f(&o, ", \"joints\": {");
        for (int k = 0; k < VR_HANDS_JOINTS; k++) {
            const VrHandJoint *j = &hj->j[k];
            const float pos[3] = { j->pose.position.x, j->pose.position.y, j->pose.position.z };
            const float ori[4] = { j->pose.orientation.x, j->pose.orientation.y, j->pose.orientation.z,
                                   j->pose.orientation.w };
            out_f(&o, "%s\"XR_HAND_JOINT_%s_EXT\": {\"position\": ", k ? ", " : "", k_names[k]);
            out_vec(&o, pos, 3);
            out_f(&o, ", \"orientation\": ");
            out_vec(&o, ori, 4);
            out_f(&o, ", ");
            if (s_t.sab != VR_HANDS_TRACE_SAB_NORADIUS) {
                out_f(&o, "\"radius\": ");
                out_num(&o, j->radius);
                out_f(&o, ", ");
            }
            out_f(&o, "\"valid\": %s, \"tracked\": %s, \"flags\": %llu}", j->valid ? "true" : "false",
                  j->tracked ? "true" : "false", (unsigned long long)j->flags);
        }
        out_f(&o, "}");
    }
    out_f(&o, "}");
    if (o.p >= o.end) {
        return 0;
    }
    fseek(s_t.f, s_t.any ? -4 : -3, SEEK_END);
    fwrite(s_t.line, 1, (size_t)(o.p - s_t.line), s_t.f);
    fputs("\n]}\n", s_t.f);
    fflush(s_t.f);
    s_t.any = true;
    return 1;
}

static bool frame_begin(const VrHandsTraceFrame *fx, const char **seg_out, char marker[16], const char **mk_out)
{
    if (!s_t.have_t0) {
        s_t.have_t0 = true;
        s_t.t0 = fx->t_us;
    }
    const int64_t el = fx->t_us - s_t.t0;
    if (el >= s_t.dur_us) {
        vr_hands_trace_stop();
        return false;
    }
    const char *seg = NULL;
    if (s_t.seg_n) {
        int i = 0;
        int64_t start = 0;
        while (i < s_t.seg_n - 1 && el >= start + s_t.seg[i].dur_us) {
            start += s_t.seg[i].dur_us;
            i++;
        }
        if (s_t.sab == VR_HANDS_TRACE_SAB_NOADVANCE) {
            i = 0;
        }
        if (i != s_t.seg_cur) {
            s_t.seg_cur = i;
            fprintf(stderr, "PenguinBox: hands trace: segment %d/%d \"%s\" (%.4g s): %s\n", i + 1, s_t.seg_n,
                    s_t.seg[i].id, s_t.seg[i].dur_us / 1e6, s_t.seg[i].prompt);
        }
        seg = s_t.seg[i].id;
    }
    const char *mk = NULL;
    const int presses = __atomic_exchange_n(&s_mark_req, 0, __ATOMIC_ACQ_REL);
    if (presses > 0 && s_t.sab != VR_HANDS_TRACE_SAB_DROPMARKER) {
        snprintf(marker, 16, "m%d", ++s_t.marker_n);
        mk = marker;
        fprintf(stderr, "PenguinBox: hands trace: marker %s at frame %u (utc_us %lld)\n", marker, s_t.frame,
                (long long)fx->utc_us);
    }
    *seg_out = seg;
    *mk_out = mk;
    return true;
}

int vr_hands_trace_frame(const VrHandJoints hands[2], const VrHandsTraceFrame *fx)
{
    if (s_t.f == NULL || fx == NULL) {
        return 0;
    }
    const char *seg, *mk;
    char marker[16];
    if (!frame_begin(fx, &seg, marker, &mk)) {
        return 0;
    }
    int n = 0;
    for (int h = 0; h < 2; h++) {
        if (!hands[h].active) {
            continue;
        }
        const int label = (s_t.sab == VR_HANDS_TRACE_SAB_SWAP) ? 1 - h : h;
        n += write_record(&hands[h], h, label, fx, seg, mk, NULL);
    }
    if (n == 0 && s_t.sab != VR_HANDS_TRACE_SAB_DROPNOHAND) {
        n = write_record(NULL, -1, -1, fx, seg, mk, NULL);
    }
    s_t.frame++;
    return n;
}

int vr_hands_trace_skipped(const char *reason, const VrHandsTraceFrame *fx)
{
    if (s_t.f == NULL || fx == NULL || s_t.sab == VR_HANDS_TRACE_SAB_NOSKIP) {
        return 0;
    }
    const char *seg, *mk;
    char marker[16];
    if (!frame_begin(fx, &seg, marker, &mk)) {
        return 0;
    }
    const int n = write_record(NULL, -1, -1, fx, seg, mk, (reason && reason[0]) ? reason : "unknown");
    s_t.frame++;
    return n;
}
