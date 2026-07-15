/*
 * STM32F1XX SPI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/ssi/stm32f1xx_spi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef STM_F1_SPI_ERR_DEBUG
#define STM_F1_SPI_ERR_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (STM_F1_SPI_ERR_DEBUG >= 1) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

static uint32_t stm32f1xx_spi_data_mask(STM32F1XXSPIState *s)
{
    return (s->spi_cr1 & SPI_F1_CR1_DFF) ? 0xffff : 0xff;
}

static void stm32f1xx_spi_update_irq(STM32F1XXSPIState *s)
{
    bool pending = false;

    if ((s->spi_cr2 & SPI_F1_CR2_TXEIE) && (s->spi_sr & SPI_F1_SR_TXE)) {
        pending = true;
    }

    if ((s->spi_cr2 & SPI_F1_CR2_RXNEIE) && (s->spi_sr & SPI_F1_SR_RXNE)) {
        pending = true;
    }

    if ((s->spi_cr2 & SPI_F1_CR2_ERRIE) &&
        (s->spi_sr & (SPI_F1_SR_CRCERR | SPI_F1_SR_MODF | SPI_F1_SR_OVR))) {
        pending = true;
    }

    qemu_set_irq(s->irq, pending);
}

static void stm32f1xx_spi_reset(DeviceState *dev)
{
    STM32F1XXSPIState *s = STM32F1XX_SPI(dev);

    s->spi_cr1 = 0x00000000;
    s->spi_cr2 = 0x00000000;
    s->spi_sr = SPI_F1_SR_RESET;
    s->spi_dr = 0x00000000;
    s->spi_crcpr = 0x00000007;
    s->spi_rxcrcr = 0x00000000;
    s->spi_txcrcr = 0x00000000;
    s->spi_i2scfgr = 0x00000000;
    s->spi_i2spr = 0x00000002;
    qemu_set_irq(s->irq, 0);
}

static void stm32f1xx_spi_transfer(STM32F1XXSPIState *s)
{
    uint32_t mask = stm32f1xx_spi_data_mask(s);
    uint32_t tx = s->spi_dr & mask;

    DB_PRINT("Data to send: 0x%x\n", tx);

    if (!(s->spi_cr1 & SPI_F1_CR1_SPE)) {
        s->spi_dr = tx;
        s->spi_sr |= SPI_F1_SR_TXE;
        s->spi_sr &= ~SPI_F1_SR_BSY;
        stm32f1xx_spi_update_irq(s);
        return;
    }

    if (s->spi_i2scfgr & SPI_F1_I2SCFGR_I2SMOD) {
        qemu_log_mask(LOG_UNIMP, "%s: I2S mode is not implemented\n",
                      __func__);
        s->spi_dr = tx;
        s->spi_sr |= SPI_F1_SR_TXE;
        stm32f1xx_spi_update_irq(s);
        return;
    }

    if (s->spi_sr & SPI_F1_SR_RXNE) {
        s->spi_sr |= SPI_F1_SR_OVR;
    }

    s->spi_sr &= ~SPI_F1_SR_TXE;
    s->spi_sr |= SPI_F1_SR_BSY;
    s->spi_dr = ssi_transfer(s->ssi, tx) & mask;
    s->spi_sr &= ~SPI_F1_SR_BSY;
    s->spi_sr |= SPI_F1_SR_RXNE | SPI_F1_SR_TXE;
    stm32f1xx_spi_update_irq(s);

    DB_PRINT("Data received: 0x%x\n", s->spi_dr);
}

static uint64_t stm32f1xx_spi_read(void *opaque, hwaddr addr,
                                   unsigned int size)
{
    STM32F1XXSPIState *s = opaque;

    DB_PRINT("Address: 0x%" HWADDR_PRIx "\n", addr);

    switch (addr) {
    case SPI_F1_CR1:
        return s->spi_cr1;
    case SPI_F1_CR2:
        return s->spi_cr2;
    case SPI_F1_SR:
        return s->spi_sr;
    case SPI_F1_DR: {
        uint32_t value = s->spi_dr & stm32f1xx_spi_data_mask(s);

        s->spi_sr &= ~SPI_F1_SR_RXNE;
        s->spi_sr &= ~SPI_F1_SR_OVR;
        stm32f1xx_spi_update_irq(s);
        return value;
    }
    case SPI_F1_CRCPR:
        return s->spi_crcpr;
    case SPI_F1_RXCRCR:
        return s->spi_rxcrcr;
    case SPI_F1_TXCRCR:
        return s->spi_txcrcr;
    case SPI_F1_I2SCFGR:
        return s->spi_i2scfgr;
    case SPI_F1_I2SPR:
        return s->spi_i2spr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }

    return 0;
}

static void stm32f1xx_spi_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    STM32F1XXSPIState *s = opaque;
    uint32_t value = val64;

    DB_PRINT("Address: 0x%" HWADDR_PRIx ", Value: 0x%x\n", addr, value);

    switch (addr) {
    case SPI_F1_CR1:
        s->spi_cr1 = value & SPI_F1_CR1_WRITABLE;
        stm32f1xx_spi_update_irq(s);
        return;
    case SPI_F1_CR2:
        if (value & (SPI_F1_CR2_RXDMAEN | SPI_F1_CR2_TXDMAEN)) {
            qemu_log_mask(LOG_UNIMP, "%s: DMA is not implemented\n",
                          __func__);
        }
        s->spi_cr2 = value & SPI_F1_CR2_WRITABLE;
        stm32f1xx_spi_update_irq(s);
        return;
    case SPI_F1_SR:
        if (!(value & SPI_F1_SR_CRCERR)) {
            s->spi_sr &= ~SPI_F1_SR_CRCERR;
            stm32f1xx_spi_update_irq(s);
        }
        return;
    case SPI_F1_DR:
        s->spi_dr = value & stm32f1xx_spi_data_mask(s);
        stm32f1xx_spi_transfer(s);
        return;
    case SPI_F1_CRCPR:
        s->spi_crcpr = value & 0xffff;
        return;
    case SPI_F1_RXCRCR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Read only register: "
                      "0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    case SPI_F1_TXCRCR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Read only register: "
                      "0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    case SPI_F1_I2SCFGR:
        s->spi_i2scfgr = value & 0x0fff;
        if (s->spi_i2scfgr & SPI_F1_I2SCFGR_I2SMOD) {
            qemu_log_mask(LOG_UNIMP, "%s: I2S mode is not implemented\n",
                          __func__);
        }
        return;
    case SPI_F1_I2SPR:
        s->spi_i2spr = value & 0x03ff;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f1xx_spi_ops = {
    .read = stm32f1xx_spi_read,
    .write = stm32f1xx_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static const VMStateDescription vmstate_stm32f1xx_spi = {
    .name = TYPE_STM32F1XX_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(spi_cr1, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_cr2, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_sr, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_dr, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_crcpr, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_rxcrcr, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_txcrcr, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_i2scfgr, STM32F1XXSPIState),
        VMSTATE_UINT32(spi_i2spr, STM32F1XXSPIState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32f1xx_spi_init(Object *obj)
{
    STM32F1XXSPIState *s = STM32F1XX_SPI(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &stm32f1xx_spi_ops, s,
                          TYPE_STM32F1XX_SPI, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    s->ssi = ssi_create_bus(dev, "ssi");
}

static void stm32f1xx_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, stm32f1xx_spi_reset);
    dc->vmsd = &vmstate_stm32f1xx_spi;
}

static const TypeInfo stm32f1xx_spi_info = {
    .name          = TYPE_STM32F1XX_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F1XXSPIState),
    .instance_init = stm32f1xx_spi_init,
    .class_init    = stm32f1xx_spi_class_init,
};

static void stm32f1xx_spi_register_types(void)
{
    type_register_static(&stm32f1xx_spi_info);
}

type_init(stm32f1xx_spi_register_types)
