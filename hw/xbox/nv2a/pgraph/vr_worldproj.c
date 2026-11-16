/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "vr_worldproj.h"
#include "vr_worldproj_host.h"
#include "vr_worldproj_profiles.h"

#define WP_VPSCL_SLOT 58
#define WP_CACHE 4
#define WP_STAMP_MAX_AGE 3
#define WP_TITLE_REFRESH 300
#define WP_TITLE_RETRY 30

typedef struct WpConfig {
    bool on;
    bool hmd;
    double upm;
    VrWpView view;
    int group;
    double scope_a;
    int cyup;
    double nearfov;
    bool hmd_game_fov;
    int arms_bones;
    double arms_near;
    int palette;
    int ortho;
    double hud_m;
    double hudscale;
    bool fs_plane;
    bool qscale;
    bool hud_head;
    double pose_ypr[3];
    double sref;
    int hide_bone;
    double seat[3];
} WpConfig;

static WpConfig wp;
static WpConfig wp_next;
static bool wp_next_valid;
static bool wp_init;
static int wp_ctl_state = -1;
static const char *wp_ctl_path;
static bool wp_ctl_torn_said;
static char wp_ctl_bad_line[512];
static bool wp_ctl_seen;
static bool wp_setview_used;
static int wp_cfg_said;
static int64_t wp_cfg_said_id;
static bool wp_cfg_override_said;
static unsigned int wp_title_age;
static VrWpPoseSource wp_source;
static bool wp_hmd_lost_said;
static bool wp_drew;
static bool wp_frame_rewritten;
#define WP_NO_COLOUR UINT64_MAX
static uint64_t wp_draw_key;
static bool wp_draw_relative;
#define WP_SCOPED_SURFACES 8
static uint64_t wp_scoped_keys[WP_SCOPED_SURFACES];
static unsigned int wp_scoped_n;
static bool wp_scoped_everywhere;

static void wp_scoped_clear(void)
{
    wp_scoped_n = 0;
    wp_scoped_everywhere = false;
}

static void wp_scoped_note(uint64_t key)
{
    if (key == WP_NO_COLOUR) {
        return;
    }
    if (!key) {
        wp_scoped_everywhere = true;
        return;
    }
    for (unsigned int i = 0; i < wp_scoped_n; i++) {
        if (wp_scoped_keys[i] == key) {
            return;
        }
    }
    if (wp_scoped_n < WP_SCOPED_SURFACES) {
        wp_scoped_keys[wp_scoped_n++] = key;
    } else {
        wp_scoped_everywhere = true;
    }
}

static bool wp_scoped_on(uint64_t key)
{
    if (wp_scoped_everywhere || (!key && wp_scoped_n)) {
        return true;
    }
    for (unsigned int i = 0; i < wp_scoped_n; i++) {
        if (wp_scoped_keys[i] == key) {
            return true;
        }
    }
    return false;
}
static double wp_frame_gtan[4];
static bool wp_frame_gtan_have, wp_frame_fovmix;
static VrWpStampMachine wp_sm;
static VrWpStamp wp_shown;
static VrWpStampSource wp_shown_src;
static struct {
    unsigned int n, src[3], projection, age[4];
} wp_pstat;
static double wp_tan_said[4];
static int wp_invalidate_req;
static bool wp_stale_said;

static struct {
    bool valid, rewritten, scoped, cyup;
    float in[4][4], out[4][4];
    double gtan[4];
    VrWpDecomp d;
    unsigned int hits;
} wp_cache[WP_CACHE];
static unsigned int wp_cache_next;
static int wp_main_camera(void);

static struct {
    bool on;
    double yaw, pitch;
} wp_slave, wp_slave_frame;

static struct {
    bool valid;
    VrWp2d cls;
    float in[4][4], cam[4][4], out[4][4];
} wp_2d_cache[WP_CACHE];
static unsigned int wp_2d_cache_next;
static bool wp_draw_fullscreen;
static bool wp_draw_2d;
static struct {
    double game, eye, game_px;
} wp_q;
static int wp_2d_key = -1;

static unsigned int wp_frame;
static struct {
    unsigned int draws, matrices, rewrites, nearfov, scoped, refused[4];
    unsigned int arms_skipped;
    unsigned int flat2d, depth2d, full2d, qscaled;
    double a, b, resid;
    bool have_a;
} wp_stat;


static void wp_config_off(WpConfig *c)
{
    *c = (WpConfig){ .group = 0, .scope_a = 2.0, .cyup = -1,
                     .upm = 1.0 / 3.048, .arms_near = 0.04, .palette = 60,
                     .ortho = -1, .hud_m = 2.0, .hudscale = 0.8,
                     .qscale = true, .hide_bone = -1 };
    vr_wp_view_identity(&c->view);
}

static bool wp_view_equal(const VrWpView *x, const VrWpView *y)
{
    return x->fov == y->fov && x->tan_l == y->tan_l && x->tan_r == y->tan_r &&
           x->tan_d == y->tan_d && x->tan_u == y->tan_u &&
           x->shear_p == y->shear_p &&
           !memcmp(x->rot, y->rot, sizeof(x->rot)) &&
           !memcmp(x->pos, y->pos, sizeof(x->pos));
}

static bool wp_config_equal(const WpConfig *x, const WpConfig *y)
{
    return x->on == y->on && x->hmd == y->hmd && x->upm == y->upm &&
           x->group == y->group &&
           x->scope_a == y->scope_a && x->cyup == y->cyup &&
           x->nearfov == y->nearfov && x->hmd_game_fov == y->hmd_game_fov &&
           x->arms_bones == y->arms_bones && x->arms_near == y->arms_near &&
           x->palette == y->palette && x->ortho == y->ortho &&
           x->hud_m == y->hud_m && x->hudscale == y->hudscale &&
           x->fs_plane == y->fs_plane && x->qscale == y->qscale &&
           x->hud_head == y->hud_head && x->sref == y->sref &&
           x->hide_bone == y->hide_bone &&
           !memcmp(x->seat, y->seat, sizeof(x->seat)) &&
           wp_view_equal(&x->view, &y->view);
}

