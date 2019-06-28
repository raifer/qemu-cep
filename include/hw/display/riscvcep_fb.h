
#ifndef RISCVCEP_FB_H
#define RISCVCEP_FB_H

/* riscvcep_fb.c */
struct mipscep_fb_s;
void mipscep_fb_reset(struct mipscep_fb_s *s);
void mipscep_fb_init(MemoryRegion *sysmem, hwaddr vram_offset,
                     hwaddr periph_offset, qemu_irq pushbtn_irq);

#endif
