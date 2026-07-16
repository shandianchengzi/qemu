# STM32F103 QEMU External Peripheral Modeling Workflow

## Purpose

Use this workflow whenever an STM32F103 QEMU peripheral is added, changed, or validated. The required output is not only C code: it is a reproducible evidence package containing the model, focused qtest coverage, a real ARM ELF run, and a validation note.

The project-local reusable instruction entry point is [SKILL.md](../../skills/stm32f103-peripheral-modeling/SKILL.md). It is intentionally stored in this repository rather than installed globally. In a new Codex task, explicitly ask to use that file or mention `stm32f103-peripheral-modeling` together with its project path.

## Register Authority

Use the local copy of ST's reference manual as the primary register authority:

- [RM0008 local PDF](rm0008-stm32f10x-reference-manual.pdf)
- [RM0008 official ST PDF](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)

For a USART/UART task, use RM0008 Rev 21 chapter 27, pages 785-827. The register table is page 827; individual register descriptions are pages 818-826.

## Required Input

Before editing, record the target peripheral, requested features, and an acceptance boundary. For example:

```text
Target: USART1
Required: register model, polling TX, RX interrupt
Deferred: DMA, LIN, smartcard, synchronous clock
ELF: ARDUINO-F103-Serial.elf
```

If the request does not define a boundary, derive a conservative one from the chosen ELF and state it in the validation note before implementation.

## 1. Design From RM0008

Create a behavior table before writing C. Include every register touched by the requested feature.

| Item | Required record |
| --- | --- |
| Addressing | peripheral base, MMIO region size, each register offset |
| Reset | reset value and reset side effects |
| Access | byte/half-word/word support, RO/WO/RW/W1C bits, reserved bits |
| Behavior | reads/writes that clear flags, start work, or move data |
| Events | state changes, FIFO/shift-register behavior, timers |
| Connections | RCC enable/reset, GPIO/AFIO, IRQ, DMA, bus devices |
| Scope | implemented behavior and explicitly deferred behavior |

Do not copy an F2/F4 device model solely because its register offsets look similar. Compare reset values, bit meanings, unsupported instances, and side effects against RM0008. If reusing it is correct for the required scope, document the reuse and its limitations.

## 2. Place Code According To the Existing Structure

Inspect a nearby F103 model first. Use the QEMU subsystem directory that fits the device, rather than creating a catch-all STM32 directory.

| Work | Usual location |
| --- | --- |
| Device state and register constants | `include/hw/<subsystem>/stm32f1xx_<device>.h` |
| QOM device, reset, MMIO and state machine | `hw/<subsystem>/stm32f1xx_<device>.c` |
| Build source registration | `hw/<subsystem>/meson.build` |
| ARM target configuration | `hw/arm/Kconfig` |
| F103 device creation, base mapping and IRQ wiring | `hw/arm/stm32f103_soc.c` |
| F103 SoC state member, if added | `include/hw/arm/stm32f103_soc.h` |
| Focused model test | `tests/qtest/stm32f103_<device>-test.c` and its Meson entry |
| Reproducible result | `docs/reference/stm32f103-<device>-model-notes.md` |

Use a generic or another-family model only when that is an intentional compatibility decision. Name the F103 adapter or validation note so the reuse is visible.

## 3. Implement the Model

Implement the following in the model, in this order:

1. QOM type, state structure, MMIO `MemoryRegion`, reset callback, and `MemoryRegionOps`.
2. Register storage with RM0008 reset values and reserved-bit masks.
3. Read/write side effects, data path, and state transitions.
4. IRQ and DMA update helpers, with the enable and status masks matching the manual.
5. RCC/reset, GPIO/AFIO, timer, bus, or chardev connections required by the requested scope.
6. `LOG_GUEST_ERROR` diagnostics for invalid offsets or unsupported accesses.

Treat access width as behavior, not plumbing. A firmware that uses `strb` or `strh` must not silently receive incorrect 32-bit semantics.

## 4. Build and Run Focused qtests

After adding or changing a device, build the ARM target and run its qtest before using a full ELF:

```bash
cd ~/2026_qemu/qemu_f103/build-arm-f103
ninja qemu-system-arm
QTEST_QEMU_BINARY=./qemu-system-arm ./tests/qtest/stm32f103_<device>-test
```

At minimum, test reset values, valid and invalid register accesses, write masks, supported access widths, and each requested state transition. Add IRQ and DMA assertions when those features are part of the scope.

## 5. Run a Real F103 ELF

Use the appropriate program from `~/2026_qemu/SEmu/DataSet/p2im-unit-tests/F103/`. Common candidates include `ARDUINO-F103-Serial.elf`, `NUTTX-USART.elf`, `F103-RIOT-USART.elf`, SPI, I2C, ADC, GPIO, and PWM tests.

Start an interactive firmware-debug session with:

```bash
cd ~/2026_qemu/qemu_f103/build-arm-f103

./qemu-system-arm \
  -M stm32f103 \
  -nographic \
  -kernel ~/2026_qemu/SEmu/DataSet/p2im-unit-tests/F103/<TEST>.elf \
  -S \
  -gdb tcp::1234 \
  -d guest_errors,unimp \
  -D /tmp/stm32f103-<device>.log
```

`-M` selects the F103 board, `-kernel` loads the ARM ELF, `-nographic` uses terminal I/O, `-S` pauses the guest CPU at reset, `-gdb` opens QEMU's guest-debug port, `-d` selects diagnostics, and `-D` writes those diagnostics to a file.

For a bounded, trace-based smoke test, use `timeout` because these ELFs may intentionally loop forever:

```bash
timeout --signal=INT 8s ./qemu-system-arm \
  -M stm32f103 \
  -nographic \
  -kernel ~/2026_qemu/SEmu/DataSet/p2im-unit-tests/F103/<TEST>.elf \
  -trace enable=<device>_read,enable=<device>_write,file=/tmp/stm32f103-<device>.trace \
  -d guest_errors,unimp \
  -D /tmp/stm32f103-<device>.log
```

An exit caused by `timeout` proves only that the process was stopped after the observation window. Determine success from the trace, diagnostics, and GDB evidence.

## 6. Prove Both Sides of the Access

Guest GDB explains the firmware side. It needs the ELF for ARM symbols and source mappings even though QEMU has already loaded the ELF for execution:

```bash
gdb-multiarch ~/2026_qemu/SEmu/DataSet/p2im-unit-tests/F103/<TEST>.elf
(gdb) set architecture arm
(gdb) set arm fallback-mode thumb
(gdb) target remote :1234
(gdb) continue
```

Use `x/wx <base + offset>` to inspect guest MMIO registers. Use the address table from RM0008 and the F103 SoC source.

Host GDB proves the QEMU C code executed. Run QEMU as the host program and set breakpoints on the model callbacks:

```bash
cd ~/2026_qemu/qemu_f103/build-arm-f103
gdb --args ./qemu-system-arm -M stm32f103 -nographic \
  -kernel ~/2026_qemu/SEmu/DataSet/p2im-unit-tests/F103/<TEST>.elf

(gdb) break stm32f1xx_<device>_read
(gdb) break stm32f1xx_<device>_write
(gdb) run
(gdb) bt
(gdb) p/x addr
(gdb) p/x val64
(gdb) p size
```

For a reused model, use the actual callback names, not an assumed F1 name. Conditional breakpoints such as `break stm32f2xx_usart_write if addr == 4` isolate a data-register write.

## 7. Required Result Note

Create `docs/reference/stm32f103-<device>-model-notes.md` after every validation. Include:

1. Target, scope, and RM0008 chapter/pages.
2. Code locations and SoC mapping.
3. qtest command and result, or an explicit statement that no test exists.
4. Exact QEMU and ELF commands.
5. A short actual trace excerpt and a Host GDB excerpt.
6. Interpretation of every offset/value needed to support the conclusion.
7. Diagnostics from unrelated dependencies, clearly separated from the target peripheral.
8. A final four-way summary: implemented, observed in ELF, tested, and deferred.

Do not say "UART works" or "device model is complete" without stating which functions were observed and which remain untested.
