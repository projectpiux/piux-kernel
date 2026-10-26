#include "video.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12)
#define VGA_MEMORY 0xB8000
#define VGA_COLUMNS 80
#define VGA_ROWS 25

#ifndef VIDEO_BACKBUFFER_WIDTH
#define VIDEO_BACKBUFFER_WIDTH 1920
#endif
#ifndef VIDEO_BACKBUFFER_HEIGHT
#define VIDEO_BACKBUFFER_HEIGHT 1080
#endif
#define VIDEO_BACKBUFFER_BYTES (VIDEO_BACKBUFFER_WIDTH * VIDEO_BACKBUFFER_HEIGHT * 4)

#ifndef VIDEO_MAX_DRIVERS
#define VIDEO_MAX_DRIVERS 8
#endif
#define VIDEO_MAX_DIMENSION 16384u

#define GLYPH_ROWS 7
#define GLYPH_TOP ((VIDEO_CELL_HEIGHT - GLYPH_ROWS) / 2)

#ifndef VIDEO_TEXT_SCROLL
#define VIDEO_TEXT_SCROLL 1
#endif

#if defined(__i386__) || defined(__x86_64__)
#define VIDEO_X86 1
#else
#define VIDEO_X86 0
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define VIDEO_LE 0
#else
#define VIDEO_LE 1
#endif

typedef char video_cell_size_check[(VIDEO_CELL_WIDTH >= 6 && VIDEO_CELL_HEIGHT >= GLYPH_ROWS) ? 1 : -1];
typedef uint32_t __attribute__((may_alias, aligned(1))) video_u32;
typedef uint16_t __attribute__((may_alias, aligned(1))) video_u16;

/* ------------------------------------------------------------------------- */
/* Exported state                                                            */
/* ------------------------------------------------------------------------- */

int video_framebuffer_ready;
int video_columns = 80;
int video_rows = 25;
int video_pixel_width;
int video_pixel_height;

/* ------------------------------------------------------------------------- */
/* Internal state                                                            */
/* ------------------------------------------------------------------------- */

static struct {
    uint8_t *base;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t bytes;
    uint8_t rpos, rsize, gpos, gsize, bpos, bsize;
    uint8_t xrgb;
} fb;

static uint8_t video_backbuffer[VIDEO_BACKBUFFER_BYTES] __attribute__((aligned(64)));
static uint32_t shadow_pitch;
static int shadow_ready;

static uint32_t video_origin_x;
static uint32_t video_origin_y;

static int video_frame_active;
static uint32_t video_frame_x;
static uint32_t video_frame_y;
static uint32_t video_frame_width;
static uint32_t video_frame_height;

static int dirty_valid;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;

static uint32_t palette_pixel[16];
static uint32_t lut_r[256], lut_g[256], lut_b[256];
static uint8_t font[256][GLYPH_ROWS];
static int font_ready;

static struct video_gpu_info gpu_info;
static int gpu_present;
static const struct video_driver *registered_drivers[VIDEO_MAX_DRIVERS];
static int registered_count;
static const struct video_driver *active_driver;
static int accel_pending;

struct multiboot_info {
    uint32_t flags;
    uint32_t memory_lower;
    uint32_t memory_upper;
    uint32_t boot_device;
    uint32_t command_line;
    uint32_t modules_count;
    uint32_t modules_address;
    uint32_t symbols[4];
    uint32_t memory_map_length;
    uint32_t memory_map_address;
    uint32_t drives_length;
    uint32_t drives_address;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_segment;
    uint16_t vbe_interface_offset;
    uint16_t vbe_interface_length;
    uint64_t framebuffer_address;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t color_info[6];
} __attribute__((packed));

/* ------------------------------------------------------------------------- */
/* Port I/O                                                                  */
/* ------------------------------------------------------------------------- */

#if VIDEO_X86
static inline void port_outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint16_t port_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void port_outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint32_t port_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
#else
static inline void port_outw(uint16_t port, uint16_t value) { (void)port; (void)value; }
static inline uint16_t port_inw(uint16_t port) { (void)port; return 0xFFFF; }
static inline void port_outl(uint16_t port, uint32_t value) { (void)port; (void)value; }
static inline uint32_t port_inl(uint16_t port) { (void)port; return 0xFFFFFFFFu; }
#endif

/* ------------------------------------------------------------------------- */
/* Memory primitives                                                         */
/* ------------------------------------------------------------------------- */

static inline void mem_fill32(void *dst, uint32_t value, uint32_t count) {
#if VIDEO_X86
    uintptr_t n = count;
    __asm__ volatile("cld\n\trep stosl" : "+D"(dst), "+c"(n) : "a"(value) : "memory", "cc");
#else
    video_u32 *d = (video_u32 *)dst;
    while (count--) *d++ = value;
#endif
}

static inline void mem_zero(void *dst, uint32_t bytes) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t words = bytes >> 2;
    mem_fill32(d, 0, words);
    d += (uintptr_t)words << 2;
    for (bytes &= 3u; bytes; bytes--) *d++ = 0;
}

static inline void mem_copy(void *dst, const void *src, uint32_t bytes) {
#if VIDEO_X86
    uintptr_t words = bytes >> 2;
    uintptr_t tail = bytes & 3u;
    __asm__ volatile("cld\n\trep movsl" : "+D"(dst), "+S"(src), "+c"(words) : : "memory", "cc");
    if (tail)
        __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(tail) : : "memory", "cc");
#else
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (bytes--) *d++ = *s++;
#endif
}

/* Overlap-safe copy. The backward path now moves whole words. */
static void mem_move(void *dst, const void *src, uint32_t bytes) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d <= s || d >= s + bytes) {
        mem_copy(d, s, bytes);
        return;
    }
    d += bytes;
    s += bytes;
    while (bytes >= 4) {
        d -= 4;
        s -= 4;
        *(video_u32 *)d = *(const video_u32 *)s;
        bytes -= 4;
    }
    while (bytes--) *--d = *--s;
}

/* ------------------------------------------------------------------------- */
/* Raw pixel access (any 2/3/4 byte format)                                  */
/* ------------------------------------------------------------------------- */

static inline uint32_t pixel_read(const uint8_t *p, uint8_t bytes) {
    if (bytes == 4) return *(const video_u32 *)p;
    if (bytes == 3) return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    return *(const video_u16 *)p;
}

static inline void pixel_write(uint8_t *p, uint32_t value, uint8_t bytes) {
    if (bytes == 4) {
        *(video_u32 *)p = value;
    } else if (bytes == 3) {
        p[0] = (uint8_t)value;
        p[1] = (uint8_t)(value >> 8);
        p[2] = (uint8_t)(value >> 16);
    } else {
        *(video_u16 *)p = (uint16_t)value;
    }
}

