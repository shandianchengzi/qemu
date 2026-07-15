/*
 * STM32F1XX general-purpose timer
 *
 * This implements the up-counting, update and output-compare functionality
 * needed by STM32F103 TIM2 firmware.  Input capture, PWM output, slave mode,
 * DMA and RCC clock gating are not modelled yet.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/timer/stm32f1xx_timer.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* STM32F1 general-purpose timer register offsets (TIM2 to TIM5). */
#define TIM_CR1      0x00
#define TIM_CR2      0x04
#define TIM_SMCR     0x08
#define TIM_DIER     0x0c
#define TIM_SR       0x10
#define TIM_EGR      0x14
#define TIM_CCMR1    0x18
#define TIM_CCMR2    0x1c
#define TIM_CCER     0x20
#define TIM_CNT      0x24
#define TIM_PSC      0x28
#define TIM_ARR      0x2c
#define TIM_CCR1     0x34
#define TIM_CCR2     0x38
#define TIM_CCR3     0x3c
#define TIM_CCR4     0x40
#define TIM_DCR      0x48
#define TIM_DMAR     0x4c

#define TIM_CR1_CEN  (1 << 0)
#define TIM_CR1_UDIS (1 << 1)
#define TIM_CR1_URS  (1 << 2)
#define TIM_CR1_OPM  (1 << 3)
#define TIM_CR1_ARPE (1 << 7)

#define TIM_CR1_SUPPORTED_MASK (TIM_CR1_CEN | TIM_CR1_UDIS | TIM_CR1_URS | \
                                TIM_CR1_OPM | TIM_CR1_ARPE)

#define TIM_DIER_UIE   (1 << 0)
#define TIM_DIER_CC1IE (1 << 1)
#define TIM_DIER_CC2IE (1 << 2)
#define TIM_DIER_CC3IE (1 << 3)
#define TIM_DIER_CC4IE (1 << 4)
#define TIM_DIER_IRQ_MASK (TIM_DIER_UIE | TIM_DIER_CC1IE | TIM_DIER_CC2IE | \
                           TIM_DIER_CC3IE | TIM_DIER_CC4IE)

#define TIM_SR_UIF   (1 << 0)
#define TIM_SR_CC1IF (1 << 1)
#define TIM_SR_CC2IF (1 << 2)
#define TIM_SR_CC3IF (1 << 3)
#define TIM_SR_CC4IF (1 << 4)
#define TIM_SR_IRQ_MASK (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | \
                         TIM_SR_CC3IF | TIM_SR_CC4IF)

#define TIM_EGR_UG   (1 << 0)
#define TIM_EGR_CC1G (1 << 1)
#define TIM_EGR_CC2G (1 << 2)
#define TIM_EGR_CC3G (1 << 3)
#define TIM_EGR_CC4G (1 << 4)
#define TIM_EGR_EVENT_MASK (TIM_EGR_UG | TIM_EGR_CC1G | TIM_EGR_CC2G | \
                            TIM_EGR_CC3G | TIM_EGR_CC4G)

#define STM32F1XX_TIMER_REG_SIZE 0x400

static uint64_t stm32f1xx_timer_ns_to_ticks(STM32F1XXTimerState *s,
                                             int64_t ns)
{
    uint64_t input_ticks;

    if (ns <= 0) {
        return 0;
    }

    input_ticks = muldiv64(ns, s->freq_hz, NANOSECONDS_PER_SECOND);
    return input_ticks / (s->active_psc + 1);
}

static int64_t stm32f1xx_timer_ticks_to_ns(STM32F1XXTimerState *s,
                                            uint64_t ticks)
{
    uint64_t input_ticks = ticks * (s->active_psc + 1ULL);

    return muldiv64_round_up(input_ticks, NANOSECONDS_PER_SECOND,
                             s->freq_hz);
}

static uint64_t stm32f1xx_timer_get_count(STM32F1XXTimerState *s, int64_t now)
{
    uint64_t period = s->active_arr + 1ULL;
    uint64_t elapsed = 0;

    if (s->tim_cr1 & TIM_CR1_CEN) {
        elapsed = stm32f1xx_timer_ns_to_ticks(s, now - s->base_ns);
    }

    return (s->base_cnt + elapsed) % period;
}

