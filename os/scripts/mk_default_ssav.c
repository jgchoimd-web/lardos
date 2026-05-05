/*
 * mk_default_ssav - Generate default.ssav (32x32 gradient, 1 frame).
 * Replaces mk_default_ssav.py. No Python required.
 *
 * Usage: mk_default_ssav [output.bin]
 * Default output: kernel/default_ssav.bin (relative to os/)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define W 32
#define H 32

static void write_u16(unsigned char* p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

int main(int argc, char** argv) {
    const char* out_path = (argc >= 2) ? argv[1] : "kernel/default_ssav.bin";

    unsigned char header[22];
    memcpy(header, "SSAV", 4);
    header[4] = 1;
    header[5] = 0;
    header[6] = 0;
    header[7] = 0;
    write_u16(header + 8, 1);
    write_u16(header + 10, W);
    write_u16(header + 12, H);
    header[14] = 32;
    header[15] = 0;
    memset(header + 16, 0, 6);

    unsigned char pixels[W * H * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float t = (float)(x + y) / (float)(W + H);
            int r = (int)(20 + 100 * t);
            int g = (int)(40 + 150 * t);
            int b = (int)(120 + 100 * t);
            int a = 255;
            int i = (y * W + x) * 4;
            pixels[i++] = (unsigned char)(b > 255 ? 255 : b);
            pixels[i++] = (unsigned char)(g > 255 ? 255 : g);
            pixels[i++] = (unsigned char)(r > 255 ? 255 : r);
            pixels[i] = (unsigned char)a;
        }
    }

    FILE* f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "mk_default_ssav: cannot write %s\n", out_path);
        return 1;
    }
    fwrite(header, 1, 22, f);
    fwrite(pixels, 1, W * H * 4, f);
    fclose(f);

    printf("Wrote %s (%d bytes)\n", out_path, 22 + W * H * 4);
    return 0;
}
