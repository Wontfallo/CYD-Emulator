/*
 * cyd_core - the CYD board wrapped around an emulated ESP32.
 *
 * libqemu-xtensa.dll (QEMU, machine "esp32-picsimlab") executes the real firmware image.
 * This file is everything soldered to the ESP32 on a Cheap Yellow Display:
 *   - ILI9341 / ST7789 TFT controller on a hardware SPI bus (MIPI DCS command decoder + GRAM)
 *   - XPT2046 resistive touch controller (bit-banged GPIO *or* hardware SPI, both decoded)
 *   - GPIO32..39 output registers, which this QEMU build does not model
 *   - UART byte queues, paced at the firmware's baud rate in virtual time
 *   - a virtual-time event queue (touch, buttons, ADC, reset) so input timing is deterministic
 *
 * Nothing here knows about any particular firmware.
 */
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define API __declspec(dllexport)

/* ------------------------------------------------------------------ config */

typedef struct {
    int32_t panel_w, panel_h;      /* native (portrait) size, e.g. 240 x 320 */
    int32_t controller;            /* 0 = ILI9341, 1 = ST7789 */
    int32_t gram_mirror_x;         /* 1 if GRAM column 0 is at the viewer's right at MADCTL=0 (ILI9341) */
    int32_t panel_bgr;             /* 1 if the colour filter is BGR (firmware must set MADCTL.BGR) */
    int32_t panel_inverted;        /* 1 if the panel needs INVON to look normal (IPS) */
    int32_t blank_white;           /* 1 if an undriven panel shows white (TN), 0 for black */
    int32_t tft_dc, tft_cs, tft_rst;
    int32_t touch_clk, touch_mosi, touch_miso, touch_cs, touch_irq;
    int32_t reserved[8];
} cyd_config_t;

static cyd_config_t g_cfg;

/* ------------------------------------------------------------------ QEMU imports */

typedef struct {
    void (*write_pin)(int pin, int value);
    void (*dir_pin)(int pin, int value);
    int (*i2c_event)(const uint8_t id, const uint8_t addr, const uint16_t event);
    uint8_t (*spi_event)(const uint8_t id, const uint16_t event);
    void (*uart_tx_event)(const uint8_t id, const uint8_t value);
    const short int *pinmap;
    void (*rmt_event)(const uint8_t channel, const uint32_t config0, const uint32_t value);
} callbacks_t;

typedef struct {
    uint64_t (*read)(void *opaque, uint64_t addr, unsigned size);
    void (*write)(void *opaque, uint64_t addr, uint64_t data, unsigned size);
    void *read_with_attrs;
    void *write_with_attrs;
    int endianness;
    struct { unsigned min_access_size, max_access_size; bool unaligned; void *accepts; } valid;
    struct { unsigned min_access_size, max_access_size; bool unaligned; } impl;
} MemoryRegionOps;

static void (*q_qemu_init)(int, char **, const char **);
static void (*q_qemu_main_loop)(void);
static void (*q_register_callbacks)(void *);
static void (*q_set_pin)(int, int);
static void (*q_set_apin)(int, int);
static void (*q_uart_receive)(int, const uint8_t *, int);
static uint32_t *(*q_get_internals)(int);
static int64_t (*q_clock_get_ns)(int);
static void (*q_timer_init_full)(void *, void *, int, int, int, void (*)(void *), void *);
static void (*q_timer_mod_ns)(void *, int64_t);
static void *(*q_get_system_memory)(void);
static void (*q_memory_region_init_io)(void *, void *, const MemoryRegionOps *, void *, const char *, uint64_t);
static void (*q_memory_region_add_subregion_overlap)(void *, uint64_t, void *, int);
static void (*q_qmp_system_reset)(void **);
static void (*q_qmp_stop)(void **);
static void (*q_qmp_cont)(void **);
static int (*q_flash_dump)(int64_t, void *, int);

#define QEMU_CLOCK_VIRTUAL 1
#define NGPIO 40

/* ------------------------------------------------------------------ state */

static SRWLOCK g_lock = SRWLOCK_INIT;
static volatile LONG g_started = 0, g_exited = 0;
static char g_error[512];

static volatile int64_t g_virt_ns = 0;
static volatile int64_t g_ticks = 0;

static uint8_t g_out[NGPIO];        /* last level driven by the firmware */
static uint8_t g_dir[NGPIO];        /* 1 = output enabled */
static uint8_t g_in[NGPIO];         /* level the board presents on the pin */
static uint32_t g_pin_writes[NGPIO];
static uint32_t g_out1, g_enable1;  /* GPIO32..39 registers we model ourselves */

/* --- display ------------------------------------------------------------- */
#define GRAM_MAX_W 320
#define GRAM_MAX_H 480
static uint32_t g_gram[GRAM_MAX_W * GRAM_MAX_H];   /* 0x00C1C2C3, C1 = first component sent */

typedef struct {
    uint8_t cmd;
    int argi;
    uint8_t args[16];
    uint16_t x0, x1, y0, y1;
    uint16_t cx, cy;
    uint8_t madctl, colmod;
    uint8_t view_madctl;
    bool view_latched;
    bool inv, on, sleep;
    uint16_t tfa, vsa, bfa, vsp;
    uint8_t pix[3];
    int pixn;
    /* read-back */
    uint8_t rd[8];
    int rdlen, rdpos;
    bool ramrd;
    int ramrd_phase;
    uint32_t ramrd_px;
    uint8_t brightness;
} tft_t;
static tft_t g_tft;