static void fill_pixels(uint8_t *dst, uint32_t value, uint32_t count, uint8_t bytes) {
    if (count == 0) return;
    if (value == 0) {
        mem_zero(dst, count * bytes);
    } else if (bytes == 4) {
        mem_fill32(dst, value, count);
    } else if (bytes == 2) {
        uint16_t v = (uint16_t)value;
        if (count && ((uintptr_t)dst & 2u)) {
            *(video_u16 *)dst = v;
            dst += 2;
            count--;
        }
        mem_fill32(dst, (uint32_t)v | ((uint32_t)v << 16), count >> 1);
        dst += (uintptr_t)(count >> 1) << 2;
        if (count & 1u) *(video_u16 *)dst = v;
    } else {
#if VIDEO_LE
        /* 4 pixels = 12 bytes = 3 aligned-agnostic words */
        uint32_t b0 = value & 0xFFu, b1 = (value >> 8) & 0xFFu, b2 = (value >> 16) & 0xFFu;
        uint32_t w0 = b0 | (b1 << 8) | (b2 << 16) | (b0 << 24);
        uint32_t w1 = b1 | (b2 << 8) | (b0 << 16) | (b1 << 24);
        uint32_t w2 = b2 | (b0 << 8) | (b1 << 16) | (b2 << 24);
        while (count >= 4) {
            ((video_u32 *)dst)[0] = w0;
            ((video_u32 *)dst)[1] = w1;
            ((video_u32 *)dst)[2] = w2;
            dst += 12;
            count -= 4;
        }
#endif
        while (count--) {
            pixel_write(dst, value, 3);
            dst += 3;
        }
    }
}

static void write_pixels(uint8_t *dst, const uint32_t *src, uint32_t count, uint8_t bytes) {
    if (bytes == 4) {
        mem_copy(dst, src, count * 4u);
        return;
    }
    while (count--) {
        pixel_write(dst, *src++, bytes);
        dst += bytes;
    }
}

/* ------------------------------------------------------------------------- */
/* Colour packing / blending                                                 */
/* ------------------------------------------------------------------------- */

/* Channel sizes up to 16 bits are supported (e.g. 10-bit deep colour). */
static inline uint32_t pack_channel(uint32_t c8, uint8_t pos, uint8_t size) {
    uint32_t max;
    if (!size) return 0;
    if (size == 8) return c8 << pos;
    if (size > 8) return ((c8 << (size - 8)) | (c8 >> (16 - size))) << pos;
    max = (1u << size) - 1u;
    return ((c8 * max + 127u) / 255u) << pos;
}

static inline uint32_t expand_channel(uint32_t value, uint8_t pos, uint8_t size) {
    uint32_t v;
    if (!size) return 0;
    v = (value >> pos) & ((1u << size) - 1u);
    if (size == 8) return v;
    if (size > 8) return v >> (size - 8);
    v <<= (8 - size);
    for (uint32_t s = size; s < 8; s <<= 1) v |= v >> s;
    return v;
}

static inline uint32_t framebuffer_color(uint32_t rgb) {
    if (fb.xrgb) return rgb & 0x00FFFFFFu;
    return lut_r[(rgb >> 16) & 0xFFu] | lut_g[(rgb >> 8) & 0xFFu] | lut_b[rgb & 0xFFu];
}

static inline uint32_t mix8(uint32_t src, uint32_t dst, uint32_t alpha) {
    uint32_t t = src * alpha + dst * (255u - alpha) + 128u;
    return (t + (t >> 8)) >> 8;
}

/* t = src*alpha + 128 is pre-multiplied by the caller */
static inline uint32_t mix8p(uint32_t src_a, uint32_t dst, uint32_t inv_alpha) {
    uint32_t t = src_a + dst * inv_alpha;
    return (t + (t >> 8)) >> 8;
}

static inline uint32_t blend_xrgb(uint32_t dst, uint32_t rgb, uint32_t alpha) {
    uint32_t r = mix8((rgb >> 16) & 0xFFu, (dst >> 16) & 0xFFu, alpha);
    uint32_t g = mix8((rgb >> 8) & 0xFFu, (dst >> 8) & 0xFFu, alpha);
    uint32_t b = mix8(rgb & 0xFFu, dst & 0xFFu, alpha);
    return (r << 16) | (g << 8) | b;
}

static uint32_t blend_pixel(uint32_t raw_dst, uint32_t rgb, uint8_t alpha) {
    uint32_t r = mix8((rgb >> 16) & 0xFFu, expand_channel(raw_dst, fb.rpos, fb.rsize), alpha);
    uint32_t g = mix8((rgb >> 8) & 0xFFu, expand_channel(raw_dst, fb.gpos, fb.gsize), alpha);
    uint32_t b = mix8(rgb & 0xFFu, expand_channel(raw_dst, fb.bpos, fb.bsize), alpha);
    return pack_channel(r, fb.rpos, fb.rsize) | pack_channel(g, fb.gpos, fb.gsize) | pack_channel(b, fb.bpos, fb.bsize);
}

/* Blend a solid colour over `count` pixels in place. */
static void blend_span(uint8_t *p, uint32_t count, uint32_t color, uint8_t alpha) {
    if (fb.xrgb) {
        uint32_t a = alpha, ia = 255u - a;
        uint32_t sr = ((color >> 16) & 0xFFu) * a + 128u;
        uint32_t sg = ((color >> 8) & 0xFFu) * a + 128u;
        uint32_t sb = (color & 0xFFu) * a + 128u;
        for (; count; count--, p += 4) {
            uint32_t d = *(video_u32 *)p;
            *(video_u32 *)p = (mix8p(sr, (d >> 16) & 0xFFu, ia) << 16) |
                              (mix8p(sg, (d >> 8) & 0xFFu, ia) << 8) |
                              mix8p(sb, d & 0xFFu, ia);
        }
        return;
    }
    for (; count; count--, p += fb.bytes)
        pixel_write(p, blend_pixel(pixel_read(p, fb.bytes), color, alpha), fb.bytes);
}

/* ------------------------------------------------------------------------- */
/* Font: 5x7 glyphs parsed once into row bitmasks                            */
/* ------------------------------------------------------------------------- */