static bool wp_num(const char *tok, double lo, double hi, double *out)
{
    char *end;
    double v;

    if (!tok || !*tok) {
        return false;
    }
    v = strtod(tok, &end);
    if (*end || !isfinite(v) || v < lo || v > hi) {
        return false;
    }
    *out = v;
    return true;
}

static bool wp_int(const char *tok, int lo, int hi, int *out)
{
    double v;

    if (!wp_num(tok, lo, hi, &v) || v != floor(v)) {
        return false;
    }
    *out = (int)v;
    return true;
}

static bool wp_parse(char *line, WpConfig *c)
{
    static const char *const sep = " \t\r\n";
    char *save = NULL;
    char *tok = strtok_r(line, sep, &save);
    double v[6];

    wp_config_off(c);
    if (!tok) {
        return false;
    }
    if (!strcmp(tok, "off")) {
        return strtok_r(NULL, sep, &save) == NULL;
    }
    if (!strcmp(tok, "hmd")) {
        c->hmd = true;
    } else if (strcmp(tok, "pose")) {
        goto bad;
    } else {
        for (int i = 0; i < 6; i++) {
            if (!wp_num(strtok_r(NULL, sep, &save), i < 3 ? -360.0 : -1e4,
                        i < 3 ? 360.0 : 1e4, &v[i])) {
                goto bad;
            }
        }
        vr_wp_rot_ypr(v[0], v[1], v[2], c->view.rot);
        c->pose_ypr[0] = v[0];
        c->pose_ypr[1] = v[1];
        c->pose_ypr[2] = v[2];
        c->view.pos[0] = v[3];
        c->view.pos[1] = v[4];
        c->view.pos[2] = v[5];
    }
    while ((tok = strtok_r(NULL, sep, &save))) {
        if (c->hmd && (!strcmp(tok, "shear") || !strcmp(tok, "nearfov"))) {
            goto bad;
        }
        if (!strcmp(tok, "upm")) {
            if (!wp_num(strtok_r(NULL, sep, &save), 1e-3, 1e3, &c->upm)) {
                goto bad;
            }
        } else if (!strcmp(tok, "fov")) {
            tok = strtok_r(NULL, sep, &save);
            if (tok && !strcmp(tok, "game")) {
                c->view.fov = VR_WP_FOV_GAME;
                c->hmd_game_fov = c->hmd;
            } else if (c->hmd) {
                goto bad;
            } else if (tok && !strcmp(tok, "tan")) {
                double *t[4] = { &c->view.tan_l, &c->view.tan_r,
                                 &c->view.tan_d, &c->view.tan_u };
                for (int i = 0; i < 4; i++) {
                    if (!wp_num(strtok_r(NULL, sep, &save), -50.0, 50.0,
                                t[i])) {
                        goto bad;
                    }
                }
                if (!(c->view.tan_r - c->view.tan_l > 1e-3) ||
                    !(c->view.tan_u - c->view.tan_d > 1e-3)) {
                    goto bad;
                }
                c->view.fov = VR_WP_FOV_TANGENTS;
            } else {
                goto bad;
            }
        } else if (!strcmp(tok, "shear")) {
            if (!wp_num(strtok_r(NULL, sep, &save), -10.0, 10.0,
                        &c->view.shear_p)) {
                goto bad;
            }
        } else if (!strcmp(tok, "group")) {
            if (!wp_int(strtok_r(NULL, sep, &save), 0,
                        NV2A_VERTEXSHADER_CONSTANTS - 4, &c->group)) {
                goto bad;
            }
        } else if (!strcmp(tok, "scope")) {
            if (!wp_num(strtok_r(NULL, sep, &save), 1e-6, 1e6,
                        &c->scope_a)) {
                goto bad;
            }
        } else if (!strcmp(tok, "nearfov")) {
            if (!wp_num(strtok_r(NULL, sep, &save), 0.0, 1e6, &c->nearfov)) {
                goto bad;
            }
        } else if (!strcmp(tok, "arms")) {
            if (!wp_int(strtok_r(NULL, sep, &save), 1, 64, &c->arms_bones)) {
                goto bad;
            }
        } else if (!strcmp(tok, "armsnear")) {
            if (!wp_num(strtok_r(NULL, sep, &save), 1e-6, 1e3,
                        &c->arms_near)) {
                goto bad;
            }
        } else if (!strcmp(tok, "palette")) {
            if (!wp_int(strtok_r(NULL, sep, &save), 0,
                        NV2A_VERTEXSHADER_CONSTANTS - 3, &c->palette)) {
                goto bad;
            }
        } else if (!strcmp(tok, "ortho")) {
            if (!wp_int(strtok_r(NULL, sep, &save), 0,
                        NV2A_VERTEXSHADER_CONSTANTS - 4, &c->ortho)) {
                goto bad;
            }
        } else if (!strcmp(tok, "hud")) {
            if (!wp_num(strtok_r(NULL, sep, &save), 0.05, 1000.0, &c->hud_m)) {
                goto bad;
            }
        } else if (!strcmp(tok, "hudscale")) {
            if (!wp_num(strtok_r(NULL, sep, &save), 0.05, 10.0,
                        &c->hudscale)) {
                goto bad;
            }
        } else if (!strcmp(tok, "hudlock")) {
            tok = strtok_r(NULL, sep, &save);
            if (tok && !strcmp(tok, "head")) {
                c->hud_head = true;
            } else if (tok && !strcmp(tok, "world")) {
                c->hud_head = false;
            } else {
                goto bad;
            }
        } else if (!strcmp(tok, "qscale")) {
            tok = strtok_r(NULL, sep, &save);
            if (tok && !strcmp(tok, "auto")) {
                c->qscale = true;
            } else if (tok && !strcmp(tok, "1")) {
                c->qscale = false;
            } else {
                goto bad;
            }
        } else if (!strcmp(tok, "fullscreen")) {
            tok = strtok_r(NULL, sep, &save);
            if (tok && !strcmp(tok, "keep")) {
                c->fs_plane = false;
            } else if (tok && !strcmp(tok, "plane")) {
                c->fs_plane = true;
            } else {
                goto bad;
            }
        } else if (!strcmp(tok, "cyup")) {
            if (!wp_int(strtok_r(NULL, sep, &save), 0, 1, &c->cyup)) {
                goto bad;
            }
        } else if (!strcmp(tok, "sref")) {
            if (!wp_num(strtok_r(NULL, sep, &save), 1e-6, 1e9, &c->sref)) {
                goto bad;
            }
        } else if (!strcmp(tok, "hidebone")) {
            if (!wp_int(strtok_r(NULL, sep, &save), 0,
                        NV2A_VERTEXSHADER_CONSTANTS - 3, &c->hide_bone)) {
                goto bad;
            }
        } else if (!strcmp(tok, "seat")) {
            for (int i = 0; i < 3; i++) {
                if (!wp_num(strtok_r(NULL, sep, &save), -10.0, 10.0,
                            &c->seat[i])) {
                    goto bad;
                }
            }
        } else {
            goto bad;
        }
    }
    if (c->cyup < 0 && c->group <= WP_VPSCL_SLOT &&
        c->group + 3 >= WP_VPSCL_SLOT) {
        goto bad;
    }
    if (c->ortho >= 0 &&
        ((c->ortho <= c->group + 3 && c->ortho + 3 >= c->group) ||
         (c->ortho <= WP_VPSCL_SLOT + 1 && c->ortho + 3 >= WP_VPSCL_SLOT))) {
        goto bad;
    }
    if (!c->hmd) {
        for (int i = 0; i < 3; i++) {
            c->view.pos[i] += c->upm * c->seat[i];
        }
    }
    c->on = true;
    return true;
bad:
    wp_config_off(c);
    return false;
}

