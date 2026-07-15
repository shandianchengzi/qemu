/*
 * QTest testcase for the STM32F103 TIM2 device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest-single.h"

#define TIM2_BASE_ADDR 0x40000000
#define TIM2_CLOCK_HZ  72000000
#define TIM2_IRQ       28

#define TIM_CR1  0x00
#define TIM_DIER 0x0c
#define TIM_SR   0x10
#define TIM_EGR  0x14
#define TIM_CNT  0x24
#define TIM_PSC  0x28
#define TIM_ARR  0x2c
#define TIM_CCR2 0x38

#define TIM_CR1_CEN    BIT(0)
#define TIM_CR1_UDIS   BIT(1)
#define TIM_CR1_URS    BIT(2)
#define TIM_CR1_OPM    BIT(3)
#define TIM_CR1_ARPE   BIT(7)
#define TIM_DIER_UIE   BIT(0)
#define TIM_DIER_CC2IE BIT(2)
#define TIM_SR_UIF     BIT(0)
#define TIM_SR_CC2IF   BIT(2)
#define TIM_EGR_UG     BIT(0)

static void tim2_writel(unsigned int offset, uint32_t value)
{
    writel(TIM2_BASE_ADDR + offset, value);
}

static uint32_t tim2_readl(unsigned int offset)
{
    return readl(TIM2_BASE_ADDR + offset);
}

/* Return the time needed to advance by @ticks with the given prescaler. */
static int64_t tim2_ticks_to_ns(uint32_t ticks, uint32_t prescaler)
{
    return 1000000000LL * ticks * (prescaler + 1) / TIM2_CLOCK_HZ;
}

/* Reset TIM2 and verify that its interrupt output is inactive. */
static void tim2_reset(void)
{
    qtest_system_reset(global_qtest);
    g_assert_false(get_irq(TIM2_IRQ));
}

/* Program PSC and ARR, then transfer their preload values with UG. */
static void tim2_init(uint32_t prescaler, uint32_t auto_reload)
{
    tim2_writel(TIM_PSC, prescaler);
    tim2_writel(TIM_ARR, auto_reload);
    tim2_writel(TIM_EGR, TIM_EGR_UG);
    tim2_writel(TIM_SR, 0);
}

/* Verify the TIM2 register reset values implemented by the model. */
static void test_reset(void)
{
    tim2_reset();

    g_assert_cmphex(tim2_readl(TIM_CR1), ==, 0);
    g_assert_cmphex(tim2_readl(TIM_DIER), ==, 0);
    g_assert_cmphex(tim2_readl(TIM_SR), ==, 0);
    g_assert_cmphex(tim2_readl(TIM_EGR), ==, 0);
    g_assert_cmphex(tim2_readl(TIM_CNT), ==, 0);
    g_assert_cmphex(tim2_readl(TIM_PSC), ==, 0);
    g_assert_cmphex(tim2_readl(TIM_ARR), ==, 0xffff);
}

/* Verify update timing, status clearing and the level interrupt output. */
static void test_update_irq(void)
{
    const uint32_t prescaler = 71;
    const uint32_t period = 1000;
    const int64_t period_ns = tim2_ticks_to_ns(period, prescaler);

    tim2_reset();
    tim2_init(prescaler, period - 1);
    tim2_writel(TIM_DIER, TIM_DIER_UIE);
    tim2_writel(TIM_CR1, TIM_CR1_CEN);

    clock_step(period_ns - 1);
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, 0);
    g_assert_false(get_irq(TIM2_IRQ));

    clock_step(1);
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, TIM_SR_UIF);
    g_assert_true(get_irq(TIM2_IRQ));

    /* SR is rc_w0: clearing UIF must also deassert the interrupt output. */
    tim2_writel(TIM_SR, 0);
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, 0);
    g_assert_false(get_irq(TIM2_IRQ));
}

/* Verify that clearing CEN pauses the counter. */
static void test_enable_disable(void)
{
    const uint32_t prescaler = 71;
    uint32_t count;

    tim2_reset();
    tim2_init(prescaler, 999);
    tim2_writel(TIM_CR1, TIM_CR1_CEN);

    clock_step(tim2_ticks_to_ns(123, prescaler));
    count = tim2_readl(TIM_CNT);
    g_assert_cmpuint(count, ==, 123);

    tim2_writel(TIM_CR1, 0);
    clock_step(tim2_ticks_to_ns(500, prescaler));
    g_assert_cmpuint(tim2_readl(TIM_CNT), ==, count);
}

/* Verify CC2 flag generation independently from CC2 interrupt enable. */
static void test_output_compare(void)
{
    const uint32_t prescaler = 71;
    const uint32_t period = 1000;

    tim2_reset();
    tim2_init(prescaler, period - 1);
    tim2_writel(TIM_CCR2, 250);
    tim2_writel(TIM_CR1, TIM_CR1_CEN);

    clock_step(tim2_ticks_to_ns(250, prescaler));
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_CC2IF, ==, TIM_SR_CC2IF);
    g_assert_false(get_irq(TIM2_IRQ));

    tim2_writel(TIM_SR, 0);
    tim2_writel(TIM_DIER, TIM_DIER_CC2IE);

    /* The next CCR2 match is one complete period later. */
    clock_step(tim2_ticks_to_ns(period, prescaler));
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_CC2IF, ==, TIM_SR_CC2IF);
    g_assert_true(get_irq(TIM2_IRQ));

    tim2_writel(TIM_SR, 0);
    g_assert_false(get_irq(TIM2_IRQ));
}