static const char *const glyph_letters[26] = {
    "01110 10001 10001 11111 10001 10001 10001", "11110 10001 10001 11110 10001 10001 11110",
    "01111 10000 10000 10000 10000 10000 01111", "11110 10001 10001 10001 10001 10001 11110",
    "11111 10000 10000 11110 10000 10000 11111", "11111 10000 10000 11110 10000 10000 10000",
    "01111 10000 10000 10111 10001 10001 01111", "10001 10001 10001 11111 10001 10001 10001",
    "11111 00100 00100 00100 00100 00100 11111", "00111 00010 00010 00010 10010 10010 01100",
    "10001 10010 10100 11000 10100 10010 10001", "10000 10000 10000 10000 10000 10000 11111",
    "10001 11011 10101 10101 10001 10001 10001", "10001 11001 10101 10011 10001 10001 10001",
    "01110 10001 10001 10001 10001 10001 01110", "11110 10001 10001 11110 10000 10000 10000",
    "01110 10001 10001 10001 10101 10010 01101", "11110 10001 10001 11110 10100 10010 10001",
    "01111 10000 10000 01110 00001 00001 11110", "11111 00100 00100 00100 00100 00100 00100",
    "10001 10001 10001 10001 10001 10001 01110", "10001 10001 10001 10001 10001 01010 00100",
    "10001 10001 10001 10101 10101 11011 10001", "10001 10001 01010 00100 01010 10001 10001",
    "10001 10001 01010 00100 00100 00100 00100", "11111 00001 00010 00100 01000 10000 11111"
};

static const char *const glyph_lowercase[26] = {
    "00000 00000 01110 00001 01111 10001 01111", "10000 10000 11110 10001 10001 10001 11110",
    "00000 00000 01111 10000 10000 10000 01111", "00001 00001 01111 10001 10001 10001 01111",
    "00000 00000 01110 10001 11111 10000 01110", "00110 01001 01000 11100 01000 01000 01000",
    "00000 00000 01111 10001 01111 00001 01110", "10000 10000 10110 11001 10001 10001 10001",
    "00100 00000 01100 00100 00100 00100 01110", "00010 00000 00110 00010 00010 10010 01100",
    "10000 10000 10010 10100 11000 10100 10010", "01100 00100 00100 00100 00100 00100 01110",
    "00000 00000 11010 10101 10101 10101 10101", "00000 00000 10110 11001 10001 10001 10001",
    "00000 00000 01110 10001 10001 10001 01110", "00000 00000 11110 10001 11110 10000 10000",
    "00000 00000 01111 10001 01111 00001 00001", "00000 00000 10111 11000 10000 10000 10000",
    "00000 00000 01111 10000 01110 00001 11110", "01000 01000 11100 01000 01000 01001 00110",
    "00000 00000 10001 10001 10001 10011 01101", "00000 00000 10001 10001 10001 01010 00100",
    "00000 00000 10001 10101 10101 11011 10001", "00000 00000 10001 01010 00100 01010 10001",
    "00000 00000 10001 10001 01111 00001 01110", "00000 00000 11111 00010 00100 01000 11111"
};

static const char *const glyph_digits[10] = {
    "01110 10001 10011 10101 11001 10001 01110", "00100 01100 00100 00100 00100 00100 01110",
    "01110 10001 00001 00010 00100 01000 11111", "11110 00001 00001 01110 00001 00001 11110",
    "00010 00110 01010 10010 11111 00010 00010", "11111 10000 10000 11110 00001 00001 11110",
    "00110 01000 10000 11110 10001 10001 01110", "11111 00001 00010 00100 01000 01000 01000",
    "01110 10001 10001 01110 10001 10001 01110", "01110 10001 10001 01111 00001 00010 01100"
};

static const char *glyph_pattern(int c) {
    if (c >= 'A' && c <= 'Z') return glyph_letters[c - 'A'];
    if (c >= 'a' && c <= 'z') return glyph_lowercase[c - 'a'];
    if (c >= '0' && c <= '9') return glyph_digits[c - '0'];
    switch (c) {
    case '!': return "00100 00100 00100 00100 00100 00000 00100";
    case '.': return "00000 00000 00000 00000 00000 00110 00110";
    case ',': return "00000 00000 00000 00000 00000 00110 00100";
    case ':': return "00000 00110 00110 00000 00110 00110 00000";
    case '-': return "00000 00000 00000 11111 00000 00000 00000";
    case '_': return "00000 00000 00000 00000 00000 00000 11111";
    case '/': return "00001 00010 00100 01000 10000 00000 00000";
    case '>': return "10000 01000 00100 00010 00100 01000 10000";
    case '<': return "00001 00010 00100 01000 00100 00010 00001";
    case '+': return "00000 00100 00100 11111 00100 00100 00000";
    case '=': return "00000 11111 00000 11111 00000 00000 00000";
    case '?': return "01110 10001 00001 00010 00100 00000 00100";
    case '(': return "00010 00100 01000 01000 01000 00100 00010";
    case ')': return "01000 00100 00010 00010 00010 00100 01000";
    case '*': return "00000 10101 01110 11111 01110 10101 00000";
    case '|': return "00100 00100 00100 00100 00100 00100 00100";
    case '[': return "01110 01000 01000 01000 01000 01000 01110";
    case ']': return "01110 00010 00010 00010 00010 00010 01110";
    case '#': return "01010 11111 01010 01010 11111 01010 00000";
    case '%': return "11001 11010 00010 00100 01000 01011 10011";
    case '"': return "01010 01010 00000 00000 00000 00000 00000";
    case '\'': return "00100 00100 00000 00000 00000 00000 00000";
    case '$': return "00100 01111 10100 01110 00101 11110 00100";
    case '&': return "01100 10010 10100 01000 10101 10010 01101";
    case ';': return "00110 00110 00000 00110 00100 01000 00000";
    case '@': return "01110 10001 10111 10101 10111 10000 01110";
    case '\\': return "10000 01000 00100 00010 00001 00000 00000";
    case '^': return "00100 01010 10001 00000 00000 00000 00000";
    case '`': return "00100 00010 00000 00000 00000 00000 00000";
    case '{': return "00010 00100 00100 01000 00100 00100 00010";
    case '}': return "01000 00100 00100 00010 00100 00100 01000";
    case '~': return "00000 01001 10110 00000 00000 00000 00000";
    default: return 0;
    }
}

static void font_init(void) {
    if (font_ready) return;
    for (int c = 0; c < 256; c++) {
        const char *pattern = glyph_pattern(c);
        for (int r = 0; r < GLYPH_ROWS; r++) {
            uint8_t bits = 0;
            if (pattern)
                for (int i = 0; i < 5; i++)
                    bits = (uint8_t)((bits << 1) | (uint8_t)(pattern[r * 6 + i] - '0'));
            font[c][r] = bits;
        }
    }
    font_ready = 1;
}

static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

/* ------------------------------------------------------------------------- */
/* Clipping, dirty tracking, shadow surface                                  */
/* ------------------------------------------------------------------------- */

