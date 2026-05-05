/*
 * lib3d: 3D math and software rasterizer.
 */
#include "lib3d.h"
#include "mem.h"
#include "string.h"
#include <stdint.h>

#define LIB3D_MAX_WH 512

typedef struct {
    uint16_t w, h;
    lib3d_putpixel_fn putpixel;
    void* user;
    float* depth;
    mat4_t proj;
    mat4_t modelview;
    mat4_t mvp;
    int mvp_dirty;
} lib3d_ctx_t;

static lib3d_ctx_t g_ctx;
static int g_inited;

static void update_mvp(void)
{
    if (!g_ctx.mvp_dirty) return;
    lib3d_mat4_mul(&g_ctx.mvp, &g_ctx.proj, &g_ctx.modelview);
    g_ctx.mvp_dirty = 0;
}

int lib3d_init(uint16_t w, uint16_t h, lib3d_putpixel_fn putpixel, void* user)
{
    if (w == 0 || h == 0 || w > LIB3D_MAX_WH || h > LIB3D_MAX_WH || !putpixel)
        return -1;
    if (g_inited && g_ctx.depth) kfree(g_ctx.depth);
    g_ctx.w = w;
    g_ctx.h = h;
    g_ctx.putpixel = putpixel;
    g_ctx.user = user;
    g_ctx.depth = (float*)kmalloc((uint32_t)w * (uint32_t)h * sizeof(float));
    if (!g_ctx.depth) return -1;
    g_ctx.mvp_dirty = 1;
    lib3d_mat4_identity(&g_ctx.modelview);
    lib3d_set_projection(0.785398f, (float)w / (float)h, 0.1f, 100.f);
    g_inited = 1;
    return 0;
}

void lib3d_clear(uint32_t argb)
{
    if (!g_inited) return;
    for (uint16_t y = 0; y < g_ctx.h; y++)
        for (uint16_t x = 0; x < g_ctx.w; x++)
            g_ctx.putpixel(x, y, argb, g_ctx.user);
}

void lib3d_clear_depth(void)
{
    if (!g_inited || !g_ctx.depth) return;
    uint32_t n = (uint32_t)g_ctx.w * (uint32_t)g_ctx.h;
    for (uint32_t i = 0; i < n; i++) g_ctx.depth[i] = 1.f; /* far in NDC */
}

void lib3d_set_projection(float fov_y, float aspect, float near_z, float far_z)
{
    lib3d_mat4_perspective(&g_ctx.proj, fov_y, aspect, near_z, far_z);
    g_ctx.mvp_dirty = 1;
}

void lib3d_set_modelview(const mat4_t* mv)
{
    g_ctx.modelview = *mv;
    g_ctx.mvp_dirty = 1;
}

/* --- vec3/mat4 math --- */

void lib3d_mat4_identity(mat4_t* out)
{
    memset(out, 0, sizeof(mat4_t));
    out->m[0][0] = out->m[1][1] = out->m[2][2] = out->m[3][3] = 1.f;
}

void lib3d_mat4_mul(mat4_t* out, const mat4_t* a, const mat4_t* b)
{
    mat4_t tmp;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            tmp.m[i][j] = 0;
            for (int k = 0; k < 4; k++)
                tmp.m[i][j] += a->m[i][k] * b->m[k][j];
        }
    *out = tmp;
}

void lib3d_mat4_translate(mat4_t* out, float tx, float ty, float tz)
{
    lib3d_mat4_identity(out);
    out->m[3][0] = tx;
    out->m[3][1] = ty;
    out->m[3][2] = tz;
}

static void sincos_approx(float x, float* s, float* c)
{
    while (x > 3.14159265f) x -= 6.2831853f;
    while (x < -3.14159265f) x += 6.2831853f;
    float x2 = x * x;
    *c = 1.f - x2 / 2.f + x2 * x2 / 24.f;
    *s = x - x2 * x / 6.f + x2 * x2 * x / 120.f;
}

void lib3d_mat4_rotate_y(mat4_t* out, float rad)
{
    float c, s;
    sincos_approx(rad, &s, &c);
    lib3d_mat4_identity(out);
    out->m[0][0] = c;   out->m[0][2] = s;
    out->m[2][0] = -s;  out->m[2][2] = c;
}

void lib3d_mat4_rotate_x(mat4_t* out, float rad)
{
    float c, s;
    sincos_approx(rad, &s, &c);
    lib3d_mat4_identity(out);
    out->m[1][1] = c;   out->m[1][2] = -s;
    out->m[2][1] = s;   out->m[2][2] = c;
}

void lib3d_mat4_scale(mat4_t* out, float sx, float sy, float sz)
{
    lib3d_mat4_identity(out);
    out->m[0][0] = sx;
    out->m[1][1] = sy;
    out->m[2][2] = sz;
}

static float tan_approx(float x)
{
    if (x < 0) return -tan_approx(-x);
    if (x > 1.57f) return -tan_approx(3.14159f - x);
    float x2 = x * x;
    return x * (1.f + x2 / 3.f + 2.f * x2 * x2 / 15.f);
}

void lib3d_mat4_perspective(mat4_t* out, float fov_y, float aspect, float near_z, float far_z)
{
    float f = (fov_y > 0.001f) ? 1.f / tan_approx(fov_y * 0.5f) : 1000.f;
    float n = near_z, fz = far_z;
    memset(out, 0, sizeof(mat4_t));
    out->m[0][0] = f / aspect;
    out->m[1][1] = f;
    out->m[2][2] = (fz + n) / (n - fz);
    out->m[2][3] = -1.f;
    out->m[3][2] = (2.f * fz * n) / (n - fz);
}

