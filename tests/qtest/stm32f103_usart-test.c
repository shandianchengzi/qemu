/*
 * QTest testcase for STM32F103 USART
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define USART1_BASE_ADDR 0x40013800

#define USART_SR   0x00
#define USART_DR   0x04
#define USART_BRR  0x08
#define USART_CR1  0x0c
#define USART_CR2  0x10
#define USART_CR3  0x14
#define USART_GTPR 0x18

#define USART_SR_TXE  (1U << 7)
#define USART_SR_TC   (1U << 6)
#define USART_SR_RXNE (1U << 5)
#define USART_SR_ORE  (1U << 3)

#define USART_CR1_UE     (1U << 13)
#define USART_CR1_TXEIE  (1U << 7)
#define USART_CR1_TCIE   (1U << 6)
#define USART_CR1_RXNEIE (1U << 5)
#define USART_CR1_TE     (1U << 3)
#define USART_CR1_RE     (1U << 2)

#define NVIC_ISPR1 0xe000e204
#define NVIC_ICPR1 0xe000e284
#define USART1_IRQ 37

static bool check_nvic_pending(QTestState *qts, unsigned int n)
{
    assert(n > 32);
    n -= 32;
    return qtest_readl(qts, NVIC_ISPR1) & (1U << n);
}

static void clear_nvic_pending(QTestState *qts, unsigned int n)
{
    assert(n > 32);
    n -= 32;
    qtest_writel(qts, NVIC_ICPR1, 1U << n);
}

static bool usart_wait_for_flag(QTestState *qts, uint32_t reg,
                                uint32_t flag)
{
    int64_t end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (g_get_monotonic_time() < end_time) {
        if (qtest_readl(qts, reg) & flag) {
            return true;
        }
        g_usleep(1000);
    }

    return false;
}

static bool usart_wait_for_no_flag(QTestState *qts, uint32_t reg,
                                   uint32_t flag)
{
    int64_t end_time = g_get_monotonic_time() + 100 * G_TIME_SPAN_MILLISECOND;

    while (g_get_monotonic_time() < end_time) {
        if (qtest_readl(qts, reg) & flag) {
            return false;
        }
        g_usleep(1000);
    }

    return true;
}

static void usart_init(QTestState *qts, uint32_t cr1)
{
    qtest_writel(qts, USART1_BASE_ADDR + USART_BRR, 0x45);
    qtest_writel(qts, USART1_BASE_ADDR + USART_CR1, cr1);
}

static void assert_no_socket_input(int sock_fd)
{
    GPollFD pfd = {
        .fd = sock_fd,
        .events = G_IO_IN,
    };

    g_assert_cmpint(g_poll(&pfd, 1, 100), ==, 0);
}

static void test_reset(void)
{
    uint32_t sr;
    QTestState *qts = qtest_init("-M stm32f103");

    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_cmphex(sr & (USART_SR_TXE | USART_SR_TC | USART_SR_RXNE), ==,
                    USART_SR_TXE | USART_SR_TC);

    qtest_quit(qts);
}

static void test_registers(void)
{
    QTestState *qts = qtest_init("-M stm32f103");

    qtest_writel(qts, USART1_BASE_ADDR + USART_BRR, 0x1234);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_BRR), ==,
                    0x1234);

    qtest_writel(qts, USART1_BASE_ADDR + USART_CR1, USART_CR1_UE);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_CR1), ==,
                    USART_CR1_UE);

    qtest_writel(qts, USART1_BASE_ADDR + USART_CR2, 0x3456);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_CR2), ==,
                    0x3456);

    qtest_writel(qts, USART1_BASE_ADDR + USART_CR3, 0x5678);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_CR3), ==,
                    0x5678);

    qtest_writel(qts, USART1_BASE_ADDR + USART_GTPR, 0x9abc);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_GTPR), ==,
                    0x9abc);

    qtest_quit(qts);
}

static void test_tx(void)
{
    int sock_fd;
    char ch;
    QTestState *qts = qtest_init_with_serial("-M stm32f103", &sock_fd);

    qtest_writel(qts, USART1_BASE_ADDR + USART_CR1, 0);
    qtest_writel(qts, USART1_BASE_ADDR + USART_DR, 'A');
    assert_no_socket_input(sock_fd);

    usart_init(qts, USART_CR1_UE | USART_CR1_TE);
    qtest_writel(qts, USART1_BASE_ADDR + USART_DR, 'A');
    g_assert_cmpint(recv(sock_fd, &ch, 1, 0), ==, 1);
    g_assert_cmphex(ch, ==, 'A');

    close(sock_fd);
    qtest_quit(qts);
}

static void test_rx(void)
{
    int sock_fd;
    uint32_t sr;
    QTestState *qts = qtest_init_with_serial("-M stm32f103", &sock_fd);

    usart_init(qts, USART_CR1_UE | USART_CR1_RE);

    g_assert_cmpint(send(sock_fd, "B", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_RXNE));
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_DR), ==, 'B');

    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_RXNE);

    close(sock_fd);
    qtest_quit(qts);
}

static void test_rx_irq(void)
{
    int sock_fd;
    QTestState *qts = qtest_init_with_serial("-M stm32f103", &sock_fd);

    usart_init(qts, USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE);

    g_assert_cmpint(send(sock_fd, "C", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_RXNE));
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));

    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_DR), ==, 'C');
    clear_nvic_pending(qts, USART1_IRQ);
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    close(sock_fd);
    qtest_quit(qts);
}

static void test_tc_clear_sequence(void)
{
    uint32_t sr;
    QTestState *qts = qtest_init("-M stm32f103");

    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_true(sr & USART_SR_TC);

    qtest_writel(qts, USART1_BASE_ADDR + USART_DR, 'D');
    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_TC);
    g_assert_true(sr & USART_SR_TXE);

    qtest_quit(qts);
}

static void test_sr_clear(void)
{
    int sock_fd;
    uint32_t sr;
    QTestState *qts = qtest_init_with_serial("-M stm32f103", &sock_fd);

    qtest_writel(qts, USART1_BASE_ADDR + USART_SR,
                 USART_SR_TXE | USART_SR_RXNE | USART_SR_ORE);
    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_TC);

    qtest_writel(qts, USART1_BASE_ADDR + USART_SR,
                 USART_SR_TXE | USART_SR_TC | USART_SR_RXNE |
                 USART_SR_ORE);
    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_TC);
    g_assert_false(sr & USART_SR_RXNE);
    g_assert_false(sr & USART_SR_ORE);

    usart_init(qts, USART_CR1_UE | USART_CR1_RE);
    g_assert_cmpint(send(sock_fd, "E", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_RXNE));

    qtest_writel(qts, USART1_BASE_ADDR + USART_SR,
                 USART_SR_TXE | USART_SR_TC | USART_SR_ORE);
    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_RXNE);

    g_assert_cmpint(send(sock_fd, "F", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_RXNE));
    g_assert_cmpint(send(sock_fd, "G", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_ORE));

    qtest_writel(qts, USART1_BASE_ADDR + USART_SR,
                 USART_SR_TXE | USART_SR_TC | USART_SR_RXNE);
    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_true(sr & USART_SR_ORE);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_DR), ==, 'F');

    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_RXNE);
    g_assert_false(sr & USART_SR_ORE);

    close(sock_fd);
    qtest_quit(qts);
}

static void test_overrun(void)
{
    int sock_fd;
    uint32_t sr;
    QTestState *qts = qtest_init_with_serial("-M stm32f103", &sock_fd);

    usart_init(qts, USART_CR1_UE | USART_CR1_RE);

    g_assert_cmpint(send(sock_fd, "A", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_RXNE));

    g_assert_cmpint(send(sock_fd, "B", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_ORE));
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_DR), ==, 'A');

    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_RXNE);
    g_assert_false(sr & USART_SR_ORE);

    close(sock_fd);
    qtest_quit(qts);
}

static void test_overrun_irq(void)
{
    int sock_fd;
    QTestState *qts = qtest_init_with_serial("-M stm32f103", &sock_fd);

    usart_init(qts, USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE);

    g_assert_cmpint(send(sock_fd, "A", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_RXNE));
    clear_nvic_pending(qts, USART1_IRQ);

    g_assert_cmpint(send(sock_fd, "B", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_ORE));
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));

    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_DR), ==, 'A');
    clear_nvic_pending(qts, USART1_IRQ);
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    close(sock_fd);
    qtest_quit(qts);
}

static void test_tx_irq(void)
{
    QTestState *qts = qtest_init("-M stm32f103");

    qtest_writel(qts, USART1_BASE_ADDR + USART_CR1, USART_CR1_TXEIE);
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));

    qtest_writel(qts, USART1_BASE_ADDR + USART_CR1, 0);
    clear_nvic_pending(qts, USART1_IRQ);
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    qtest_writel(qts, USART1_BASE_ADDR + USART_CR1, USART_CR1_TCIE);
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));

    qtest_writel(qts, USART1_BASE_ADDR + USART_SR, USART_SR_TXE);
    clear_nvic_pending(qts, USART1_IRQ);
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    qtest_quit(qts);
}

static void check_rx_gate(uint32_t cr1, char ch)
{
    int sock_fd;
    QTestState *qts = qtest_init_with_serial("-M stm32f103", &sock_fd);

    usart_init(qts, cr1);

    g_assert_cmpint(send(sock_fd, &ch, 1, 0), ==, 1);
    g_assert_true(usart_wait_for_no_flag(qts, USART1_BASE_ADDR + USART_SR,
                                         USART_SR_RXNE));
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    close(sock_fd);
    qtest_quit(qts);
}

static void test_rx_gate(void)
{
    int sock_fd;
    uint32_t sr;
    QTestState *qts;

    check_rx_gate(USART_CR1_RE | USART_CR1_RXNEIE, 'A');
    check_rx_gate(USART_CR1_UE | USART_CR1_RXNEIE, 'B');

    qts = qtest_init_with_serial("-M stm32f103", &sock_fd);
    usart_init(qts, USART_CR1_UE | USART_CR1_RE);

    g_assert_cmpint(send(sock_fd, "C", 1, 0), ==, 1);
    g_assert_true(usart_wait_for_flag(qts, USART1_BASE_ADDR + USART_SR,
                                      USART_SR_RXNE));
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + USART_DR), ==, 'C');

    sr = qtest_readl(qts, USART1_BASE_ADDR + USART_SR);
    g_assert_false(sr & USART_SR_RXNE);

    close(sock_fd);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("stm32f103/usart/reset", test_reset);
    qtest_add_func("stm32f103/usart/registers", test_registers);
    qtest_add_func("stm32f103/usart/tx", test_tx);
    qtest_add_func("stm32f103/usart/rx", test_rx);
    qtest_add_func("stm32f103/usart/rx_irq", test_rx_irq);
    qtest_add_func("stm32f103/usart/tc_clear_sequence",
                   test_tc_clear_sequence);
    qtest_add_func("stm32f103/usart/sr_clear", test_sr_clear);
    qtest_add_func("stm32f103/usart/overrun", test_overrun);
    qtest_add_func("stm32f103/usart/overrun_irq", test_overrun_irq);
    qtest_add_func("stm32f103/usart/tx_irq", test_tx_irq);
    qtest_add_func("stm32f103/usart/rx_gate", test_rx_gate);

    return g_test_run();
}