static volatile LONG64 g_pixels = 0;        /* pixels ever written */
static volatile LONG64 g_draw_seq = 0;      /* bumped whenever the visible image may have changed */
static volatile LONG64 g_tft_cmds = 0;
static volatile LONG64 g_tft_bytes = 0;
static volatile LONG64 g_last_draw_tick = 0;    /* GetTickCount64 */
static volatile int64_t g_last_draw_virt_ns = 0;
static volatile LONG64 g_spi_bytes[2];
static volatile LONG64 g_spi_unrouted = 0;
static int g_hwcs[2];                       /* hardware chip-select active per bus */

/* --- touch --------------------------------------------------------------- */
typedef struct {
    bool down;
    uint16_t ypos, xpos, z1, z2;    /* channel 0x90, 0xD0, 0xB0, 0xC0 */
    /* serial engine */
    int cmd_bits;
    uint8_t cmd_shift;
    uint16_t out_reg;
    int out_pos;
    int miso;
    uint32_t conversions;
    uint32_t bb_edges, hw_bytes;
} xpt_t;
static xpt_t g_xpt;

/* --- uart ---------------------------------------------------------------- */
#define UART_TX_CAP (1 << 20)
#define UART_RX_CAP (1 << 18)
static uint8_t g_tx[3][UART_TX_CAP];
static uint32_t g_tx_head[3], g_tx_tail[3];
static uint64_t g_tx_total[3], g_tx_dropped[3];
static uint8_t g_rx[UART_RX_CAP];
static uint32_t g_rx_head, g_rx_tail;
static double g_rx_credit;

/* --- events -------------------------------------------------------------- */
enum { EV_NONE, EV_TOUCH, EV_PIN, EV_ADC, EV_RESET, EV_PAUSE, EV_RESUME };
typedef struct { int type; int64_t due_ns; int a, b, c, d, e; uint64_t seq; } event_t;
#define EV_CAP 512
static event_t g_ev[EV_CAP];
static uint64_t g_ev_seq;
static volatile LONG g_ev_pending = 0;
static volatile LONG g_resets = 0;

static uint8_t g_timer[256];
static uint8_t g_mr[2][8192];

/* ------------------------------------------------------------------ helpers */

static void set_input(int gpio, int level)
{
    if (gpio < 0 || gpio >= NGPIO) return;
    g_in[gpio] = (uint8_t)(level ? 1 : 0);
    if (q_set_pin) q_set_pin(gpio + 1, level ? 1 : 0);
}

static inline int pin_out(int gpio, int dflt)
{
    if (gpio < 0 || gpio >= NGPIO) return dflt;
    if (!g_dir[gpio] && !g_pin_writes[gpio]) return dflt;
    return g_out[gpio];
}

/* ------------------------------------------------------------------ display */

static uint32_t snow(uint32_t i)
{
    /* what an uninitialised GRAM looks like: deterministic noise */
    uint32_t x = i * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    uint32_t v = 0x58 + (x & 0x7f);
    return (v << 16) | (v << 8) | v;
}

static void tft_power_on_reset(void)
{
    memset(&g_tft, 0, sizeof g_tft);
    g_tft.x1 = (uint16_t)(g_cfg.panel_w - 1);
    g_tft.y1 = (uint16_t)(g_cfg.panel_h - 1);
    g_tft.colmod = 0x66;
    g_tft.sleep = true;
    g_tft.on = false;
    g_tft.vsa = (uint16_t)g_cfg.panel_h;
    InterlockedIncrement64(&g_draw_seq);
}

static void tft_soft_reset(void)
{
    uint8_t view = g_tft.view_madctl;
    bool latched = g_tft.view_latched;
    tft_power_on_reset();
    g_tft.view_madctl = view;
    g_tft.view_latched = latched;
}

static inline void tft_map(uint8_t madctl, int x, int y, int *c, int *r)
{
    int cc, rr;
    if (madctl & 0x20) { cc = y; rr = x; } else { cc = x; rr = y; }
    if (madctl & 0x40) cc = g_cfg.panel_w - 1 - cc;
    if (madctl & 0x80) rr = g_cfg.panel_h - 1 - rr;
    *c = cc; *r = rr;
}

static void tft_touch_draw(void)
{
    InterlockedIncrement64(&g_draw_seq);
    g_last_draw_tick = (LONG64)GetTickCount64();
    g_last_draw_virt_ns = g_virt_ns;
}

static void tft_put_pixel(uint32_t px)
{
    tft_t *t = &g_tft;
    int lw = (t->madctl & 0x20) ? g_cfg.panel_h : g_cfg.panel_w;
    int lh = (t->madctl & 0x20) ? g_cfg.panel_w : g_cfg.panel_h;
    if (t->cx < lw && t->cy < lh) {
        int c, r;
        tft_map(t->madctl, t->cx, t->cy, &c, &r);
        g_gram[r * GRAM_MAX_W + c] = px;
    }
    InterlockedIncrement64(&g_pixels);
    if (++t->cx > t->x1) {
        t->cx = t->x0;
        if (++t->cy > t->y1) t->cy = t->y0;
    }
}

