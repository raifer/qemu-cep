/*
 * QEMU RISC-V Board Compatible with SiFive Freedom U SDK
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Provides a board compatible with the SiFive Freedom U SDK:
 *
 * 0) UART
 * 1) CLINT (Core Level Interruptor)
 * 2) PLIC (Platform Level Interrupt Controller)
 *
 * This board currently uses a hardcoded devicetree that indicates one hart.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_plic.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/sifive_uart.h"
#include "hw/riscv/sifive_prci.h"
#include "hw/riscv/sifive_u.h"
#include "hw/riscv/cep_riscv.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "exec/address-spaces.h"
#include "elf.h"

#include <libfdt.h>

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} sifive_u_memmap[] = {
    [SIFIVE_U_BRAM] =     { 0x0,        0x0 }, // taille dimensionée par la ligne de commande
    [SIFIVE_U_CLINT] =    { 0x2000000,  0x10000 },
    [SIFIVE_U_PLIC] =     { 0xc000000,  0x4000000 },
    [SIFIVE_U_UART0] =    { 0x10013000, 0x1000 },
    [SIFIVE_U_FB] =       { 0x30000000,  0x20 },
    [SIFIVE_U_VRAM] =     { 0x80000000, 0x10000 },
};

static target_ulong load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf(kernel_filename, NULL, NULL, NULL,
                 &kernel_entry, NULL, &kernel_high,
                 0, EM_RISCV, 1, 0) < 0) {
        error_report("could not load kernel '%s'", kernel_filename);
        exit(1);
    }
    return kernel_entry;
}

static void riscv_sifive_u_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = sifive_u_memmap;

    SiFiveUState *s = g_new0(SiFiveUState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            sizeof(s->soc), TYPE_RISCV_CEP_SOC,
                            &error_abort, NULL);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* register RAM */
    memory_region_init_ram(main_mem, NULL, "riscv.sifive.u.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SIFIVE_U_BRAM].base,
                                main_mem);

    if (machine->kernel_filename) {
        load_kernel(machine->kernel_filename);
    }

}

static void riscv_sifive_u_soc_init(Object *obj)
{
    SiFiveUSoCState *s = RISCV_CEP_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, sizeof(s->cpus),
                            TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
    object_property_set_str(OBJECT(&s->cpus), SIFIVE_U_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), smp_cpus, "num-harts",
                            &error_abort);
}

static void riscv_sifive_u_soc_realize(DeviceState *dev, Error **errp)
{
    SiFiveUSoCState *s = RISCV_CEP_SOC(dev);
    const struct MemmapEntry *memmap = sifive_u_memmap;
    MemoryRegion *system_memory = get_system_memory();

    object_property_set_bool(OBJECT(&s->cpus), true, "realized",
                             &error_abort);

    /* MMIO */
    s->plic = sifive_plic_create(memmap[SIFIVE_U_PLIC].base,
        (char *)SIFIVE_U_PLIC_HART_CONFIG,
        SIFIVE_U_PLIC_NUM_SOURCES,
        SIFIVE_U_PLIC_NUM_PRIORITIES,
        SIFIVE_U_PLIC_PRIORITY_BASE,
        SIFIVE_U_PLIC_PENDING_BASE,
        SIFIVE_U_PLIC_ENABLE_BASE,
        SIFIVE_U_PLIC_ENABLE_STRIDE,
        SIFIVE_U_PLIC_CONTEXT_BASE,
        SIFIVE_U_PLIC_CONTEXT_STRIDE,
        memmap[SIFIVE_U_PLIC].size);
    sifive_uart_create(system_memory, memmap[SIFIVE_U_UART0].base,
        serial_hd(0), qdev_get_gpio_in(DEVICE(s->plic), SIFIVE_U_UART0_IRQ));
    sifive_clint_create(memmap[SIFIVE_U_CLINT].base,
        memmap[SIFIVE_U_CLINT].size, smp_cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE);

       mipscep_fb_init(system_memory, memmap[SIFIVE_U_VRAM].base, memmap[SIFIVE_U_FB].base, qdev_get_gpio_in(DEVICE(s->plic), SIFIVE_U_PUSH_BUTTON_IRQ));

}

static void riscv_sifive_u_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Board compatible with SiFive U SDK";
    mc->init = riscv_sifive_u_init;
    /* The real hardware has 5 CPUs, but one of them is a small embedded power
     * management CPU.
     */
    mc->max_cpus = 4;
}

DEFINE_MACHINE("cep", riscv_sifive_u_machine_init)

static void riscv_sifive_u_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = riscv_sifive_u_soc_realize;
    /* Reason: Uses serial_hds in realize function, thus can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo riscv_sifive_u_soc_type_info = {
    .name = TYPE_RISCV_CEP_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SiFiveUSoCState),
    .instance_init = riscv_sifive_u_soc_init,
    .class_init = riscv_sifive_u_soc_class_init,
};

static void riscv_sifive_u_soc_register_types(void)
{
    type_register_static(&riscv_sifive_u_soc_type_info);
}

type_init(riscv_sifive_u_soc_register_types)