static int wp_read_ctl(WpConfig *c, bool *parsed)
{
    char line[512], work[512];
    FILE *f = fopen(wp_ctl_path, "r");
    size_t n;

    if (!f) {
        return 0;
    }
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    n = strlen(line);
    if (n == 0 || line[n - 1] != '\n') {
        return 0;
    }
    memcpy(work, line, n + 1);
    *parsed = wp_parse(work, c);
    if (!*parsed && strcmp(line, wp_ctl_bad_line)) {
        fprintf(stderr, "vr-wp: frame %u: the control line did not parse; "
                "the view is off until a line does (an option this build "
                "does not know?): %.*s\n", wp_frame, (int)(n - 1), line);
    }
    if (*parsed) {
        wp_ctl_bad_line[0] = '\0';
    } else {
        memcpy(wp_ctl_bad_line, line, n + 1);
    }
    return 1;
}

enum { WP_CFG_SCREEN, WP_CFG_TITLE_UNKNOWN, WP_CFG_NO_PROFILE, WP_CFG_WAITS_MOD,
       WP_CFG_WAITS_VIEW, WP_CFG_ON };

static void wp_from_config(WpConfig *c, bool want)
{
    const VrWpProfile *p = NULL;
    int64_t id = -1;
    int state = WP_CFG_SCREEN;
    bool waits = false, waits_mod = false;

    wp_config_off(c);
    if (want) {
        unsigned int period = vr_wp_host_title_id() < 0 ? WP_TITLE_RETRY
                                                        : WP_TITLE_REFRESH;
        if (wp_title_age == 0) {
            vr_wp_host_title_request(false);
        }
        wp_title_age = (wp_title_age + 1) % period;
        id = vr_wp_host_title_id();
        p = id >= 0 ? vr_wp_profile_find((uint32_t)id) : NULL;
        if (p && p->cyup < 0 && p->group <= WP_VPSCL_SLOT &&
            p->group + 3 >= WP_VPSCL_SLOT) {
            p = NULL;
        }
        if (p && p->layer2d &&
            ((p->ortho <= p->group + 3 && p->ortho + 3 >= p->group) ||
             (p->ortho <= WP_VPSCL_SLOT + 1 && p->ortho + 3 >= WP_VPSCL_SLOT))) {
            p = NULL;
        }
        waits_mod = p && p->requires_mod &&
                    !vr_wp_host_mod_armed(p->requires_mod);
        waits = waits_mod || (p && p->requires_view &&
                              !vr_wp_host_mod_armed(p->requires_view->name));
        state = id < 0     ? WP_CFG_TITLE_UNKNOWN :
                !p         ? WP_CFG_NO_PROFILE :
                waits_mod  ? WP_CFG_WAITS_MOD :
                waits      ? WP_CFG_WAITS_VIEW : WP_CFG_ON;
    }
    if (state != wp_cfg_said || (id >= 0 && id != wp_cfg_said_id)) {
        switch (state) {
        case WP_CFG_SCREEN:
            fprintf(stderr, "vr-wp: frame %u: view from config: screen\n",
                    wp_frame);
            break;
        case WP_CFG_TITLE_UNKNOWN:
            fprintf(stderr, "vr-wp: frame %u: view from config: projection, "
                    "title not known yet: screen\n", wp_frame);
            break;
        case WP_CFG_WAITS_MOD:
        case WP_CFG_WAITS_VIEW:
            fprintf(stderr, "vr-wp: frame %u: view from config: profile "
                    "%08llX %s waits for '%s' (not armed): screen\n",
                    wp_frame, (unsigned long long)id, p->name,
                    state == WP_CFG_WAITS_MOD ? p->requires_mod
                                              : p->requires_view->name);
            break;
        case WP_CFG_NO_PROFILE:
            fprintf(stderr, "vr-wp: frame %u: view from config: no profile "
                    "for title %08llX: screen\n", wp_frame,
                    (unsigned long long)id);
            break;
        default:
            fprintf(stderr, "vr-wp: frame %u: view from config: projection "
                    "(profile %08llX %s)\n", wp_frame,
                    (unsigned long long)id, p->name);
        }
        wp_cfg_said = state;
        wp_cfg_said_id = id;
    }
    if (!p || waits) {
        return;
    }
    c->on = true;
    c->hmd = true;
    c->group = p->group;
    c->scope_a = p->scope_a;
    c->cyup = p->cyup;
    c->upm = p->upm;
    c->palette = p->palette;
    c->arms_bones = p->arms_bones;
    c->arms_near = p->arms_near;
    c->sref = p->sref;
    c->hide_bone = p->hide_bone > 0 ? p->hide_bone : -1;
    memcpy(c->seat, p->seat, sizeof(c->seat));
    if (p->layer2d) {
        c->hud_head = p->hud_head;
        c->ortho = p->ortho;
        c->hud_m = p->hud_m;
        c->hudscale = p->hudscale;
    }
}

