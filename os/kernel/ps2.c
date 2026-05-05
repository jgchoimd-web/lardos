#include "ps2.h"
#include "io.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64
#define PS2_CMD  0x64

static void cpu_pause(void)
{
    __asm__ __volatile__("pause");
}

static int wait_in(void)
{
    // Wait for input buffer empty (bit 1 == 0)
    for (int i = 0; i < 100000; i++) {
        if ((inb(PS2_STAT) & 0x02) == 0) return 0;
        cpu_pause();
    }
    return -1;
}

static int wait_out(void)
{
    // Wait for output buffer full (bit 0 == 1)
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STAT) & 0x01) return 0;
        cpu_pause();
    }
    return -1;
}

static void flush_out(void)
{
    for (int i = 0; i < 64; i++) {
        if ((inb(PS2_STAT) & 0x01) == 0) break;
        (void)inb(PS2_DATA);
    }
}

static int write_cmd(uint8_t cmd)
{
    if (wait_in() != 0) return -1;
    outb(PS2_CMD, cmd);
    return 0;
}

static int write_data(uint8_t v)
{
    if (wait_in() != 0) return -1;
    outb(PS2_DATA, v);
    return 0;
}

static int read_data(uint8_t* out)
{
    if (wait_out() != 0) return -1;
    *out = inb(PS2_DATA);
    return 0;
}

static int mouse_write(uint8_t v)
{
    // Tell controller next byte is for mouse
    if (write_cmd(0xD4) != 0) return -1;
    if (write_data(v) != 0) return -1;
    uint8_t ack = 0;
    if (read_data(&ack) != 0) return -1;
    return ack == 0xFA ? 0 : -2;
}

int ps2_init(void)
{
    // Basic controller init; best-effort.
    flush_out();

    // Disable devices
    write_cmd(0xAD);
    write_cmd(0xA7);
    flush_out();

    // Read config byte
    if (write_cmd(0x20) != 0) return -1;
    uint8_t cfg = 0;
    if (read_data(&cfg) != 0) return -2;

    // Enable IRQs bits off (we poll), enable mouse clock (bit 5 = disable mouse)
    cfg &= ~(1u << 0);
    cfg &= ~(1u << 1);
    cfg &= ~(1u << 6); // translation off

    // Write config byte
    if (write_cmd(0x60) != 0) return -3;
    if (write_data(cfg) != 0) return -4;

    // Re-enable devices
    write_cmd(0xAE);
    write_cmd(0xA8);
    flush_out();
    return 0;
}

int ps2_mouse_init(void)
{
    // Enable aux device
    write_cmd(0xA8);
    flush_out();

    // Set defaults
    if (mouse_write(0xF6) != 0) return -1;
    // Enable data reporting (streaming)
    if (mouse_write(0xF4) != 0) return -2;
    return 0;
}

int ps2_mouse_poll(int* out_dx, int* out_dy, int* out_buttons)
{
    static uint8_t pkt[3];
    static int idx;

    // Need a mouse byte: status bit 0=out full, bit5=aux data
    uint8_t st = inb(PS2_STAT);
    if ((st & 0x01) == 0) return 1;
    if ((st & 0x20) == 0) {
        // keyboard byte, drop for now
        (void)inb(PS2_DATA);
        return 1;
    }

    uint8_t b = inb(PS2_DATA);
    if (idx == 0 && (b & 0x08) == 0) {
        // not a valid first byte
        return 1;
    }
    pkt[idx++] = b;
    if (idx < 3) return 1;
    idx = 0;

    int dx = (int8_t)pkt[1];
    int dy = (int8_t)pkt[2];
    int btn = pkt[0] & 0x07;

    *out_dx = dx;
    *out_dy = -dy; // screen y grows down
    *out_buttons = btn;
    return 0;
}

static char scancode_to_ascii(uint8_t sc, int shift)
{
    // Set 1 scancodes (US layout), minimal subset.
    // Letters
    if (sc >= 0x10 && sc <= 0x19) { // q..p
        static const char* t = "qwertyuiop";
        char c = t[sc - 0x10];
        return shift ? (char)(c - 32) : c;
    }
    if (sc >= 0x1E && sc <= 0x26) { // a..l
        static const char* t = "asdfghjkl";
        char c = t[sc - 0x1E];
        return shift ? (char)(c - 32) : c;
    }
    if (sc >= 0x2C && sc <= 0x32) { // z..m
        static const char* t = "zxcvbnm";
        char c = t[sc - 0x2C];
        return shift ? (char)(c - 32) : c;
    }

    // Numbers row
    if (sc >= 0x02 && sc <= 0x0B) {
        static const char* n = "1234567890";
        static const char* ns = "!@#$%^&*()";
        if (sc == 0x0B) return shift ? ')' : '0';
        return shift ? ns[sc - 0x02] : n[sc - 0x02];
    }

    switch (sc) {
        case 0x39: return ' ';
        case 0x1C: return '\n';
        case 0x0E: return '\b';
        case 0x0F: return '\t';
        case 0x34: return shift ? '>' : '.';
        case 0x33: return shift ? '<' : ',';
        case 0x35: return shift ? '?' : '/';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x2B: return shift ? '|' : '\\';
        default: return 0;
    }
}

int ps2_kbd_poll(ps2_key_t* out)
{
    static int shift;
    static int ext;

    uint8_t st = inb(PS2_STAT);
    if ((st & 0x01) == 0) return 1;
    if (st & 0x20) {
        // mouse byte; don't consume here
        return 1;
    }

    uint8_t sc = inb(PS2_DATA);
    if (sc == 0xE0) {
        ext = 1;
        return 1;
    }
    int released = (sc & 0x80) != 0;
    uint8_t code = (uint8_t)(sc & 0x7F);

    // Shift make/break
    if (code == 0x2A || code == 0x36) {
        shift = released ? 0 : 1;
        ext = 0;
        return 1;
    }

    if (released) return 1;

    // Extended keys (E0 prefix), set 1
    if (ext) {
        ext = 0;
        out->ch = 0;
        switch (code) {
            case 0x4B: out->kind = PS2K_LEFT; return 0;
            case 0x4D: out->kind = PS2K_RIGHT; return 0;
            case 0x48: out->kind = PS2K_UP; return 0;
            case 0x50: out->kind = PS2K_DOWN; return 0;
            case 0x47: out->kind = PS2K_HOME; return 0;
            case 0x4F: out->kind = PS2K_END; return 0;
            case 0x49: out->kind = PS2K_PGUP; return 0;
            case 0x51: out->kind = PS2K_PGDN; return 0;
            case 0x53: out->kind = PS2K_DEL; return 0;
            default: return 1;
        }
    }

    char c = scancode_to_ascii(code, shift);
    if (!c) return 1;
    out->kind = PS2K_ASCII;
    out->ch = c;
    return 0;
}