static void stm32f1xx_timer_latch_count(STM32F1XXTimerState *s, int64_t now)
{
    s->base_cnt = stm32f1xx_timer_get_count(s, now);
    s->base_ns = now;
}

static void stm32f1xx_timer_update_irq(STM32F1XXTimerState *s)
{
    qemu_set_irq(s->irq, (s->tim_sr & s->tim_dier & TIM_SR_IRQ_MASK) != 0);
}

static bool stm32f1xx_timer_channel_is_output(STM32F1XXTimerState *s,
                                               unsigned int channel)
{
    uint32_t ccmr = channel < 2 ? s->tim_ccmr1 : s->tim_ccmr2;
    unsigned int shift = (channel & 1) * 8;

    return ((ccmr >> shift) & 3) == 0;
}

static void stm32f1xx_timer_find_compare(STM32F1XXTimerState *s,
                                         unsigned int channel,
                                         uint64_t period, uint64_t count,
                                         uint64_t *best_delta,
                                         uint32_t *next_event)
{
    uint64_t delta;
    uint32_t flag = TIM_SR_CC1IF << channel;

    if (!stm32f1xx_timer_channel_is_output(s, channel) ||
        s->tim_ccr[channel] > s->active_arr) {
        return;
    }

    delta = (s->tim_ccr[channel] + period - count) % period;
    if (delta == 0) {
        delta = period;
    }

    if (delta < *best_delta) {
        *best_delta = delta;
        *next_event = flag;
    } else if (delta == *best_delta) {
        *next_event |= flag;
    }
}

static void stm32f1xx_timer_set_alarm(STM32F1XXTimerState *s, int64_t now)
{
    uint64_t count;
    uint64_t elapsed;
    uint64_t period;
    uint64_t ticks;
    int64_t deadline;
    unsigned int channel;

    if (!(s->tim_cr1 & TIM_CR1_CEN) || s->active_arr == 0) {
        s->next_event = 0;
        timer_del(s->timer);
        return;
    }

    period = s->active_arr + 1ULL;
    elapsed = stm32f1xx_timer_ns_to_ticks(s, now - s->base_ns);
    count = (s->base_cnt + elapsed) % period;
    ticks = period - count;
    s->next_event = TIM_SR_UIF;

    for (channel = 0; channel < ARRAY_SIZE(s->tim_ccr); channel++) {
        stm32f1xx_timer_find_compare(s, channel, period, count, &ticks,
                                     &s->next_event);
    }

    deadline = s->base_ns +
               stm32f1xx_timer_ticks_to_ns(s, elapsed + ticks);
    timer_mod(s->timer, MAX(deadline, now + 1));
}

static void stm32f1xx_timer_update_event(STM32F1XXTimerState *s,
                                         bool set_uif)
{
    if (s->tim_cr1 & TIM_CR1_UDIS) {
        return;
    }

    s->active_psc = s->tim_psc;
    s->active_arr = s->tim_arr;
    if (set_uif) {
        s->tim_sr |= TIM_SR_UIF;
    }
    if (s->tim_cr1 & TIM_CR1_OPM) {
        s->tim_cr1 &= ~TIM_CR1_CEN;
    }
}

static void stm32f1xx_timer_expire(void *opaque)
{
    STM32F1XXTimerState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t event = s->next_event;

    stm32f1xx_timer_latch_count(s, now);
    s->tim_sr |= event & ~TIM_SR_UIF;

    if (event & TIM_SR_UIF) {
        stm32f1xx_timer_update_event(s, true);
    }

    stm32f1xx_timer_update_irq(s);
    stm32f1xx_timer_set_alarm(s, now);
}

