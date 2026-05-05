/*
 * lib3d demo: wireframe cube rendered in a small viewport.
 */
#include "lib3d.h"
#include "gui.h"
#include <stdint.h>

#define DEMO_W 160
#define DEMO_H 120

static uint16_t g_demo_ox, g_demo_oy;

static void demo_putpixel(uint16_t x, uint16_t y, uint32_t argb, void* user)
{
    (void)user;
    uint16_t gx = (uint16_t)(g_demo_ox + x);
    uint16_t gy = (uint16_t)(g_demo_oy + y);
    gui_syscall_put_pixel(gx, gy, argb);
}

/* Cube vertices (centered at origin, size 1) */
static const vec3_t cube_verts[] = {
    {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f,  0.5f}, {0.5f, -0.5f,  0.5f}, {0.5f, 0.5f,  0.5f}, {-0.5f, 0.5f,  0.5f},
};
static const uint16_t cube_indices[] = {
    0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1,
    1,5,6, 1,6,2, 2,6,7, 2,7,3, 3,7,4, 3,4,0
};

void lib3d_demo_render(uint16_t view_x, uint16_t view_y, float angle_y)
{
    uint16_t fw = gui_syscall_get_width();
    uint16_t fh = gui_syscall_get_height();
    if (fw < DEMO_W + 4 || fh < DEMO_H + 4) return;

    g_demo_ox = view_x;
    g_demo_oy = view_y;

    if (lib3d_init(DEMO_W, DEMO_H, demo_putpixel, 0) != 0) return;

    lib3d_clear(0xFF0A0A18);
    lib3d_clear_depth();

    mat4_t view, rot, model;
    lib3d_mat4_translate(&view, 0.f, 0.f, -2.5f);
    lib3d_mat4_rotate_y(&rot, angle_y);
    lib3d_mat4_mul(&model, &view, &rot);
    lib3d_set_modelview(&model);

    uint32_t color = lib3d_argb(255, 64, 128, 255);
    lib3d_draw_mesh(cube_verts, 8, cube_indices, 36, 0, color);
}
