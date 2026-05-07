/*
 * LARSH - Lard Animation/Rich Shell
 * 운영체제 전용 Flash+Entry 스타일 포맷
 */
#include "larsh.h"
#include "lard_doc.h"
#include "string.h"
#include <stddef.h>

#define SKIP(p) do { while (*(p) == ' ' || *(p) == '\t') (p)++; } while (0)

static int parse_int(const char* p, int32_t* out, const char** endp)
{
    SKIP(p);
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        uint32_t v = 0;
        while (*p) {
            if (*p >= '0' && *p <= '9') v = (v << 4) + (*p - '0');
            else if (*p >= 'a' && *p <= 'f') v = (v << 4) + (*p - 'a' + 10);
            else if (*p >= 'A' && *p <= 'F') v = (v << 4) + (*p - 'A' + 10);
            else break;
            p++;
        }
        *out = neg ? -(int32_t)v : (int32_t)v;
        *endp = p;
        return 0;
    }
    if (*p < '0' || *p > '9') return -1;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -(int32_t)v : (int32_t)v;
    *endp = p;
    return 0;
}

static void putpixel(uint32_t* pixels, uint16_t bw, uint16_t bh, int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= bw || y >= bh) return;
    pixels[y * bw + x] = c;
}

static void fill_rect(uint32_t* pixels, uint16_t bw, uint16_t bh, int x, int y, int w, int h, uint32_t c)
{
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            putpixel(pixels, bw, bh, x + dx, y + dy, c);
}

static void fill_circle(uint32_t* pixels, uint16_t bw, uint16_t bh, int cx, int cy, int r, uint32_t c)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                putpixel(pixels, bw, bh, cx + dx, cy + dy, c);
}

static void draw_line(uint32_t* pixels, uint16_t bw, uint16_t bh, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = (dx >= 0 ? dx : -dx) > (dy >= 0 ? dy : -dy) ? (dx >= 0 ? dx : -dx) : (dy >= 0 ? dy : -dy);
    if (steps == 0) {
        putpixel(pixels, bw, bh, x0, y0, c);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (dx * i) / steps;
        int y = y0 + (dy * i) / steps;
        putpixel(pixels, bw, bh, x, y, c);
    }
}