void vr_worldproj_set_view(const VrWpView *view, bool on)
{
    wp_setview_used = true;
    wp_config_off(&wp_next);
    if (view) {
        wp_next.view = *view;
    }
    wp_next.on = on;
    wp_next_valid = true;
}

bool vr_worldproj_script_head(double *yaw_left, double *pitch_up)
{
    if (!wp.on || wp.hmd) {
        return false;
    }
    *yaw_left = -wp.pose_ypr[0] * M_PI / 180.0;
    *pitch_up = wp.pose_ypr[1] * M_PI / 180.0;
    return true;
}

void vr_worldproj_set_slave(bool on, double body_yaw, double body_pitch)
{
    wp_slave.on = on && isfinite(body_yaw) && !isinf(body_pitch);
    wp_slave.yaw = body_yaw;
    wp_slave.pitch = body_pitch;
}

static void wp_view_for(const VrWpDecomp *d, VrWpView *view)
{
    if (wp_slave_frame.on) {
        VrWpView v = *view;
        vr_wp_slave_view(d, &v, wp_slave_frame.yaw, wp_slave_frame.pitch,
                         view);
    }
}

void vr_worldproj_set_pose_source(VrWpPoseSource src)
{
    wp_source = src;
}

bool vr_worldproj_active(void)
{
    return wp.on;
}

int vr_worldproj_renderer_rows(int *rows)
{
    int n = 0;

    if (!wp.on) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        rows[n++] = wp.group + i;
    }
    if (wp.ortho >= 0) {
        for (int i = 0; i < 4; i++) {
            rows[n++] = wp.ortho + i;
        }
    }
    if (wp.hide_bone >= 0) {
        for (int i = 0; i < 3; i++) {
            rows[n++] = wp.hide_bone + i;
        }
    }
    return n;
}

void vr_worldproj_draw_target(uint64_t key, bool writes_colour)
{
    wp_draw_key = writes_colour ? key : WP_NO_COLOUR;
}

void vr_worldproj_draw_relative(bool relative)
{
    wp_draw_relative = relative;
}

static bool wp_tan_new(const double t[4])
{
    for (int i = 0; i < 4; i++) {
        if (!(fabs(t[i] - wp_tan_said[i]) <= 5e-5)) {
            return true;
        }
    }
    return false;
}

static bool wp_hmd_view(WpConfig *c, bool fresh, VrWpStamp *s)
{
    VrWpHmdPose hp;

    if (!wp_source || !wp_source(fresh, &hp)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!isfinite(hp.q[i]) || !isfinite(hp.ref_q[i]) ||
            !isfinite(hp.tan[i])) {
            return false;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(hp.p[i]) || !isfinite(hp.ref_p[i])) {
            return false;
        }
    }
    if (!(hp.tan[1] - hp.tan[0] > 1e-3) || !(hp.tan[3] - hp.tan[2] > 1e-3)) {
        return false;
    }
    vr_wp_view_from_xr(hp.ref_q, hp.ref_p, hp.q, hp.p, c->upm, &c->view);
    for (int i = 0; i < 3; i++) {
        c->view.pos[i] += c->upm * c->seat[i];
    }
    c->view.fov = c->hmd_game_fov ? VR_WP_FOV_GAME : VR_WP_FOV_TANGENTS;
    c->view.tan_l = hp.tan[0];
    c->view.tan_r = hp.tan[1];
    c->view.tan_d = hp.tan[2];
    c->view.tan_u = hp.tan[3];
    s->display_time = hp.display_time;
    memcpy(s->xr_q, hp.q, sizeof(s->xr_q));
    memcpy(s->xr_p, hp.p, sizeof(s->xr_p));
    memcpy(s->tan, hp.tan, sizeof(s->tan));
    if (fresh || wp_frame % 30 == 0) {
        const double r2d = 180.0 / 3.14159265358979323846;
        fprintf(stderr, "vr-wp: frame %u: hmd pose yaw %.2f pitch %.2f roll "
                "%.2f pos %.4f %.4f %.4f (game units)%s\n", wp_frame,
                atan2(c->view.rot[0][2], c->view.rot[2][2]) * r2d,
                asin(fmax(-1.0, fmin(1.0, c->view.rot[1][2]))) * r2d,
                atan2(-c->view.rot[1][0], c->view.rot[1][1]) * r2d,
                c->view.pos[0], c->view.pos[1], c->view.pos[2],
                fresh ? " (fresh: re-anchored)" : "");
    }
    if (!c->hmd_game_fov && wp_tan_new(hp.tan)) {
        fprintf(stderr, "vr-wp: frame %u: headset frustum (drawn and "
                "declared) tan l %.4f r %.4f d %.4f u %.4f\n", wp_frame,
                hp.tan[0], hp.tan[1], hp.tan[2], hp.tan[3]);
        memcpy(wp_tan_said, hp.tan, sizeof(wp_tan_said));
    }
    return true;
}

