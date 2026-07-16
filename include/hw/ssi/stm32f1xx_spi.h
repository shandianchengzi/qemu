/*
 * STM32F1XX SPI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_STM32F1XX_SPI_H
#define HW_STM32F1XX_SPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define SPI_F1_CR1     0x00
#define SPI_F1_CR2     0x04
#define SPI_F1_SR      0x08
#define SPI_F1_DR      0x0C
#define SPI_F1_CRCPR   0x10
#define SPI_F1_RXCRCR  0x14
#define SPI_F1_TXCRCR  0x18
#define SPI_F1_I2SCFGR 0x1C
#define SPI_F1_I2SPR   0x20

#define SPI_F1_CR1_CPHA     (1 << 0)
#define SPI_F1_CR1_CPOL     (1 << 1)
#define SPI_F1_CR1_MSTR     (1 << 2)
#define SPI_F1_CR1_BR_MASK  (7 << 3)
#define SPI_F1_CR1_SPE      (1 << 6)
#define SPI_F1_CR1_LSBFIRST (1 << 7)
#define SPI_F1_CR1_SSI      (1 << 8)
#define SPI_F1_CR1_SSM      (1 << 9)
#define SPI_F1_CR1_RXONLY   (1 << 10)
#define SPI_F1_CR1_DFF      (1 << 11)
#define SPI_F1_CR1_CRCNEXT  (1 << 12)
#define SPI_F1_CR1_CRCEN    (1 << 13)
#define SPI_F1_CR1_BIDIOE   (1 << 14)
#define SPI_F1_CR1_BIDIMODE (1 << 15)
#define SPI_F1_CR1_WRITABLE 0xffff

#define SPI_F1_CR2_RXDMAEN  (1 << 0)
#define SPI_F1_CR2_TXDMAEN  (1 << 1)
#define SPI_F1_CR2_SSOE     (1 << 2)
#define SPI_F1_CR2_ERRIE    (1 << 5)
#define SPI_F1_CR2_RXNEIE   (1 << 6)
#define SPI_F1_CR2_TXEIE    (1 << 7)
#define SPI_F1_CR2_WRITABLE 0x00e7

#define SPI_F1_SR_RXNE      (1 << 0)
#define SPI_F1_SR_TXE       (1 << 1)
#define SPI_F1_SR_CHSIDE    (1 << 2)
#define SPI_F1_SR_UDR       (1 << 3)
#define SPI_F1_SR_CRCERR    (1 << 4)
#define SPI_F1_SR_MODF      (1 << 5)
#define SPI_F1_SR_OVR       (1 << 6)
#define SPI_F1_SR_BSY       (1 << 7)
#define SPI_F1_SR_RESET     SPI_F1_SR_TXE

#define SPI_F1_I2SCFGR_I2SMOD (1 << 11)

#define TYPE_STM32F1XX_SPI "stm32f1xx-spi"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F1XXSPIState, STM32F1XX_SPI)

struct STM32F1XXSPIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t spi_cr1;
    uint32_t spi_cr2;
    uint32_t spi_sr;
    uint32_t spi_dr;
    uint32_t spi_crcpr;
    uint32_t spi_rxcrcr;
    uint32_t spi_txcrcr;
    uint32_t spi_i2scfgr;
    uint32_t spi_i2spr;

    qemu_irq irq;
    SSIBus *ssi;
};

#endif /* HW_STM32F1XX_SPI_H */