int larsh_parse(const char* src, uint32_t len, larsh_scene_t* out)
{
    if (!src || !out || len == 0) return -1;
    memset(out, 0, sizeof(larsh_scene_t));
    out->w = 256;
    out->h = 192;
    out->fps = 12;
    out->bg = 0xFF1a1a2e;

    const char* p = src;
    const char* end = src + len;

    while (p < end) {
        SKIP(p);
        if (p >= end || *p == '\0') break;
        if (*p == ';' || *p == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        if (*p == '\n') { p++; continue; }

        if ((p[0] == 'L' || p[0] == 'l') && (p[1] == 'A' || p[1] == 'a') && (p[2] == 'R' || p[2] == 'r') &&
            (p[3] == 'S' || p[3] == 's') && (p[4] == 'H' || p[4] == 'h')) {
            p += 5;
            int32_t ver;
            const char* ep;
            if (parse_int(p, &ver, &ep) == 0 && ver == 1) p = ep;
            continue;
        }
        if ((p[0] == 'w' || p[0] == 'W') && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            int32_t v;
            const char* ep;
            if (parse_int(p, &v, &ep) == 0 && v > 0 && v <= 512) { out->w = (uint16_t)v; p = ep; }
            continue;
        }
        if ((p[0] == 'h' || p[0] == 'H') && (p[1] == ' ' || p[1] == '\t') && p != src && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n')) {
            p += 2;
            int32_t v;
            const char* ep;
            if (parse_int(p, &v, &ep) == 0 && v > 0 && v <= 512) { out->h = (uint16_t)v; p = ep; }
            continue;
        }
        if ((p[0] == 'f' || p[0] == 'F') && (p[1] == 'p' || p[1] == 'P') && (p[2] == 's' || p[2] == 'S')) {
            p += 3;
            int32_t v;
            const char* ep;
            if (parse_int(p, &v, &ep) == 0 && v > 0 && v <= 60) { out->fps = (uint8_t)v; p = ep; }
            continue;
        }
        if ((p[0] == 'b' || p[0] == 'B') && (p[1] == 'g' || p[1] == 'G') && (p[2] == ' ' || p[2] == '\t')) {
            p += 3;
            int32_t v;
            const char* ep;
            if (parse_int(p, &v, &ep) == 0) { out->bg = (uint32_t)v | 0xFF000000; p = ep; }
            continue;
        }
        if ((p[0] == 'l' || p[0] == 'L') && (p[1] == 'o' || p[1] == 'O') && (p[2] == 'o' || p[2] == 'O') && (p[3] == 'p' || p[3] == 'P')) {
            out->loop = 1;
            p += 4;
            continue;
        }
        if ((p[0] == 'o' || p[0] == 'O') && (p[1] == 'b' || p[1] == 'B') && (p[2] == 'j' || p[2] == 'J') && (p[3] == ' ' || p[3] == '\t')) {
            p += 4;
            if (out->n_obj >= LARSH_MAX_OBJ) { while (p < end && *p != '\n') p++; continue; }
            int32_t id;
            const char* ep;
            if (parse_int(p, &id, &ep) != 0 || id < 0 || id >= LARSH_MAX_OBJ) { while (p < end && *p != '\n') p++; continue; }
            p = ep;
            SKIP(p);
            uint8_t ty = 0;
            if ((p[0] == 'r' || p[0] == 'R') && (p[1] == 'e' || p[1] == 'E') && (p[2] == 'c' || p[2] == 'C') && (p[3] == 't' || p[3] == 'T')) { ty = 0; p += 4; }
            else if ((p[0] == 'c' || p[0] == 'C') && (p[1] == 'i' || p[1] == 'I') && (p[2] == 'r' || p[2] == 'R') && (p[3] == 'c' || p[3] == 'C') && (p[4] == 'l' || p[4] == 'L') && (p[5] == 'e' || p[5] == 'E')) { ty = 1; p += 6; }
            else if ((p[0] == 'l' || p[0] == 'L') && (p[1] == 'i' || p[1] == 'I') && (p[2] == 'n' || p[2] == 'N') && (p[3] == 'e' || p[3] == 'E')) { ty = 2; p += 4; }
            else if ((p[0] == 't' || p[0] == 'T') && (p[1] == 'e' || p[1] == 'E') && (p[2] == 'x' || p[2] == 'X') && (p[3] == 't' || p[3] == 'T')) { ty = 3; p += 4; }
            else if ((p[0] == 'l' || p[0] == 'L') && (p[1] == 'a' || p[1] == 'A') && (p[2] == 'r' || p[2] == 'R') &&
                     (p[3] == 'd' || p[3] == 'D') && (p[4] == 'd' || p[4] == 'D')) { ty = 4; p += 5; }
            else { while (p < end && *p != '\n') p++; continue; }
            larsh_obj_t* o = &out->obj[out->n_obj];
            o->type = ty;
            int32_t x, y, w, h, col;
            if (parse_int(p, &x, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
            p = ep;
            if (parse_int(p, &y, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
            p = ep;
            o->x = (int16_t)x;
            o->y = (int16_t)y;
            if (ty == 1) {
                if (parse_int(p, &w, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
                p = ep;
                o->w = (int16_t)(w > 0 ? w : 10);
                if (parse_int(p, &col, &ep) == 0) { o->color = (uint32_t)col | 0xFF000000; p = ep; }
            } else if (ty == 3) {
                SKIP(p);
                if (*p == '"' || *p == '\'') {
                    char q = *p++;
                    uint32_t ti = 0;
                    while (*p && *p != q && ti < LARSH_MAX_TEXT - 1) o->text[ti++] = *p++;
                    o->text[ti] = '\0';
                    if (*p == q) p++;
                }
                SKIP(p);
                if (parse_int(p, &col, &ep) == 0) { o->color = (uint32_t)col | 0xFF000000; p = ep; }
            } else if (ty == 4) {
                if (parse_int(p, &w, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
                p = ep;
                if (parse_int(p, &h, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
                p = ep;
                o->w = (int16_t)w;
                o->h = (int16_t)h;
                if (parse_int(p, &col, &ep) == 0) { o->color = (uint32_t)col | 0xFF000000; p = ep; }
                SKIP(p);
                if (*p == '"' || *p == '\'') {
                    char q = *p++;
                    uint32_t ti = 0;
                    while (p < end && *p != q && ti < LARSH_MAX_LARDD - 1) {
                        o->lardd[ti++] = *p++;
                    }
                    o->lardd[ti] = '\0';
                    if (p < end && *p == q) p++;
                }
            } else {
                if (parse_int(p, &w, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
                p = ep;
                if (parse_int(p, &h, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
                p = ep;
                o->w = (int16_t)w;
                o->h = (int16_t)h;
                if (parse_int(p, &col, &ep) == 0) { o->color = (uint32_t)col | 0xFF000000; p = ep; }
            }
            out->n_obj++;
            continue;
        }
        if ((p[0] == 'k' || p[0] == 'K') && (p[1] == 'e' || p[1] == 'E') && (p[2] == 'y' || p[2] == 'Y') && (p[3] == ' ' || p[3] == '\t')) {
            p += 4;
            if (out->n_key >= LARSH_MAX_KEY) { while (p < end && *p != '\n') p++; continue; }
            int32_t frame, id, val;
            const char* ep;
            if (parse_int(p, &frame, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
            p = ep;
            if (parse_int(p, &id, &ep) != 0 || id < 0 || id >= LARSH_MAX_OBJ) { while (p < end && *p != '\n') p++; continue; }
            p = ep;
            SKIP(p);
            uint8_t prop = 0;
            if ((p[0] == 'x' || p[0] == 'X') && (p[1] == ' ' || p[1] == '\t' || p[1] == '\0')) { prop = 0; p++; }
            else if ((p[0] == 'y' || p[0] == 'Y') && (p[1] == ' ' || p[1] == '\t' || p[1] == '\0')) { prop = 1; p++; }
            else if ((p[0] == 'w' || p[0] == 'W') && (p[1] == ' ' || p[1] == '\t' || p[1] == '\0')) { prop = 2; p++; }
            else if ((p[0] == 'h' || p[0] == 'H') && (p[1] == ' ' || p[1] == '\t' || p[1] == '\0')) { prop = 3; p++; }
            else if ((p[0] == 'c' || p[0] == 'C') && (p[1] == 'o' || p[1] == 'O') && (p[2] == 'l' || p[2] == 'L') && (p[3] == 'o' || p[3] == 'O') && (p[4] == 'r' || p[4] == 'R')) { prop = 4; p += 5; }
            SKIP(p);
            if (parse_int(p, &val, &ep) != 0) { while (p < end && *p != '\n') p++; continue; }
            p = ep;
            larsh_key_t* k = &out->key[out->n_key];
            k->frame = (uint32_t)(frame >= 0 ? frame : 0);
            k->obj_id = (uint8_t)id;
            k->prop = prop;
            k->value = val;
            out->n_key++;
            continue;
        }
        while (p < end && *p != '\n') p++;
    }
    return 0;
}

static uint32_t lerp_key(const larsh_scene_t* s, uint8_t obj_id, uint8_t prop, uint32_t frame, int32_t def)
{
    int32_t v0 = def;
    uint32_t t0 = 0;
    int32_t v1 = def;
    uint32_t t1 = 0x7FFFFFFF;
    for (uint32_t i = 0; i < s->n_key; i++) {
        if (s->key[i].obj_id != obj_id || s->key[i].prop != prop) continue;
        uint32_t kf = s->key[i].frame;
        if (kf <= frame) {
            v0 = s->key[i].value;
            t0 = kf;
        }
        if (kf >= frame && kf < t1) {
            v1 = s->key[i].value;
            t1 = kf;
        }
    }
    if (t1 == t0 || t1 == 0x7FFFFFFF) return (uint32_t)(v1 & 0xFFFFFFFF);
    int32_t num = (int32_t)frame - (int32_t)t0;
    int32_t den = (int32_t)t1 - (int32_t)t0;
    int32_t v = v0 + (int32_t)((int64_t)(v1 - v0) * num / den);
    return (uint32_t)(v & 0xFFFFFFFF);
}

void larsh_render_frame(const larsh_scene_t* s, uint32_t tick, uint32_t* pixels, uint16_t buf_w, uint16_t buf_h)
{
    if (!s || !pixels) return;
    uint32_t max_frame = 0;
    for (uint32_t i = 0; i < s->n_key; i++) {
        if (s->key[i].frame > max_frame) max_frame = s->key[i].frame;
    }
    if (max_frame == 0) max_frame = 1;
    uint32_t frame = tick % (max_frame + 1);
    if (s->loop == 0 && tick > max_frame) frame = max_frame;

    for (uint32_t i = 0; i < (uint32_t)buf_w * buf_h; i++) pixels[i] = s->bg;

    int scale_x = (int)buf_w / (int)s->w;
    int scale_y = (int)buf_h / (int)s->h;
    if (scale_x < 1) scale_x = 1;
    if (scale_y < 1) scale_y = 1;

    for (uint8_t i = 0; i < s->n_obj; i++) {
        const larsh_obj_t* o = &s->obj[i];
        int x = (int)lerp_key(s, i, 0, frame, o->x);
        int y = (int)lerp_key(s, i, 1, frame, o->y);
        int w = (int)lerp_key(s, i, 2, frame, o->w);
        int h = (int)lerp_key(s, i, 3, frame, o->h);
        uint32_t c = (uint32_t)lerp_key(s, i, 4, frame, (int32_t)(o->color & 0xFFFFFF)) | 0xFF000000;

        int sx = x * scale_x;
        int sy = y * scale_y;
        int sw = w * scale_x;
        int sh = h * scale_y;
        if (o->type == 1) {
            int r = (w > 0 ? w : 10) * scale_x;
            if (r > sw) r = sw;
            fill_circle(pixels, buf_w, buf_h, sx + r, sy + r, r, c);
        } else if (o->type == 2) {
            int x1 = sx + sw;
            int y1 = sy + sh;
            draw_line(pixels, buf_w, buf_h, sx, sy, x1, y1, c);
        } else if (o->type == 4) {
            /* LARDD document block rendered with the same native document parser used by Doc. */
            static const uint8_t font8[96][8] = {
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0x02,0x04,0x08,0x10,0x20,0x40,0,0},
                {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0},{0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0},
                {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0},{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0},
                {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0},{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0},
                {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0},{0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0},
                {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0},{0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0},
                {0,0x18,0x18,0,0,0x18,0x18,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0},{0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0},
                {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0},{0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0},
                {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0},{0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0},
                {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0},{0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0},
                {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0},{0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0},
                {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0},{0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0},
                {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0},{0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0},
                {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0},{0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0},
                {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0},{0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0},
                {0x3C,0x66,0x30,0x18,0x0C,0x66,0x3C,0},{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0},
                {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0},{0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0},
                {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0},{0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0},
                {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0},{0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0},
            };
            int doc_y = sy;
            int line_h = 10;
            int col_w = 8;
            int max_w = sw > 0 ? sw / col_w : 40;
            char rendered[512];
            const char* ln = o->lardd;
            uint32_t doc_len = 0;
            while (o->lardd[doc_len]) doc_len++;
            if (lard_doc_to_text(o->lardd, doc_len, rendered, sizeof(rendered)) == 0) ln = rendered;
            while (*ln && doc_y + line_h <= sy + sh && doc_y < (int)buf_h) {
                while (*ln == ' ' || *ln == '\t') ln++;
                if (!*ln || *ln == '\n') { ln += (*ln ? 1 : 0); continue; }
                int scale = 1, bullet = 0;
                if (ln[0] == '#' && ln[1] == ' ') { scale = 2; ln += 2; }
                else if (ln[0] == '#' && ln[1] == '#' && ln[2] == ' ') { scale = 2; ln += 3; }
                else if (ln[0] == '#' && ln[1] == '#' && ln[2] == '#' && ln[3] == ' ') { scale = 1; ln += 4; }
                else if (ln[0] == '-' && (ln[1] == ' ' || ln[1] == '\t')) { bullet = 1; ln += 2; }
                int lx = sx;
                if (bullet) {
                    for (int by = 2; by < 6; by++)
                        for (int bx = 2; bx < 6; bx++)
                            putpixel(pixels, buf_w, buf_h, lx + bx, doc_y + by, c);
                    lx += col_w;
                }
                int cx = 0;
                while (*ln && *ln != '\n' && cx < max_w) {
                    int bold = 0;
                    if (ln[0] == '*' && ln[1] == '*' && ln[2] != '*') {
                        bold = 1;
                        ln += 2;
                    }
                    unsigned char ch = (unsigned char)*ln;
                    if (ch == '*') {
                        if (ln[1] == '*' && ln[2] != '*') { ln += 2; bold = 0; continue; }
                        ln++; continue;
                    }
                    if (ch == '`') {
                        ln++;
                        while (*ln && *ln != '`' && cx < max_w) {
                            ch = (unsigned char)*ln++;
                            if (ch >= 'a' && ch <= 'z') ch -= 32;
                            uint8_t rowbits[8] = {0,0,0,0,0,0,0,0};
                            if (ch >= 32 && ch < 128) {
                                int idx = ch - 32;
                                if (idx < 64) for (int r = 0; r < 8; r++) rowbits[r] = font8[idx][r];
                            }
                            uint32_t cc = (c & 0xFF000000) | (((c >> 16) & 0xFF) * 3 / 4) | (((c >> 8) & 0xFF) * 3 / 4) | ((c & 0xFF) * 3 / 4);
                            for (int row = 0; row < 8; row++)
                                for (int col = 0; col < 8; col++)
                                    if (rowbits[row] & (1 << (7 - col))) {
                                        int px = lx + (int)cx * col_w + col;
                                        int py = doc_y + row;
                                        putpixel(pixels, buf_w, buf_h, px, py, cc);
                                    }
                            cx++;
                        }
                        if (*ln == '`') ln++;
                        continue;
                    }
                    if (!ch || ch == '\n') break;
                    ln++;
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    uint8_t rowbits[8] = {0,0,0,0,0,0,0,0};
                    if (ch >= 32 && ch < 128) {
                        int idx = ch - 32;
                        if (idx < 64) for (int r = 0; r < 8; r++) rowbits[r] = font8[idx][r];
                    }
                    for (int row = 0; row < 8; row++)
                        for (int col = 0; col < 8; col++)
                            if (rowbits[row] & (1 << (7 - col))) {
                                int px = lx + (int)cx * col_w + col;
                                int py = doc_y + row;
                                if (scale == 2) {
                                    putpixel(pixels, buf_w, buf_h, px * 2, py * 2, c);
                                    putpixel(pixels, buf_w, buf_h, px * 2 + 1, py * 2, c);
                                    putpixel(pixels, buf_w, buf_h, px * 2, py * 2 + 1, c);
                                    putpixel(pixels, buf_w, buf_h, px * 2 + 1, py * 2 + 1, c);
                                } else {
                                    putpixel(pixels, buf_w, buf_h, px, py, c);
                                    if (bold) putpixel(pixels, buf_w, buf_h, px + 1, py, c);
                                }
                            }
                    cx++;
                }
                while (*ln && *ln != '\n') ln++;
                if (*ln == '\n') ln++;
                doc_y += (scale == 2 ? 16 : line_h);
            }
        } else if (o->type == 3) {
            static const uint8_t font8[96][8] = {
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0x02,0x04,0x08,0x10,0x20,0x40,0,0},
                {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0},{0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0},
                {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0},{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0},
                {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0},{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0},
                {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0},{0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0},
                {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0},{0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0},
                {0,0x18,0x18,0,0,0x18,0x18,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
                {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0},{0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0},
                {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0},{0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0},
                {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0},{0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0},
                {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0},{0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0},
                {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0},{0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0},
                {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0},{0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0},
                {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0},{0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0},
                {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0},{0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0},
                {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0},{0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0},
                {0x3C,0x66,0x30,0x18,0x0C,0x66,0x3C,0},{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0},
                {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0},{0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0},
                {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0},{0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0},
                {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0},{0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0},
            };
            for (uint32_t ci = 0; o->text[ci] && ci < 32; ci++) {
                int tx = sx + (int)ci * 8;
                if (tx + 8 > (int)buf_w) break;
                unsigned char ch = (unsigned char)o->text[ci];
                if (ch >= 'a' && ch <= 'z') ch -= 32;
                uint8_t rowbits[8] = {0,0,0,0,0,0,0,0};
                if (ch >= 32 && ch < 128) {
                    int idx = ch - 32;
                    if (idx < 64)
                        for (int r = 0; r < 8; r++) rowbits[r] = font8[idx][r];
                }
                for (int row = 0; row < 8; row++)
                    for (int col = 0; col < 8; col++)
                        if (rowbits[row] & (1 << (7 - col)))
                            putpixel(pixels, buf_w, buf_h, tx + col, sy + row, c);
            }
        } else {
            fill_rect(pixels, buf_w, buf_h, sx, sy, sw > 0 ? sw : 1, sh > 0 ? sh : 1, c);
        }
    }
}
