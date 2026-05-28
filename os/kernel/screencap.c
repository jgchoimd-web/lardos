#include "screencap.h"
#include "fs.h"
#include "gui.h"
#include "string.h"
#include <stdio.h>

#define LSHOT_HEADER_SIZE 32u
#define LREC_HEADER_SIZE 32u
#define LSHOT_DEFAULT_W 96u
#define LSHOT_DEFAULT_H 54u
#define LREC_DEFAULT_W 64u
#define LREC_DEFAULT_H 36u
#define LREC_DEFAULT_FRAMES 8u
#define LREC_MAX_FRAMES 24u
#define LSHOT_FORMAT_RGB565 1u
#define LREC_FORMAT_LUMA8 1u

static uint32_t s_shot_count;
static uint32_t s_last_shot_width;
static uint32_t s_last_shot_height;
static uint32_t s_last_shot_bytes;
static int32_t s_last_error;
static char s_last_file[32];

static uint32_t s_rec_active;
static char s_rec_file[32];
static uint32_t s_rec_width;
static uint32_t s_rec_height;
static uint32_t s_rec_frames;
static uint32_t s_rec_max_frames;
static uint32_t s_rec_bytes;
static uint32_t s_rec_src_w;
static uint32_t s_rec_src_h;

static void sc_copy_name(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void put16(uint8_t* p, uint32_t off, uint32_t v)
{
    p[off] = (uint8_t)(v & 0xFFu);
    p[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put32(uint8_t* p, uint32_t off, uint32_t v)
{
    p[off] = (uint8_t)(v & 0xFFu);
    p[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    p[off + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    p[off + 3u] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t rgb565(uint32_t p)
{
    uint32_t r = (p >> 16) & 0xFFu;
    uint32_t g = (p >> 8) & 0xFFu;
    uint32_t b = p & 0xFFu;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint8_t luma8(uint32_t p)
{
    uint32_t r = (p >> 16) & 0xFFu;
    uint32_t g = (p >> 8) & 0xFFu;
    uint32_t b = p & 0xFFu;
    return (uint8_t)((r * 30u + g * 59u + b * 11u) / 100u);
}

static uint32_t sample_x(uint32_t x, uint32_t out_w, uint32_t src_w)
{
    uint32_t sx;
    if (out_w == 0 || src_w == 0) return 0;
    sx = (x * src_w + out_w / 2u) / out_w;
    return sx >= src_w ? src_w - 1u : sx;
}

static uint32_t sample_y(uint32_t y, uint32_t out_h, uint32_t src_h)
{
    uint32_t sy;
    if (out_h == 0 || src_h == 0) return 0;
    sy = (y * src_h + out_h / 2u) / out_h;
    return sy >= src_h ? src_h - 1u : sy;
}

static FsWritableFile* open_capture_file(const char* file, const char* fallback)
{
    char name[32];
    sc_copy_name(name, sizeof(name), file && file[0] ? file : fallback);
    return fs_open_or_create_writable(name);
}

int screencap_screenshot(const char* file, uint32_t out_w, uint32_t out_h)
{
    FsWritableFile* w;
    uint8_t hdr[LSHOT_HEADER_SIZE];
    uint32_t src_w = 0;
    uint32_t src_h = 0;
    uint32_t data_bytes;
    uint32_t pos;
    char name[32];

    if (gui_capture_frame_info(&src_w, &src_h) != 0 || src_w == 0 || src_h == 0) {
        s_last_error = -1;
        return -1;
    }
    if (out_w == 0) out_w = LSHOT_DEFAULT_W;
    if (out_h == 0) out_h = LSHOT_DEFAULT_H;
    if (out_w > src_w) out_w = src_w;
    if (out_h > src_h) out_h = src_h;
    if (out_w == 0 || out_h == 0 || out_w > 255u || out_h > 255u) {
        s_last_error = -2;
        return -2;
    }
    data_bytes = out_w * out_h * 2u;
    if (data_bytes + LSHOT_HEADER_SIZE > 0xFFFFu) {
        s_last_error = -3;
        return -3;
    }
    sc_copy_name(name, sizeof(name), file && file[0] ? file : SCREENCAP_SHOT_DEFAULT);
    w = open_capture_file(name, SCREENCAP_SHOT_DEFAULT);
    if (!w || w->cap < data_bytes + LSHOT_HEADER_SIZE) {
        s_last_error = -4;
        return -4;
    }

    for (uint32_t i = 0; i < sizeof(hdr); i++) hdr[i] = 0;
    hdr[0] = 'L'; hdr[1] = 'S'; hdr[2] = 'H'; hdr[3] = 'T';
    put16(hdr, 4, 1u);
    put16(hdr, 6, LSHOT_HEADER_SIZE);
    put16(hdr, 8, src_w);
    put16(hdr, 10, src_h);
    put16(hdr, 12, out_w);
    put16(hdr, 14, out_h);
    put16(hdr, 16, LSHOT_FORMAT_RGB565);
    put16(hdr, 18, (s_shot_count + 1u) & 0xFFFFu);
    put32(hdr, 20, data_bytes);
    if (fs_write(w, 0, hdr, sizeof(hdr)) != sizeof(hdr)) {
        s_last_error = -5;
        return -5;
    }

    pos = LSHOT_HEADER_SIZE;
    for (uint32_t y = 0; y < out_h; y++) {
        uint32_t sy = sample_y(y, out_h, src_h);
        for (uint32_t x = 0; x < out_w; x++) {
            uint32_t sx = sample_x(x, out_w, src_w);
            uint16_t c = rgb565(gui_capture_get_pixel(sx, sy));
            uint8_t px[2];
            px[0] = (uint8_t)(c & 0xFFu);
            px[1] = (uint8_t)((c >> 8) & 0xFFu);
            if (fs_write(w, pos, px, 2u) != 2u) {
                s_last_error = -6;
                return -6;
            }
            pos += 2u;
        }
    }

    s_shot_count++;
    s_last_shot_width = out_w;
    s_last_shot_height = out_h;
    s_last_shot_bytes = data_bytes + LSHOT_HEADER_SIZE;
    s_last_error = 0;
    sc_copy_name(s_last_file, sizeof(s_last_file), name);
    (void)screencap_report();
    return 0;
}

static int write_rec_header(FsWritableFile* w)
{
    uint8_t hdr[LREC_HEADER_SIZE];
    uint32_t frame_bytes = s_rec_width * s_rec_height;
    if (!w) return -1;
    for (uint32_t i = 0; i < sizeof(hdr); i++) hdr[i] = 0;
    hdr[0] = 'L'; hdr[1] = 'R'; hdr[2] = 'E'; hdr[3] = 'C';
    put16(hdr, 4, 1u);
    put16(hdr, 6, LREC_HEADER_SIZE);
    put16(hdr, 8, s_rec_src_w);
    put16(hdr, 10, s_rec_src_h);
    put16(hdr, 12, s_rec_width);
    put16(hdr, 14, s_rec_height);
    put16(hdr, 16, s_rec_max_frames);
    put16(hdr, 18, s_rec_frames);
    put16(hdr, 20, LREC_FORMAT_LUMA8);
    put16(hdr, 22, frame_bytes);
    put32(hdr, 24, s_rec_bytes);
    return fs_write(w, 0, hdr, sizeof(hdr)) == sizeof(hdr) ? 0 : -2;
}

int screencap_record_start(const char* file, uint32_t max_frames, uint32_t out_w, uint32_t out_h)
{
    FsWritableFile* w;
    uint32_t cap_frames;
    uint32_t frame_bytes;
    char name[32];

    if (gui_capture_frame_info(&s_rec_src_w, &s_rec_src_h) != 0 || s_rec_src_w == 0 || s_rec_src_h == 0) {
        s_last_error = -10;
        return -10;
    }
    if (out_w == 0) out_w = LREC_DEFAULT_W;
    if (out_h == 0) out_h = LREC_DEFAULT_H;
    if (out_w > s_rec_src_w) out_w = s_rec_src_w;
    if (out_h > s_rec_src_h) out_h = s_rec_src_h;
    if (max_frames == 0) max_frames = LREC_DEFAULT_FRAMES;
    if (max_frames > LREC_MAX_FRAMES) max_frames = LREC_MAX_FRAMES;
    if (out_w == 0 || out_h == 0 || out_w > 255u || out_h > 255u) {
        s_last_error = -11;
        return -11;
    }

    sc_copy_name(name, sizeof(name), file && file[0] ? file : SCREENCAP_REC_DEFAULT);
    w = open_capture_file(name, SCREENCAP_REC_DEFAULT);
    if (!w) {
        s_last_error = -12;
        return -12;
    }
    frame_bytes = out_w * out_h;
    if (frame_bytes == 0 || LREC_HEADER_SIZE + frame_bytes > w->cap) {
        s_last_error = -13;
        return -13;
    }
    cap_frames = (w->cap - LREC_HEADER_SIZE) / frame_bytes;
    if (max_frames > cap_frames) max_frames = cap_frames;
    if (max_frames == 0) {
        s_last_error = -14;
        return -14;
    }

    s_rec_width = out_w;
    s_rec_height = out_h;
    s_rec_frames = 0;
    s_rec_max_frames = max_frames;
    s_rec_bytes = LREC_HEADER_SIZE;
    s_rec_active = 1;
    sc_copy_name(s_rec_file, sizeof(s_rec_file), name);
    if (write_rec_header(w) != 0) {
        s_rec_active = 0;
        s_last_error = -15;
        return -15;
    }
    s_last_error = 0;
    sc_copy_name(s_last_file, sizeof(s_last_file), name);
    (void)screencap_report();
    return 0;
}

int screencap_record_stop(void)
{
    FsWritableFile* w = fs_open_writable(s_rec_file[0] ? s_rec_file : SCREENCAP_REC_DEFAULT);
    s_rec_active = 0;
    if (w) (void)write_rec_header(w);
    (void)screencap_report();
    return 0;
}

int screencap_record_frame(void)
{
    FsWritableFile* w;
    uint32_t frame_bytes;
    uint32_t pos;
    uint8_t px;

    if (!s_rec_active) {
        s_last_error = -20;
        return -20;
    }
    if (s_rec_frames >= s_rec_max_frames) {
        (void)screencap_record_stop();
        return 1;
    }
    w = fs_open_writable(s_rec_file[0] ? s_rec_file : SCREENCAP_REC_DEFAULT);
    if (!w) {
        s_last_error = -21;
        s_rec_active = 0;
        return -21;
    }
    frame_bytes = s_rec_width * s_rec_height;
    pos = LREC_HEADER_SIZE + s_rec_frames * frame_bytes;
    if (pos + frame_bytes > w->cap) {
        s_last_error = -22;
        s_rec_active = 0;
        (void)write_rec_header(w);
        return -22;
    }

    for (uint32_t y = 0; y < s_rec_height; y++) {
        uint32_t sy = sample_y(y, s_rec_height, s_rec_src_h);
        for (uint32_t x = 0; x < s_rec_width; x++) {
            uint32_t sx = sample_x(x, s_rec_width, s_rec_src_w);
            px = luma8(gui_capture_get_pixel(sx, sy));
            if (fs_write(w, pos++, &px, 1u) != 1u) {
                s_last_error = -23;
                s_rec_active = 0;
                return -23;
            }
        }
    }
    s_rec_frames++;
    s_rec_bytes = LREC_HEADER_SIZE + s_rec_frames * frame_bytes;
    if (write_rec_header(w) != 0) {
        s_last_error = -24;
        s_rec_active = 0;
        return -24;
    }
    if (s_rec_frames >= s_rec_max_frames) {
        s_rec_active = 0;
        (void)screencap_report();
    }
    s_last_error = 0;
    return s_rec_active ? 0 : 1;
}

void screencap_after_render(void)
{
    if (s_rec_active) (void)screencap_record_frame();
}

void screencap_info(screencap_info_t* out)
{
    uint32_t fw = 0;
    uint32_t fh = 0;
    if (!out) return;
    (void)gui_capture_frame_info(&fw, &fh);
    out->fb_width = fw;
    out->fb_height = fh;
    out->last_shot_width = s_last_shot_width;
    out->last_shot_height = s_last_shot_height;
    out->last_shot_bytes = s_last_shot_bytes;
    out->shot_count = s_shot_count;
    out->rec_active = s_rec_active;
    out->rec_width = s_rec_width;
    out->rec_height = s_rec_height;
    out->rec_frames = s_rec_frames;
    out->rec_max_frames = s_rec_max_frames;
    out->rec_bytes = s_rec_bytes;
    out->last_error = s_last_error;
    sc_copy_name(out->last_file, sizeof(out->last_file), s_last_file);
}

int screencap_report(void)
{
    char buf[1024];
    int n;
    screencap_info_t info;
    FsWritableFile* w = fs_open_or_create_writable(SCREENCAP_REPORT_DEFAULT);
    if (!w) return -1;
    screencap_info(&info);
    n = snprintf(buf, sizeof(buf),
                 "LARDD 1\n"
                 "TITLE Screen Capture\n"
                 "TEXT User-owned screenshots and short screen recordings.\n"
                 "SECTION State\n"
                 "ITEM framebuffer %ux%u\n"
                 "ITEM last_file %s\n"
                 "ITEM shots %u\n"
                 "ITEM last_shot %ux%u bytes %u\n"
                 "ITEM recording %s frames %u/%u size %ux%u bytes %u\n"
                 "ITEM last_error %d\n"
                 "SECTION Commands\n"
                 "ITEM screenshot [file.lshot] [w h]\n"
                 "ITEM screenrec start [frames] [w h] [file.lrec]\n"
                 "ITEM screenrec frame | stop | status\n"
                 "END\n",
                 info.fb_width, info.fb_height,
                 info.last_file[0] ? info.last_file : "(none)",
                 info.shot_count,
                 info.last_shot_width, info.last_shot_height, info.last_shot_bytes,
                 info.rec_active ? "on" : "off",
                 info.rec_frames, info.rec_max_frames,
                 info.rec_width, info.rec_height, info.rec_bytes,
                 info.last_error);
    if (n < 0) return -2;
    if ((uint32_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    return fs_write(w, 0, (const uint8_t*)buf, (uint32_t)n) == (uint32_t)n ? 0 : -3;
}

int screencap_selftest(void)
{
    uint32_t w = 0;
    uint32_t h = 0;
    if (gui_capture_frame_info(&w, &h) != 0 || w < 16u || h < 16u) return -1;
    if (!fs_open_writable(SCREENCAP_SHOT_DEFAULT)) return -2;
    if (!fs_open_writable(SCREENCAP_REC_DEFAULT)) return -3;
    if (!fs_open_writable(SCREENCAP_REPORT_DEFAULT)) return -4;
    if (fs_writable_capacity_for(SCREENCAP_SHOT_DEFAULT) < LSHOT_HEADER_SIZE + LSHOT_DEFAULT_W * LSHOT_DEFAULT_H * 2u) return -5;
    if (fs_writable_capacity_for(SCREENCAP_REC_DEFAULT) < LREC_HEADER_SIZE + LREC_DEFAULT_W * LREC_DEFAULT_H * 4u) return -6;
    return 0;
}
