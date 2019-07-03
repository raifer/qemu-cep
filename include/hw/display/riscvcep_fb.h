
#ifndef RISCVCEP_FB_H
#define RISCVCEP_FB_H

/* riscvcep_fb.c */
struct riscv_cep_fb_s;
void riscv_cep_fb_reset(struct riscv_cep_fb_s *s);
void riscv_cep_fb_init(MemoryRegion *sysmem, hwaddr vram_offset,
                     hwaddr periph_offset, qemu_irq pushbtn_irq);

#endif