void lib3d_vec3_transform(vec3_t* out, const vec3_t* v, const mat4_t* m)
{
    float x = v->x * m->m[0][0] + v->y * m->m[1][0] + v->z * m->m[2][0] + m->m[3][0];
    float y = v->x * m->m[0][1] + v->y * m->m[1][1] + v->z * m->m[2][1] + m->m[3][1];
    float z = v->x * m->m[0][2] + v->y * m->m[1][2] + v->z * m->m[2][2] + m->m[3][2];
    float w = v->x * m->m[0][3] + v->y * m->m[1][3] + v->z * m->m[2][3] + m->m[3][3];
    if (w < 0.0001f && w > -0.0001f) w = 0.0001f;
    out->x = x / w;
    out->y = y / w;
    out->z = z / w;
}

/* --- rasterizer --- */

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float fminf(float a, float b) { return a < b ? a : b; }
static inline float fmaxf(float a, float b) { return a > b ? a : b; }

static void draw_tri_internal(float x0, float y0, float z0,
                              float x1, float y1, float z1,
                              float x2, float y2, float z2,
                              uint32_t color)
{
    float minx = fminf(fminf(x0, x1), x2);
    float maxx = fmaxf(fmaxf(x0, x1), x2);
    float miny = fminf(fminf(y0, y1), y2);
    float maxy = fmaxf(fmaxf(y0, y1), y2);

    int ix0 = imax(0, (int)(minx + 0.5f));
    int ix1 = imin((int)g_ctx.w - 1, (int)(maxx + 0.5f));
    int iy0 = imax(0, (int)(miny + 0.5f));
    int iy1 = imin((int)g_ctx.h - 1, (int)(maxy + 0.5f));

    float area = (x1-x0)*(y2-y0) - (x2-x0)*(y1-y0);
    if (area >= -0.0001f && area <= 0.0001f) return;

    for (int iy = iy0; iy <= iy1; iy++) {
        for (int ix = ix0; ix <= ix1; ix++) {
            float px = (float)ix + 0.5f, py = (float)iy + 0.5f;
            float w0 = (x1-x0)*(py-y0) - (y1-y0)*(px-x0);
            float w1 = (x2-x1)*(py-y1) - (y2-y1)*(px-x1);
            float w2 = (x0-x2)*(py-y2) - (y0-y2)*(px-x2);
            if (area > 0) { if (w0 < 0 || w1 < 0 || w2 < 0) continue; }
            else          { if (w0 > 0 || w1 > 0 || w2 > 0) continue; }

            float denom = area;
            if (denom < 0) denom = -denom;
            float b0 = w0 / denom, b1 = w1 / denom, b2 = w2 / denom;
            if (area < 0) { b0 = -b0; b1 = -b1; b2 = -b2; }
            float z = b0*z0 + b1*z1 + b2*z2;

            uint32_t idx = (uint32_t)iy * (uint32_t)g_ctx.w + (uint32_t)ix;
            if (z >= g_ctx.depth[idx]) continue;
            g_ctx.depth[idx] = z;
            g_ctx.putpixel((uint16_t)ix, (uint16_t)iy, color, g_ctx.user);
        }
    }
}

void lib3d_draw_triangle(const vec3_t* v0, const vec3_t* v1, const vec3_t* v2, uint32_t color)
{
    if (!g_inited || !g_ctx.depth) return;
    update_mvp();

    vec3_t a, b, c;
    lib3d_vec3_transform(&a, v0, &g_ctx.mvp);
    lib3d_vec3_transform(&b, v1, &g_ctx.mvp);
    lib3d_vec3_transform(&c, v2, &g_ctx.mvp);

    if (a.z < -1.f && b.z < -1.f && c.z < -1.f) return;
    if (a.z > 1.f && b.z > 1.f && c.z > 1.f) return;

    float hw = (float)g_ctx.w * 0.5f, hh = (float)g_ctx.h * 0.5f;
    float x0 = a.x * hw + hw, y0 = -a.y * hh + hh;
    float x1 = b.x * hw + hw, y1 = -b.y * hh + hh;
    float x2 = c.x * hw + hw, y2 = -c.y * hh + hh;

    draw_tri_internal(x0, y0, a.z, x1, y1, b.z, x2, y2, c.z, color);
}

void lib3d_draw_mesh(const vec3_t* verts, uint32_t nverts,
                    const uint16_t* indices, uint32_t nindices,
                    const mat4_t* model, uint32_t color)
{
    if (!g_inited || !verts || !indices || nindices % 3 != 0) return;
    mat4_t prev = g_ctx.modelview;
    if (model) {
        mat4_t combined;
        lib3d_mat4_mul(&combined, &g_ctx.modelview, model);
        g_ctx.modelview = combined;
        g_ctx.mvp_dirty = 1;
    }
    for (uint32_t i = 0; i + 2 < nindices; i += 3) {
        uint16_t i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
        if (i0 >= nverts || i1 >= nverts || i2 >= nverts) continue;
        lib3d_draw_triangle(&verts[i0], &verts[i1], &verts[i2], color);
    }
    if (model) {
        g_ctx.modelview = prev;
        g_ctx.mvp_dirty = 1;
    }
}