static void get_clip(int *x0, int *y0, int *x1, int *y1) {
    if (video_frame_active) {
        *x0 = (int)video_frame_x;
        *y0 = (int)video_frame_y;
        *x1 = *x0 + (int)video_frame_width;
        *y1 = *y0 + (int)video_frame_height;
    } else {
        *x0 = 0;
        *y0 = 0;
        *x1 = (int)fb.width;
        *y1 = (int)fb.height;
    }
}

static inline int clamp_int(int64_t v) {
    if (v > 0x7FFFFFFF) return 0x7FFFFFFF;
    if (v < -0x7FFFFFFF - 1) return -0x7FFFFFFF - 1;
    return (int)v;
}

/* Clips [a, a+len) to [lo, hi); the companion coordinate b moves with a.
 * Uses 64-bit math so hostile coordinates cannot overflow. */
static int clip_axis(int *a, int *b, int *len, int lo, int hi) {
    int64_t pa = *a, pb = *b, l = *len;
    if (l <= 0) return 0;
    if (pa < lo) {
        int64_t d = (int64_t)lo - pa;
        if (d >= l) return 0;
        pa += d;
        pb += d;
        l -= d;
    }
    if (pa >= hi) return 0;
    if (l > hi - pa) l = hi - pa;
    *a = clamp_int(pa);
    *b = clamp_int(pb);
    *len = (int)l;
    return 1;
}

static void dirty_add(int x0, int y0, int x1, int y1) {
    if (!dirty_valid) {
        dirty_x0 = x0; dirty_y0 = y0; dirty_x1 = x1; dirty_y1 = y1;
        dirty_valid = 1;
        return;
    }
    if (x0 < dirty_x0) dirty_x0 = x0;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}

static inline void vram_sync(void) {
    if (!accel_pending) return;
    if (active_driver && active_driver->sync) active_driver->sync();
    accel_pending = 0;
}

static inline uint8_t *surface_base(void) { return shadow_ready ? video_backbuffer : fb.base; }
static inline uint32_t surface_pitch(void) { return shadow_ready ? shadow_pitch : fb.pitch; }

static void flush_rect(int x0, int y0, int x1, int y1) {
    uintptr_t offset;
    uint32_t bytes;
    if (!shadow_ready) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)fb.width) x1 = (int)fb.width;
    if (y1 > (int)fb.height) y1 = (int)fb.height;
    if (x1 <= x0 || y1 <= y0) return;
    vram_sync();
    offset = (uintptr_t)x0 * fb.bytes;
    bytes = (uint32_t)(x1 - x0) * fb.bytes;
    if (offset == 0 && bytes == fb.pitch && bytes == shadow_pitch) {
        mem_copy(fb.base + (uintptr_t)y0 * fb.pitch, video_backbuffer + (uintptr_t)y0 * shadow_pitch,
                 bytes * (uint32_t)(y1 - y0));
        return;
    }
    for (int y = y0; y < y1; y++)
        mem_copy(fb.base + (uintptr_t)y * fb.pitch + offset,
                 video_backbuffer + (uintptr_t)y * shadow_pitch + offset, bytes);
}

static void surface_commit(int x0, int y0, int x1, int y1) {
    if (video_frame_active) dirty_add(x0, y0, x1, y1);
    else flush_rect(x0, y0, x1, y1);
}

static inline int hw_fill(int x, int y, int w, int h, uint32_t pixel) {
    if (active_driver && active_driver->fill_rect && active_driver->fill_rect(x, y, w, h, pixel)) {
        accel_pending = 1;
        return 1;
    }
    return 0;
}

static inline int hw_copy(int sx, int sy, int dx, int dy, int w, int h) {
    if (active_driver && active_driver->copy_rect && active_driver->copy_rect(sx, sy, dx, dy, w, h)) {
        accel_pending = 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* PCI + Bochs/QEMU BGA                                                      */
/* ------------------------------------------------------------------------- */

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    port_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                     ((uint32_t)func << 8) | (offset & 0xFCu));
    return port_inl(0xCFC);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    port_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                     ((uint32_t)func << 8) | (offset & 0xFCu));
    port_outl(0xCFC, value);
}

/* Finds the first VGA-compatible display controller; falls back to any
 * display-class device (3D controller etc.) when no VGA device exists. */
static int pci_find_display(struct video_gpu_info *out) {
    struct video_gpu_info fallback;
    int have_fallback = 0;
    if (!VIDEO_X86) return 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            uint32_t header;
            int functions;
            if ((id & 0xFFFFu) == 0xFFFFu) continue;
            header = (pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C) >> 16) & 0xFFu;
            functions = (header & 0x80u) ? 8 : 1;
            for (int func = 0; func < functions; func++) {
                struct video_gpu_info found;
                uint32_t class_reg;
                id = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                class_reg = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                if ((class_reg >> 24) != 0x03) continue;
                found.bus = (uint8_t)bus;
                found.slot = (uint8_t)slot;
                found.func = (uint8_t)func;
                found.class_code = (uint8_t)(class_reg >> 24);
                found.subclass = (uint8_t)(class_reg >> 16);
                found.vendor = (uint16_t)id;
                found.device = (uint16_t)(id >> 16);
                found.bar0 = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x10);
                if (found.subclass == 0x00) {
                    *out = found;
                    return 1;
                }
                if (!have_fallback) {
                    fallback = found;
                    have_fallback = 1;
                }
            }
        }
    }
    if (have_fallback) {
        *out = fallback;
        return 1;
    }
    return 0;
}

#define BGA_INDEX_PORT 0x01CE
#define BGA_DATA_PORT 0x01CF
#define BGA_REG_ID 0
#define BGA_REG_XRES 1
#define BGA_REG_YRES 2
#define BGA_REG_BPP 3
#define BGA_REG_ENABLE 4
#define BGA_REG_VIRT_WIDTH 6
#define BGA_REG_X_OFFSET 8
#define BGA_REG_Y_OFFSET 9
#define BGA_ENABLED 0x01
#define BGA_LFB_ENABLED 0x40

static uint16_t bga_read(uint16_t index) {
    port_outw(BGA_INDEX_PORT, index);
    return port_inw(BGA_DATA_PORT);
}

static void bga_write(uint16_t index, uint16_t value) {
    port_outw(BGA_INDEX_PORT, index);
    port_outw(BGA_DATA_PORT, value);
}

static int bga_probe(const struct video_gpu_info *gpu) {
    uint16_t id;
    if (!VIDEO_X86 || !gpu) return 0;
    if (!((gpu->vendor == 0x1234 && gpu->device == 0x1111) ||
          (gpu->vendor == 0x80EE && gpu->device == 0xBEEF))) return 0;
    id = bga_read(BGA_REG_ID);
    return id >= 0xB0C0 && id <= 0xB0CF;
}

