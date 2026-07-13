/*
 * STM32F1XX USART
 *
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/char/stm32f1xx_usart.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"

static void stm32f1xx_usart_update_irq(STM32F1XXUsartState *s)
{
    if (((s->sr & USART_SR_TXE) && (s->cr1 & USART_CR1_TXEIE)) ||
        ((s->sr & USART_SR_TC) && (s->cr1 & USART_CR1_TCIE)) ||
        ((s->sr & USART_SR_RXNE) && (s->cr1 & USART_CR1_RXNEIE)) ||
        ((s->sr & USART_SR_ORE) && (s->cr1 & USART_CR1_RXNEIE))) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

static int stm32f1xx_usart_can_receive(void *opaque)
{
    return 1;
}

static void stm32f1xx_usart_receive(void *opaque, const uint8_t *buf, int size)
{
    STM32F1XXUsartState *s = opaque;
    int i;

    for (i = 0; i < size; i++) {
        if ((s->cr1 & (USART_CR1_UE | USART_CR1_RE)) !=
            (USART_CR1_UE | USART_CR1_RE)) {
            continue;
        }

        if (s->sr & USART_SR_RXNE) {
            s->sr |= USART_SR_ORE;
            continue;
        }

        s->dr = buf[i];
        s->sr |= USART_SR_RXNE;
    }
    stm32f1xx_usart_update_irq(s);
}

static void stm32f1xx_usart_reset(DeviceState *dev)
{
    STM32F1XXUsartState *s = STM32F1XX_USART(dev);

    s->sr = USART_SR_RESET;
    s->dr = 0x00000000;
    s->brr = 0x00000000;
    s->cr1 = 0x00000000;
    s->cr2 = 0x00000000;
    s->cr3 = 0x00000000;
    s->gtpr = 0x00000000;
    s->sr_read = false;

    stm32f1xx_usart_update_irq(s);
}

static uint64_t stm32f1xx_usart_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    STM32F1XXUsartState *s = opaque;

    switch (addr) {
    case USART_SR:
        s->sr_read = true;
        return s->sr;
    case USART_DR:
        if (s->sr_read) {
            s->sr &= ~USART_SR_ORE;
            s->sr_read = false;
        }
        s->sr &= ~USART_SR_RXNE;
        stm32f1xx_usart_update_irq(s);
        qemu_chr_fe_accept_input(&s->chr);
        return s->dr;
    case USART_BRR:
        return s->brr;
    case USART_CR1:
        return s->cr1;
    case USART_CR2:
        return s->cr2;
    case USART_CR3:
        return s->cr3;
    case USART_GTPR:
        return s->gtpr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

static void stm32f1xx_usart_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    STM32F1XXUsartState *s = opaque;
    uint32_t value = val64;

    switch (addr) {
    case USART_SR:
        if (!(value & USART_SR_TC)) {
            s->sr &= ~USART_SR_TC;
        }
        if (!(value & USART_SR_RXNE)) {
            s->sr &= ~USART_SR_RXNE;
        }
        stm32f1xx_usart_update_irq(s);
        return;
    case USART_DR:
        s->dr = value;
        s->sr &= ~USART_SR_TXE;
        if (s->sr_read) {
            s->sr &= ~USART_SR_TC;
            s->sr_read = false;
        }
        if ((s->cr1 & (USART_CR1_UE | USART_CR1_TE)) ==
            (USART_CR1_UE | USART_CR1_TE)) {
            uint8_t ch = value;

            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            s->sr |= USART_SR_TC;
        }
        s->sr |= USART_SR_TXE;
        stm32f1xx_usart_update_irq(s);
        return;
    case USART_BRR:
        s->brr = value;
        return;
    case USART_CR1:
        s->cr1 = value;
        stm32f1xx_usart_update_irq(s);
        return;
    case USART_CR2:
        s->cr2 = value;
        return;
    case USART_CR3:
        s->cr3 = value;
        return;
    case USART_GTPR:
        s->gtpr = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f1xx_usart_ops = {
    .read = stm32f1xx_usart_read,
    .write = stm32f1xx_usart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static const Property stm32f1xx_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", STM32F1XXUsartState, chr),
};

static void stm32f1xx_usart_init(Object *obj)
{
    STM32F1XXUsartState *s = STM32F1XX_USART(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32f1xx_usart_ops, s,
                          TYPE_STM32F1XX_USART, STM32F1XX_USART_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32f1xx_usart_realize(DeviceState *dev, Error **errp)
{
    STM32F1XXUsartState *s = STM32F1XX_USART(dev);

    qemu_chr_fe_set_handlers(&s->chr, stm32f1xx_usart_can_receive,
                             stm32f1xx_usart_receive, NULL, NULL,
                             s, NULL, true);
}

static void stm32f1xx_usart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, stm32f1xx_usart_reset);
    device_class_set_props(dc, stm32f1xx_usart_properties);
    dc->realize = stm32f1xx_usart_realize;
}

static const TypeInfo stm32f1xx_usart_info = {
    .name          = TYPE_STM32F1XX_USART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F1XXUsartState),
    .instance_init = stm32f1xx_usart_init,
    .class_init    = stm32f1xx_usart_class_init,
};

static void stm32f1xx_usart_register_types(void)
{
    type_register_static(&stm32f1xx_usart_info);
}

type_init(stm32f1xx_usart_register_types)