static uint32_t tft_get_pixel_advance(void)
{
    tft_t *t = &g_tft;
    uint32_t px = 0;
    int lw = (t->madctl & 0x20) ? g_cfg.panel_h : g_cfg.panel_w;
    int lh = (t->madctl & 0x20) ? g_cfg.panel_w : g_cfg.panel_h;
    if (t->cx < lw && t->cy < lh) {
        int c, r;
        tft_map(t->madctl, t->cx, t->cy, &c, &r);
        px = g_gram[r * GRAM_MAX_W + c];
    }
    if (++t->cx > t->x1) {
        t->cx = t->x0;
        if (++t->cy > t->y1) t->cy = t->y0;
    }
    return px;
}

static void tft_set_read(const uint8_t *bytes, int n)
{
    /* SPI read-back: one dummy byte, then the parameters */
    g_tft.rd[0] = 0;
    memcpy(g_tft.rd + 1, bytes, (size_t)n);
    g_tft.rdlen = n + 1;
    g_tft.rdpos = 0;
}

static void tft_command(uint8_t cmd)
{
    tft_t *t = &g_tft;
    t->cmd = cmd;
    t->argi = 0;
    t->pixn = 0;
    t->rdlen = t->rdpos = 0;
    t->ramrd = false;
    InterlockedIncrement64(&g_tft_cmds);
    switch (cmd) {
    case 0x01: tft_soft_reset(); break;
    case 0x10: t->sleep = true; tft_touch_draw(); break;
    case 0x11: t->sleep = false; tft_touch_draw(); break;
    case 0x20: t->inv = false; tft_touch_draw(); break;
    case 0x21: t->inv = true; tft_touch_draw(); break;
    case 0x28: t->on = false; tft_touch_draw(); break;
    case 0x29: t->on = true; tft_touch_draw(); break;
    case 0x2C:
        t->cx = t->x0; t->cy = t->y0;
        {
            int area = (t->x1 - t->x0 + 1) * (t->y1 - t->y0 + 1);
            if (!t->view_latched || area * 8 >= g_cfg.panel_w * g_cfg.panel_h) {
                t->view_madctl = t->madctl;
                t->view_latched = true;
            }
        }
        tft_touch_draw();
        break;
    case 0x3C: tft_touch_draw(); break;
    case 0x2E:
        t->cx = t->x0; t->cy = t->y0;
        t->ramrd = true; t->ramrd_phase = -1;
        break;
    case 0x04: {
        static const uint8_t ili[3] = {0x00, 0x93, 0x41}, st[3] = {0x85, 0x85, 0x52};
        tft_set_read(g_cfg.controller ? st : ili, 3);
        break;
    }
    case 0x09: {
        uint8_t b[4] = {0, 0, 0, 0};
        b[0] = (uint8_t)((t->madctl & 0xFC) >> 1);
        b[1] = (uint8_t)(((t->colmod & 7) << 4) | (t->sleep ? 0 : 0x02) | 0x01);
        b[2] = (uint8_t)((t->inv ? 0x20 : 0) | (t->on ? 0x04 : 0));
        tft_set_read(b, 4);
        break;
    }
    case 0x0A: { uint8_t b = (uint8_t)(0x08 | (t->sleep ? 0 : 0x10) | (t->on ? 0x04 : 0) | 0x80); tft_set_read(&b, 1); break; }
    case 0x0B: tft_set_read(&t->madctl, 1); break;
    case 0x0C: tft_set_read(&t->colmod, 1); break;
    case 0x52: tft_set_read(&t->brightness, 1); break;
    case 0xDA: { uint8_t b = g_cfg.controller ? 0x85 : 0x00; tft_set_read(&b, 1); break; }
    case 0xDB: { uint8_t b = g_cfg.controller ? 0x85 : 0x93; tft_set_read(&b, 1); break; }
    case 0xDC: { uint8_t b = g_cfg.controller ? 0x52 : 0x41; tft_set_read(&b, 1); break; }
    case 0xD3: {
        static const uint8_t ili[3] = {0x00, 0x93, 0x41}, st[3] = {0x00, 0x77, 0x89};
        tft_set_read(g_cfg.controller ? st : ili, 3);
        break;
    }
    default: break;
    }
}