static void default_layout(uint8_t bpp, struct video_surface_info *s) {
    s->red_position = bpp == 16 ? 11 : bpp == 15 ? 10 : 16;
    s->red_size = (bpp == 16 || bpp == 15) ? 5 : 8;
    s->green_position = (bpp >= 24) ? 8 : 5;
    s->green_size = (bpp >= 24) ? 8 : (bpp == 16 ? 6 : 5);
    s->blue_position = 0;
    s->blue_size = (bpp >= 24) ? 8 : 5;
}

static int bga_set_mode(uint32_t width, uint32_t height, uint8_t bpp, struct video_surface_info *out) {
    uint16_t old_enable, old_x, old_y, old_bpp;
    uint64_t base;
    uint32_t bar0, command;
    if (!gpu_present || !out) return 0;
    bar0 = gpu_info.bar0;
    if (bar0 & 1u) return 0;
    base = bar0 & ~0xFu;
    if (((bar0 >> 1) & 3u) == 2u)
        base |= (uint64_t)pci_read32(gpu_info.bus, gpu_info.slot, gpu_info.func, 0x14) << 32;
    if (!base || (sizeof(uintptr_t) < 8 && (base >> 32))) return 0;
    if (width < 320 || height < 200 || width > 0xFFFF || height > 0xFFFF) return 0;
    if (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return 0;

    /* Make sure the device decodes its memory BAR. Only the command half of
     * the register is written back so write-1-to-clear status bits are kept. */
    command = pci_read32(gpu_info.bus, gpu_info.slot, gpu_info.func, 0x04) & 0xFFFFu;
    if (!(command & 2u))
        pci_write32(gpu_info.bus, gpu_info.slot, gpu_info.func, 0x04, command | 2u);

    old_enable = bga_read(BGA_REG_ENABLE);
    old_x = bga_read(BGA_REG_XRES);
    old_y = bga_read(BGA_REG_YRES);
    old_bpp = bga_read(BGA_REG_BPP);

    bga_write(BGA_REG_ENABLE, 0);
    bga_write(BGA_REG_XRES, (uint16_t)width);
    bga_write(BGA_REG_YRES, (uint16_t)height);
    bga_write(BGA_REG_BPP, bpp);
    bga_write(BGA_REG_VIRT_WIDTH, (uint16_t)width);
    bga_write(BGA_REG_X_OFFSET, 0);
    bga_write(BGA_REG_Y_OFFSET, 0);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

    if (bga_read(BGA_REG_XRES) != width || bga_read(BGA_REG_YRES) != height || bga_read(BGA_REG_BPP) != bpp) {
        bga_write(BGA_REG_ENABLE, 0);
        bga_write(BGA_REG_XRES, old_x);
        bga_write(BGA_REG_YRES, old_y);
        bga_write(BGA_REG_BPP, old_bpp);
        bga_write(BGA_REG_VIRT_WIDTH, old_x);
        bga_write(BGA_REG_ENABLE, old_enable);
        return 0;
    }
    {
        uint32_t virt = bga_read(BGA_REG_VIRT_WIDTH);
        if (virt < width) virt = width;
        out->base = (uint8_t *)(uintptr_t)base;
        out->pitch = virt * ((bpp + 7u) >> 3);
        out->width = width;
        out->height = height;
        out->bpp = bpp;
        default_layout(bpp, out);
    }
    return 1;
}

static const struct video_driver driver_bga = {
    "bochs-bga", bga_probe, bga_set_mode, 0, 0, 0
};

static const struct video_driver driver_generic = {
    "generic-linear-fb", 0, 0, 0, 0, 0
};

static void set_active_driver(const struct video_driver *driver) {
    if (driver == active_driver) return;
    vram_sync(); /* drain the old driver's queue before it is replaced */
    active_driver = driver;
}

static void select_driver(void) {
    const struct video_gpu_info *gpu = gpu_present ? &gpu_info : 0;
    for (int i = 0; i < registered_count; i++) {
        const struct video_driver *driver = registered_drivers[i];
        if (!driver->probe || driver->probe(gpu)) {
            set_active_driver(driver);
            return;
        }
    }
    if (driver_bga.probe(gpu)) {
        set_active_driver(&driver_bga);
        return;
    }
    set_active_driver(&driver_generic);
}

int video_register_driver(const struct video_driver *driver) {
    if (!driver || !driver->name) return 0;
    for (int i = 0; i < registered_count; i++)
        if (registered_drivers[i] == driver) {
            select_driver();
            return 1;
        }
    if (registered_count >= VIDEO_MAX_DRIVERS) return 0;
    registered_drivers[registered_count++] = driver;
    select_driver();
    return 1;
}

const char *video_driver_name(void) {
    return active_driver ? active_driver->name : driver_generic.name;
}

const struct video_gpu_info *video_gpu_info(void) {
    return gpu_present ? &gpu_info : 0;
}

/* ------------------------------------------------------------------------- */
/* Surface setup / mode changes                                              */
/* ------------------------------------------------------------------------- */

static int layout_valid(const struct video_surface_info *s, uint8_t bytes) {
    uint32_t bits = (uint32_t)bytes * 8u;
    if (!s->red_size || !s->green_size || !s->blue_size) return 0;
    if (s->red_size > 16 || s->green_size > 16 || s->blue_size > 16) return 0;
    if ((uint32_t)s->red_position + s->red_size > bits || (uint32_t)s->green_position + s->green_size > bits ||
        (uint32_t)s->blue_position + s->blue_size > bits) return 0;
    if ((s->red_position < s->green_position + s->green_size &&
         s->green_position < s->red_position + s->red_size) ||
        (s->red_position < s->blue_position + s->blue_size &&
         s->blue_position < s->red_position + s->red_size) ||
        (s->green_position < s->blue_position + s->blue_size &&
         s->blue_position < s->green_position + s->green_size)) return 0;
    return 1;
}

/* Validates first, then commits, so a rejected surface never leaves the
 * exported state half-updated. Cancels any open frame and clears the screen. */
static int surface_apply(const struct video_surface_info *info) {
    struct video_surface_info s;
    uint8_t bytes;
    uint64_t row_bytes, total;
    if (!info || !info->base || !info->width || !info->height) return 0;
    if (info->bpp != 15 && info->bpp != 16 && info->bpp != 24 && info->bpp != 32) return 0;
    if (info->width > VIDEO_MAX_DIMENSION || info->height > VIDEO_MAX_DIMENSION) return 0;
    bytes = (uint8_t)((info->bpp + 7u) >> 3);
    row_bytes = (uint64_t)info->width * bytes;
    if (info->pitch < row_bytes) return 0;
    total = (uint64_t)info->pitch * info->height;
    if (total > 0xFFFFFFFFull) return 0;
    if (sizeof(uintptr_t) < 8 && (uint64_t)(uintptr_t)info->base + total > 0x100000000ull) return 0;

    s = *info;
    if (!layout_valid(&s, bytes)) default_layout(s.bpp, &s);

    vram_sync(); /* finish pending accelerated work on the old surface */

    video_framebuffer_ready = 0;
    video_frame_active = 0;
    dirty_valid = 0;
    shadow_ready = 0;
    accel_pending = 0;

    fb.base = s.base;
    fb.pitch = s.pitch;
    fb.width = s.width;
    fb.height = s.height;
    fb.bpp = s.bpp;
    fb.bytes = bytes;
    fb.rpos = s.red_position;
    fb.rsize = s.red_size;
    fb.gpos = s.green_position;
    fb.gsize = s.green_size;
    fb.bpos = s.blue_position;
    fb.bsize = s.blue_size;
    fb.xrgb = (bytes == 4 && fb.rpos == 16 && fb.rsize == 8 && fb.gpos == 8 && fb.gsize == 8 &&
               fb.bpos == 0 && fb.bsize == 8);

    for (int i = 0; i < 256; i++) {
        lut_r[i] = pack_channel((uint32_t)i, fb.rpos, fb.rsize);
        lut_g[i] = pack_channel((uint32_t)i, fb.gpos, fb.gsize);
        lut_b[i] = pack_channel((uint32_t)i, fb.bpos, fb.bsize);
    }

    shadow_pitch = fb.width * bytes;
    shadow_ready = shadow_pitch <= VIDEO_BACKBUFFER_BYTES / fb.height;
    for (int i = 0; i < 16; i++) palette_pixel[i] = framebuffer_color(vga_palette[i]);

    video_pixel_width = (int)fb.width;
    video_pixel_height = (int)fb.height;
    video_origin_x = 0;
    video_origin_y = 0;
    video_columns = (int)(fb.width / VIDEO_CELL_WIDTH);
    video_rows = (int)(fb.height / VIDEO_CELL_HEIGHT);
    video_framebuffer_ready = 1;
    video_clear();
    return 1;
}

int video_set_mode(uint32_t width, uint32_t height, uint8_t bpp) {
    struct video_surface_info surface;
    if (!active_driver || !active_driver->set_mode) return 0;
    vram_sync();
    mem_zero(&surface, sizeof surface);
    if (!active_driver->set_mode(width, height, bpp, &surface)) return 0;
    return surface_apply(&surface);
}

void video_init(uint32_t magic, uint32_t multiboot_address) {
    const struct multiboot_info *info;
    struct video_surface_info surface;
    uint64_t row_bytes;
    video_framebuffer_ready = 0;
    video_pixel_width = 0;
    video_pixel_height = 0;
    video_columns = VGA_COLUMNS;
    video_rows = VGA_ROWS;
    video_frame_active = 0;
    dirty_valid = 0;
    shadow_ready = 0;
    accel_pending = 0;

    font_init();
    gpu_present = pci_find_display(&gpu_info);
    select_driver();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || multiboot_address == 0) return;
    info = (const struct multiboot_info *)(uintptr_t)multiboot_address;
    if (!(info->flags & MULTIBOOT_INFO_FRAMEBUFFER) ||
        info->framebuffer_width == 0 || info->framebuffer_height == 0 ||
        (info->framebuffer_bpp != 15 && info->framebuffer_bpp != 16 &&
         info->framebuffer_bpp != 24 && info->framebuffer_bpp != 32) ||
        info->framebuffer_type != 1 || info->framebuffer_address == 0 ||
        (sizeof(uintptr_t) < 8 && (info->framebuffer_address >> 32) != 0)) return;
    row_bytes = (uint64_t)info->framebuffer_width * ((info->framebuffer_bpp + 7u) / 8u);
    if (info->framebuffer_pitch < row_bytes) return;

    surface.base = (uint8_t *)(uintptr_t)info->framebuffer_address;
    surface.pitch = info->framebuffer_pitch;
    surface.width = info->framebuffer_width;
    surface.height = info->framebuffer_height;
    surface.bpp = info->framebuffer_bpp;
    surface.red_position = info->color_info[0];
    surface.red_size = info->color_info[1];
    surface.green_position = info->color_info[2];
    surface.green_size = info->color_info[3];
    surface.blue_position = info->color_info[4];
    surface.blue_size = info->color_info[5];
    surface_apply(&surface);
}

