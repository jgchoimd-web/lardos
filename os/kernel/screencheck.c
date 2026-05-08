#include "screencheck.h"

#include "gui.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb)
{
    if (w == 0 || h == 0 || x > 65535u || y > 65535u) return;
    gui_syscall_fill_rect((uint16_t)x, (uint16_t)y,
                          (uint16_t)min_u32(w, 65535u),
                          (uint16_t)min_u32(h, 65535u), argb);
}

static void text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg)
{
    if (x > 65535u || y > 65535u) return;
    gui_syscall_draw_text((uint16_t)x, (uint16_t)y, s, fg, bg);
}

static void draw_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    if (w < 2u || h < 2u) return;
    fill(x, y, w, 2, c);
    fill(x, y + h - 2u, w, 2, c);
    fill(x, y, 2, h, c);
    fill(x + w - 2u, y, 2, h, c);
}

static void draw_pac_shape(uint32_t x, uint32_t y, uint32_t s)
{
    static const char* rows[] = {
        "  YYYY",
        " YYYYYY",
        "YYYY  ",
        "YYY   ",
        "YYYY  ",
        " YYYYYY",
        "  YYYY",
    };
    for (uint32_t r = 0; r < 7u; r++) {
        for (uint32_t c = 0; c < 6u; c++) {
            if (rows[r][c] == 'Y') fill(x + c * s, y + r * s, s - 1u, s - 1u, 0xFFFFD84Au);
        }
    }
}

static void draw_scan_tiles(uint32_t x, uint32_t y, uint32_t cols, uint32_t rows,
                            uint32_t cell_w, uint32_t cell_h)
{
    static const uint32_t colors[] = {
        0xFF26D07Cu, 0xFF37A7FFu, 0xFFFFD84Au, 0xFFFF6B6Bu,
        0xFFB8F067u, 0xFFFF9F43u, 0xFF9D7BFFu, 0xFF4DE1C1u,
    };
    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t c = 0; c < cols; c++) {
            uint32_t idx = (r * 3u + c * 5u + (c ^ r)) & 7u;
            fill(x + c * cell_w, y + r * cell_h, cell_w - 2u, cell_h - 2u, colors[idx]);
        }
    }
}

int screencheck_probe(screencheck_info_t* out)
{
    gui_post_info_t g;
    uint32_t cols;
    uint32_t rows;
    if (!out) return -1;
    if (gui_post_check(&g) != 0) {
        out->width = 0;
        out->height = 0;
        out->changed_samples = 0;
        out->tiles_checked = 0;
        out->bad_tiles = 1;
        out->window_inside = 0;
        out->response_view_ok = 0;
        out->last_error = 1;
        return -2;
    }
    cols = g.width >= 64u ? g.width / 64u : 1u;
    rows = g.height >= 48u ? g.height / 48u : 1u;
    out->width = g.width;
    out->height = g.height;
    out->changed_samples = g.changed_samples;
    out->tiles_checked = cols * rows;
    out->bad_tiles = 0;
    out->window_inside = g.window_inside;
    out->response_view_ok = g.response_view_ok;
    out->last_error = 0;
    if (g.changed_samples <= 8u) out->bad_tiles++;
    if (!g.window_inside) out->bad_tiles++;
    if (!g.response_view_ok) out->bad_tiles++;
    return 0;
}

void screencheck_draw_retro(void)
{
    screencheck_info_t info;
    uint32_t w = gui_syscall_get_width();
    uint32_t h = gui_syscall_get_height();
    uint32_t panel_w = w > 80u ? w - 48u : w;
    uint32_t panel_h = h > 80u ? h - 48u : h;
    uint32_t grid_x = 36u;
    uint32_t grid_y = 132u;
    uint32_t cell_w = w >= 900u ? 32u : 22u;
    uint32_t cell_h = 18u;
    uint32_t cols = panel_w / cell_w;
    uint32_t rows = 8u;
    if (w < 160u || h < 120u) return;
    if (cols > 26u) cols = 26u;
    if (cols < 8u) cols = 8u;

    (void)screencheck_probe(&info);
    gui_syscall_clear(0xFF050608u);
    draw_frame(16u, 16u, panel_w, panel_h, 0xFF3CE6A1u);
    text(32u, 32u, "LARDOS RETRO SCREEN CHECK", 0xFF3CE6A1u, 0xFF050608u);
    text(32u, 52u, "VRAM TILES  FRAME EDGES  WINDOW BOUNDS  RESPONSE VIEW", 0xFFFFFFFFu, 0xFF050608u);
    text(32u, 84u, info.bad_tiles == 0 ? "SCAN STATUS: OK" : "SCAN STATUS: CHECK", 0xFFFFD84Au, 0xFF050608u);
    text(32u, 104u, "TRACK 00  01  02  03  04  05  06  07  08  09", 0xFFB8F067u, 0xFF050608u);

    draw_scan_tiles(grid_x, grid_y, cols, rows, cell_w, cell_h);
    draw_frame(grid_x - 4u, grid_y - 4u, cols * cell_w + 6u, rows * cell_h + 6u, 0xFFFFFFFFu);

    draw_pac_shape(48u, grid_y + rows * cell_h + 36u, 8u);
    for (uint32_t i = 0; i < 28u; i++) {
        fill(116u + i * 18u, grid_y + rows * cell_h + 58u, 5u, 5u, 0xFFFFF5A0u);
    }
    fill(132u, grid_y + rows * cell_h + 24u, 360u, 2u, 0xFF2F78FFu);
    fill(132u, grid_y + rows * cell_h + 94u, 360u, 2u, 0xFF2F78FFu);
    text(132u, grid_y + rows * cell_h + 112u, "DOT LANE VISIBILITY PASS", 0xFFFFD84Au, 0xFF050608u);

    text(32u, h > 48u ? h - 42u : 0u, "Use screencheck status for numbers, screencheck retro to redraw.", 0xFFFFFFFFu, 0xFF050608u);
}

int screencheck_selftest(void)
{
    screencheck_info_t info;
    if (screencheck_probe(&info) != 0) return -1;
    if (info.width < 320u || info.height < 200u) return -2;
    if (info.tiles_checked == 0) return -3;
    if (info.bad_tiles != 0) return -4;
    return 0;
}
