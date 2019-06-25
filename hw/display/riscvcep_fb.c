/*
 * Ensimag CEP Platform, framebuffer and board representation
 *
 * Copyright (C) 2013 The Ensimag CEP team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/riscv/cep_riscv.h"
#include "framebuffer.h"
#include "ui/pixel_ops.h"

/* Contains images data */
#include "hw/riscv/riscvcep_fbres.h"

#define FB_WIDTH                640
#define FB_HEIGHT               480

#define VRAM_OFFSET_X           20
#define VRAM_OFFSET_Y           20
#define VVGA_WIDTH				320
#define VRAM_WIDTH              512
#define VRAM_HEIGHT             240

#define REG_LEDS                0x0
#define REG_SWITCHES            0x4
#define REG_PUSHBTN_CTL         0x8
#define REG_7SEGS               0xc
#define REG_7SEGS_CTL           0x10

#define PUSHBTN_CTL_POLL        0x0     /* Polling mode: user must read the pushbtn value */
#define PUSHBTN_CTL_INT         0x1     /* Interrupt mode: an irq is raised on 
                                           pushbtn event. Ack when read */

#define R7SEGS_CTL_HALF_LOW     0x0
#define R7SEGS_CTL_HALF_HIGH    0x1
#define R7SEGS_CTL_RAW          0x2

#define PUSHBTN_PERSISTANCE     3      /* Stay displayed red for 3 frames when in
                                          int mode */

enum gui_invalidate {
	INVAL_FB       = 1 << 0,
	INVAL_LEDS     = 1 << 1,
	INVAL_7SEGS    = 1 << 2,
	INVAL_SWITCHES = 1 << 3,
	INVAL_PUSHBTN  = 1 << 4,
	INVAL_ALL_ELT  = INVAL_FB | INVAL_LEDS | INVAL_7SEGS|
                     INVAL_SWITCHES | INVAL_PUSHBTN,

	/* background redrawing implies redrawing of everything */
	INVAL_BG       = 1 << 5,
	INVAL_ALL      = INVAL_ALL_ELT | INVAL_BG,
};

enum gui_elt_type {
    GUIELT_LED, GUIELT_SWITCH, GUIELT_PUSHBTN
};


#define GUI_7SEG(id) \
    GUI_7SEG## id ##_a, GUI_7SEG## id ##_b, GUI_7SEG## id ##_c, GUI_7SEG## id ##_d, \
    GUI_7SEG## id ##_e, GUI_7SEG## id ##_f, GUI_7SEG## id ##_g, GUI_7SEG## id ##_dp 
enum gui_elt_id {
    GUI_LED0, GUI_LED1, GUI_LED2, GUI_LED3,
    GUI_LED4, GUI_LED5, GUI_LED6, GUI_LED7, 

    GUI_7SEG(0), GUI_7SEG(1), GUI_7SEG(2), GUI_7SEG(3),

    GUI_SWITCH0, GUI_SWITCH1, GUI_SWITCH2, GUI_SWITCH3, 
    GUI_SWITCH4, GUI_SWITCH5, GUI_SWITCH6, GUI_SWITCH7, 

    GUI_PUSHBTN0, GUI_PUSHBTN1, GUI_PUSHBTN2, GUI_PUSHBTN3, 

    GUI_ELT_NUM
};
#undef GUI_7SEG

enum gui_elt_status {
    STA_OFF = 0,
    STA_ON,
};

struct gui_elt {
    enum gui_elt_id   id;
    enum gui_elt_type type;

    int x;
    int y;

    /* Two img: off and on */
    const struct img_data * s[2];

    int clickable;
};

#define _DEFINE_LED(__id, xx, yy, __img)  \
    [__id] = {                          \
        .id   = __id,                   \
        .type = GUIELT_LED,             \
        .x = xx, .y = yy,               \
        .s[STA_OFF] = &__img,            \
        .s[STA_ON]  = &__img##_on,       \
        .clickable  = 0,                \
    }

#define DEFINE_LED(__id, xx, yy)  \
    _DEFINE_LED(__id, xx, yy, img_led)