/* ------------------------------------------------------------------------- */
/* Clear / frames                                                            */
/* ------------------------------------------------------------------------- */

static void clear_surface(uint8_t *base, uint32_t pitch) {
    uint32_t row_bytes = fb.width * fb.bytes;
    if (pitch == row_bytes) {
        mem_zero(base, row_bytes * fb.height);
        return;
    }
    for (uint32_t y = 0; y < fb.height; y++) mem_zero(base + (uintptr_t)y * pitch, row_bytes);
}

void video_clear(void) {
    if (!video_framebuffer_ready) {
        volatile uint16_t *text = (volatile uint16_t *)(uintptr_t)VGA_MEMORY;
        for (int i = 0; i < VGA_COLUMNS * VGA_ROWS; i++) text[i] = (uint16_t)((0x07 << 8) | ' ');
        return;
    }
    vram_sync();
    clear_surface(fb.base, fb.pitch);
    if (shadow_ready) clear_surface(video_backbuffer, shadow_pitch);
    dirty_valid = 0;
}

int video_begin_frame(void) {
    if (!video_framebuffer_ready || !shadow_ready) return 0;
    video_frame_x = 0;
    video_frame_y = 0;
    video_frame_width = fb.width;
    video_frame_height = fb.height;
    video_frame_active = 1;
    return 1;
}

int video_begin_region_frame(int x, int y, int width, int height) {
    int64_t right, bottom;
    if (!video_framebuffer_ready || !shadow_ready || width <= 0 || height <= 0) return 0;
    right = (int64_t)x + width;
    bottom = (int64_t)y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > (int64_t)fb.width) right = (int64_t)fb.width;
    if (bottom > (int64_t)fb.height) bottom = (int64_t)fb.height;
    if (right <= x || bottom <= y) return 0;
    video_frame_x = (uint32_t)x;
    video_frame_y = (uint32_t)y;
    video_frame_width = (uint32_t)(right - x);
    video_frame_height = (uint32_t)(bottom - y);
    video_frame_active = 1;
    return 1;
}

void video_present(void) {
    if (!video_frame_active) return;
    if (dirty_valid) {
        flush_rect(dirty_x0, dirty_y0, dirty_x1, dirty_y1);
        dirty_valid = 0;
    }
    video_frame_active = 0;
}

void video_present_region(void) {
    video_present();
}

