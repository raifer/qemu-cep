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
#include "framebuffer.h"
#include "ui/pixel_ops.h"

#include "hw/display/riscvcep_fb.h"
/* Contains images data */
#include "hw/riscv/riscvcep_fbres.h"

#define VRAM_WIDTH                        1920
#define VRAM_HEIGHT                       1080
#define VRAM_WIDTH_EFFECTIVE_DEFAULT      1280
#define VRAM_HEIGHT_EFFECTIVE_DEFAULT     720
#define VRAM_SIZE_EFFECTIVE_DEFAULT       (VRAM_WIDTH_EFFECTIVE_DEFAULT * VRAM_HEIGHT_EFFECTIVE_DEFAULT * 4)
#define VRAM_SIZE                         (VRAM_WIDTH * VRAM_HEIGHT * 4)

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
#define KBD_RELEASE_DIFF        0x80

#define KBD_CODE_LEFT           0x4B
#define KBD_CODE_RIGHT          0x4D
#define KBD_CODE_HIGH           0x48
#define KBD_CODE_DOWN           0x50
#define KBD_CODE_SPACE          0x39
#define KBD_CODE_LEFT_RELEASE   KBD_RELEASE_DIFF + KBD_CODE_LEFT
#define KBD_CODE_RIGHT_RELEASE  KBD_RELEASE_DIFF + KBD_CODE_RIGHT
#define KBD_CODE_HIGH_RELEASE   KBD_RELEASE_DIFF + KBD_CODE_HIGH
#define KBD_CODE_DOWN_RELEASE   KBD_RELEASE_DIFF + KBD_CODE_DOWN
#define KBD_CODE_SPACE_RELEASE  KBD_RELEASE_DIFF + KBD_CODE_SPACE


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


/* Useless for now */
#define GUI_7SEG(id) \
    GUI_7SEG## id ##_a, GUI_7SEG## id ##_b, GUI_7SEG## id ##_c, GUI_7SEG## id ##_d, \
    GUI_7SEG## id ##_e, GUI_7SEG## id ##_f, GUI_7SEG## id ##_g, GUI_7SEG## id ##_dp 
enum gui_elt_id {
    GUI_LED0, GUI_LED1, GUI_LED2, GUI_LED3,
#if 0
    GUI_LED4, GUI_LED5, GUI_LED6, GUI_LED7, 

    GUI_7SEG(0), GUI_7SEG(1), GUI_7SEG(2), GUI_7SEG(3),
#endif

    GUI_SWITCH0, GUI_SWITCH1, GUI_SWITCH2, GUI_SWITCH3, 
#if 0
    GUI_SWITCH4, GUI_SWITCH5, GUI_SWITCH6, GUI_SWITCH7, 
#endif

    GUI_PUSHBTN0, GUI_PUSHBTN1, GUI_PUSHBTN2, GUI_PUSHBTN3, 

    GUI_ELT_NUM
};
#define GUI_LAST_SWITCH  GUI_SWITCH3
#define GUI_LAST_LED     GUI_LED3
#define GUI_LAST_PUSHBTN GUI_PUSHBTN3
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

/*
 * Zybo : 4 leds on top of the 4 switches, 4 push buttons on the side
 * Pynq : 4 leds on top of the 4 push buttons plus 2 leds on top of the 2 switches
 * For now, zybo, ...
 */
static const struct gui_elt gui_elts[] = {
    DEFINE_LED(GUI_LED3,  3, 5),
    DEFINE_LED(GUI_LED2, 19, 5),
    DEFINE_LED(GUI_LED1, 35, 5),
    DEFINE_LED(GUI_LED0, 51, 5),

    DEFINE_SWITCH(GUI_SWITCH3,  2, 29),
    DEFINE_SWITCH(GUI_SWITCH2, 18, 29),
    DEFINE_SWITCH(GUI_SWITCH1, 34, 29),
    DEFINE_SWITCH(GUI_SWITCH0, 50, 29),

    DEFINE_PUSHBTN(GUI_PUSHBTN3,  77, 30),
    DEFINE_PUSHBTN(GUI_PUSHBTN2, 102, 30),
    DEFINE_PUSHBTN(GUI_PUSHBTN1, 127, 30),
    DEFINE_PUSHBTN(GUI_PUSHBTN0, 152, 30),
};


/* 7seg mapping
 * Useless for now, but who knows, ... */

#if 0
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
#endif

struct riscv_cep_fb_s ;