void vr_worldproj_invalidate(void)
{
    __atomic_store_n(&wp_invalidate_req, 1, __ATOMIC_RELEASE);
}

static void wp_take_invalidate(void)
{
    if (!__atomic_exchange_n(&wp_invalidate_req, 0, __ATOMIC_ACQ_REL)) {
        return;
    }
    vr_wp_stamp_invalidate(&wp_sm);
    memset(&wp_shown, 0, sizeof(wp_shown));
    wp_shown_src = VR_WP_STAMP_NONE;
    wp_drew = false;
    wp_frame_rewritten = false;
    wp_scoped_clear();
    fprintf(stderr, "vr-wp: frame %u: stamps invalidated\n", wp_frame);
    if (vr_wp_host_view_projection()) {
        vr_wp_host_title_request(true);
    }
}

static uint64_t wp_probe_present;

static uint64_t wp_last_flip_key;

uint64_t vr_worldproj_flip_key(void)
{
    return wp_last_flip_key;
}

uint64_t vr_worldproj_last_present(void)
{
    return __atomic_load_n(&wp_probe_present, __ATOMIC_RELAXED);
}

void vr_worldproj_flip(PGRAPHState *pg, uint64_t surface_key)
{
    WpConfig c;
    VrWpStamp s;
    bool parsed = true;
    bool cfg_want = vr_wp_host_view_projection();

    wp_last_flip_key = surface_key;


    wp_take_invalidate();

    if (!wp_init) {
        wp_config_off(&wp);
        wp_init = true;
    }
    if (wp_ctl_state < 0) {
        wp_ctl_path = getenv("XEMU_VR_WORLDPROJ");
        wp_ctl_state = wp_ctl_path && wp_ctl_path[0];
        if (wp_ctl_state) {
            fprintf(stderr, "vr-wp: control file %s\n", wp_ctl_path);
        }
    }
    if (!wp_ctl_state && !wp_next_valid && !wp.on &&
        (!cfg_want || wp_setview_used)) {
        if (wp_sm.seq) {
            vr_wp_stamp_invalidate(&wp_sm);
            memset(&wp_shown, 0, sizeof(wp_shown));
            wp_shown_src = VR_WP_STAMP_NONE;
            wp_sm.seq = 0;
        }
        return;
    }

    if (wp_ctl_state && wp.on) {
        int m = wp_main_camera();
        if (m >= 0) {
            const VrWpDecomp *dm = &wp_cache[m].d;
            double sx = vr_wp_dot3(dm->A[0], dm->A[0]);
            double sz = vr_wp_dot3(dm->A[2], dm->A[2]);
            sx = sx > 0 ? 1.0 / sqrt(sx) : 0;
            sz = sz > 0 ? 1.0 / sqrt(sz) : 0;
            fprintf(stderr, "vr-wp: frame %u: camera fwd %.5f %.5f %.5f "
                    "right %.5f %.5f %.5f\n", wp_frame,
                    dm->A[2][0] * sz, dm->A[2][1] * sz, dm->A[2][2] * sz,
                    dm->A[0][0] * sx, dm->A[0][1] * sx, dm->A[0][2] * sx);
        }
        fprintf(stderr, "vr-wp: frame %u group %d draws %u matrices %u "
                "rewrites %u nearfov %u scoped %u arms %u 2d flat %u depth %u "
                "full %u qscaled %u refused %u/%u/%u a %.5f b %.5f resid %.1e\n",
                wp_frame, wp.group, wp_stat.draws, wp_stat.matrices,
                wp_stat.rewrites, wp_stat.nearfov, wp_stat.scoped,
                wp_stat.arms_skipped, wp_stat.flat2d, wp_stat.depth2d,
                wp_stat.full2d, wp_stat.qscaled,
                wp_stat.refused[VR_WP_NO_PERSPECTIVE],
                wp_stat.refused[VR_WP_DEGENERATE],
                wp_stat.refused[VR_WP_NOT_A_CAMERA], wp_stat.a, wp_stat.b,
                wp_stat.resid);
    }
    {
        bool reproj = wp.on && wp_frame_rewritten && !wp_scoped_on(surface_key);
        if (reproj && wp.hmd_game_fov) {
            if (!wp_frame_gtan_have || wp_frame_fovmix) {
                reproj = false;
            } else {
                memcpy(wp_sm.latch.tan, wp_frame_gtan, sizeof(wp_frame_gtan));
                if (wp_tan_new(wp_frame_gtan)) {
                    fprintf(stderr, "vr-wp: frame %u: game frustum (drawn "
                            "and declared) tan l %.4f r %.4f d %.4f u %.4f\n",
                            wp_frame, wp_frame_gtan[0], wp_frame_gtan[1],
                            wp_frame_gtan[2], wp_frame_gtan[3]);
                    memcpy(wp_tan_said, wp_frame_gtan, sizeof(wp_tan_said));
                }
            }
        }
        vr_wp_stamp_close(&wp_sm, surface_key, wp_drew, reproj);
    }
    wp_drew = false;
    wp_frame_rewritten = false;
    wp_scoped_clear();
    wp_frame_gtan_have = false;
    wp_frame_fovmix = false;
    wp_frame++;
    memset(&wp_stat, 0, sizeof(wp_stat));
    for (int i = 0; i < WP_CACHE; i++) {
        wp_cache[i].hits = 0;
    }

    c = wp;
    if (wp_ctl_state) {
        if (!wp_read_ctl(&c, &parsed)) {
            if (!wp_ctl_torn_said) {
                fprintf(stderr, "vr-wp: frame %u: no complete line in the "
                        "control file; the previous view is kept%s\n",
                        wp_frame, wp_ctl_seen ? "" : " (the config's)");
                wp_ctl_torn_said = true;
            }
            c = wp;
            if (!wp_ctl_seen && !wp_setview_used) {
                wp_from_config(&c, cfg_want);
            }
        } else {
            wp_ctl_torn_said = false;
            wp_ctl_seen = true;
            if (cfg_want && !wp_cfg_override_said) {
                fprintf(stderr, "vr-wp: frame %u: the control file overrides "
                        "the config\n", wp_frame);
                wp_cfg_override_said = true;
            }
        }
    } else if (wp_next_valid) {
        c = wp_next;
    } else if (!wp_setview_used) {
        wp_from_config(&c, cfg_want);
    }
    wp_next_valid = false;

    memset(&s, 0, sizeof(s));
    if (wp_slave_frame.on != wp_slave.on ||
        wp_slave_frame.yaw != wp_slave.yaw ||
        (wp_slave_frame.pitch != wp_slave.pitch &&
         !(isnan(wp_slave_frame.pitch) && isnan(wp_slave.pitch)))) {
        wp_slave_frame = wp_slave;
        memset(wp_cache, 0, sizeof(wp_cache));
        memset(wp_2d_cache, 0, sizeof(wp_2d_cache));
        for (int i = 0; i < 4; i++) {
            pgraph_vsh_constant_dirty(pg, wp.group + i);
            if (wp.ortho >= 0) {
                pgraph_vsh_constant_dirty(pg, wp.ortho + i);
            }
        }
    }
    if (c.on && c.hmd) {
        if (wp_hmd_view(&c, !(wp.on && wp.hmd), &s)) {
            s.declarable = true;
            wp_hmd_lost_said = false;
        } else {
            if (!wp_hmd_lost_said) {
                fprintf(stderr, "vr-wp: frame %u: hmd, but no headset pose: "
                        "drawn flat until there is one\n", wp_frame);
                wp_hmd_lost_said = true;
            }
            c.on = false;
        }
    }
    s.view = c.view;
    vr_wp_stamp_latch(&wp_sm, &s);

    if (wp_config_equal(&c, &wp)) {
        return;
    }

    if (c.on != wp.on || c.hmd != wp.hmd || !parsed) {
        fprintf(stderr, "vr-wp: frame %u -> %s%s%s\n", wp_frame,
                c.on ? "on" : "off", c.hmd ? " (hmd)" : "",
                parsed ? "" : " (the control line did not parse)");
    }
    for (int i = 0; i < 4; i++) {
        pgraph_vsh_constant_dirty(pg, wp.group + i);
        pgraph_vsh_constant_dirty(pg, c.group + i);
        if (wp.ortho >= 0) {
            pgraph_vsh_constant_dirty(pg, wp.ortho + i);
        }
        if (c.ortho >= 0) {
            pgraph_vsh_constant_dirty(pg, c.ortho + i);
        }
    }
    for (int i = 0; i < 3; i++) {
        if (wp.hide_bone >= 0) {
            pgraph_vsh_constant_dirty(pg, wp.hide_bone + i);
        }
        if (c.hide_bone >= 0) {
            pgraph_vsh_constant_dirty(pg, c.hide_bone + i);
        }
    }
    wp = c;
    memset(wp_cache, 0, sizeof(wp_cache));
    memset(wp_2d_cache, 0, sizeof(wp_2d_cache));
    wp_2d_key = -1;
}

