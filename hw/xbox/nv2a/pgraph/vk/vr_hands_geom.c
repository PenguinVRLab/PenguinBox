/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <string.h>

#include "vr_hands_geom.h"

#define J(name) XR_HAND_JOINT_##name##_EXT

static const VrHandBone s_bones[VR_HANDS_BONES] = {
    { J(WRIST), J(THUMB_METACARPAL) },
    { J(THUMB_METACARPAL), J(THUMB_PROXIMAL) },
    { J(THUMB_PROXIMAL), J(THUMB_DISTAL) },
    { J(THUMB_DISTAL), J(THUMB_TIP) },
    { J(WRIST), J(INDEX_METACARPAL) },
    { J(INDEX_METACARPAL), J(INDEX_PROXIMAL) },
    { J(INDEX_PROXIMAL), J(INDEX_INTERMEDIATE) },
    { J(INDEX_INTERMEDIATE), J(INDEX_DISTAL) },
    { J(INDEX_DISTAL), J(INDEX_TIP) },
    { J(WRIST), J(MIDDLE_METACARPAL) },
    { J(MIDDLE_METACARPAL), J(MIDDLE_PROXIMAL) },
    { J(MIDDLE_PROXIMAL), J(MIDDLE_INTERMEDIATE) },
    { J(MIDDLE_INTERMEDIATE), J(MIDDLE_DISTAL) },
    { J(MIDDLE_DISTAL), J(MIDDLE_TIP) },
    { J(WRIST), J(RING_METACARPAL) },
    { J(RING_METACARPAL), J(RING_PROXIMAL) },
    { J(RING_PROXIMAL), J(RING_INTERMEDIATE) },
    { J(RING_INTERMEDIATE), J(RING_DISTAL) },
    { J(RING_DISTAL), J(RING_TIP) },
    { J(WRIST), J(LITTLE_METACARPAL) },
    { J(LITTLE_METACARPAL), J(LITTLE_PROXIMAL) },
    { J(LITTLE_PROXIMAL), J(LITTLE_INTERMEDIATE) },
    { J(LITTLE_INTERMEDIATE), J(LITTLE_DISTAL) },
    { J(LITTLE_DISTAL), J(LITTLE_TIP) },
};

static VrHandBone s_bones_cross[VR_HANDS_BONES];

const VrHandBone *vr_hands_bones(VrHandsSabotage sab)
{
    if (sab == VR_HANDS_SAB_CROSS) {
        memcpy(s_bones_cross, s_bones, sizeof(s_bones));
        s_bones_cross[VR_HANDS_BONES - 1] =
            (VrHandBone){ J(INDEX_TIP), J(MIDDLE_TIP) };
        return s_bones_cross;
    }
    return s_bones;
}

static bool joint_drawn(const VrHandJoint *j, VrHandsSabotage sab)
{
    return j->valid || sab == VR_HANDS_SAB_INVALID;
}

static float joint_radius(const VrHandJoint *j, VrHandsSabotage sab)
{
    return sab == VR_HANDS_SAB_RADIUS ? 0.01f : j->radius;
}

uint32_t vr_hands_capsules(const VrHandJoints *h, VrHandsSabotage sab,
                           VrCapsule *out)
{
    uint32_t n = 0;
    if (!h->active) {
        return 0;
    }
    const VrHandBone *bones = vr_hands_bones(sab);
    for (uint32_t b = 0; b < VR_HANDS_BONES; b++) {
        const VrHandJoint *ja = &h->j[bones[b].from];
        const VrHandJoint *jb = &h->j[bones[b].to];
        if (!joint_drawn(ja, sab) || !joint_drawn(jb, sab)) {
            continue;
        }
        out[n++] = (VrCapsule){ ja->pose.position, jb->pose.position,
                                joint_radius(ja, sab), joint_radius(jb, sab) };
    }
    for (uint32_t k = 0; k < VR_HANDS_JOINTS; k++) {
        const VrHandJoint *j = &h->j[k];
        if (!joint_drawn(j, sab)) {
            continue;
        }
        out[n++] = (VrCapsule){ j->pose.position, j->pose.position,
                                joint_radius(j, sab), joint_radius(j, sab) };
    }
    return n;
}

