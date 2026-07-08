# STM32F103 SPI model notes

Reference manual saved locally:

- `docs/reference/rm0008-stm32f10x-reference-manual.pdf`
- Source: ST RM0008, "STM32F101xx, STM32F102xx, STM32F103xx, STM32F105xx and STM32F107xx advanced Arm-based 32-bit MCUs"

Implemented register scope:

- `SPI_CR1`: stores the STM32F1xx SPI control bits, including `SPE`, `MSTR`, and `DFF`.
- `SPI_CR2`: stores DMA enable and interrupt enable bits. DMA is not modeled, but the bits are preserved.
- `SPI_SR`: models `RXNE`, `TXE`, `CRCERR`, `MODF`, `OVR`, and `BSY` as software-visible status.
- `SPI_DR`: write starts an immediate transfer only when `SPE` is set. With no SSI peripheral attached, the received value is zero.
- `SPI_CRCPR`, `SPI_RXCRCR`, `SPI_TXCRCR`: present for register compatibility. CRC generation is not modeled.
- `SPI_I2SCFGR`, `SPI_I2SPR`: present for compatibility. I2S mode is reported as unimplemented.

Important simplifications:

- Transfers complete immediately; serial clock timing and FIFO depth are not modeled.
- Reading `SPI_DR` clears `RXNE` and the simplified `OVR` condition.
- The combined IRQ line is asserted when enabled `TXE`, `RXNE`, or error status is pending.

F103 SoC integration:

- SPI1: `0x40013000`, IRQ 35
- SPI2: `0x40003800`, IRQ 36
- SPI3: `0x40003c00`, IRQ 51
- The old `SPI2/I2S` unimplemented placeholder at `0x40003800` must not coexist with the real SPI2 device.

Test commands:

```sh
cd /home/muyi/2026_qemu/qemu_f103/build-arm-f103
ninja qemu-system-arm tests/qtest/stm32f103_spi-test
QTEST_QEMU_BINARY=./qemu-system-arm ./tests/qtest/stm32f103_spi-test
```

Basic machine smoke test:

```sh
cd /home/muyi/2026_qemu/qemu_f103/build-arm-f103
./qemu-system-arm -machine help | grep stm32f103
echo AAABIAkAAAD+5w== | base64 -d > /tmp/f103_loop.bin
timeout 3 ./qemu-system-arm -M stm32f103 -kernel /tmp/f103_loop.bin \
  -nographic -serial none -monitor none
```