struct riscv_cep_fb_ctrl_s {
    struct riscv_cep_fb_s *s;
    MemoryRegion mem_fb_ctrl;
    uint32_t Reg_MODE;
    uint32_t Reg_ADDR;
};

struct riscv_cep_fb_s {
    MemoryRegion mem_vram;
    uint8_t *vram;

    uint32_t vram_size;
    uint32_t vram_size_effective;
    uint32_t vram_width_effective;
    uint32_t vram_heigth_effective;

    MemoryRegion mem_periph;
    uint32_t board_size;
    uint32_t board_width_effective;
    uint32_t board_heigth_effective;

    QemuConsole *con_board;
    QemuConsole *con_fb;
    QEMUPutMouseEntry *mouse_hdl;  
    QEMUPutMouseEntry *mouse_hdl_fb;  
    QEMUPutKbdEntry   *kbd_hdl;

    enum gui_invalidate invalidate;
    enum gui_invalidate invalidate_fb;

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

    struct riscv_cep_fb_ctrl_s *ctrl;
};


static inline void fill_draw_info(struct riscv_cep_fb_s *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con_board);
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
            hw_error("riscv_cep_fb: unknown host depth %d",
                     surface_bits_per_pixel(surface));
    }
}


static inline uint8_t* surface_offset(DisplaySurface *s, uint8_t *d, int dx, int dy)
{
    return (d + surface_stride(s) * dy + surface_bytes_per_pixel(s) * dx);
}