static void stm32f1xx_timer_reset(DeviceState *dev)
{
    STM32F1XXTimerState *s = STM32F1XXTIMER(dev);

    if (s->timer) {
        timer_del(s->timer);
    }

    s->base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->base_cnt = 0;
    s->next_event = 0;
    s->tim_cr1 = 0;
    s->tim_cr2 = 0;
    s->tim_smcr = 0;
    s->tim_dier = 0;
    s->tim_sr = 0;
    s->tim_ccmr1 = 0;
    s->tim_ccmr2 = 0;
    s->tim_ccer = 0;
    s->tim_psc = 0;
    s->active_psc = 0;
    s->tim_arr = 0xffff;
    s->active_arr = 0xffff;
    memset(s->tim_ccr, 0, sizeof(s->tim_ccr));
    s->tim_dcr = 0;
    s->tim_dmar = 0;
    qemu_set_irq(s->irq, 0);
}

static uint64_t stm32f1xx_timer_read(void *opaque, hwaddr offset,
                                     unsigned int size)
{
    STM32F1XXTimerState *s = opaque;

    switch (offset) {
    case TIM_CR1:
        return s->tim_cr1;
    case TIM_CR2:
        return s->tim_cr2;
    case TIM_SMCR:
        return s->tim_smcr;
    case TIM_DIER:
        return s->tim_dier;
    case TIM_SR:
        return s->tim_sr;
    case TIM_EGR:
        return 0;
    case TIM_CCMR1:
        return s->tim_ccmr1;
    case TIM_CCMR2:
        return s->tim_ccmr2;
    case TIM_CCER:
        return s->tim_ccer;
    case TIM_CNT:
        return stm32f1xx_timer_get_count(
            s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    case TIM_PSC:
        return s->tim_psc;
    case TIM_ARR:
        return s->tim_arr;
    case TIM_CCR1:
    case TIM_CCR2:
    case TIM_CCR3:
    case TIM_CCR4:
        return s->tim_ccr[(offset - TIM_CCR1) / 4];
    case TIM_DCR:
        return s->tim_dcr;
    case TIM_DMAR:
        return s->tim_dmar;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad offset 0x%" HWADDR_PRIx "\n", __func__,
                      offset);
        return 0;
    }
}

static void stm32f1xx_timer_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned int size)
{
    STM32F1XXTimerState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    unsigned int channel;

    switch (offset) {
    case TIM_CR1:
        stm32f1xx_timer_latch_count(s, now);
        s->tim_cr1 = value & TIM_CR1_SUPPORTED_MASK;
        if (!(s->tim_cr1 & TIM_CR1_ARPE)) {
            s->active_arr = s->tim_arr;
        }
        break;
    case TIM_CR2:
        s->tim_cr2 = value & 0xffff;
        return;
    case TIM_SMCR:
        s->tim_smcr = value & 0xffff;
        return;
    case TIM_DIER:
        s->tim_dier = value & TIM_DIER_IRQ_MASK;
        stm32f1xx_timer_update_irq(s);
        return;
    case TIM_SR:
        s->tim_sr &= value & TIM_SR_IRQ_MASK;
        stm32f1xx_timer_update_irq(s);
        return;
    case TIM_EGR:
        value &= TIM_EGR_EVENT_MASK;
        if (value & TIM_EGR_UG) {
            stm32f1xx_timer_latch_count(s, now);
            s->base_cnt = 0;
            stm32f1xx_timer_update_event(
                s, !(s->tim_cr1 & TIM_CR1_URS));
        }
        s->tim_sr |= value & (TIM_SR_CC1IF | TIM_SR_CC2IF |
                              TIM_SR_CC3IF | TIM_SR_CC4IF);
        stm32f1xx_timer_update_irq(s);
        break;
    case TIM_CCMR1:
        s->tim_ccmr1 = value & 0xffff;
        break;
    case TIM_CCMR2:
        s->tim_ccmr2 = value & 0xffff;
        break;
    case TIM_CCER:
        s->tim_ccer = value & 0xffff;
        return;
    case TIM_CNT:
        s->base_cnt = (value & 0xffff) % (s->active_arr + 1ULL);
        s->base_ns = now;
        break;
    case TIM_PSC:
        s->tim_psc = value & 0xffff;
        return;
    case TIM_ARR:
        stm32f1xx_timer_latch_count(s, now);
        s->tim_arr = value & 0xffff;
        if (!(s->tim_cr1 & TIM_CR1_ARPE)) {
            s->active_arr = s->tim_arr;
            s->base_cnt %= s->active_arr + 1ULL;
        }
        break;
    case TIM_CCR1:
    case TIM_CCR2:
    case TIM_CCR3:
    case TIM_CCR4:
        channel = (offset - TIM_CCR1) / 4;
        s->tim_ccr[channel] = value & 0xffff;
        break;
    case TIM_DCR:
        s->tim_dcr = value & 0xffff;
        return;
    case TIM_DMAR:
        s->tim_dmar = value & 0xffff;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad offset 0x%" HWADDR_PRIx "\n", __func__,
                      offset);
        return;
    }

    stm32f1xx_timer_set_alarm(s, now);
}

