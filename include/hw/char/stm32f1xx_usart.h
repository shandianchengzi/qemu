/*
 * STM32F1XX USART
 *
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_STM32F1XX_USART_H
#define HW_STM32F1XX_USART_H

#include "hw/core/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define USART_SR   0x00
#define USART_DR   0x04
#define USART_BRR  0x08
#define USART_CR1  0x0c
#define USART_CR2  0x10
#define USART_CR3  0x14
#define USART_GTPR 0x18

#define STM32F1XX_USART_MMIO_SIZE 0x400

#define USART_SR_TXE  (1U << 7)
#define USART_SR_TC   (1U << 6)
#define USART_SR_RXNE (1U << 5)
#define USART_SR_ORE  (1U << 3)

#define USART_SR_RESET 0x000000c0

#define USART_CR1_UE     (1U << 13)
#define USART_CR1_TXEIE  (1U << 7)
#define USART_CR1_TCIE   (1U << 6)
#define USART_CR1_RXNEIE (1U << 5)
#define USART_CR1_TE     (1U << 3)
#define USART_CR1_RE     (1U << 2)

#define TYPE_STM32F1XX_USART "stm32f1xx-usart"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F1XXUsartState, STM32F1XX_USART)

struct STM32F1XXUsartState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharFrontend chr;
    qemu_irq irq;

    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    uint32_t gtpr;

    bool sr_read;
};

#endif /* HW_STM32F1XX_USART_H */