/* Verify that a PSC write is transferred by the next update event. */
static void test_prescaler_preload(void)
{
    const uint32_t old_prescaler = 71;
    const uint32_t new_prescaler = 719;
    const uint32_t period = 10;

    tim2_reset();
    tim2_init(old_prescaler, period - 1);
    tim2_writel(TIM_CR1, TIM_CR1_CEN);

    tim2_writel(TIM_PSC, new_prescaler);
    clock_step(tim2_ticks_to_ns(period, old_prescaler));
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, TIM_SR_UIF);
    tim2_writel(TIM_SR, 0);

    clock_step(tim2_ticks_to_ns(period, new_prescaler) - 1);
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, 0);
    clock_step(1);
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, TIM_SR_UIF);
}

/* Verify that UDIS suppresses software update events and preload transfer. */
static void test_update_disable(void)
{
    tim2_reset();
    tim2_writel(TIM_CR1, TIM_CR1_ARPE | TIM_CR1_UDIS);
    tim2_writel(TIM_PSC, 71);
    tim2_writel(TIM_ARR, 9);
    tim2_writel(TIM_EGR, TIM_EGR_UG);

    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, 0);

    /* UDIS keeps the reset PSC/ARR active, so no overflow occurs at 10 us. */
    tim2_writel(TIM_CR1, TIM_CR1_ARPE | TIM_CR1_UDIS | TIM_CR1_CEN);
    clock_step(tim2_ticks_to_ns(10, 71));
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, 0);
}

/* Verify that URS suppresses UIF for UG, but not for counter overflow. */
static void test_update_request_source(void)
{
    const uint32_t prescaler = 71;
    const uint32_t period = 10;

    tim2_reset();
    tim2_writel(TIM_CR1, TIM_CR1_URS);
    tim2_writel(TIM_PSC, prescaler);
    tim2_writel(TIM_ARR, period - 1);
    tim2_writel(TIM_EGR, TIM_EGR_UG);

    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, 0);

    tim2_writel(TIM_CR1, TIM_CR1_URS | TIM_CR1_CEN);
    clock_step(tim2_ticks_to_ns(period, prescaler));
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, TIM_SR_UIF);
}

/* Verify that OPM stops the counter after one update event. */
static void test_one_pulse(void)
{
    const uint32_t prescaler = 71;
    const uint32_t period = 10;
    uint32_t count;

    tim2_reset();
    tim2_init(prescaler, period - 1);
    tim2_writel(TIM_CR1, TIM_CR1_OPM | TIM_CR1_CEN);

    clock_step(tim2_ticks_to_ns(period, prescaler));
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, TIM_SR_UIF);
    g_assert_cmphex(tim2_readl(TIM_CR1) & TIM_CR1_CEN, ==, 0);

    count = tim2_readl(TIM_CNT);
    clock_step(tim2_ticks_to_ns(period, prescaler));
    g_assert_cmpuint(tim2_readl(TIM_CNT), ==, count);
}

/* Verify that ARPE transfers ARR at the next update event. */
static void test_auto_reload_preload(void)
{
    const uint32_t prescaler = 71;
    const uint32_t old_period = 10;
    const uint32_t new_period = 20;

    tim2_reset();
    tim2_init(prescaler, old_period - 1);
    tim2_writel(TIM_CR1, TIM_CR1_ARPE | TIM_CR1_CEN);

    tim2_writel(TIM_ARR, new_period - 1);
    clock_step(tim2_ticks_to_ns(old_period, prescaler));
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, TIM_SR_UIF);
    tim2_writel(TIM_SR, 0);

    clock_step(tim2_ticks_to_ns(new_period, prescaler) - 1);
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, 0);
    clock_step(1);
    g_assert_cmphex(tim2_readl(TIM_SR) & TIM_SR_UIF, ==, TIM_SR_UIF);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("stm32f103/tim2/reset", test_reset);
    qtest_add_func("stm32f103/tim2/update-irq", test_update_irq);
    qtest_add_func("stm32f103/tim2/enable-disable", test_enable_disable);
    qtest_add_func("stm32f103/tim2/output-compare", test_output_compare);
    qtest_add_func("stm32f103/tim2/prescaler-preload",
                   test_prescaler_preload);
    qtest_add_func("stm32f103/tim2/update-disable", test_update_disable);
    qtest_add_func("stm32f103/tim2/update-request-source",
                   test_update_request_source);
    qtest_add_func("stm32f103/tim2/one-pulse", test_one_pulse);
    qtest_add_func("stm32f103/tim2/auto-reload-preload",
                   test_auto_reload_preload);

    qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(global_qtest, "/machine/soc/armv7m");
    ret = g_test_run();
    qtest_end();

    return ret;
}
