#ifndef HW_CEP_RISCV_H
#define HW_CEP_RISCV_H

#include "exec/memory.h"
#include "net/net.h"
#include "hw/sysbus.h"

#define TYPE_RISCV_CEP_SOC "riscv.cep.soc"
#define RISCV_U_SOC(obj) \
    OBJECT_CHECK(CepSoCState, (obj), TYPE_RISCV_U_SOC)
#define RISCV_CEP_SOC(obj) \
    OBJECT_CHECK(CepSoCState, (obj), TYPE_RISCV_CEP_SOC)

typedef struct CepSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
} CepSoCState;

typedef struct CepState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    CepSoCState soc;
} CepState;

enum {
    CEP_CLINT,
    CEP_PLIC,
    CEP_UART0,
    CEP_PERIPHS,
    CEP_VRAM,
    CEP_BRAM,
    CEP_TEST
};

enum {
    CEP_UART0_IRQ          = 0,
    CEP_PUSH_BUTTON_IRQ    = 1,
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
