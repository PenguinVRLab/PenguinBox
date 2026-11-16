/*
 * SPDX-FileCopyrightText: 2026 Patrick Carey
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgltf.h"
#include "vr_hands_gltf.h"

static const char *const s_names[VR_HANDS_JOINTS] = {
    "XR_HAND_JOINT_PALM_EXT",
    "XR_HAND_JOINT_WRIST_EXT",
    "XR_HAND_JOINT_THUMB_METACARPAL_EXT",
    "XR_HAND_JOINT_THUMB_PROXIMAL_EXT",
    "XR_HAND_JOINT_THUMB_DISTAL_EXT",
    "XR_HAND_JOINT_THUMB_TIP_EXT",
    "XR_HAND_JOINT_INDEX_METACARPAL_EXT",
    "XR_HAND_JOINT_INDEX_PROXIMAL_EXT",
    "XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT",
    "XR_HAND_JOINT_INDEX_DISTAL_EXT",
    "XR_HAND_JOINT_INDEX_TIP_EXT",
    "XR_HAND_JOINT_MIDDLE_METACARPAL_EXT",
    "XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT",
    "XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT",
    "XR_HAND_JOINT_MIDDLE_DISTAL_EXT",
    "XR_HAND_JOINT_MIDDLE_TIP_EXT",
    "XR_HAND_JOINT_RING_METACARPAL_EXT",
    "XR_HAND_JOINT_RING_PROXIMAL_EXT",
    "XR_HAND_JOINT_RING_INTERMEDIATE_EXT",
    "XR_HAND_JOINT_RING_DISTAL_EXT",
    "XR_HAND_JOINT_RING_TIP_EXT",
    "XR_HAND_JOINT_LITTLE_METACARPAL_EXT",
    "XR_HAND_JOINT_LITTLE_PROXIMAL_EXT",
    "XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT",
    "XR_HAND_JOINT_LITTLE_DISTAL_EXT",
    "XR_HAND_JOINT_LITTLE_TIP_EXT",
};

const char *vr_hands_gltf_joint_name(int joint)
{
    return joint >= 0 && joint < VR_HANDS_JOINTS ? s_names[joint] : "?";
}

static bool refuse(char *why, size_t n, const char *code, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static bool refuse(char *why, size_t n, const char *code, const char *fmt, ...)
{
    if (why && n) {
        int k = snprintf(why, n, "%s: ", code);
        if (k >= 0 && (size_t)k < n) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(why + k, n - (size_t)k, fmt, ap);
            va_end(ap);
        }
    }
    return false;
}

static int name_to_joint(const char *name)
{
    if (!name) {
        return -1;
    }
    for (int j = 0; j < VR_HANDS_JOINTS; j++) {
        if (!strcmp(name, s_names[j])) {
            return j;
        }
    }
    return -1;
}

static float mat_mul_dev_identity(const float a[16], const float b[16])
{
    float dev = 0.0f;
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) {
                s += a[k * 4 + r] * b[c * 4 + k];
            }
            float d = fabsf(s - (r == c ? 1.0f : 0.0f));
            dev = d > dev ? d : dev;
        }
    }
    return dev;
}

static const cgltf_attribute *find_attr(const cgltf_primitive *p,
                                        cgltf_attribute_type t, int index)
{
    for (cgltf_size i = 0; i < p->attributes_count; i++) {
        if (p->attributes[i].type == t && p->attributes[i].index == index) {
            return &p->attributes[i];
        }
    }
    return NULL;
}

static bool check_float_attr(const cgltf_attribute *a, cgltf_type type,
                             cgltf_size count)
{
    return a && a->data && a->data->type == type &&
           a->data->component_type == cgltf_component_type_r_32f &&
           !a->data->is_sparse && a->data->count == count;
}

bool vr_hands_gltf_load(const void *glb, size_t size, XrHandEXT side,
                        VrGltfSabotage sab, VrGltfHand *out, char *why,
                        size_t why_len)
{
    memset(out, 0, sizeof(*out));
    if (why && why_len) {
        why[0] = '\0';
    }
    cgltf_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.type = cgltf_file_type_glb;
    cgltf_data *d = NULL;
    bool ok = false;

    if (size < 12 || memcmp(glb, "glTF", 4) != 0) {
        return refuse(why, why_len, "FORMAT", "not a binary glTF (.glb)");
    }
    cgltf_result res = cgltf_parse(&opt, glb, size, &d);
    if (res != cgltf_result_success) {
        return refuse(why, why_len, "FORMAT", "cgltf_parse failed (%d)",
                      (int)res);
    }
    if (d->file_type != cgltf_file_type_glb || !d->asset.version ||
        strcmp(d->asset.version, "2.0") != 0) {
        refuse(why, why_len, "FORMAT", "not glTF 2.0 binary");
        goto done;
    }
    if (d->extensions_required_count) {
        refuse(why, why_len, "FORMAT", "extensionsRequired: %s",
               d->extensions_required[0]);
        goto done;
    }
    if (d->buffers_count != 1 || d->buffers[0].uri || !d->bin) {
        refuse(why, why_len, "FORMAT",
               "%u buffers; exactly one, embedded, is allowed",
               (unsigned)d->buffers_count);
        goto done;
    }
    if (d->images_count || d->textures_count) {
        refuse(why, why_len, "MATERIAL", "%u images, %u textures: none allowed",
               (unsigned)d->images_count, (unsigned)d->textures_count);
        goto done;
    }
    res = cgltf_load_buffers(&opt, d, NULL);
    if (res != cgltf_result_success) {
        refuse(why, why_len, "FORMAT", "embedded buffer unreadable (%d)",
               (int)res);
        goto done;
    }
    res = cgltf_validate(d);
    if (res != cgltf_result_success) {
        refuse(why, why_len, "FORMAT", "cgltf_validate failed (%d)", (int)res);
        goto done;
    }

    {
        const char *want = side == XR_HAND_LEFT_EXT ? "\"left\"" : "\"right\"";
        const char *ex = d->scenes_count ? d->scenes[0].extras.data : NULL;
        const char *k = ex ? strstr(ex, "\"xr_hand\"") : NULL;
        const char *colon = k ? strchr(k, ':') : NULL;
        while (colon && (*++colon == ' ' || *colon == '\t')) {
        }
        if (!colon || strncmp(colon, want, strlen(want)) != 0) {
            refuse(why, why_len, "SIDE", "scene extras do not say xr_hand %s",
                   want);
            goto done;
        }
    }

    if (d->skins_count != 1) {
        refuse(why, why_len, "JOINTS", "%u skins; exactly one is allowed",
               (unsigned)d->skins_count);
        goto done;
    }
    const cgltf_skin *skin = &d->skins[0];
    if (skin->joints_count != VR_HANDS_JOINTS) {
        refuse(why, why_len, "JOINTS", "%u skin joints; the contract has %d",
               (unsigned)skin->joints_count, VR_HANDS_JOINTS);
        goto done;
    }
    int map[VR_HANDS_JOINTS];
    int named[VR_HANDS_JOINTS];
    bool seen[VR_HANDS_JOINTS] = { false };
    for (int k = 0; k < VR_HANDS_JOINTS; k++) {
        int j = name_to_joint(skin->joints[k]->name);
        if (j < 0 || seen[j]) {
            refuse(why, why_len, "JOINTS", "skin joint %d is \"%s\": %s", k,
                   skin->joints[k]->name ? skin->joints[k]->name : "(null)",
                   j < 0 ? "not an XrHandJointEXT name" : "named twice");
            goto done;
        }
        seen[j] = true;
        named[k] = j;
        map[k] = j;
        if (sab == VR_GLTF_SAB_JOINTMAP && j >= XR_HAND_JOINT_INDEX_METACARPAL_EXT) {
            map[k] = XR_HAND_JOINT_INDEX_METACARPAL_EXT +
                     (j - XR_HAND_JOINT_INDEX_METACARPAL_EXT + 5) % 20;
        }
    }
    const cgltf_accessor *ibm = skin->inverse_bind_matrices;
    if (!ibm || ibm->type != cgltf_type_mat4 ||
        ibm->component_type != cgltf_component_type_r_32f ||
        ibm->count != VR_HANDS_JOINTS) {
        refuse(why, why_len, "BIND", "inverse bind matrices missing or not "
                                     "26 float MAT4");
        goto done;
    }
    float rest_pos[VR_HANDS_JOINTS][3];
    for (int k = 0; k < VR_HANDS_JOINTS; k++) {
        float m[16], rest[16];
        cgltf_accessor_read_float(ibm, (cgltf_size)k, m, 16);
        memcpy(out->ibm[map[k]], m, sizeof(m));
        cgltf_node_transform_world(skin->joints[k], rest);
        memcpy(rest_pos[named[k]], &rest[12], sizeof(rest_pos[0]));
        float dev = mat_mul_dev_identity(m, rest);
        if (dev > 1e-4f) {
            refuse(why, why_len, "BIND", "%s: IBM x rest transform differs "
                   "from the identity by %.2g", skin->joints[k]->name,
                   (double)dev);
            goto done;
        }
        if (named[k] == XR_HAND_JOINT_WRIST_EXT) {
            float off = 0.0f, rot = 0.0f;
            for (int c = 0; c < 3; c++) {
                off = fmaxf(off, fabsf(rest[12 + c]));
                for (int r = 0; r < 3; r++) {
                    rot = fmaxf(rot, fabsf(rest[c * 4 + r] - (r == c)));
                }
            }
            if (off > 1e-3f || rot > 0.035f) {
                refuse(why, why_len, "AXES", "the wrist's rest transform is "
                       "not the identity at the origin (%.2g m, %.2g)",
                       (double)off, (double)rot);
                goto done;
            }
        }
    }
    {
        const float *a = rest_pos[XR_HAND_JOINT_WRIST_EXT];
        const float *b = rest_pos[XR_HAND_JOINT_MIDDLE_TIP_EXT];
        out->wrist_to_middle_tip_m = sqrtf((b[0] - a[0]) * (b[0] - a[0]) +
                                           (b[1] - a[1]) * (b[1] - a[1]) +
                                           (b[2] - a[2]) * (b[2] - a[2]));
    }

    const cgltf_node *mnode = NULL;
    for (cgltf_size i = 0; i < d->nodes_count; i++) {
        if (d->nodes[i].mesh) {
            if (mnode) {
                refuse(why, why_len, "FORMAT", "more than one mesh node");
                goto done;
            }
            mnode = &d->nodes[i];
        }
    }
    if (d->meshes_count != 1 || !mnode || mnode->skin != skin) {
        refuse(why, why_len, "FORMAT", "%u meshes; exactly one, skinned by "
               "the hand's skin, is allowed", (unsigned)d->meshes_count);
        goto done;
    }
    const cgltf_mesh *mesh = &d->meshes[0];
    if (mesh->primitives_count != 1 ||
        mesh->primitives[0].type != cgltf_primitive_type_triangles ||
        !mesh->primitives[0].indices || mesh->primitives[0].targets_count) {
        refuse(why, why_len, "TRIS", "the mesh must be one indexed TRIANGLES "
                                     "primitive with no morph targets");
        goto done;
    }
    const cgltf_primitive *prim = &mesh->primitives[0];
    const cgltf_attribute *ap = find_attr(prim, cgltf_attribute_type_position, 0);
    if (!ap || !ap->data) {
        refuse(why, why_len, "FORMAT", "no POSITION");
        goto done;
    }
    const cgltf_size nv = ap->data->count;
    const cgltf_size ni = prim->indices->count;
    if (nv == 0 || nv > VR_GLTF_MAX_VERTS || ni % 3 || ni / 3 == 0 ||
        ni / 3 > VR_GLTF_MAX_TRIS) {
        refuse(why, why_len, "TRIS", "%u vertices, %u indices: the budget is "
               "%u vertices and 1..%u triangles", (unsigned)nv, (unsigned)ni,
               VR_GLTF_MAX_VERTS, VR_GLTF_MAX_TRIS);
        goto done;
    }
    const cgltf_attribute *an = find_attr(prim, cgltf_attribute_type_normal, 0);
    const cgltf_attribute *ac = find_attr(prim, cgltf_attribute_type_color, 0);
    const cgltf_attribute *aj = find_attr(prim, cgltf_attribute_type_joints, 0);
    const cgltf_attribute *aw = find_attr(prim, cgltf_attribute_type_weights, 0);
    if (!check_float_attr(ap, cgltf_type_vec3, nv) ||
        !check_float_attr(an, cgltf_type_vec3, nv)) {
        refuse(why, why_len, "FORMAT", "POSITION and NORMAL must be float VEC3");
        goto done;
    }
    if (!aj || !aw || !aj->data || !aw->data || aj->data->type != cgltf_type_vec4 ||
        aw->data->type != cgltf_type_vec4 || aj->data->count != nv ||
        aw->data->count != nv) {
        refuse(why, why_len, "WEIGHTS.COUNT", "JOINTS_0 and WEIGHTS_0 must be "
                                              "VEC4, one per vertex");
        goto done;
    }
    if (find_attr(prim, cgltf_attribute_type_joints, 1) ||
        find_attr(prim, cgltf_attribute_type_weights, 1)) {
        refuse(why, why_len, "WEIGHTS.COUNT", "JOINTS_1/WEIGHTS_1: at most 4 "
                                              "influences");
        goto done;
    }
    if (ac && (!ac->data || ac->data->count != nv ||
               (ac->data->type != cgltf_type_vec3 &&
                ac->data->type != cgltf_type_vec4))) {
        refuse(why, why_len, "MATERIAL", "COLOR_0 must be VEC3/VEC4 per vertex");
        goto done;
    }
    if (d->materials_count > 1) {
        refuse(why, why_len, "MATERIAL", "%u materials; at most one",
               (unsigned)d->materials_count);
        goto done;
    }
    float base[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (prim->material) {
        if (!prim->material->has_pbr_metallic_roughness) {
            refuse(why, why_len, "MATERIAL", "the material is not "
                                             "pbrMetallicRoughness");
            goto done;
        }
        memcpy(base, prim->material->pbr_metallic_roughness.base_color_factor,
               sizeof(base));
    }

    out->nv = (uint32_t)nv;
    out->ni = (uint32_t)ni;
    out->pos = malloc(nv * 3 * sizeof(float));
    out->nrm = malloc(nv * 3 * sizeof(float));
    out->col = malloc(nv * 3 * sizeof(float));
    out->jnt = malloc(nv * 4);
    out->wgt = malloc(nv * 4 * sizeof(float));
    out->idx = malloc(ni * sizeof(uint32_t));
    if (!out->pos || !out->nrm || !out->col || !out->jnt || !out->wgt ||
        !out->idx) {
        refuse(why, why_len, "FORMAT", "out of memory");
        goto done;
    }
    for (cgltf_size v = 0; v < nv; v++) {
        float c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        cgltf_accessor_read_float(ap->data, v, &out->pos[v * 3], 3);
        cgltf_accessor_read_float(an->data, v, &out->nrm[v * 3], 3);
        if (ac) {
            cgltf_accessor_read_float(ac->data, v, c,
                                      ac->data->type == cgltf_type_vec4 ? 4 : 3);
        }
        for (int i = 0; i < 3; i++) {
            out->col[v * 3 + i] = c[i] * base[i];
        }
        cgltf_uint jk[4];
        float w[4];
        if (!cgltf_accessor_read_uint(aj->data, v, jk, 4) ||
            !cgltf_accessor_read_float(aw->data, v, w, 4)) {
            refuse(why, why_len, "WEIGHTS.COUNT", "vertex %u: unreadable "
                   "joints or weights", (unsigned)v);
            goto done;
        }
        float sum = 0.0f;
        for (int i = 0; i < 4; i++) {
            if (w[i] < 0.0f || (jk[i] >= VR_HANDS_JOINTS &&
                                sab != VR_GLTF_SAB_LAXJOINTS)) {
                refuse(why, why_len, "WEIGHTS.COUNT", "vertex %u: influence "
                       "%d (joint %u, weight %g) is not a skin joint",
                       (unsigned)v, i, jk[i], (double)w[i]);
                goto done;
            }
            sum += w[i];
            if (jk[i] >= VR_HANDS_JOINTS) {
                w[i] = 0.0f;
                jk[i] = 0;
            }
            out->jnt[v * 4 + i] = (uint8_t)(w[i] > 0.0f ? map[jk[i]] : 0);
            out->wgt[v * 4 + i] = w[i] > 0.0f ? w[i] : 0.0f;
            if (w[i] > 0.0f) {
                out->weighted[map[jk[i]]] = true;
            }
        }
        if (sab != VR_GLTF_SAB_LAXWEIGHTS && fabsf(sum - 1.0f) > 1e-3f) {
            refuse(why, why_len, sum == 0.0f ? "WEIGHTS.UNWEIGHTED" :
                   "WEIGHTS.SUM", "vertex %u: weights sum to %g",
                   (unsigned)v, (double)sum);
            goto done;
        }
    }
    for (cgltf_size i = 0; i < ni; i++) {
        cgltf_size k = cgltf_accessor_read_index(prim->indices, i);
        if (k >= nv) {
            refuse(why, why_len, "TRIS", "index %u is %u, past %u vertices",
                   (unsigned)i, (unsigned)k, (unsigned)nv);
            goto done;
        }
        out->idx[i] = (uint32_t)k;
    }
    out->side = side;
    ok = true;

done:
    cgltf_free(d);
    if (!ok) {
        vr_hands_gltf_free(out);
    }
    return ok;
}

void vr_hands_gltf_free(VrGltfHand *h)
{
    free(h->pos);
    free(h->nrm);
    free(h->col);
    free(h->jnt);
    free(h->wgt);
    free(h->idx);
    memset(h, 0, sizeof(*h));
}

bool vr_hands_gltf_drawable(const VrGltfHand *g, const VrHandJoints *h,
                            VrGltfSabotage sab)
{
    if (!g->nv || !h->active) {
        return false;
    }
    if (sab == VR_GLTF_SAB_MISSING) {
        return true;
    }
    for (int j = 0; j < VR_HANDS_JOINTS; j++) {
        if (g->weighted[j] && !h->j[j].valid) {
            return false;
        }
    }
    return true;
}

void vr_hands_gltf_joint_matrix(const VrGltfHand *g, const XrPosef *pose,
                                int joint, VrGltfSabotage sab, float m[12])
{
    const XrQuaternionf q = pose->orientation;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    const float R[3][3] = {
        { 1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy) },
        { 2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx) },
        { 2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy) },
    };
    const float t[3] = { pose->position.x, pose->position.y, pose->position.z };
    if (sab == VR_GLTF_SAB_NOIBM) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                m[r * 4 + c] = R[r][c];
            }
            m[r * 4 + 3] = t[r];
        }
        return;
    }
    const float *b = g->ibm[joint];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            m[r * 4 + c] = R[r][0] * b[c * 4 + 0] + R[r][1] * b[c * 4 + 1] +
                           R[r][2] * b[c * 4 + 2] + t[r] * b[c * 4 + 3];
        }
    }
}

static int parent_of(int j)
{
    if (j == XR_HAND_JOINT_WRIST_EXT) {
        return -1;
    }
    if (j == XR_HAND_JOINT_PALM_EXT || j == XR_HAND_JOINT_THUMB_METACARPAL_EXT ||
        (j >= XR_HAND_JOINT_INDEX_METACARPAL_EXT &&
         (j - XR_HAND_JOINT_INDEX_METACARPAL_EXT) % 5 == 0)) {
        return XR_HAND_JOINT_WRIST_EXT;
    }
    return j - 1;
}

static XrPosef pose_mul(const XrPosef *a, const XrPosef *b)
{
    const XrQuaternionf p = a->orientation, q = b->orientation;
    XrPosef o;
    o.orientation = (XrQuaternionf){
        p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y,
        p.w * q.y - p.x * q.z + p.y * q.w + p.z * q.x,
        p.w * q.z + p.x * q.y - p.y * q.x + p.z * q.w,
        p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z,
    };
    const XrVector3f v = b->position;
    const float tx = 2.0f * (p.y * v.z - p.z * v.y);
    const float ty = 2.0f * (p.z * v.x - p.x * v.z);
    const float tz = 2.0f * (p.x * v.y - p.y * v.x);
    o.position = (XrVector3f){
        a->position.x + v.x + p.w * tx + (p.y * tz - p.z * ty),
        a->position.y + v.y + p.w * ty + (p.z * tx - p.x * tz),
        a->position.z + v.z + p.w * tz + (p.x * ty - p.y * tx),
    };
    return o;
}

uint32_t vr_hands_gltf_skin(const VrGltfHand *g, const VrHandJoints *h,
                            VrGltfSabotage sab, VrNfVertexC *out)
{
    if (!vr_hands_gltf_drawable(g, h, sab)) {
        return 0;
    }
    XrPosef composed[VR_HANDS_JOINTS];
    if (sab == VR_GLTF_SAB_PARENTREL) {
        for (int j = 0; j < VR_HANDS_JOINTS; j++) {
            composed[j] = h->j[j].pose;
            for (int a = parent_of(j); a >= 0; a = parent_of(a)) {
                composed[j] = pose_mul(&h->j[a].pose, &composed[j]);
            }
        }
    }
    float M[VR_HANDS_JOINTS][12];
    for (int j = 0; j < VR_HANDS_JOINTS; j++) {
        if (g->weighted[j]) {
            vr_hands_gltf_joint_matrix(g, sab == VR_GLTF_SAB_PARENTREL ?
                                       &composed[j] : &h->j[j].pose, j, sab, M[j]);
        } else {
            memset(M[j], 0, sizeof(M[j]));
        }
    }
    const int ninf = sab == VR_GLTF_SAB_ONEWEIGHT ? 1 : 4;
    for (uint32_t v = 0; v < g->nv; v++) {
        float B[12] = { 0 };
        const uint8_t *jk = &g->jnt[v * 4];
        const float *w = &g->wgt[v * 4];
        float wsum = 0.0f;
        for (int i = 0; i < ninf; i++) {
            if (w[i] > 0.0f) {
                const float *m = M[jk[i]];
                for (int e = 0; e < 12; e++) {
                    B[e] += w[i] * m[e];
                }
                wsum += w[i];
            }
        }
        if (ninf == 1 && wsum > 0.0f) {
            for (int e = 0; e < 12; e++) {
                B[e] /= wsum;
            }
        }
        const float *p = &g->pos[v * 3];
        const float *n = &g->nrm[v * 3];
        VrNfVertexC *o = &out[v];
        for (int r = 0; r < 3; r++) {
            o->p[r] = B[r * 4 + 0] * p[0] + B[r * 4 + 1] * p[1] +
                      B[r * 4 + 2] * p[2] + B[r * 4 + 3];
        }
        if (sab == VR_GLTF_SAB_NONORMAL) {
            memcpy(o->n, n, sizeof(o->n));
        } else {
            float nn[3];
            for (int r = 0; r < 3; r++) {
                nn[r] = B[r * 4 + 0] * n[0] + B[r * 4 + 1] * n[1] +
                        B[r * 4 + 2] * n[2];
            }
            float l = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            l = l > 1e-12f ? 1.0f / l : 0.0f;
            for (int r = 0; r < 3; r++) {
                o->n[r] = nn[r] * l;
            }
        }
        memcpy(o->c, &g->col[v * 3], sizeof(o->c));
    }
    return g->nv;
}