void vr_worldproj_present(uint64_t key)
{
    __atomic_store_n(&wp_probe_present, key, __ATOMIC_RELAXED);
    if (!wp_init) {
        return;
    }
    wp_take_invalidate();
    wp_shown_src = vr_wp_stamp_present(&wp_sm, key, &wp_shown);
    if (wp_shown_src != VR_WP_STAMP_NONE &&
        !vr_wp_stamp_fresh(&wp_sm, &wp_shown, WP_STAMP_MAX_AGE)) {
        if (!wp_stale_said) {
            fprintf(stderr, "vr-wp: frame %u: the surface on show has a "
                    "stamp %llu flips old: shown flat\n", wp_frame,
                    (unsigned long long)(wp_sm.seq - wp_shown.seq));
            wp_stale_said = true;
        }
        memset(&wp_shown, 0, sizeof(wp_shown));
        wp_shown_src = VR_WP_STAMP_NONE;
    } else if (wp_shown_src != VR_WP_STAMP_NONE) {
        wp_stale_said = false;
    }
    if (!wp_ctl_state) {
        return;
    }
    wp_pstat.n++;
    wp_pstat.src[wp_shown_src]++;
    wp_pstat.projection += wp_shown_src != VR_WP_STAMP_NONE &&
                           wp_shown.declarable && wp_shown.reprojected;
    if (wp_shown_src != VR_WP_STAMP_NONE) {
        uint64_t age = wp_sm.seq - wp_shown.seq;
        wp_pstat.age[age < 3 ? age : 3]++;
    }
    if (wp_pstat.n == 120) {
        fprintf(stderr, "vr-wp: frame %u: last 120 presents: stamp copied %u "
                "last %u none %u; projection %u; age 0/1/2/3+: %u/%u/%u/%u\n",
                wp_frame, wp_pstat.src[VR_WP_STAMP_COPIED],
                wp_pstat.src[VR_WP_STAMP_LAST], wp_pstat.src[VR_WP_STAMP_NONE],
                wp_pstat.projection, wp_pstat.age[0], wp_pstat.age[1],
                wp_pstat.age[2], wp_pstat.age[3]);
        memset(&wp_pstat, 0, sizeof(wp_pstat));
    }
}

VrWpStampSource vr_worldproj_presented(VrWpStamp *out)
{
    *out = wp_shown;
    return wp_shown_src;
}