static uint8_t tft_data(uint8_t b)
{
    tft_t *t = &g_tft;
    uint8_t out = 0;

    if (t->ramrd) {
        /* dummy byte, then R,G,B per pixel (always 18-bit on read) */
        if (t->ramrd_phase < 0) { t->ramrd_phase = 0; return 0; }
        if (t->ramrd_phase == 0) t->ramrd_px = tft_get_pixel_advance();
        out = (uint8_t)(t->ramrd_px >> (16 - 8 * t->ramrd_phase));
        t->ramrd_phase = (t->ramrd_phase + 1) % 3;
        return out & 0xFC;
    }
    if (t->rdpos < t->rdlen) out = t->rd[t->rdpos++];

    switch (t->cmd) {
    case 0x2C:
    case 0x3C: {
        int bpp = t->colmod & 7;
        t->pix[t->pixn++] = b;
        if (bpp == 6 || bpp == 7) {
            if (t->pixn == 3) {
                t->pixn = 0;
                tft_put_pixel(((uint32_t)(t->pix[0] & 0xFC) << 16) | ((uint32_t)(t->pix[1] & 0xFC) << 8) | (t->pix[2] & 0xFC));
            }
        } else if (t->pixn == 2) {
            uint32_t v = ((uint32_t)t->pix[0] << 8) | t->pix[1];
            uint32_t c1 = (v >> 11) & 0x1F, c2 = (v >> 5) & 0x3F, c3 = v & 0x1F;
            t->pixn = 0;
            tft_put_pixel((((c1 << 3) | (c1 >> 2)) << 16) | (((c2 << 2) | (c2 >> 4)) << 8) | ((c3 << 3) | (c3 >> 2)));
        }
        return out;
    }
    default: break;
    }

    if (t->argi < (int)sizeof t->args) t->args[t->argi] = b;
    t->argi++;
    switch (t->cmd) {
    case 0x2A:
        if (t->argi == 4) {
            t->x0 = (uint16_t)((t->args[0] << 8) | t->args[1]);
            t->x1 = (uint16_t)((t->args[2] << 8) | t->args[3]);
        }
        break;
    case 0x2B:
        if (t->argi == 4) {
            t->y0 = (uint16_t)((t->args[0] << 8) | t->args[1]);
            t->y1 = (uint16_t)((t->args[2] << 8) | t->args[3]);
        }
        break;
    case 0x36:
        if (t->argi == 1) {
            t->madctl = b;
            if (!t->view_latched) t->view_madctl = b;
            tft_touch_draw();
        }
        break;
    case 0x3A: if (t->argi == 1) t->colmod = b; break;
    case 0x33:
        if (t->argi == 6) {
            t->tfa = (uint16_t)((t->args[0] << 8) | t->args[1]);
            t->vsa = (uint16_t)((t->args[2] << 8) | t->args[3]);
            t->bfa = (uint16_t)((t->args[4] << 8) | t->args[5]);
        }
        break;
    case 0x37:
        if (t->argi == 2) { t->vsp = (uint16_t)((t->args[0] << 8) | t->args[1]); tft_touch_draw(); }
        break;
    case 0x51: if (t->argi == 1) t->brightness = b; break;
    default: break;
    }
    return out;
}

/* ------------------------------------------------------------------ XPT2046 */

static uint16_t xpt_sample(uint8_t cmd)
{
    xpt_t *x = &g_xpt;
    x->conversions++;
    switch ((cmd >> 4) & 7) {
    case 1: return x->down ? x->ypos : 4095;
    case 5: return x->down ? x->xpos : 0;
    case 3: return x->down ? x->z1 : 0;
    case 4: return x->down ? x->z2 : 4095;
    case 0: return 1870;      /* temp0 */
    case 7: return 2180;      /* temp1 */
    case 2: return 1024;      /* vbat  */
    default: return 0;
    }
}

static void xpt_deselect(void)
{
    g_xpt.cmd_bits = 0;
    g_xpt.out_pos = -1;
}

static void xpt_rising(int mosi)
{
    xpt_t *x = &g_xpt;
    if (x->cmd_bits == 0) {
        if (mosi) { x->cmd_shift = 1; x->cmd_bits = 1; }
        return;
    }
    x->cmd_shift = (uint8_t)((x->cmd_shift << 1) | (mosi ? 1 : 0));
    if (++x->cmd_bits == 8) {
        uint16_t v = xpt_sample(x->cmd_shift);
        x->out_reg = (x->cmd_shift & 0x08) ? (uint16_t)((v >> 4) << 7) : (uint16_t)(v << 3);
        x->out_pos = 15;
        x->cmd_bits = 0;
    }
}

static int xpt_falling(void)
{
    xpt_t *x = &g_xpt;
    if (x->out_pos >= 0) { x->miso = (x->out_reg >> x->out_pos) & 1; x->out_pos--; }
    else x->miso = 0;
    return x->miso;
}

static uint8_t xpt_byte(uint8_t in)
{
    uint8_t out = 0;
    g_xpt.hw_bytes++;
    for (int i = 7; i >= 0; i--) {
        out = (uint8_t)((out << 1) | (g_xpt.miso & 1));
        xpt_rising((in >> i) & 1);
        xpt_falling();
    }
    return out;
}

/* ------------------------------------------------------------------ pins */

static void on_pin(int gpio, int level)
{
    if (gpio < 0 || gpio >= NGPIO) return;
    int old = g_out[gpio];
    g_out[gpio] = (uint8_t)(level ? 1 : 0);
    g_pin_writes[gpio]++;

    if (gpio == g_cfg.touch_clk && old != g_out[gpio]) {
        if (pin_out(g_cfg.touch_cs, 0) == 0) {
            g_xpt.bb_edges++;
            if (level) xpt_rising(pin_out(g_cfg.touch_mosi, 0));
            else {
                int before = g_xpt.miso;
                int m = xpt_falling();
                if (m != before || g_in[g_cfg.touch_miso] != m) set_input(g_cfg.touch_miso, m);
            }
        }
    } else if (gpio == g_cfg.touch_cs) {
        if (level) xpt_deselect();
    } else if (gpio == g_cfg.tft_rst) {
        if (!level) tft_soft_reset();
    }
}

static void cb_write_pin(int pin, int value) { on_pin(pin - 1, value); }

static void cb_dir_pin(int pin, int value)
{
    if (pin >= 1 && pin <= NGPIO) g_dir[pin - 1] = (uint8_t)(value ? 1 : 0);
}

