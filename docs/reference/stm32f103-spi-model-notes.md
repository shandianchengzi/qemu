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
- `SPI_DR` supports byte, halfword, and word MMIO accesses. This matters for
  STM32 HAL 8-bit transfers, which write the data register with `strb`.
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

External firmware smoke tests:

The SEmu P2IM F103 unit-test dataset provides ready-to-run STM32F103 ELF
firmware images. Keep them outside this QEMU worktree to avoid adding large
test binaries to git status.

```sh
mkdir -p /tmp/semu_f103_probe
git clone --depth 1 --filter=blob:none --sparse \
  https://github.com/MCUSec/SEmu.git /tmp/semu_f103_probe/SEmu
cd /tmp/semu_f103_probe/SEmu
git sparse-checkout set DataSet/p2im-unit-tests/F103
```

Run an ELF with the STM32F103 machine:

```sh
cd /home/muyi/2026_qemu/qemu_f103/build-arm-f103
timeout 3 ./qemu-system-arm \
  -M stm32f103 \
  -nographic \
  -kernel /tmp/semu_f103_probe/SEmu/DataSet/p2im-unit-tests/F103/NUTTX-SPI.elf
```

Useful SPI-focused images in that dataset:

- `ARDUINO-F103-SPI.elf`
- `F103-RIOT-SPI.elf`
- `NUTTX-SPI.elf`

Observed result with the current model:

- `ARDUINO-F103-SPI.elf`: starts and keeps running for the timeout window.
- `F103-RIOT-SPI.elf`: starts and keeps running for the timeout window.
- `NUTTX-SPI.elf`: starts and repeatedly prints output like
  `Temperature = 0F  -17C` until the timeout terminates QEMU.

GDB validation with `ARDUINO-F103-SPI.elf`:

```sh
cd /home/muyi/2026_qemu/qemu_f103/build-arm-f103
./qemu-system-arm -M stm32f103 -nographic \
  -kernel /tmp/semu_f103_probe/SEmu/DataSet/p2im-unit-tests/F103/ARDUINO-F103-SPI.elf \
  -S -gdb tcp::1234
```

In another WSL terminal:

```gdb
gdb-multiarch /tmp/semu_f103_probe/SEmu/DataSet/p2im-unit-tests/F103/ARDUINO-F103-SPI.elf
set architecture arm
set arm fallback-mode thumb
target remote :1234
hbreak *0x080020c6
continue
x/i $pc
x/wx 0x40013008
si
x/wx 0x40013008
hbreak *0x080021de
continue
x/wx 0x40013008
si
x/wx 0x40013008
```

Observed SPI behavior:

- Before `strb r2, [r3, #12]` writes `SPI1_DR`, `SPI1_SR` is `0x00000002`.
- After that byte write to `SPI1_DR`, `SPI1_SR` becomes `0x00000003`
  (`TXE | RXNE`).
- Before `ldr r3, [r3, #12]` reads `SPI1_DR`, `SPI1_SR` is `0x00000003`.
- After reading `SPI1_DR`, `SPI1_SR` returns to `0x00000002`, so `RXNE` is
  cleared.
