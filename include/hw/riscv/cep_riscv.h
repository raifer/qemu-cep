#ifndef HW_CEP_RISCV_H
#define HW_CEP_RISCV_H

#include "exec/memory.h"

/* riscvcep_fb.c */
struct mipscep_fb_s;
void mipscep_fb_reset(struct mipscep_fb_s *s);
void mipscep_fb_init(MemoryRegion *sysmem, hwaddr vram_offset,
                     hwaddr periph_offset, qemu_irq pushbtn_irq);

#endif
