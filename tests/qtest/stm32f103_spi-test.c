/*
 * QTest testcase for STM32F103 SPI controllers.
 *
 * The register layout and reset values follow RM0008, "SPI/I2S registers".
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define STM32F103_SPI1_BASE 0x40013000

#define SPI_CR1     0x00
#define SPI_CR2     0x04
#define SPI_SR      0x08
#define SPI_DR      0x0c
#define SPI_CRCPR   0x10
#define SPI_RXCRCR  0x14
#define SPI_TXCRCR  0x18
#define SPI_I2SCFGR 0x1c
#define SPI_I2SPR   0x20

#define SPI_CR1_MSTR     (1 << 2)
#define SPI_CR1_SPE      (1 << 6)
#define SPI_CR1_DFF      (1 << 11)

#define SPI_CR2_RXDMAEN  (1 << 0)
#define SPI_CR2_TXDMAEN  (1 << 1)
#define SPI_CR2_SSOE     (1 << 2)
#define SPI_CR2_ERRIE    (1 << 5)
#define SPI_CR2_RXNEIE   (1 << 6)
#define SPI_CR2_TXEIE    (1 << 7)
#define SPI_CR2_WRITABLE (SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN | SPI_CR2_SSOE | \
                          SPI_CR2_ERRIE | SPI_CR2_RXNEIE | SPI_CR2_TXEIE)

#define SPI_SR_RXNE      (1 << 0)
#define SPI_SR_TXE       (1 << 1)
#define SPI_SR_OVR       (1 << 6)

static uint32_t spi_readl(QTestState *qts, uint32_t reg)
{
    return qtest_readl(qts, STM32F103_SPI1_BASE + reg);
}

static void spi_writel(QTestState *qts, uint32_t reg, uint32_t value)
{
    qtest_writel(qts, STM32F103_SPI1_BASE + reg, value);
}

static uint8_t spi_readb(QTestState *qts, uint32_t reg)
{
    return qtest_readb(qts, STM32F103_SPI1_BASE + reg);
}

static void spi_writeb(QTestState *qts, uint32_t reg, uint8_t value)
{
    qtest_writeb(qts, STM32F103_SPI1_BASE + reg, value);
}

static QTestState *stm32f103_qtest_init(void)
{
    return qtest_init("-machine stm32f103 -monitor none -serial none");
}

static void test_reset_values(void)
{
    QTestState *qts = stm32f103_qtest_init();

    g_assert_cmphex(spi_readl(qts, SPI_CR1), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_CR2), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_SR), ==, SPI_SR_TXE);
    g_assert_cmphex(spi_readl(qts, SPI_DR), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_CRCPR), ==, 7);
    g_assert_cmphex(spi_readl(qts, SPI_RXCRCR), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_TXCRCR), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_I2SCFGR), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_I2SPR), ==, 2);

    qtest_quit(qts);
}

static void test_disabled_write_does_not_receive(void)
{
    QTestState *qts = stm32f103_qtest_init();

    spi_writel(qts, SPI_DR, 0xa5);
    g_assert_cmphex(spi_readl(qts, SPI_SR), ==, SPI_SR_TXE);
    g_assert_cmphex(spi_readl(qts, SPI_DR), ==, 0xa5);
    g_assert_cmphex(spi_readl(qts, SPI_SR), ==, SPI_SR_TXE);

    qtest_quit(qts);
}

static void test_enabled_transfer_sets_rxne(void)
{
    QTestState *qts = stm32f103_qtest_init();

    spi_writel(qts, SPI_CR1, SPI_CR1_MSTR | SPI_CR1_SPE);
    spi_writel(qts, SPI_DR, 0x5a);
    g_assert_cmphex(spi_readl(qts, SPI_SR) & (SPI_SR_TXE | SPI_SR_RXNE), ==,
                    SPI_SR_TXE | SPI_SR_RXNE);

    /*
     * No SSI peripheral is attached in the board, so the bus returns zero.
     * Reading DR clears RXNE.
     */
    g_assert_cmphex(spi_readl(qts, SPI_DR), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_SR) & SPI_SR_RXNE, ==, 0);

    qtest_quit(qts);
}

static void test_byte_transfer_sets_rxne(void)
{
    QTestState *qts = stm32f103_qtest_init();

    spi_writel(qts, SPI_CR1, SPI_CR1_MSTR | SPI_CR1_SPE);
    spi_writeb(qts, SPI_DR, 0x5a);
    g_assert_cmphex(spi_readl(qts, SPI_SR) & (SPI_SR_TXE | SPI_SR_RXNE), ==,
                    SPI_SR_TXE | SPI_SR_RXNE);
    g_assert_cmphex(spi_readb(qts, SPI_DR), ==, 0);
    g_assert_cmphex(spi_readl(qts, SPI_SR) & SPI_SR_RXNE, ==, 0);

    qtest_quit(qts);
}

static void test_overrun_is_reported_and_cleared_by_dr_read(void)
{
    QTestState *qts = stm32f103_qtest_init();

    spi_writel(qts, SPI_CR1, SPI_CR1_MSTR | SPI_CR1_SPE);
    spi_writel(qts, SPI_DR, 0x11);
    spi_writel(qts, SPI_DR, 0x22);
    g_assert_cmphex(spi_readl(qts, SPI_SR) & (SPI_SR_RXNE | SPI_SR_OVR), ==,
                    SPI_SR_RXNE | SPI_SR_OVR);

    spi_readl(qts, SPI_DR);
    g_assert_cmphex(spi_readl(qts, SPI_SR) & (SPI_SR_RXNE | SPI_SR_OVR), ==,
                    0);

    qtest_quit(qts);
}

static void test_write_masks_and_16bit_frame(void)
{
    QTestState *qts = stm32f103_qtest_init();

    spi_writel(qts, SPI_CR2, 0xffffffff);
    g_assert_cmphex(spi_readl(qts, SPI_CR2), ==, SPI_CR2_WRITABLE);

    spi_writel(qts, SPI_DR, 0x1a5);
    g_assert_cmphex(spi_readl(qts, SPI_DR), ==, 0xa5);

    spi_writel(qts, SPI_CR1, SPI_CR1_DFF);
    spi_writel(qts, SPI_DR, 0x1a5);
    g_assert_cmphex(spi_readl(qts, SPI_DR), ==, 0x1a5);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/stm32f103/spi/reset-values", test_reset_values);
    qtest_add_func("/stm32f103/spi/disabled-write",
                   test_disabled_write_does_not_receive);
    qtest_add_func("/stm32f103/spi/enabled-transfer",
                   test_enabled_transfer_sets_rxne);
    qtest_add_func("/stm32f103/spi/byte-transfer",
                   test_byte_transfer_sets_rxne);
    qtest_add_func("/stm32f103/spi/overrun",
                   test_overrun_is_reported_and_cleared_by_dr_read);
    qtest_add_func("/stm32f103/spi/write-masks-and-16bit-frame",
                   test_write_masks_and_16bit_frame);

    return g_test_run();
}
