#ifndef HW_CEP_RISCV_H
#define HW_CEP_RISCV_H

#include "exec/memory.h"
#include "net/net.h"
#include "hw/sysbus.h"

#define TYPE_RISCV_CEP_SOC "riscv.cep.soc"
#define RISCV_U_SOC(obj) \
    OBJECT_CHECK(SiFiveUSoCState, (obj), TYPE_RISCV_U_SOC)
#define RISCV_CEP_SOC(obj) \
    OBJECT_CHECK(SiFiveUSoCState, (obj), TYPE_RISCV_CEP_SOC)

typedef struct SiFiveUSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
} SiFiveUSoCState;

typedef struct SiFiveUState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    SiFiveUSoCState soc;
} SiFiveUState;

enum {
    SIFIVE_U_CLINT,
    SIFIVE_U_PLIC,
    SIFIVE_U_UART0,
    SIFIVE_U_PERIPHS,
    SIFIVE_U_VRAM,
    SIFIVE_U_BRAM
};

enum {
    SIFIVE_U_UART0_IRQ          = 0,
    SIFIVE_U_PUSH_BUTTON_IRQ    = 1,
};

#define SIFIVE_U_PLIC_HART_CONFIG "MS"
#define SIFIVE_U_PLIC_NUM_SOURCES 54
#define SIFIVE_U_PLIC_NUM_PRIORITIES 7
#define SIFIVE_U_PLIC_PRIORITY_BASE 0x04
#define SIFIVE_U_PLIC_PENDING_BASE 0x1000
#define SIFIVE_U_PLIC_ENABLE_BASE 0x2000
#define SIFIVE_U_PLIC_ENABLE_STRIDE 0x80
#define SIFIVE_U_PLIC_CONTEXT_BASE 0x200000
#define SIFIVE_U_PLIC_CONTEXT_STRIDE 0x1000

#if defined(TARGET_RISCV32)
#define SIFIVE_U_CPU TYPE_RISCV_CPU_SIFIVE_U34
#elif defined(TARGET_RISCV64)
#define SIFIVE_U_CPU TYPE_RISCV_CPU_SIFIVE_U54
#endif

#endif