bool vr_hands_layer_needed(const VrHandJoints h[2], VrHandsSabotage sab)
{
    if (sab == VR_HANDS_SAB_ALWAYS) {
        return true;
    }
    for (int k = 0; k < 2; k++) {
        if (!h[k].active) {
            continue;
        }
        for (uint32_t j = 0; j < VR_HANDS_JOINTS; j++) {
            if (joint_drawn(&h[k].j[j], sab)) {
                return true;
            }
        }
    }
    return false;
}

static bool hand_drawable(const VrHandJoints *h)
{
    if (!h->active) {
        return false;
    }
    for (uint32_t j = 0; j < VR_HANDS_JOINTS; j++) {
        if (h->j[j].valid) {
            return true;
        }
    }
    return false;
}

bool vr_hands_filter(VrHandJoints *h, VrHandsHold *st, XrTime t,
                     const VrHandsFilterCfg *cfg, VrHandsSabotage sab)
{
    h->held = false;
    if (cfg->require_tracked && sab != VR_HANDS_SAB_UNTRACKED) {
        for (uint32_t j = 0; j < VR_HANDS_JOINTS; j++) {
            h->j[j].valid = h->j[j].valid && h->j[j].tracked;
        }
    }
    if (hand_drawable(h)) {
        st->have = true;
        st->t_good = t;
        st->last = *h;
        return false;
    }
    if (cfg->hold_ns <= 0 || !st->have) {
        return false;
    }
    const int64_t age = (int64_t)t - (int64_t)st->t_good;
    if ((age < 0 || age > cfg->hold_ns) && sab != VR_HANDS_SAB_HOLDFOREVER) {
        st->have = false;
        return false;
    }
    *h = st->last;
    h->held = true;
    return true;
}