static void wp_rewrite_group(float in[4][4], bool cyup, float out[4][4],
                             bool *rewritten, bool *scoped, double gtan[4],
                             VrWpDecomp *dout)
{
    VrWpDecomp d;
    VrWpVerdict verdict = vr_wp_decompose(in, &d);

    *rewritten = false;
    *scoped = false;
    wp_stat.matrices++;
    if (verdict != VR_WP_OK) {
        wp_stat.refused[verdict]++;
        return;
    }
    if (!wp_stat.have_a) {
        wp_stat.a = d.a;
        wp_stat.b = d.b;
        wp_stat.resid = d.resid;
        wp_stat.have_a = true;
    }
    if (d.a > wp.scope_a) {
        wp_stat.scoped++;
        *scoped = true;
        return;
    }
    VrWpView view = wp.view;
    wp_view_for(&d, &view);
    bool in_near = wp.nearfov > 0 && d.r != 0 && -d.t / d.r < wp.nearfov;
    if (in_near) {
        view.fov = VR_WP_FOV_GAME;
        view.shear_p = 0;
    }
    if (wp.sref > 0) {
        for (int i = 0; i < 3; i++) {
            view.pos[i] *= wp.sref / d.s;
        }
    }
    if (!vr_wp_rewrite(&d, &view, cyup, out)) {
        wp_stat.refused[VR_WP_DEGENERATE]++;
        return;
    }
    vr_wp_game_tangents(&d, cyup, gtan);
    wp_stat.nearfov += in_near;
    wp_stat.rewrites++;
    *dout = d;
    *rewritten = true;
}

static int wp_main_camera(void)
{
    int best = -1;

    for (int i = 0; i < WP_CACHE; i++) {
        if (wp_cache[i].valid && wp_cache[i].rewritten && wp_cache[i].hits &&
            (best < 0 || wp_cache[i].hits > wp_cache[best].hits)) {
            best = i;
        }
    }
    return best;
}

static void wp_apply_2d(float c[][4])
{
    float (*o)[4] = &c[wp.ortho];
    VrWp2d cls = vr_wp_2d_class(o);
    int m = wp_main_camera();
    int hit = -1;

    if (cls == VR_WP_2D_NONE || m < 0) {
        return;
    }
    if (cls == VR_WP_2D_FLAT && wp_draw_fullscreen && !wp.fs_plane) {
        wp_stat.full2d++;
        return;
    }
    for (int i = 0; i < WP_CACHE; i++) {
        if (wp_2d_cache[i].valid &&
            !memcmp(wp_2d_cache[i].in, o, sizeof(wp_2d_cache[i].in)) &&
            !memcmp(wp_2d_cache[i].cam, wp_cache[m].in,
                    sizeof(wp_2d_cache[i].cam))) {
            hit = i;
            break;
        }
    }
    if (hit < 0) {
        float out[4][4];
        double depth = wp.hud_m * wp.upm *
                       (wp.sref > 0 ? wp.sref : wp_cache[m].d.s);
        VrWpView view = wp.view;
        wp_view_for(&wp_cache[m].d, &view);
        if (cls == VR_WP_2D_FLAT && wp.hud_head) {
            VrWpView id;
            vr_wp_view_identity(&id);
            memcpy(view.rot, id.rot, sizeof(view.rot));
            memcpy(view.pos, id.pos, sizeof(view.pos));
        }
        if (!vr_wp_rewrite_2d(&wp_cache[m].d, &view, wp_cache[m].cyup, o,
                              cls, depth, wp.hudscale, out)) {
            return;
        }
        hit = wp_2d_cache_next++ % WP_CACHE;
        memcpy(wp_2d_cache[hit].in, o, sizeof(wp_2d_cache[hit].in));
        memcpy(wp_2d_cache[hit].cam, wp_cache[m].in,
               sizeof(wp_2d_cache[hit].cam));
        memcpy(wp_2d_cache[hit].out, out, sizeof(out));
        wp_2d_cache[hit].cls = cls;
        wp_2d_cache[hit].valid = true;
    }
    if (cls == VR_WP_2D_FLAT) {
        wp_stat.flat2d++;
    } else {
        wp_stat.depth2d++;
    }
    memcpy(o, wp_2d_cache[hit].out, sizeof(wp_2d_cache[hit].out));
}

static void wp_measure_query(PGRAPHState *pg, const float *xyzw,
                             unsigned int n)
{
    float o[4][4], e[4][4], vp[4];
    double pg2[64][2], pe2[64][2];
    int m = wp_main_camera();

    memcpy(o, pg->vsh_constants[wp.ortho], sizeof(o));
    memcpy(vp, pg->vsh_constants[WP_VPSCL_SLOT], sizeof(vp));
    if (m < 0 || vr_wp_2d_class(o) != VR_WP_2D_DEPTH ||
        !vr_wp_rewrite_2d(&wp_cache[m].d, &wp.view, wp_cache[m].cyup, o,
                          VR_WP_2D_DEPTH, 1.0, 1.0, e)) {
        return;
    }
    for (unsigned int k = 0; k < n; k++) {
        const float *v = &xyzw[4 * k];
        double cg[4], ce[4];
        for (int i = 0; i < 4; i++) {
            cg[i] = o[i][0] * v[0] + o[i][1] * v[1] + o[i][2] * v[2] +
                    o[i][3] * v[3];
            ce[i] = e[i][0] * v[0] + e[i][1] * v[1] + e[i][2] * v[2] +
                    e[i][3] * v[3];
        }
        if (!(cg[3] > 1e-9) || !(ce[3] > 1e-9)) {
            return;
        }
        pg2[k][0] = cg[0] / cg[3];
        pg2[k][1] = cg[1] / cg[3];
        pe2[k][0] = ce[0] / ce[3];
        pe2[k][1] = ce[1] / ce[3];
    }
    double ag = vr_wp_hull_area(pg2, (int)n), ae = vr_wp_hull_area(pe2, (int)n);
    if (!(ag > 0) || !(ae > 0)) {
        return;
    }
    wp_q.game += ag;
    wp_q.eye += ae;
    wp_q.game_px += ag * fabs((double)vp[0] * vp[1]);
    wp_stat.qscaled++;
}

void vr_worldproj_report_clear(void)
{
    memset(&wp_q, 0, sizeof(wp_q));
}

bool vr_worldproj_report_take(double *scale, double *cap)
{
    bool have = wp_q.eye > 0 && wp_q.game > 0;

    *scale = have ? wp_q.game / wp_q.eye : 1.0;
    *cap = have ? wp_q.game_px : 0.0;
    memset(&wp_q, 0, sizeof(wp_q));
    return have;
}