static inline void draw_img(struct riscv_cep_fb_s *s, const struct img_data *img, int x, int y)
{
    DisplaySurface *surface;
    int w, i, j;
    const uint8_t *src = img->data;
    uint8_t *dst;

    surface = qemu_console_surface(s->con_board);
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

static inline void draw_bg(struct riscv_cep_fb_s *s)
{
    draw_img(s, &img_cep_board_bg, 0, 0);
}

static inline uint32_t set_hdmi_mode(struct riscv_cep_fb_s *s, uint32_t mode) {

    switch (mode) {
        case 4:
            s->vram_width_effective            = 1280;
            s->vram_heigth_effective           = 720;
            break;
        case 19:
        case 32 ... 34:
            s->vram_width_effective            = 1920;
            s->vram_heigth_effective           = 1080;
            break;
        default:
            printf("ERROR: Unsupported HDMI mode (%u), switching to 720p mode (4)\n", mode);
            s->vram_width_effective            = 1280;
            s->vram_heigth_effective           = 720;
            mode = 4;
            break;
    }
    return mode;
}

static inline void draw_vram(struct riscv_cep_fb_s *s)
{
    DisplaySurface *surface;
    uint32_t *d, *start, *vram;
    int w, i, j;
    uint32_t width, heigth;
    width = s->vram_width_effective;
    heigth= s->vram_heigth_effective;

    surface = qemu_console_surface(s->con_fb);
    
    w = s->draw_info.w;

    start = (uint32_t *)surface_offset(surface, surface_data(surface), 0, 0);
    d = start;
    vram = (uint32_t *)s->vram;
    for(j = 0; j < heigth; j++) {
        for(i = 0; i < width; i++) {
            memcpy(d, vram, w);
            d ++;
            vram++;
        }
    }
}

static inline void draw_guielt(struct riscv_cep_fb_s *s)
{
    enum gui_elt_id i;

    if(s->invalidate & INVAL_LEDS) {
	    for(i = GUI_LED0; i <= GUI_LAST_LED; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }

#if 0
    if(s->invalidate & INVAL_7SEGS) {
	    for(i = GUI_7SEG0_a; i <= GUI_7SEG3_dp; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }
#endif

    if(s->invalidate & INVAL_SWITCHES) {
	    for(i = GUI_SWITCH0; i <= GUI_LAST_SWITCH; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }

    if(s->invalidate & INVAL_PUSHBTN) {
	    for(i = GUI_PUSHBTN0; i <= GUI_LAST_PUSHBTN; i++) {
		    draw_img(s, gui_elts[i].s[s->periph_sta[i] || s->persistance[i]],
			     gui_elts[i].x, gui_elts[i].y);
	    }
    }
}

/* Return the bounding box of the area to be redrawn */
static inline void get_redraw_bb(const struct riscv_cep_fb_s *s, int *x0, int *y0,
				 int *x1, int *y1)
{
    if(s->invalidate & INVAL_BG) {
        *x0 = *y0 = 0;
        *x1 = img_cep_board_bg.w;
        *y1 = img_cep_board_bg.h;
        return;
    }

    *x0 = img_cep_board_bg.w; *y0 = img_cep_board_bg.h;
    *x1 = *y1 = 0;

    if(s->invalidate & INVAL_LEDS) {
        *x0 = MIN(*x0, gui_elts[GUI_LAST_LED].x);
        *y0 = MIN(*y0, gui_elts[GUI_LAST_LED].y);
        *x1 = MAX(*x1, gui_elts[GUI_LED0].x + gui_elts[GUI_LED0].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_LED0].y + gui_elts[GUI_LED0].s[0]->h);
    }

#if 0
    if(s->invalidate & INVAL_7SEGS) {
        *x0 = MIN(*x0, gui_elts[GUI_7SEG3_a].x);
        *y0 = MIN(*y0, gui_elts[GUI_7SEG3_a].y);
        *x1 = MAX(*x1, gui_elts[GUI_7SEG0_dp].x + gui_elts[GUI_7SEG0_dp].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_7SEG0_dp].y + gui_elts[GUI_7SEG0_dp].s[0]->h);
    }
#endif

    if(s->invalidate & INVAL_SWITCHES) {
        *x0 = MIN(*x0, gui_elts[GUI_LAST_SWITCH].x);
        *y0 = MIN(*y0, gui_elts[GUI_LAST_SWITCH].y);
        *x1 = MAX(*x1, gui_elts[GUI_SWITCH0].x + gui_elts[GUI_SWITCH0].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_SWITCH0].y + gui_elts[GUI_SWITCH0].s[0]->h);
    }

    if(s->invalidate & INVAL_PUSHBTN) {
        *x0 = MIN(*x0, gui_elts[GUI_LAST_PUSHBTN].x);
        *y0 = MIN(*y0, gui_elts[GUI_LAST_PUSHBTN].y);
        *x1 = MAX(*x1, gui_elts[GUI_PUSHBTN0].x + gui_elts[GUI_PUSHBTN0].s[0]->w);
        *y1 = MAX(*y1, gui_elts[GUI_PUSHBTN0].y + gui_elts[GUI_PUSHBTN0].s[0]->h);
    }
}

static inline bool vram_is_dirty(struct riscv_cep_fb_s *s)
{
    return memory_region_snapshot_and_clear_dirty(&s->mem_vram, 0, s->vram_size_effective, DIRTY_MEMORY_VGA);
}

static void riscv_cep_board_update_display(void *opaque)
{
    struct riscv_cep_fb_s *s = (struct riscv_cep_fb_s*) opaque; 
    int x0, y0, x1, y1;
    enum gui_elt_id i;
    DisplaySurface *surface;

    surface = qemu_console_surface(s->con_board);

    if (surface_width(surface) != img_cep_board_bg.w ||
        surface_height(surface) != img_cep_board_bg.h) {
        qemu_console_resize(s->con_board,
                            img_cep_board_bg.w, img_cep_board_bg.h);
        s->invalidate = INVAL_ALL;
    }

#if 0
    /* Let's make sure this is useless */
    if(s->invalidate & INVAL_ALL) {
        fill_draw_info(s);
    }
#endif

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

    draw_guielt(s);

    get_redraw_bb(s, &x0, &y0, &x1, &y1);

    dpy_gfx_update(s->con_board, x0, y0, x1 - x0, y1 - y0);

    s->invalidate = 0;
}

static void riscv_cep_fb_update_display(void *opaque)
{
    struct riscv_cep_fb_s *s = (struct riscv_cep_fb_s*) opaque; 
    DisplaySurface *surface;

    surface = qemu_console_surface(s->con_fb);

    if (surface_width(surface) != s->vram_width_effective ||
        surface_height(surface) != s->vram_heigth_effective) {
        qemu_console_resize(s->con_fb,
                            s->vram_width_effective, s->vram_heigth_effective);
        s->invalidate_fb = INVAL_ALL;
    }

    fill_draw_info(s);

    if(vram_is_dirty(s)) {
        /* Read and clear dirty bits on vram.
         * One possible optimisation: Reduce the redraw grain of the fb
         * ie. could redraw only dirty lines */
        s->invalidate_fb |= INVAL_BG;
    }

    if(s->invalidate_fb & INVAL_BG) {
        draw_vram(s);
    }

    dpy_gfx_update_full(s->con_fb);

    s->invalidate_fb = 0;
}

static void guielt_click_event(struct riscv_cep_fb_s *s, 
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


static void press_button(struct riscv_cep_fb_s *s, int id, int press)
{
    if (s->pushbtn_mode == PUSHBTN_CTL_POLL) {
        s->periph_sta[id] = press;
    } else if(press) {
        qemu_irq_raise(s->pushbtn_irq);
        s->persistance[id] = PUSHBTN_PERSISTANCE;
        s->periph_sta[id] = press;
    }
    s->invalidate |= INVAL_PUSHBTN;
}


static void riscv_cep_kbd_event(void *opaque, int keycode)
{
    struct riscv_cep_fb_s *s = (struct riscv_cep_fb_s*) opaque; 
    //printf("keycode : 0x%x\n", keycode);
    switch(keycode) {
        case KBD_CODE_LEFT:
            press_button(s, GUI_PUSHBTN1, 1);
            break;
        case KBD_CODE_LEFT_RELEASE:
            press_button(s, GUI_PUSHBTN1, 0);
            break;
        case KBD_CODE_RIGHT:
            press_button(s, GUI_PUSHBTN0, 1);
            break;
        case KBD_CODE_RIGHT_RELEASE:
            press_button(s, GUI_PUSHBTN0, 0);
            break;
        case KBD_CODE_HIGH:
        case KBD_CODE_SPACE:
            press_button(s, GUI_PUSHBTN2, 1);
            break;
        case KBD_CODE_HIGH_RELEASE:
        case KBD_CODE_SPACE_RELEASE:
            press_button(s, GUI_PUSHBTN2, 0);
            break;
        case KBD_CODE_DOWN:
            press_button(s, GUI_PUSHBTN3, 1);
            break;
        case KBD_CODE_DOWN_RELEASE:
            press_button(s, GUI_PUSHBTN3, 0);
            break;
        default:
            break;
    }
}


static void riscv_cep_fb_mouse_event(void *opaque, int dx, int dy, int dz, 
                                   int bstate)
{
    struct riscv_cep_fb_s *s = (struct riscv_cep_fb_s*) opaque; 
    int i;
    int xbstate = bstate ^ s->last_bstate;
    const struct gui_elt *new_was_in = NULL;

    /* QEMU reports absolute position btw 0 and 2^15-1 */
    int x = (dx * img_cep_board_bg.w) >> 15;
    int y = (dy * img_cep_board_bg.h) >> 15;

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

static void riscv_cep_board_invalidate_display(void *opaque) {
    struct riscv_cep_fb_s *riscv_cep_fb_lcd = opaque;
    riscv_cep_fb_lcd->invalidate = INVAL_ALL;
}

static void riscv_cep_fb_invalidate_display2(void *opaque) {
    struct riscv_cep_fb_s *riscv_cep_fb_lcd = opaque;
    riscv_cep_fb_lcd->invalidate = INVAL_ALL;
}

static uint64_t riscv_cep_periph_read(void *opaque, hwaddr addr, 
                                    unsigned int size)
{
    struct riscv_cep_fb_s *s = (struct riscv_cep_fb_s*) opaque; 
    uint64_t val;
    int i;

    switch(addr) {
    case REG_SWITCHES:
        val = 0;
        for(i = GUI_LAST_PUSHBTN; i >= GUI_PUSHBTN0; i--) {
            val <<= 1;
            val |= !!(s->periph_sta[i]);
            if(s->pushbtn_mode == PUSHBTN_CTL_INT) {
                /* In INT mode, read inplies ack and return to 0 state */
                s->periph_sta[i] = 0;
            }
        }
        val <<= 12;

        for(i = GUI_LAST_SWITCH; i >= GUI_SWITCH0; i--) {
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
static uint64_t riscv_cep_fb_ctrl_read(void *opaque, hwaddr addr, 
                                    unsigned int size)
{
    struct riscv_cep_fb_ctrl_s *ctrl = (struct riscv_cep_fb_ctrl_s*) opaque; 
    uint32_t val;

    switch(addr) {
    case 0:
        val = ctrl->Reg_MODE;
        break;
    case 4:
        val = ctrl->Reg_ADDR;
        break;
    default:
        val=0;
    }

    return val;
}


#if 0
static void update_7seg(struct riscv_cep_fb_s* s, uint32_t val)
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
#endif


static void riscv_cep_periph_write(void *opaque, hwaddr addr, 
                                    uint64_t val, unsigned int size)
{
    struct riscv_cep_fb_s *s = (struct riscv_cep_fb_s*) opaque; 
    int i;

    switch(addr) {
    case REG_LEDS:
        for(i = GUI_LED0; i <= GUI_LAST_LED; i++) {
            s->periph_sta[i] = (val & 1);
            val >>= 1;
        }
        s->invalidate |= INVAL_LEDS;
        break;

#if 0
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
#endif

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

static void riscv_cep_fb_ctrl_write(void *opaque, hwaddr addr, 
                                    uint64_t val, unsigned int size)
{

    struct riscv_cep_fb_ctrl_s *ctrl = (struct riscv_cep_fb_ctrl_s*) opaque; 
    switch(addr) {
    case 0: 
        ctrl->Reg_MODE = set_hdmi_mode(ctrl->s, val);
        break;
    case 4:
        ctrl->Reg_ADDR = (uint32_t)val;
        break;
    default:
        break;
    }
}


void riscv_cep_fb_reset(struct riscv_cep_fb_s *s)
{
    memset(s->periph_sta, 0, sizeof(s->periph_sta));
    s->r7segs_mode = R7SEGS_CTL_HALF_LOW;
    s->pushbtn_mode = PUSHBTN_CTL_POLL;
    s->invalidate = INVAL_ALL;

#if 0
    update_7seg(s, 0);
#endif
}

static const GraphicHwOps riscv_cep_board_ops = {
    .invalidate  = riscv_cep_board_invalidate_display,
    .gfx_update  = riscv_cep_board_update_display,
};

static const GraphicHwOps riscv_cep_fb_ops = {
    .invalidate  = riscv_cep_fb_invalidate_display2,
    .gfx_update  = riscv_cep_fb_update_display,
};

static const MemoryRegionOps riscv_cep_periph_op = {
    .read = riscv_cep_periph_read,
    .write = riscv_cep_periph_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps riscv_cep_fb_ctrl_op = {
    .read = riscv_cep_fb_ctrl_read,
    .write = riscv_cep_fb_ctrl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


void riscv_cep_fb_ctrl_init(struct riscv_cep_fb_ctrl_s **ptr, MemoryRegion *sysmem, hwaddr fb_ctrl_offset);
void riscv_cep_fb_ctrl_init(struct riscv_cep_fb_ctrl_s **ptr, MemoryRegion *sysmem, hwaddr fb_ctrl_offset) 
{
    *ptr = (struct riscv_cep_fb_ctrl_s *)
            g_malloc0(sizeof(struct riscv_cep_fb_ctrl_s));
    struct riscv_cep_fb_ctrl_s * ctrl = *ptr;
    memory_region_init_io(&ctrl->mem_fb_ctrl, NULL, &riscv_cep_fb_ctrl_op, ctrl,
                          "riscv_cep_fb_ctrl", 0x8);
    memory_region_add_subregion(sysmem, fb_ctrl_offset, &ctrl->mem_fb_ctrl);
}

void riscv_cep_fb_init(MemoryRegion *sysmem, hwaddr vram_offset,
                     hwaddr periph_offset, qemu_irq pushbtn_irq)
{
    struct riscv_cep_fb_s *s = (struct riscv_cep_fb_s *)
            g_malloc0(sizeof(struct riscv_cep_fb_s));

    s->vram_size           = VRAM_SIZE;
    s->vram_size_effective = VRAM_SIZE_EFFECTIVE_DEFAULT;
    s->vram = g_malloc0(VRAM_SIZE);

    set_hdmi_mode(s, 4);

    s->con_board = graphic_console_init(NULL, 0, &riscv_cep_board_ops, s);
    s->mouse_hdl = qemu_add_mouse_event_handler(riscv_cep_fb_mouse_event, 
                                                s, 1, "riscv_cep_fb mouse");
    s->kbd_hdl = qemu_add_kbd_event_handler(riscv_cep_kbd_event, s);
    s->con_fb = graphic_console_init(NULL, 0, &riscv_cep_fb_ops, s);

    s->pushbtn_irq = pushbtn_irq;

    memory_region_init_ram_ptr(&s->mem_vram, NULL , "riscv_cep_fb_vram",
                               s->vram_size, s->vram);
    memory_region_add_subregion(sysmem, vram_offset, &s->mem_vram);
    memory_region_set_log(&s->mem_vram, true, DIRTY_MEMORY_VGA);

    memory_region_init_io(&s->mem_periph, NULL, &riscv_cep_periph_op, s,
                          "riscv_cep_periph", 0x20);
    memory_region_add_subregion(sysmem, periph_offset, &s->mem_periph);

    riscv_cep_fb_ctrl_init(&s->ctrl, sysmem, 0x70000000);
    s->ctrl->s = s;

    riscv_cep_fb_reset(s);
}