static const MemoryRegionOps stm32f1xx_timer_ops = {
    .read = stm32f1xx_timer_read,
    .write = stm32f1xx_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
};

static int stm32f1xx_timer_pre_save(void *opaque)
{
    STM32F1XXTimerState *s = opaque;

    stm32f1xx_timer_latch_count(
        s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    return 0;
}

static int stm32f1xx_timer_post_load(void *opaque, int version_id)
{
    STM32F1XXTimerState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->base_ns = now;
    stm32f1xx_timer_update_irq(s);
    stm32f1xx_timer_set_alarm(s, now);
    return 0;
}

static const VMStateDescription vmstate_stm32f1xx_timer = {
    .name = TYPE_STM32F1XX_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = stm32f1xx_timer_pre_save,
    .post_load = stm32f1xx_timer_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(base_ns, STM32F1XXTimerState),
        VMSTATE_UINT64(base_cnt, STM32F1XXTimerState),
        VMSTATE_UINT32(next_event, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_cr1, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_cr2, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_smcr, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_dier, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_sr, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_ccmr1, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_ccmr2, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_ccer, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_psc, STM32F1XXTimerState),
        VMSTATE_UINT32(active_psc, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_arr, STM32F1XXTimerState),
        VMSTATE_UINT32(active_arr, STM32F1XXTimerState),
        VMSTATE_UINT32_ARRAY(tim_ccr, STM32F1XXTimerState, 4),
        VMSTATE_UINT32(tim_dcr, STM32F1XXTimerState),
        VMSTATE_UINT32(tim_dmar, STM32F1XXTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property stm32f1xx_timer_properties[] = {
    DEFINE_PROP_UINT64("clock-frequency", STM32F1XXTimerState,
                       freq_hz, 72000000),
};

static void stm32f1xx_timer_init(Object *obj)
{
    STM32F1XXTimerState *s = STM32F1XXTIMER(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    memory_region_init_io(&s->iomem, obj, &stm32f1xx_timer_ops, s,
                          TYPE_STM32F1XX_TIMER, STM32F1XX_TIMER_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void stm32f1xx_timer_realize(DeviceState *dev, Error **errp)
{
    STM32F1XXTimerState *s = STM32F1XXTIMER(dev);

    if (s->freq_hz == 0 || s->freq_hz > UINT32_MAX) {
        error_setg(errp, "clock-frequency must be between 1 and %u Hz",
                   UINT32_MAX);
        return;
    }

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, stm32f1xx_timer_expire, s);
}

static void stm32f1xx_timer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, stm32f1xx_timer_reset);
    device_class_set_props(dc, stm32f1xx_timer_properties);
    dc->realize = stm32f1xx_timer_realize;
    dc->vmsd = &vmstate_stm32f1xx_timer;
}

static const TypeInfo stm32f1xx_timer_info = {
    .name = TYPE_STM32F1XX_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F1XXTimerState),
    .instance_init = stm32f1xx_timer_init,
    .class_init = stm32f1xx_timer_class_init,
};

static void stm32f1xx_timer_register_types(void)
{
    type_register_static(&stm32f1xx_timer_info);
}

type_init(stm32f1xx_timer_register_types)