/* ------------------------------------------------------------------------- */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------- */

void video_fill_rect(int x, int y, int width, int height, uint32_t color, uint8_t alpha) {
    int cx0, cy0, cx1, cy1, unused = 0;
    uint8_t *base;
    uint32_t pitch;
    if (!video_framebuffer_ready || alpha == 0) return;
    get_clip(&cx0, &cy0, &cx1, &cy1);
    if (!clip_axis(&x, &unused, &width, cx0, cx1) || !clip_axis(&y, &unused, &height, cy0, cy1)) return;

    if (alpha == 255) {
        uint32_t pixel = framebuffer_color(color);
        if (!shadow_ready && hw_fill(x, y, width, height, pixel)) return;
        if (!shadow_ready) vram_sync();
        base = surface_base();
        pitch = surface_pitch();
        if (x == 0 && (uint32_t)width * fb.bytes == pitch) {
            fill_pixels(base + (uintptr_t)y * pitch, pixel, (uint32_t)width * (uint32_t)height, fb.bytes);
        } else {
            for (int row = 0; row < height; row++)
                fill_pixels(base + (uintptr_t)(y + row) * pitch + (uintptr_t)x * fb.bytes, pixel,
                            (uint32_t)width, fb.bytes);
        }
        if (shadow_ready && !video_frame_active && hw_fill(x, y, width, height, pixel)) return;
        surface_commit(x, y, x + width, y + height);
        return;
    }
    if (!shadow_ready) vram_sync();
    base = surface_base();
    pitch = surface_pitch();
    for (int row = 0; row < height; row++)
        blend_span(base + (uintptr_t)(y + row) * pitch + (uintptr_t)x * fb.bytes, (uint32_t)width, color, alpha);
    surface_commit(x, y, x + width, y + height);
}

static void surface_copy(uint8_t *base, uint32_t pitch, int sx, int sy, int dx, int dy, int w, int h) {
    uint32_t bytes = (uint32_t)w * fb.bytes;
    int reverse;
    if (bytes == pitch) {
        /* full-width rows are contiguous: one overlap-safe move */
        mem_move(base + (uintptr_t)dy * pitch, base + (uintptr_t)sy * pitch, bytes * (uint32_t)h);
        return;
    }
    reverse = (dy > sy) || (dy == sy && dx > sx);
    for (int i = 0; i < h; i++) {
        int row = reverse ? h - 1 - i : i;
        mem_move(base + (uintptr_t)(dy + row) * pitch + (uintptr_t)dx * fb.bytes,
                 base + (uintptr_t)(sy + row) * pitch + (uintptr_t)sx * fb.bytes, bytes);
    }
}

void video_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height) {
    int cx0, cy0, cx1, cy1;
    if (!video_framebuffer_ready) return;
    get_clip(&cx0, &cy0, &cx1, &cy1);
    if (!clip_axis(&dst_x, &src_x, &width, cx0, cx1) || !clip_axis(&dst_y, &src_y, &height, cy0, cy1)) return;
    if (!clip_axis(&src_x, &dst_x, &width, 0, (int)fb.width) ||
        !clip_axis(&src_y, &dst_y, &height, 0, (int)fb.height)) return;
    if (src_x == dst_x && src_y == dst_y) return;

    if (!shadow_ready) {
        if (hw_copy(src_x, src_y, dst_x, dst_y, width, height)) return;
        vram_sync();
        surface_copy(fb.base, fb.pitch, src_x, src_y, dst_x, dst_y, width, height);
        return;
    }
    surface_copy(video_backbuffer, shadow_pitch, src_x, src_y, dst_x, dst_y, width, height);
    if (video_frame_active) {
        dirty_add(dst_x, dst_y, dst_x + width, dst_y + height);
        return;
    }
    if (hw_copy(src_x, src_y, dst_x, dst_y, width, height)) return;
    flush_rect(dst_x, dst_y, dst_x + width, dst_y + height);
}

void video_blit(int x, int y, int width, int height, const uint32_t *argb, int stride) {
    int cx0, cy0, cx1, cy1, ox = 0, oy = 0;
    uint8_t *base;
    uint32_t pitch;
    if (!video_framebuffer_ready || !argb) return;
    if (stride <= 0) stride = width;
    get_clip(&cx0, &cy0, &cx1, &cy1);
    if (!clip_axis(&x, &ox, &width, cx0, cx1) || !clip_axis(&y, &oy, &height, cy0, cy1)) return;
    if (!shadow_ready) vram_sync();
    base = surface_base();
    pitch = surface_pitch();
    for (int row = 0; row < height; row++) {
        const uint32_t *source = argb + (intptr_t)(oy + row) * stride + ox;
        uint8_t *p = base + (uintptr_t)(y + row) * pitch + (uintptr_t)x * fb.bytes;
        if (fb.xrgb) {
            video_u32 *d = (video_u32 *)p;
            for (int column = 0; column < width; column++) {
                uint32_t value = source[column];
                uint32_t alpha = value >> 24;
                if (alpha == 255u) d[column] = value & 0x00FFFFFFu;
                else if (alpha) d[column] = blend_xrgb(d[column], value, alpha);
            }
            continue;
        }
        for (int column = 0; column < width; column++, p += fb.bytes) {
            uint32_t value = source[column];
            uint8_t alpha = (uint8_t)(value >> 24);
            if (alpha == 0) continue;
            pixel_write(p, alpha == 255 ? framebuffer_color(value) : blend_pixel(pixel_read(p, fb.bytes), value, alpha),
                        fb.bytes);
        }
    }
    surface_commit(x, y, x + width, y + height);
}

void video_scroll(void) {
    if (!video_framebuffer_ready) {
#if VIDEO_TEXT_SCROLL
        volatile uint16_t *text = (volatile uint16_t *)(uintptr_t)VGA_MEMORY;
        mem_copy((void *)(uintptr_t)VGA_MEMORY, (const void *)(uintptr_t)(VGA_MEMORY + VGA_COLUMNS * 2),
                 VGA_COLUMNS * (VGA_ROWS - 1) * 2u);
        for (int column = 0; column < VGA_COLUMNS; column++)
            text[(VGA_ROWS - 1) * VGA_COLUMNS + column] = (uint16_t)((0x07 << 8) | ' ');
#endif
        return;
    }
    {
        int width = video_columns * VIDEO_CELL_WIDTH;
        int height = video_rows * VIDEO_CELL_HEIGHT;
        int x = (int)video_origin_x;
        int y = (int)video_origin_y;
        if (height <= VIDEO_CELL_HEIGHT) {
            video_fill_rect(x, y, width, height, 0x000000, 255);
            return;
        }
        video_copy_rect(x, y + VIDEO_CELL_HEIGHT, x, y, width, height - VIDEO_CELL_HEIGHT);
        video_fill_rect(x, y + height - VIDEO_CELL_HEIGHT, width, VIDEO_CELL_HEIGHT, 0x000000, 255);
    }
}