static int cb_i2c(const uint8_t id, const uint8_t addr, const uint16_t event)
{
    (void)id; (void)addr; (void)event;
    return 0;
}

static uint8_t cb_spi(const uint8_t id, const uint16_t event)
{
    int bus = id & 1;
    if ((event & 0xFF) == 1) {
        /* hardware chip-select: bit 8 = level, bits 9+ = line */
        int level = (event >> 8) & 1;
        g_hwcs[bus] = !level;
        return 0;
    }
    uint8_t b = (uint8_t)(event >> 8);
    InterlockedIncrement64(&g_spi_bytes[bus]);

    int tft_cs = pin_out(g_cfg.tft_cs, 1);
    int tch_cs = pin_out(g_cfg.touch_cs, 1);

    if (g_cfg.touch_cs >= 0 && tch_cs == 0 && tft_cs != 0) return xpt_byte(b);
    if (tft_cs == 0 || g_hwcs[bus]) {
        InterlockedIncrement64(&g_tft_bytes);
        if (pin_out(g_cfg.tft_dc, 1) == 0) { tft_command(b); return 0; }
        return tft_data(b);
    }
    InterlockedIncrement64(&g_spi_unrouted);
    return 0xFF;
}

static void cb_uart_tx(const uint8_t id, const uint8_t value)
{
    if (id > 2) return;
    AcquireSRWLockExclusive(&g_lock);
    uint32_t next = (g_tx_head[id] + 1) % UART_TX_CAP;
    if (next == g_tx_tail[id]) { g_tx_tail[id] = (g_tx_tail[id] + 1) % UART_TX_CAP; g_tx_dropped[id]++; }
    g_tx[id][g_tx_head[id]] = value;
    g_tx_head[id] = next;
    g_tx_total[id]++;
    ReleaseSRWLockExclusive(&g_lock);
}

static void cb_rmt(const uint8_t channel, const uint32_t config0, const uint32_t value)
{
    (void)channel; (void)config0; (void)value;
}

/* GPIO32..39 output registers (QEMU's model stops at GPIO31) */
static uint64_t gpio1_read(void *opaque, uint64_t addr, unsigned size)
{
    (void)size;
    if ((intptr_t)opaque == 0) return addr == 0 ? g_out1 : 0;
    return addr == 0 ? g_enable1 : 0;
}

static void gpio1_write(void *opaque, uint64_t addr, uint64_t val, unsigned size)
{
    (void)size;
    uint32_t v = (uint32_t)val & 0xFF;
    uint32_t old_out = g_out1, old_en = g_enable1;
    if ((intptr_t)opaque == 0) {
        if (addr == 0) g_out1 = v;
        else if (addr == 4) g_out1 |= v;
        else if (addr == 8) g_out1 &= ~v;
    } else {
        if (addr == 0) g_enable1 = v;
        else if (addr == 4) g_enable1 |= v;
        else if (addr == 8) g_enable1 &= ~v;
    }
    uint32_t diff = (g_out1 ^ old_out) | (g_enable1 ^ old_en);
    for (int i = 0; i < 8; i++) {
        if (!(diff & (1u << i))) continue;
        int gpio = 32 + i;
        g_dir[gpio] = (uint8_t)((g_enable1 >> i) & 1);
        if (g_enable1 & (1u << i)) {
            int level = (g_out1 >> i) & 1;
            on_pin(gpio, level);
            if (q_set_pin) q_set_pin(gpio + 1, level);
        }
    }
}

static const MemoryRegionOps g_gpio1_ops = { gpio1_read, gpio1_write, NULL, NULL, 0, {0}, {0} };

/* ------------------------------------------------------------------ virtual-time tick */

static void apply_event(const event_t *e)
{
    switch (e->type) {
    case EV_TOUCH:
        g_xpt.down = e->a != 0;
        g_xpt.ypos = (uint16_t)e->b;
        g_xpt.xpos = (uint16_t)e->c;
        g_xpt.z1 = (uint16_t)e->d;
        g_xpt.z2 = (uint16_t)e->e;
        if (g_cfg.touch_irq >= 0) set_input(g_cfg.touch_irq, g_xpt.down ? 0 : 1);
        break;
    case EV_PIN: set_input(e->a, e->b); break;
    case EV_ADC: if (q_set_apin) q_set_apin(e->a, e->b); break;
    case EV_RESET:
        tft_soft_reset();
        xpt_deselect();
        InterlockedIncrement(&g_resets);
        if (q_qmp_system_reset) q_qmp_system_reset(NULL);
        break;
    default: break;
    }
}

