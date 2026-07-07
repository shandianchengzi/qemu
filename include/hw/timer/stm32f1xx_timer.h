/*
 * STM32F1XX general-purpose timer
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_STM32F1XX_TIMER_H
#define HW_STM32F1XX_TIMER_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_STM32F1XX_TIMER "stm32f1xx-timer"
typedef struct STM32F1XXTimerState STM32F1XXTimerState;
DECLARE_INSTANCE_CHECKER(STM32F1XXTimerState, STM32F1XXTIMER,
                         TYPE_STM32F1XX_TIMER)

struct STM32F1XXTimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer *timer;
    qemu_irq irq;

    /* Fixed timer input clock until STM32F1 RCC clock outputs are modelled. */
    uint64_t freq_hz;

    int64_t base_ns;
    uint64_t base_cnt;
    uint32_t next_event;

    uint32_t tim_cr1;
    uint32_t tim_cr2;
    uint32_t tim_smcr;
    uint32_t tim_dier;
    uint32_t tim_sr;
    uint32_t tim_ccmr1;
    uint32_t tim_ccmr2;
    uint32_t tim_ccer;
    uint32_t tim_psc;
    uint32_t active_psc;
    uint32_t tim_arr;
    uint32_t active_arr;
    uint32_t tim_ccr[4];
    uint32_t tim_dcr;
    uint32_t tim_dmar;
};

#endif /* HW_STM32F1XX_TIMER_H */