#define DEFINE_7SEG(id, xx, yy) \
    _DEFINE_LED(id##_a, xx, yy, img_7seg_a), \
    _DEFINE_LED(id##_b, xx+9, yy, img_7seg_b), \
    _DEFINE_LED(id##_c, xx+9, yy+13, img_7seg_c), \
    _DEFINE_LED(id##_d, xx, yy+22, img_7seg_d), \
    _DEFINE_LED(id##_e, xx-4, yy+13, img_7seg_e), \
    _DEFINE_LED(id##_f, xx-4, yy, img_7seg_f), \
    _DEFINE_LED(id##_g, xx-1, yy+11, img_7seg_g), \
    _DEFINE_LED(id##_dp, xx+13, yy+22, img_7seg_dp)

#define DEFINE_SWITCH(__id, xx, yy)  \
    [__id] = {                          \
        .id   = __id,                   \
        .type = GUIELT_SWITCH,          \
        .x = xx, .y = yy,               \
        .s[STA_OFF] = &img_switch,       \
        .s[STA_ON]  = &img_switch_on,    \
        .clickable  = 1,                \
    }

#define DEFINE_PUSHBTN(__id, xx, yy)  \
    [__id] = {                          \
        .id   = __id,                   \
        .type = GUIELT_PUSHBTN,         \
        .x = xx, .y = yy,               \
        .s[STA_OFF] = &img_pushbtn,      \
        .s[STA_ON]  = &img_pushbtn_on,   \
        .clickable  = 1,                \
    }

static const struct gui_elt gui_elts[] = {
    DEFINE_LED(GUI_LED7, 356, 404),
    DEFINE_LED(GUI_LED6, 369, 404),
    DEFINE_LED(GUI_LED5, 382, 404),
    DEFINE_LED(GUI_LED4, 395, 404),
    DEFINE_LED(GUI_LED3, 408, 404),
    DEFINE_LED(GUI_LED2, 421, 404),
    DEFINE_LED(GUI_LED1, 434, 404),
    DEFINE_LED(GUI_LED0, 447, 404),

    DEFINE_7SEG(GUI_7SEG3, 502, 392),
    DEFINE_7SEG(GUI_7SEG2, 527, 392),
    DEFINE_7SEG(GUI_7SEG1, 552, 392),
    DEFINE_7SEG(GUI_7SEG0, 577, 392),

    DEFINE_SWITCH(GUI_SWITCH7, 474, 426),
    DEFINE_SWITCH(GUI_SWITCH6, 490, 426),
    DEFINE_SWITCH(GUI_SWITCH5, 506, 426),
    DEFINE_SWITCH(GUI_SWITCH4, 522, 426),
    DEFINE_SWITCH(GUI_SWITCH3, 538, 426),
    DEFINE_SWITCH(GUI_SWITCH2, 554, 426),
    DEFINE_SWITCH(GUI_SWITCH1, 570, 426),
    DEFINE_SWITCH(GUI_SWITCH0, 586, 426),

    DEFINE_PUSHBTN(GUI_PUSHBTN3, 358, 427),
    DEFINE_PUSHBTN(GUI_PUSHBTN2, 358+25, 427),
    DEFINE_PUSHBTN(GUI_PUSHBTN1, 358+25*2, 427),
    DEFINE_PUSHBTN(GUI_PUSHBTN0, 358+25*3, 427),
};


/* 7seg mapping */
enum { SEG_A = 0, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G, SEG_DP, NUM_SEG };
static int const r7segs_mapping[][NUM_SEG] = {
    [0x0] = { 
        [SEG_A] = 1, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 0, [SEG_DP] = 0,
    },
    [0x1] = { 
        [SEG_A] = 0, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 0,
        [SEG_E] = 0, [SEG_F] = 0, [SEG_G] = 0, [SEG_DP] = 0,
    },
    [0x2] = { 
        [SEG_A] = 1, [SEG_B] = 1, [SEG_C] = 0, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 0, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0x3] = { 
        [SEG_A] = 1, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 0, [SEG_F] = 0, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0x4] = { 
        [SEG_A] = 0, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 0,
        [SEG_E] = 0, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0x5] = { 
        [SEG_A] = 1, [SEG_B] = 0, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 0, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0x6] = { 
        [SEG_A] = 1, [SEG_B] = 0, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0x7] = { 
        [SEG_A] = 1, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 0,
        [SEG_E] = 0, [SEG_F] = 0, [SEG_G] = 0, [SEG_DP] = 0,
    },
    [0x8] = { 
        [SEG_A] = 1, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0x9] = { 
        [SEG_A] = 1, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 0, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0xa] = { 
        [SEG_A] = 1, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 0,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0xb] = { 
        [SEG_A] = 0, [SEG_B] = 0, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0xc] = { 
        [SEG_A] = 1, [SEG_B] = 0, [SEG_C] = 0, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 0, [SEG_DP] = 0,
    },
    [0xd] = { 
        [SEG_A] = 0, [SEG_B] = 1, [SEG_C] = 1, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 0, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0xe] = { 
        [SEG_A] = 1, [SEG_B] = 0, [SEG_C] = 0, [SEG_D] = 1,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
    [0xf] = { 
        [SEG_A] = 1, [SEG_B] = 0, [SEG_C] = 0, [SEG_D] = 0,
        [SEG_E] = 1, [SEG_F] = 1, [SEG_G] = 1, [SEG_DP] = 0,
    },
};


struct mipscep_fb_s {
    MemoryRegion mem_vram;
    uint8_t *vram;

    MemoryRegion mem_periph;

    uint32_t vram_size;

    QemuConsole *con;
    QEMUPutMouseEntry *mouse_hdl;  

    enum gui_invalidate invalidate;

    int last_bstate;

    struct {
        unsigned int (*rgb_to_pixel)(unsigned int r, unsigned int g, unsigned int b);
        int w;
    } draw_info;

    int periph_sta[GUI_ELT_NUM];
    int r7segs_mode;
    int pushbtn_mode;
    int persistance[GUI_ELT_NUM]; /* used by pushbtn when in int mode
                                     to let the user see the red button
                                     even if the software acknowledge the irq
                                     immediately */


    qemu_irq pushbtn_irq;

    const struct gui_elt *was_in;
};

static inline void fill_draw_info(struct mipscep_fb_s *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    switch (surface_bits_per_pixel(surface)) {
        case 8:
            s->draw_info.rgb_to_pixel = rgb_to_pixel8;
            s->draw_info.w = 1;
            break;
        case 15:
            s->draw_info.rgb_to_pixel = rgb_to_pixel15;
            s->draw_info.w = 2;
            break;
        case 16:
            s->draw_info.rgb_to_pixel = rgb_to_pixel16;
            s->draw_info.w = 2;
            break;
        case 32:
            s->draw_info.rgb_to_pixel = rgb_to_pixel32;
            s->draw_info.w = 4;
            break;
        default:
            hw_error("mipscep_fb: unknown host depth %d",
                     surface_bits_per_pixel(surface));
    }
}


static inline uint8_t* surface_offset(DisplaySurface *s, uint8_t *d, int dx, int dy)
{
    return (d + surface_stride(s) * dy + surface_bytes_per_pixel(s) * dx);
}

static inline void draw_img(struct mipscep_fb_s *s, const struct img_data *img, int x, int y)
{
    DisplaySurface *surface;
    int w, i, j;
    const uint8_t *src = img->data;
    uint8_t *dst;

    surface = qemu_console_surface(s->con);
    dst = surface_offset(surface, surface_data(surface), x, y);
    w = s->draw_info.w;

    for(j = 0; j < img->h; j++) {
        for(i = 0; i < img->w; i++) {
            uint8_t pixel[3];
            HEADER_PIXEL(src, pixel);

            unsigned int color = s->draw_info.rgb_to_pixel(pixel[0], pixel[1], pixel[2]);
            memcpy(dst, &color, w);
            dst += w;
        }

        dst += surface_stride(surface) - (img->w * w);
    }
}

static inline void draw_bg(struct mipscep_fb_s *s)
{
    draw_img(s, &img_cep_fb_bg, 0, 0);
}

static inline void draw_vram(struct mipscep_fb_s *s)
{
    DisplaySurface *surface;
    unsigned int black, white;
    uint8_t *d, *vram;
    int w, i, j, k;

    surface = qemu_console_surface(s->con);
    black = s->draw_info.rgb_to_pixel(0, 0, 0);
    white = s->draw_info.rgb_to_pixel(255, 255, 255);
    w = s->draw_info.w;

    d = surface_offset(surface, surface_data(surface), VRAM_OFFSET_X, VRAM_OFFSET_Y);

    vram = s->vram;

    for(j = 0; j < VRAM_HEIGHT; j++) {
        for(i = 0; i < VVGA_WIDTH/8; i++) {
            uint8_t b = *vram++;
            for(k = 0; k < 8; k++) {
                memcpy(d, (b & 0x80) ? &white : &black, w);
                b <<= 1;
                d += w;
            }
        }
        d += (surface_stride(surface) - VVGA_WIDTH * w);
        vram += (VRAM_WIDTH-VVGA_WIDTH)>>3;
    }
}

static inline void draw_guielt(struct mipscep_fb_s *s)
{
    enum gui_elt_id i;

    if(s->invalidate & INVAL_LEDS) {
	    for(i = GUI_LED0; i <= GUI_LED7; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }

    if(s->invalidate & INVAL_7SEGS) {
	    for(i = GUI_7SEG0_a; i <= GUI_7SEG3_dp; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }

    if(s->invalidate & INVAL_SWITCHES) {
	    for(i = GUI_SWITCH0; i <= GUI_SWITCH7; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }

    if(s->invalidate & INVAL_PUSHBTN) {
	    for(i = GUI_PUSHBTN0; i <= GUI_PUSHBTN3; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i] || s->persistance[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }
}

/* Return the bounding box of the area to be redrawn */
static inline void get_redraw_bb(const struct mipscep_fb_s *s, int *x0, int *y0,
				 int *x1, int *y1)
{
    if(s->invalidate & INVAL_BG) {
        *x0 = *y0 = 0;
        *x1 = FB_WIDTH;
        *y1 = FB_HEIGHT;
        return;
    }

    *x0 = FB_HEIGHT; *y0 = FB_HEIGHT;
    *x1 = *y1 = 0;

    if(s->invalidate & INVAL_FB) {
        *x0 = MIN(*x0, VRAM_OFFSET_X);
        *y0 = MIN(*y0, VRAM_OFFSET_Y);
        *x1 = MAX(*x1, VRAM_OFFSET_X + VVGA_WIDTH);
        *y1 = MAX(*y1, VRAM_OFFSET_Y + VRAM_HEIGHT);
    }

    if(s->invalidate & INVAL_LEDS) {
        *x0 = MIN(*x0, gui_elts[GUI_LED7].x);
        *y0 = MIN(*y0, gui_elts[GUI_LED7].y);
        *x1 = MAX(*x1, gui_elts[GUI_LED0].x + gui_elts[GUI_LED0].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_LED0].y + gui_elts[GUI_LED0].s[0]->h);
    }

    if(s->invalidate & INVAL_7SEGS) {
        *x0 = MIN(*x0, gui_elts[GUI_7SEG3_a].x);
        *y0 = MIN(*y0, gui_elts[GUI_7SEG3_a].y);
        *x1 = MAX(*x1, gui_elts[GUI_7SEG0_dp].x + gui_elts[GUI_7SEG0_dp].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_7SEG0_dp].y + gui_elts[GUI_7SEG0_dp].s[0]->h);
    }

    if(s->invalidate & INVAL_SWITCHES) {
        *x0 = MIN(*x0, gui_elts[GUI_SWITCH7].x);
        *y0 = MIN(*y0, gui_elts[GUI_SWITCH7].y);
        *x1 = MAX(*x1, gui_elts[GUI_SWITCH0].x + gui_elts[GUI_SWITCH0].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_SWITCH0].y + gui_elts[GUI_SWITCH0].s[0]->h);
    }

    if(s->invalidate & INVAL_PUSHBTN) {
        *x0 = MIN(*x0, gui_elts[GUI_PUSHBTN3].x);
        *y0 = MIN(*y0, gui_elts[GUI_PUSHBTN3].y);
        *x1 = MAX(*x1, gui_elts[GUI_PUSHBTN0].x + gui_elts[GUI_PUSHBTN0].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_PUSHBTN0].y + gui_elts[GUI_PUSHBTN0].s[0]->h);
    }
}

static inline bool vram_is_dirty(struct mipscep_fb_s *s)
{
    return memory_region_snapshot_and_clear_dirty(&s->mem_vram, 0, s->vram_size, DIRTY_MEMORY_VGA);
}

static void mipscep_fb_update_display(void *opaque)
{
    struct mipscep_fb_s *s = (struct mipscep_fb_s*) opaque; 
    int x0, y0, x1, y1;
    enum gui_elt_id i;
    DisplaySurface *surface;


    surface = qemu_console_surface(s->con);

    if (surface_width(surface) != FB_WIDTH ||
        surface_height(surface) != FB_HEIGHT) {
        qemu_console_resize(s->con,
                            FB_WIDTH, FB_HEIGHT);
        s->invalidate = INVAL_ALL;
    }

    if(s->invalidate & INVAL_ALL) {
        fill_draw_info(s);
    }


    for(i = GUI_PUSHBTN0; i <= GUI_PUSHBTN3; i++) {
        if (s->persistance[i]) {
            s->persistance[i]--;
            if (!s->persistance[i]) {
                s->invalidate |= INVAL_PUSHBTN;
            }
        }
    }

    if(s->invalidate & INVAL_BG) {
	    draw_bg(s);

	    /* Ensure we redraw everything */
	    s->invalidate = INVAL_ALL;
    }

    if(vram_is_dirty(s)) {
        /* Read and clear dirty bits on vram.
         * One possible optimisation: Reduce the redraw grain of the fb
         * ie. could redraw only dirty lines */
        s->invalidate |= INVAL_FB;
    }

    if(s->invalidate & INVAL_FB) {
	    draw_vram(s);
    }

    draw_guielt(s);

    get_redraw_bb(s, &x0, &y0, &x1, &y1);

    dpy_gfx_update(s->con, x0, y0, x1 - x0, y1 - y0);

    s->invalidate = 0;
}


static void guielt_click_event(struct mipscep_fb_s *s, 
                               const struct gui_elt *e, int b)
{
    switch(e->type) {
    case GUIELT_SWITCH:
        if(b) {
            s->periph_sta[e->id] = !s->periph_sta[e->id];
            s->invalidate |= INVAL_SWITCHES;
        }
        break;

    case GUIELT_PUSHBTN:
        if(s->pushbtn_mode == PUSHBTN_CTL_POLL) {
            s->periph_sta[e->id] = !!b;
        } else if(b) {
            qemu_irq_raise(s->pushbtn_irq);
            s->persistance[e->id] = PUSHBTN_PERSISTANCE;
            s->periph_sta[e->id] = !!b;
        }

        s->invalidate |= INVAL_PUSHBTN;
        break;

    default:
        hw_error("Huh Oo");
    }
}

static inline int cursor_is_in(int x, int y, const struct gui_elt *e)
{
    return ((x >= e->x) && (y >= e->y) &&
            (x < e->x + e->s[0]->w) && (y < e->y + e->s[0]->h));
}

static void mipscep_fb_mouse_event(void *opaque, int dx, int dy, int dz, 
                                   int bstate)
{
    struct mipscep_fb_s *s = (struct mipscep_fb_s*) opaque; 
    int i;
    int xbstate = bstate ^ s->last_bstate;
    const struct gui_elt *new_was_in = NULL;

    /* QEMU reports absolute position btw 0 and 2^15-1 */
    int x = (dx * FB_WIDTH) >> 15;
    int y = (dy * FB_HEIGHT) >> 15;

    s->last_bstate = bstate;

    /* Left click event */
    if(xbstate & MOUSE_EVENT_LBUTTON) {
        for(i = 0; i < ARRAY_SIZE(gui_elts); i++) {
            if(!gui_elts[i].clickable) {
                continue;
            }

            if(cursor_is_in(x, y, gui_elts+i)) {
                guielt_click_event(s, gui_elts+i, bstate & MOUSE_EVENT_LBUTTON);
                new_was_in = gui_elts + i;
            } else if(gui_elts+i == s->was_in) {
                /* FIXME... */
                guielt_click_event(s, s->was_in, 0);
            }
        }
        s->was_in = new_was_in;

    }

}

static void mipscep_fb_invalidate_display(void *opaque) {
    struct mipscep_fb_s *mipscep_fb_lcd = opaque;
    mipscep_fb_lcd->invalidate = INVAL_ALL;
}

static uint64_t mipscep_periph_read(void *opaque, hwaddr addr, 
                                    unsigned int size)
{
    struct mipscep_fb_s *s = (struct mipscep_fb_s*) opaque; 
    uint64_t val;
    int i;

    switch(addr) {
    case REG_SWITCHES:
        val = 0;
        for(i = GUI_PUSHBTN3; i >= GUI_PUSHBTN0; i--) {
            val <<= 1;
            val |= !!(s->periph_sta[i]);
            if(s->pushbtn_mode == PUSHBTN_CTL_INT) {
                /* In INT mode, read inplies ack and return to 0 state */
                s->periph_sta[i] = 0;
            }

        }

        for(i = GUI_SWITCH7; i >= GUI_SWITCH0; i--) {
            val <<= 1;
            val |= !!(s->periph_sta[i]);
        }

        if(s->pushbtn_mode == PUSHBTN_CTL_INT) {
            /* IRQ ack if in INT mode */
            qemu_irq_lower(s->pushbtn_irq);
        }
        break;

    case REG_PUSHBTN_CTL:
    case REG_LEDS:
    case REG_7SEGS:
    case REG_7SEGS_CTL:
    default:
        val = 0;
    }

    return val;
}

static void update_7seg(struct mipscep_fb_s* s, uint32_t val)
{
    int print_base = 0, i;
    uint16_t v = 0;

    switch(s->r7segs_mode) {
    case R7SEGS_CTL_HALF_LOW:
        print_base = 16;
        v = val & 0xffff;
        break;

    case R7SEGS_CTL_HALF_HIGH:
        print_base = 16;
        v = (val >> 16) & 0xffff;
        break;

    case R7SEGS_CTL_RAW:
        print_base = 10;
        v = val;
    default:
        break;
    }


    if(print_base == 16) {
        for(i = GUI_7SEG0_a; i <= GUI_7SEG3_a; i+=NUM_SEG) {
            memcpy(s->periph_sta + i, r7segs_mapping + (v & 0xf), sizeof(r7segs_mapping[0]));
            v >>= 4;
        }
    } else {
        for(i = GUI_7SEG0_a; i <= GUI_7SEG3_a; i+=NUM_SEG) {
            memcpy(s->periph_sta + i, r7segs_mapping + (v % 10), sizeof(r7segs_mapping[0]));
            v /= 10;
        }
    }
}


static void mipscep_periph_write(void *opaque, hwaddr addr, 
                                    uint64_t val, unsigned int size)
{
    struct mipscep_fb_s *s = (struct mipscep_fb_s*) opaque; 
    int i;

    switch(addr) {
    case REG_LEDS:
        for(i = GUI_LED0; i <= GUI_LED7; i++) {
            s->periph_sta[i] = (val & 1);
            val >>= 1;
        }
        s->invalidate |= INVAL_LEDS;
        break;

    case REG_7SEGS:
        update_7seg(s, (uint32_t)val);
        s->invalidate |= INVAL_7SEGS;
        break;

    case REG_7SEGS_CTL:
        if(val <= R7SEGS_CTL_RAW) {
            s->r7segs_mode = val;
            s->invalidate |= INVAL_7SEGS;
        }
        break;

    case REG_PUSHBTN_CTL:
        if(val <= PUSHBTN_CTL_INT) {
            s->pushbtn_mode = val;
            qemu_irq_lower(s->pushbtn_irq);
        }
        break;

    case REG_SWITCHES:
    default:
        break;
   }
}

void mipscep_fb_reset(struct mipscep_fb_s *s)
{
    memset(s->periph_sta, 0, sizeof(s->periph_sta));
    s->r7segs_mode = R7SEGS_CTL_HALF_LOW;
    s->pushbtn_mode = PUSHBTN_CTL_POLL;
    s->invalidate = INVAL_ALL;

    update_7seg(s, 0);
}

static const GraphicHwOps mipscep_fb_ops = {
    .invalidate  = mipscep_fb_invalidate_display,
    .gfx_update  = mipscep_fb_update_display,
};

static const MemoryRegionOps mipscep_periph_op = {
    .read = mipscep_periph_read,
    .write = mipscep_periph_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void mipscep_fb_init(MemoryRegion *sysmem, hwaddr vram_offset,
                     hwaddr periph_offset, qemu_irq pushbtn_irq)
{
    struct mipscep_fb_s *s = (struct mipscep_fb_s *)
            g_malloc0(sizeof(struct mipscep_fb_s));

    s->vram_size = (VRAM_WIDTH * VRAM_HEIGHT) >> 3;

    s->vram = g_malloc0(s->vram_size);

    s->con = graphic_console_init(NULL, 0, &mipscep_fb_ops, s);
    s->mouse_hdl = qemu_add_mouse_event_handler(mipscep_fb_mouse_event, 
                                                s, 1, "mipscep_fb mouse");

    s->pushbtn_irq = pushbtn_irq;

    memory_region_init_ram_ptr(&s->mem_vram, NULL , "mipscep_fb_vram",
                               s->vram_size, s->vram);
    memory_region_add_subregion(sysmem, vram_offset, &s->mem_vram);
    memory_region_set_log(&s->mem_vram, true, DIRTY_MEMORY_VGA);

    memory_region_init_io(&s->mem_periph, NULL, &mipscep_periph_op, s,
                          "mipscep_fb_periph", 0x20);
    memory_region_add_subregion(sysmem, periph_offset, &s->mem_periph);

    mipscep_fb_reset(s);
}