static void tick(void *opaque)
{
    (void)opaque;
    int64_t now = q_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    g_virt_ns = now;
    g_ticks++;

    if (g_ev_pending) {
        event_t due[EV_CAP];
        int n = 0;
        AcquireSRWLockExclusive(&g_lock);
        for (;;) {
            int best = -1;
            for (int i = 0; i < EV_CAP; i++) {
                if (g_ev[i].type == EV_NONE || g_ev[i].due_ns > now) continue;
                if (best < 0 || g_ev[i].due_ns < g_ev[best].due_ns ||
                    (g_ev[i].due_ns == g_ev[best].due_ns && g_ev[i].seq < g_ev[best].seq)) best = i;
            }
            if (best < 0) break;
            due[n++] = g_ev[best];
            g_ev[best].type = EV_NONE;
            InterlockedDecrement(&g_ev_pending);
        }
        ReleaseSRWLockExclusive(&g_lock);
        for (int i = 0; i < n; i++) apply_event(&due[i]);
    }

    if (g_rx_head != g_rx_tail) {
        uint32_t baud = 115200;
        uint32_t *pb = q_get_internals(7);
        if (pb && *pb >= 300 && *pb <= 4000000) baud = *pb;
        g_rx_credit += baud / 10000.0;       /* bytes per millisecond */
        if (g_rx_credit > 64) g_rx_credit = 64;
        uint8_t buf[64];
        int n = 0;
        AcquireSRWLockExclusive(&g_lock);
        while (g_rx_credit >= 1.0 && g_rx_head != g_rx_tail && n < (int)sizeof buf) {
            buf[n++] = g_rx[g_rx_tail];
            g_rx_tail = (g_rx_tail + 1) % UART_RX_CAP;
            g_rx_credit -= 1.0;
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (n) q_uart_receive(0, buf, n);
    } else {
        g_rx_credit = 0;
    }

    q_timer_mod_ns(g_timer, now + 1000000);
}

/* ------------------------------------------------------------------ lifecycle */

typedef struct { int argc; char **argv; } start_args_t;
static const short g_pinmap[NGPIO + 1] = {
    NGPIO, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39
};
static callbacks_t g_callbacks;
static uint8_t g_in_default[NGPIO];

static DWORD WINAPI qemu_thread(LPVOID p)
{
    start_args_t *a = (start_args_t *)p;
    q_qemu_init(a->argc, a->argv, NULL);

    /* machine exists, BQL held: add what the board needs */
    void *sysmem = q_get_system_memory();
    q_memory_region_init_io(g_mr[0], NULL, &g_gpio1_ops, (void *)(intptr_t)0, "cyd.gpio.out1", 0xC);
    q_memory_region_add_subregion_overlap(sysmem, 0x3FF44010, g_mr[0], 10);
    q_memory_region_init_io(g_mr[1], NULL, &g_gpio1_ops, (void *)(intptr_t)1, "cyd.gpio.enable1", 0xC);
    q_memory_region_add_subregion_overlap(sysmem, 0x3FF4402C, g_mr[1], 10);

    for (int i = 0; i < NGPIO; i++) set_input(i, g_in_default[i]);

    q_timer_init_full(g_timer, NULL, QEMU_CLOCK_VIRTUAL, 1, 0, tick, NULL);
    q_timer_mod_ns(g_timer, q_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);

    InterlockedExchange(&g_started, 1);
    q_qemu_main_loop();
    InterlockedExchange(&g_exited, 1);
    return 0;
}

#define QSYM(var, name) do { *(void **)&(var) = (void *)GetProcAddress(hq, name); \
    if (!(var)) { snprintf(g_error, sizeof g_error, "QEMU symbol missing: %s", name); return -2; } } while (0)

API int cyd_start(const cyd_config_t *cfg, const uint8_t *input_defaults, int argc, char **argv)
{
    static start_args_t args;
    HMODULE hq = GetModuleHandleA("libqemu-xtensa.dll");
    if (!hq) { snprintf(g_error, sizeof g_error, "libqemu-xtensa.dll is not loaded"); return -1; }
    if (g_started) { snprintf(g_error, sizeof g_error, "already started"); return -3; }

    QSYM(q_qemu_init, "qemu_init");
    QSYM(q_qemu_main_loop, "qemu_main_loop");
    QSYM(q_register_callbacks, "qemu_picsimlab_register_callbacks");
    QSYM(q_set_pin, "qemu_picsimlab_set_pin");
    QSYM(q_set_apin, "qemu_picsimlab_set_apin");
    QSYM(q_uart_receive, "qemu_picsimlab_uart_receive");
    QSYM(q_get_internals, "qemu_picsimlab_get_internals");
    QSYM(q_flash_dump, "qemu_picsimlab_flash_dump");
    QSYM(q_clock_get_ns, "qemu_clock_get_ns");
    QSYM(q_timer_init_full, "timer_init_full");
    QSYM(q_timer_mod_ns, "timer_mod_ns");
    QSYM(q_get_system_memory, "get_system_memory");
    QSYM(q_memory_region_init_io, "memory_region_init_io");
    QSYM(q_memory_region_add_subregion_overlap, "memory_region_add_subregion_overlap");
    QSYM(q_qmp_system_reset, "qmp_system_reset");
    QSYM(q_qmp_stop, "qmp_stop");
    QSYM(q_qmp_cont, "qmp_cont");

    g_cfg = *cfg;
    if (g_cfg.panel_w <= 0 || g_cfg.panel_w > GRAM_MAX_W || g_cfg.panel_h <= 0 || g_cfg.panel_h > GRAM_MAX_H) {
        snprintf(g_error, sizeof g_error, "unsupported panel size %dx%d", g_cfg.panel_w, g_cfg.panel_h);
        return -4;
    }
    for (uint32_t i = 0; i < GRAM_MAX_W * GRAM_MAX_H; i++) g_gram[i] = snow(i);
    tft_power_on_reset();
    xpt_deselect();
    memcpy(g_in_default, input_defaults, NGPIO);
    memcpy(g_in, input_defaults, NGPIO);

    g_callbacks.write_pin = cb_write_pin;
    g_callbacks.dir_pin = cb_dir_pin;
    g_callbacks.i2c_event = cb_i2c;
    g_callbacks.spi_event = cb_spi;
    g_callbacks.uart_tx_event = cb_uart_tx;
    g_callbacks.pinmap = g_pinmap;
    g_callbacks.rmt_event = cb_rmt;
    q_register_callbacks(&g_callbacks);

    args.argc = argc;
    args.argv = argv;
    HANDLE h = CreateThread(NULL, 64 * 1024 * 1024, qemu_thread, &args, 0, NULL);
    if (!h) { snprintf(g_error, sizeof g_error, "CreateThread failed"); return -5; }
    CloseHandle(h);
    return 0;
}