void vr_worldproj_draw_extent(PGRAPHState *pg, const float *xyzw,
                              unsigned int n)
{
    bool full = false;
    int key;

    if (!wp.on || wp.ortho < 0) {
        wp_draw_fullscreen = false;
        wp_draw_2d = false;
        return;
    }
    wp_draw_2d = wp_frame_rewritten && !wp_scoped_on(wp_draw_key);
    if (xyzw && n) {
        float o[4][4];
        double lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
        memcpy(o, pg->vsh_constants[wp.ortho], sizeof(o));
        for (unsigned int k = 0; k < n; k++) {
            const float *v = &xyzw[4 * k];
            double w = o[3][0] * v[0] + o[3][1] * v[1] + o[3][2] * v[2] +
                       o[3][3] * v[3];
            for (int i = 0; i < 2; i++) {
                double x = (o[i][0] * v[0] + o[i][1] * v[1] + o[i][2] * v[2] +
                            o[i][3] * v[3]) / w;
                lo[i] = fmin(lo[i], x);
                hi[i] = fmax(hi[i], x);
            }
        }
        full = lo[0] <= -0.99 && hi[0] >= 0.99 && lo[1] <= -0.99 &&
               hi[1] >= 0.99;
    }
    wp_draw_fullscreen = full;
    if (wp.qscale && pg->zpass_pixel_count_enable && xyzw && n >= 3 &&
        n <= 64 && wp_draw_2d) {
        wp_measure_query(pg, xyzw, n);
    }
    key = (full ? 1 : 0) | (wp_draw_2d ? 2 : 0) |
          ((wp_main_camera() + 1) << 2);
    if (key != wp_2d_key) {
        for (int i = 0; i < 4; i++) {
            pgraph_vsh_constant_dirty(pg, wp.ortho + i);
        }
        wp_2d_key = key;
    }
}


void vr_worldproj_apply(bool fixed_function, float c[][4])
{
    wp_drew = true;
    if (!wp.on) {
        return;
    }
    if (fixed_function) {
        return;
    }
    wp_stat.draws++;
    if (wp.hide_bone >= 0 && wp_draw_relative &&
        wp_draw_key != WP_NO_COLOUR) {
        for (int r = 0; r < 3; r++) {
            c[wp.hide_bone + r][0] = 0.0f;
            c[wp.hide_bone + r][1] = 0.0f;
            c[wp.hide_bone + r][2] = 0.0f;
        }
    }

    float (*in)[4] = &c[wp.group];
    bool cyup = wp.cyup >= 0 ? wp.cyup : !(c[WP_VPSCL_SLOT][1] > 0);
    int hit = -1;
    for (int i = 0; i < WP_CACHE; i++) {
        if (wp_cache[i].valid && wp_cache[i].cyup == cyup &&
            !memcmp(wp_cache[i].in, in, sizeof(wp_cache[i].in))) {
            hit = i;
            break;
        }
    }
    if (hit < 0) {
        hit = wp_cache_next++ % WP_CACHE;
        memcpy(wp_cache[hit].in, in, sizeof(wp_cache[hit].in));
        wp_cache[hit].cyup = cyup;
        wp_cache[hit].valid = true;
        wp_rewrite_group(wp_cache[hit].in, cyup, wp_cache[hit].out,
                         &wp_cache[hit].rewritten, &wp_cache[hit].scoped,
                         wp_cache[hit].gtan, &wp_cache[hit].d);
        wp_cache[hit].hits = 0;
    }
    wp_cache[hit].hits += wp_cache[hit].rewritten;
    wp_frame_rewritten |= wp_cache[hit].rewritten;
    if (wp_cache[hit].scoped) {
        wp_scoped_note(wp_draw_key);
    }
    if (wp_cache[hit].rewritten && wp.hmd_game_fov) {
        if (!wp_frame_gtan_have) {
            memcpy(wp_frame_gtan, wp_cache[hit].gtan, sizeof(wp_frame_gtan));
            wp_frame_gtan_have = true;
        } else {
            for (int i = 0; i < 4; i++) {
                if (!(fabs(wp_cache[hit].gtan[i] - wp_frame_gtan[i]) <=
                      1e-4 * fabs(wp_frame_gtan[i]))) {
                    wp_frame_fovmix = true;
                }
            }
        }
    }
    if (wp_cache[hit].rewritten) {
        memcpy(in, wp_cache[hit].out, sizeof(wp_cache[hit].out));
    }
    if (wp.ortho >= 0 && wp_draw_2d) {
        wp_apply_2d(c);
    }
}

bool vr_worldproj_end_skips(PGRAPHState *pg, bool vertex_program)
{
    float m[4][4];
    VrWpDecomp d;
    int bones = 0;

    if (!wp.on || wp.arms_bones <= 0 || !vertex_program) {
        return false;
    }
    memcpy(m, pg->vsh_constants[wp.group], sizeof(m));
    if (vr_wp_decompose(m, &d) != VR_WP_OK || !(-d.t / d.r < wp.arms_near)) {
        return false;
    }
    for (int row = wp.palette; row + 2 < NV2A_VERTEXSHADER_CONSTANTS;
         row += 3) {
        float b[3][4];
        memcpy(b, pg->vsh_constants[row], sizeof(b));
        double det = b[0][0] * (b[1][1] * b[2][2] - b[1][2] * b[2][1]) -
                     b[0][1] * (b[1][0] * b[2][2] - b[1][2] * b[2][0]) +
                     b[0][2] * (b[1][0] * b[2][1] - b[1][1] * b[2][0]);
        if (!(fabs(fabs(det) - 1.0) < 0.05)) {
            break;
        }
        bones++;
    }
    if (bones < wp.arms_bones) {
        return false;
    }
    wp_stat.arms_skipped++;
    return true;
}