static XrVector3f v3(float x, float y, float z)
{
    return (XrVector3f){ x, y, z };
}
static XrVector3f v3_sub(XrVector3f a, XrVector3f b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static XrVector3f v3_add(XrVector3f a, XrVector3f b)
{
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static XrVector3f v3_scale(XrVector3f a, float s)
{
    return v3(a.x * s, a.y * s, a.z * s);
}
static float v3_dot(XrVector3f a, XrVector3f b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static XrVector3f v3_cross(XrVector3f a, XrVector3f b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}
static float v3_len(XrVector3f a)
{
    return sqrtf(v3_dot(a, a));
}
static XrVector3f v3_norm(XrVector3f a)
{
    const float l = v3_len(a);
    return l > 1e-9f ? v3_scale(a, 1.0f / l) : v3(0.0f, 1.0f, 0.0f);
}
static XrVector3f v3_perp(XrVector3f v)
{
    const XrVector3f a = fabsf(v.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0);
    return v3_norm(v3_cross(a, v));
}

static struct {
    bool ready;
    float ring_c[VR_HANDS_SEG + 1], ring_s[VR_HANDS_SEG + 1];
    XrVector3f sphere_n[(VR_HANDS_RINGS + 1) * (VR_HANDS_SEG + 1)];
} s_tab;

static void tables(void)
{
    if (s_tab.ready) {
        return;
    }
    for (uint32_t s = 0; s <= VR_HANDS_SEG; s++) {
        const float th = 2.0f * (float)M_PI * (float)s / (float)VR_HANDS_SEG;
        s_tab.ring_c[s] = cosf(th);
        s_tab.ring_s[s] = sinf(th);
    }
    for (uint32_t ring = 0; ring <= VR_HANDS_RINGS; ring++) {
        const float phi = (float)M_PI * (float)ring / (float)VR_HANDS_RINGS;
        const float sp = sinf(phi), cp = cosf(phi);
        for (uint32_t s = 0; s <= VR_HANDS_SEG; s++) {
            s_tab.sphere_n[ring * (VR_HANDS_SEG + 1) + s] =
                (XrVector3f){ sp * s_tab.ring_c[s], cp, sp * s_tab.ring_s[s] };
        }
    }
    s_tab.ready = true;
}

static bool is_sphere(const VrCapsule *c)
{
    return v3_len(v3_sub(c->b, c->a)) < 1e-6f;
}

uint32_t vr_hands_capsule_vertices(const VrCapsule *c)
{
    return is_sphere(c) ? (VR_HANDS_RINGS + 1) * (VR_HANDS_SEG + 1) :
                          2 * VR_HANDS_SEG;
}

uint32_t vr_hands_capsule_indices(const VrCapsule *c)
{
    return is_sphere(c) ? 6 * VR_HANDS_RINGS * VR_HANDS_SEG :
                          6 * VR_HANDS_SEG;
}

static void put_vertex(VrNfMesh *m, XrVector3f p, XrVector3f n,
                       VrHandsSabotage sab)
{
    if (m->nv >= m->vcap) {
        m->overflow = true;
        return;
    }
    if (sab == VR_HANDS_SAB_FLIP) {
        n = v3_scale(n, -1.0f);
    }
    VrNfVertex *v = &m->v[m->nv++];
    v->p[0] = p.x;
    v->p[1] = p.y;
    v->p[2] = p.z;
    v->n[0] = n.x;
    v->n[1] = n.y;
    v->n[2] = n.z;
}

static void put_tri(VrNfMesh *m, uint32_t a, uint32_t b, uint32_t c,
                    VrHandsSabotage sab)
{
    if (m->ni + 3 > m->icap) {
        m->overflow = true;
        return;
    }
    m->i[m->ni++] = a;
    if (sab == VR_HANDS_SAB_FLIP || sab == VR_HANDS_SAB_WIND) {
        m->i[m->ni++] = c;
        m->i[m->ni++] = b;
    } else {
        m->i[m->ni++] = b;
        m->i[m->ni++] = c;
    }
}

static void mesh_sphere(VrNfMesh *m, XrVector3f c, float r,
                        VrHandsSabotage sab)
{
    const uint32_t base = m->nv;
    tables();
    for (uint32_t k = 0; k < (VR_HANDS_RINGS + 1) * (VR_HANDS_SEG + 1); k++) {
        const XrVector3f n = s_tab.sphere_n[k];
        put_vertex(m, v3_add(c, v3_scale(n, r)), n, sab);
    }
    for (uint32_t ring = 0; ring < VR_HANDS_RINGS; ring++) {
        for (uint32_t s = 0; s < VR_HANDS_SEG; s++) {
            const uint32_t a = base + ring * (VR_HANDS_SEG + 1) + s;
            const uint32_t b = a + VR_HANDS_SEG + 1;
            put_tri(m, a, b + 1, b, sab);
            put_tri(m, a, a + 1, b + 1, sab);
        }
    }
}

static void mesh_tube(VrNfMesh *m, const VrCapsule *c, VrHandsSabotage sab)
{
    const XrVector3f ab = v3_sub(c->b, c->a);
    const float len = v3_len(ab);
    const XrVector3f axis = v3_scale(ab, 1.0f / len);
    const XrVector3f u = v3_perp(axis);
    const XrVector3f w = v3_cross(axis, u);
    const float lean = (c->rb - c->ra) / len;
    const uint32_t base = m->nv;
    tables();
    for (uint32_t s = 0; s < VR_HANDS_SEG; s++) {
        const XrVector3f radial =
            v3_add(v3_scale(u, s_tab.ring_c[s]), v3_scale(w, s_tab.ring_s[s]));
        const XrVector3f n = v3_norm(v3_sub(radial, v3_scale(axis, lean)));
        put_vertex(m, v3_add(c->a, v3_scale(radial, c->ra)), n, sab);
        put_vertex(m, v3_add(c->b, v3_scale(radial, c->rb)), n, sab);
    }
    for (uint32_t s = 0; s < VR_HANDS_SEG; s++) {
        const uint32_t s1 = (s + 1) % VR_HANDS_SEG;
        const uint32_t a0 = base + 2 * s, b0 = a0 + 1;
        const uint32_t a1 = base + 2 * s1, b1 = a1 + 1;
        put_tri(m, a0, a1, b1, sab);
        put_tri(m, a0, b1, b0, sab);
    }
}

void vr_hands_mesh_capsule(VrNfMesh *m, const VrCapsule *c,
                           VrHandsSabotage sab)
{
    if (is_sphere(c)) {
        mesh_sphere(m, c->a, c->ra, sab);
    } else {
        mesh_tube(m, c, sab);
    }
}

uint32_t vr_hands_mesh(VrNfMesh *m, const VrHandJoints *h,
                       VrHandsSabotage sab)
{
    VrCapsule caps[VR_HANDS_MAX_CAPSULES];
    const uint32_t n = vr_hands_capsules(h, sab, caps);
    for (uint32_t k = 0; k < n; k++) {
        vr_hands_mesh_capsule(m, &caps[k], sab);
    }
    return n;
}
