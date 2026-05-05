#pragma once

#include <stdint.h>

/*
 * lib3d: minimal 3D math and software rasterizer for kernel framebuffer.
 * Uses float math; supports perspective projection, depth buffer, triangle drawing.
 */

typedef struct { float x, y, z; } vec3_t;
typedef struct { float m[4][4]; } mat4_t;

typedef void (*lib3d_putpixel_fn)(uint16_t x, uint16_t y, uint32_t argb, void* user);

/* Initialize 3D context. w,h = viewport size; putpixel/user = output callback.
 * Returns 0 on success, -1 on failure (e.g. depth buffer alloc). */
int lib3d_init(uint16_t w, uint16_t h, lib3d_putpixel_fn putpixel, void* user);

/* Clear color and depth buffer. */
void lib3d_clear(uint32_t argb);
void lib3d_clear_depth(void);

/* Set perspective projection. fov_y_rad in radians, aspect = w/h. */
void lib3d_set_projection(float fov_y_rad, float aspect, float near_z, float far_z);

/* Set model-view matrix (identity = no transform). */
void lib3d_set_modelview(const mat4_t* mv);

/* Draw triangle (vertices in clip space after MV*P, or use helpers below). */
void lib3d_draw_triangle(const vec3_t* v0, const vec3_t* v1, const vec3_t* v2, uint32_t color);

/* Draw indexed triangles. verts[] = positions, indices[] = triples (i0,i1,i2). */
void lib3d_draw_mesh(const vec3_t* verts, uint32_t nverts,
                     const uint16_t* indices, uint32_t nindices,
                     const mat4_t* model, uint32_t color);

/* Matrix/vector math */
void lib3d_mat4_identity(mat4_t* out);
void lib3d_mat4_mul(mat4_t* out, const mat4_t* a, const mat4_t* b);
void lib3d_mat4_translate(mat4_t* out, float tx, float ty, float tz);
void lib3d_mat4_rotate_y(mat4_t* out, float rad);
void lib3d_mat4_rotate_x(mat4_t* out, float rad);
void lib3d_mat4_scale(mat4_t* out, float sx, float sy, float sz);
void lib3d_mat4_perspective(mat4_t* out, float fov_y, float aspect, float near_z, float far_z);

void lib3d_vec3_transform(vec3_t* out, const vec3_t* v, const mat4_t* m);

/* ARGB helper */
static inline uint32_t lib3d_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