void video_put_cell(int column, int row, char character, uint8_t color) {
    uint32_t row_pixels[VIDEO_CELL_WIDTH];
    const uint8_t *glyph;
    uint32_t foreground, background;
    int x, y, cx0, cy0, cx1, cy1, ox = 0, oy = 0, width = VIDEO_CELL_WIDTH, height = VIDEO_CELL_HEIGHT;
    uint8_t *base;
    uint32_t pitch;

    if (!video_framebuffer_ready) {
        if (column < 0 || column >= VGA_COLUMNS || row < 0 || row >= VGA_ROWS) return;
        ((volatile uint16_t *)(uintptr_t)VGA_MEMORY)[row * VGA_COLUMNS + column] =
            (uint16_t)((uint16_t)color << 8) | (uint8_t)character;
        return;
    }
    if (column < 0 || column >= video_columns || row < 0 || row >= video_rows) return;

    glyph = font[(uint8_t)character];
    foreground = palette_pixel[color & 0x0F];
    background = palette_pixel[(color >> 4) & 0x0F];

    x = (int)video_origin_x + column * VIDEO_CELL_WIDTH;
    y = (int)video_origin_y + row * VIDEO_CELL_HEIGHT;
    get_clip(&cx0, &cy0, &cx1, &cy1);
    if (!clip_axis(&x, &ox, &width, cx0, cx1) || !clip_axis(&y, &oy, &height, cy0, cy1)) return;
    if (!shadow_ready) vram_sync();
    base = surface_base();
    pitch = surface_pitch();
    for (int r = 0; r < height; r++) {
        int gy = oy + r - GLYPH_TOP;
        uint8_t *dst = base + (uintptr_t)(y + r) * pitch + (uintptr_t)x * fb.bytes;
        uint8_t bits = (gy >= 0 && gy < GLYPH_ROWS) ? glyph[gy] : 0;
        if (!bits) {
            fill_pixels(dst, background, (uint32_t)width, fb.bytes);
            continue;
        }
        for (int gx = 0; gx < VIDEO_CELL_WIDTH; gx++)
            row_pixels[gx] = (gx >= 1 && gx <= 5 && ((bits >> (5 - gx)) & 1)) ? foreground : background;
        write_pixels(dst, row_pixels + ox, (uint32_t)width, fb.bytes);
    }
    surface_commit(x, y, x + width, y + height);
}

void video_put_pixel(int x, int y, uint32_t color, uint8_t alpha) {
    int cx0, cy0, cx1, cy1;
    uint8_t *p;
    uint32_t value;
    if (!video_framebuffer_ready || alpha == 0) return;
    get_clip(&cx0, &cy0, &cx1, &cy1);
    if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) return;
    if (!shadow_ready) vram_sync();
    p = surface_base() + (uintptr_t)y * surface_pitch() + (uintptr_t)x * fb.bytes;
    value = alpha == 255 ? framebuffer_color(color) : blend_pixel(pixel_read(p, fb.bytes), color, alpha);
    pixel_write(p, value, fb.bytes);
    if (video_frame_active) {
        dirty_add(x, y, x + 1, y + 1);
    } else if (shadow_ready) {
        vram_sync();
        pixel_write(fb.base + (uintptr_t)y * fb.pitch + (uintptr_t)x * fb.bytes, value, fb.bytes);
    }
}

/* Buffer format: 4 bytes per pixel holding the raw framebuffer pixel value
 * (zero-extended for 15/16/24 bpp). capture/restore use the same layout. */
void video_capture_region(int x, int y, int width, int height, uint8_t *buffer) {
    const uint8_t *base;
    uint32_t pitch;
    if (!video_framebuffer_ready || buffer == 0 || width <= 0 || height <= 0) return;
    if (!shadow_ready) vram_sync();
    base = surface_base();
    pitch = surface_pitch();
    for (int row = 0; row < height; row++) {
        uint8_t *destination = buffer + (uintptr_t)row * (uint32_t)width * 4u;
        int64_t source_y = (int64_t)y + row;
        int64_t first = 0, last = width;
        if (source_y < 0 || source_y >= (int64_t)fb.height) {
            mem_zero(destination, (uint32_t)width * 4u);
            continue;
        }
        if (x < 0) first = -(int64_t)x;
        if ((int64_t)x + width > (int64_t)fb.width) last = (int64_t)fb.width - x;
        if (first >= last || first >= width || last <= 0) {
            mem_zero(destination, (uint32_t)width * 4u);
            continue;
        }
        if (first > 0) mem_zero(destination, (uint32_t)first * 4u);
        if (last < width) mem_zero(destination + (uintptr_t)last * 4u, (uint32_t)(width - last) * 4u);
        {
            const uint8_t *source = base + (uintptr_t)source_y * pitch + (uintptr_t)((int64_t)x + first) * fb.bytes;
            uint8_t *out = destination + (uintptr_t)first * 4u;
            if (fb.bytes == 4) {
                mem_copy(out, source, (uint32_t)(last - first) * 4u);
            } else {
                for (int64_t column = first; column < last; column++, source += fb.bytes, out += 4)
                    *(video_u32 *)out = pixel_read(source, fb.bytes);
            }
        }
    }
}

void video_restore_region(int x, int y, int width, int height, const uint8_t *buffer) {
    int cx0, cy0, cx1, cy1, ox = 0, oy = 0, full_width = width;
    uint8_t *base;
    uint32_t pitch;
    if (!video_framebuffer_ready || buffer == 0 || width <= 0 || height <= 0) return;
    get_clip(&cx0, &cy0, &cx1, &cy1);
    if (!clip_axis(&x, &ox, &width, cx0, cx1) || !clip_axis(&y, &oy, &height, cy0, cy1)) return;
    if (!shadow_ready) vram_sync();
    base = surface_base();
    pitch = surface_pitch();
    for (int row = 0; row < height; row++) {
        uint8_t *destination = base + (uintptr_t)(y + row) * pitch + (uintptr_t)x * fb.bytes;
        const uint8_t *source = buffer + ((uintptr_t)(oy + row) * (uint32_t)full_width + (uint32_t)ox) * 4u;
        if (fb.bytes == 4) {
            mem_copy(destination, source, (uint32_t)width * 4u);
        } else {
            for (int column = 0; column < width; column++, destination += fb.bytes, source += 4)
                pixel_write(destination, *(const video_u32 *)source, fb.bytes);
        }
    }
    surface_commit(x, y, x + width, y + height);
}