API int cyd_started(void) { return g_started; }
API int cyd_exited(void) { return g_exited; }
API const char *cyd_error(void) { return g_error; }
API int64_t cyd_virtual_ns(void) { return g_virt_ns; }

/* ------------------------------------------------------------------ host API */

API int cyd_post_event(int delay_ms, int type, int a, int b, int c, int d, int e)
{
    int ok = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < EV_CAP; i++) {
        if (g_ev[i].type != EV_NONE) continue;
        g_ev[i].type = type;
        g_ev[i].due_ns = g_virt_ns + (int64_t)delay_ms * 1000000;
        g_ev[i].a = a; g_ev[i].b = b; g_ev[i].c = c; g_ev[i].d = d; g_ev[i].e = e;
        g_ev[i].seq = ++g_ev_seq;
        InterlockedIncrement(&g_ev_pending);
        ok = 1;
        break;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return ok;
}

API int cyd_events_pending(void) { return g_ev_pending; }

API int cyd_uart_read(int id, uint8_t *buf, int max)
{
    int n = 0;
    if (id < 0 || id > 2) return 0;
    AcquireSRWLockExclusive(&g_lock);
    while (n < max && g_tx_tail[id] != g_tx_head[id]) {
        buf[n++] = g_tx[id][g_tx_tail[id]];
        g_tx_tail[id] = (g_tx_tail[id] + 1) % UART_TX_CAP;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return n;
}

API int cyd_uart_write(const uint8_t *buf, int n)
{
    int w = 0;
    AcquireSRWLockExclusive(&g_lock);
    while (w < n) {
        uint32_t next = (g_rx_head + 1) % UART_RX_CAP;
        if (next == g_rx_tail) break;
        g_rx[g_rx_head] = buf[w++];
        g_rx_head = next;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return w;
}

API int cyd_uart_rx_pending(void)
{
    return (int)((g_rx_head + UART_RX_CAP - g_rx_tail) % UART_RX_CAP);
}

/*
 * Render what a person looking at the panel sees.
 *   rotation: -1 = follow the firmware (the MADCTL it draws full frames with),
 *             0..3 = force that many quarter turns of the native portrait panel
 *   brightness: 0..255 backlight; 255 = unscaled
 * Writes RGB888, returns (w << 16) | h.
 */
API int cyd_render(uint8_t *rgb, int rotation, int brightness)
{
    tft_t t = g_tft;
    uint8_t view;
    if (rotation < 0) view = t.view_madctl & 0xE0;
    else {
        /* quarter turns expressed as the MADCTL a driver would use on this panel */
        static const uint8_t mir[4] = {0x40, 0x20, 0x80, 0xE0}, nomir[4] = {0x00, 0x60, 0xC0, 0xA0};
        view = (g_cfg.gram_mirror_x ? mir : nomir)[rotation & 3];
    }
    int w = (view & 0x20) ? g_cfg.panel_h : g_cfg.panel_w;
    int h = (view & 0x20) ? g_cfg.panel_w : g_cfg.panel_h;
    bool blank = t.sleep || !t.on;
    bool swap_rb = ((t.madctl >> 3) & 1) != (g_cfg.panel_bgr & 1);
    bool invert = t.inv != (g_cfg.panel_inverted != 0);
    uint32_t blank_px = g_cfg.blank_white ? 0xFFFFFF : 0x000000;
    bool scroll = t.vsp != 0 && t.vsa > 0 && (t.tfa + t.vsa) <= g_cfg.panel_h;
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;

    for (int y = 0; y < h; y++) {
        uint8_t *o = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            uint32_t px;
            if (blank) px = blank_px;
            else {
                int c, r;
                tft_map(view, x, y, &c, &r);
                if (scroll && r >= t.tfa && r < t.tfa + t.vsa) {
                    int off = (t.vsp >= t.tfa ? t.vsp - t.tfa : 0) % t.vsa;
                    r = t.tfa + (r - t.tfa + off) % t.vsa;
                }
                px = g_gram[r * GRAM_MAX_W + c];
                if (invert) px ^= 0xFFFFFF;
            }
            uint32_t c1 = (px >> 16) & 0xFF, c2 = (px >> 8) & 0xFF, c3 = px & 0xFF;
            if (swap_rb && !blank) { uint32_t s = c1; c1 = c3; c3 = s; }
            if (brightness != 255) {
                c1 = c1 * brightness / 255; c2 = c2 * brightness / 255; c3 = c3 * brightness / 255;
            }
            o[0] = (uint8_t)c1; o[1] = (uint8_t)c2; o[2] = (uint8_t)c3;
            o += 3;
        }
    }
    return (w << 16) | h;
}

/* Map a point in the rendered image back to the viewer's portrait panel (u right, v down). */
API void cyd_view_to_panel(int rotation, int x, int y, int *u, int *v)
{
    uint8_t view;
    int c, r;
    if (rotation < 0) view = g_tft.view_madctl & 0xE0;
    else {
        static const uint8_t mir[4] = {0x40, 0x20, 0x80, 0xE0}, nomir[4] = {0x00, 0x60, 0xC0, 0xA0};
        view = (g_cfg.gram_mirror_x ? mir : nomir)[rotation & 3];
    }
    tft_map(view, x, y, &c, &r);
    *u = g_cfg.gram_mirror_x ? g_cfg.panel_w - 1 - c : c;
    *v = r;
}

typedef struct {
    int64_t virt_ns;
    int64_t ticks;
    int64_t pixels, draw_seq, tft_cmds, tft_bytes;
    int64_t last_draw_wall_ms;      /* host ms since the image last changed */
    int64_t last_draw_virt_ns;
    int64_t spi_bytes[2], spi_unrouted;
    int64_t uart_tx_total[3], uart_tx_dropped[3];
    int32_t started, exited, resets, events_pending;
    int32_t view_w, view_h;
    uint8_t madctl, view_madctl, colmod, inv, on, sleep, view_latched, brightness;
    uint16_t x0, x1, y0, y1, tfa, vsa, bfa, vsp;
    uint8_t touch_down, pad[3];
    uint16_t touch_ypos, touch_xpos, touch_z1, touch_z2;
    uint32_t touch_conversions, touch_bb_edges, touch_hw_bytes;
    uint8_t out[NGPIO], dir[NGPIO], in[NGPIO];
    uint32_t pin_writes[NGPIO];
    uint32_t out_sel[NGPIO];
    uint32_t in_sel_reserved;
    float ledc_duty[16];
    uint32_t ledc_freq[8];
    uint32_t ledc_conf0[16];
    uint32_t uart_baud[3];
    uint32_t strap;
} cyd_state_t;

API void cyd_get_state(cyd_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->virt_ns = g_virt_ns;
    s->ticks = g_ticks;
    s->pixels = g_pixels; s->draw_seq = g_draw_seq; s->tft_cmds = g_tft_cmds; s->tft_bytes = g_tft_bytes;
    s->last_draw_wall_ms = g_last_draw_tick ? (int64_t)GetTickCount64() - g_last_draw_tick : -1;
    s->last_draw_virt_ns = g_last_draw_virt_ns;
    s->spi_bytes[0] = g_spi_bytes[0]; s->spi_bytes[1] = g_spi_bytes[1]; s->spi_unrouted = g_spi_unrouted;
    for (int i = 0; i < 3; i++) { s->uart_tx_total[i] = (int64_t)g_tx_total[i]; s->uart_tx_dropped[i] = (int64_t)g_tx_dropped[i]; }
    s->started = g_started; s->exited = g_exited; s->resets = g_resets; s->events_pending = g_ev_pending;
    tft_t t = g_tft;
    uint8_t view = t.view_madctl & 0xE0;
    s->view_w = (view & 0x20) ? g_cfg.panel_h : g_cfg.panel_w;
    s->view_h = (view & 0x20) ? g_cfg.panel_w : g_cfg.panel_h;
    s->madctl = t.madctl; s->view_madctl = t.view_madctl; s->colmod = t.colmod;
    s->inv = t.inv; s->on = t.on; s->sleep = t.sleep; s->view_latched = t.view_latched; s->brightness = t.brightness;
    s->x0 = t.x0; s->x1 = t.x1; s->y0 = t.y0; s->y1 = t.y1;
    s->tfa = t.tfa; s->vsa = t.vsa; s->bfa = t.bfa; s->vsp = t.vsp;
    s->touch_down = g_xpt.down; s->touch_ypos = g_xpt.ypos; s->touch_xpos = g_xpt.xpos;
    s->touch_z1 = g_xpt.z1; s->touch_z2 = g_xpt.z2;
    s->touch_conversions = g_xpt.conversions; s->touch_bb_edges = g_xpt.bb_edges; s->touch_hw_bytes = g_xpt.hw_bytes;
    memcpy(s->out, g_out, NGPIO); memcpy(s->dir, g_dir, NGPIO); memcpy(s->in, g_in, NGPIO);
    memcpy(s->pin_writes, g_pin_writes, sizeof g_pin_writes);
    if (g_started && q_get_internals) {
        uint32_t *p;
        if ((p = q_get_internals(2)) != NULL) memcpy(s->out_sel, p, sizeof s->out_sel);
        if ((p = q_get_internals(6)) != NULL) memcpy(s->ledc_duty, p, sizeof s->ledc_duty);
        if ((p = q_get_internals(5)) != NULL) memcpy(s->ledc_freq, p, sizeof s->ledc_freq);
        if ((p = q_get_internals(4)) != NULL) memcpy(s->ledc_conf0, p, sizeof s->ledc_conf0);
        for (int i = 0; i < 3; i++) if ((p = q_get_internals(7 + i)) != NULL) s->uart_baud[i] = *p;
        if ((p = q_get_internals(0)) != NULL) s->strap = *p;
    }
}

API int cyd_state_size(void) { return (int)sizeof(cyd_state_t); }
API int cyd_config_size(void) { return (int)sizeof(cyd_config_t); }
