# STM32F407 Bare-Metal Peripheral Drivers

Register-level (no HAL/LL) peripheral drivers for the STM32F407VGT6,
written in C, developed while learning embedded systems on the STM32F4.

## Peripherals implemented


 GPIO,UART,I2C,SPI
## Applications implemented:
  01_usart_stm32_arduino_uart/            <- STM32 <-> Arduino over UART
  02_spi_stm32_master_arduino_slave/      <- STM32 (master) -> Arduino (SPI slave)
  03_i2c_stm32_arduino_slave/             <- STM32 (master) -> Arduino (I2C slave)

All four PCLK/clock-frequency helpers (used for I2C CCR/TRISE and USART
baud rate) now support HSI, HSE, and PLL as the SYSCLK source.

## Folder structure

```
drivers/
  inc/   stm32f407xx.h                    <- base register/struct/NVIC defs
         stm32f407xx_gpio_driver.h
         stm32f407xx_spi_driver.h
         stm32f407xx_i2c_driver.h
         stm32f407xx_usart_driver.h
  src/   stm32f407xx_nvic.c               <- shared IRQ enable/priority logic
         stm32f407xx_gpio_driver.c
         stm32f407xx_spi_driver.c
         stm32f407xx_i2c_driver.c
         stm32f407xx_usart_driver.c
applications/
  01_usart_stm32_arduino_uart/            <- STM32 <-> Arduino over UART
  02_spi_stm32_master_arduino_slave/      <- STM32 (master) -> Arduino (SPI slave)
  03_i2c_stm32_arduino_slave/             <- STM32 (master) -> Arduino (I2C slave)
```

## Usage (polling)

```c
#include "stm32f407xx.h"

GPIO_Handle_t led = {0};
led.pGPIOx = GPIOD;
led.GPIO_Config.GPIO_PinNumber   = GPIO_PIN_NO_12;
led.GPIO_Config.GPIO_PinMode     = GPIO_MODE_OUT;
led.GPIO_Config.GPIO_OutputType  = GPIO_OP_TYPE_PP;
led.GPIO_Config.GPIO_OutputSpeed = GPIO_SPEED_FAST;
led.GPIO_Config.GPIO_PinPUPD     = GPIO_NO_PUPD;

GPIO_PeripheralClockControl(GPIOD, ENABLE);
GPIO_Init(&led);
GPIO_ToggleOutputPin(GPIOD, GPIO_PIN_NO_12);
```

## Usage (interrupt-driven, SPI example)

```c
SPI_Handle_t spi1 = { .pSPIx = SPI1, /* ...config... */ };
SPI_Init(&spi1);
SPI_IRQPriorityConfig(IRQ_NO_SPI1, NVIC_IRQ_PRI0);
SPI_IRQInterruptConfig(IRQ_NO_SPI1, ENABLE);
SPI_Enable(SPI1, ENABLE);

SPI_SendDataIT(&spi1, myBuffer, len); // returns immediately

/* in the vector table / startup file: */
void SPI1_IRQHandler(void) { SPI_IRQHandling(&spi1); }

/* override this in your application to know when it's done: */
void SPI_ApplicationEventCallback(SPI_Handle_t *pSPIHandle, uint8_t AppEv)
{
    if (AppEv == SPI_EVENT_TX_CMPLT) { /* ... */ }
}
```

The same `*_IRQHandling` + weak `*_ApplicationEventCallback` pattern is
used for GPIO (`GPIO_IRQHandling`), I2C (`I2C_EV_IRQHandling` /
`I2C_ER_IRQHandling`), and USART (`USART_IRQHandling`).

## Applications (STM32 <-> Arduino demos)

Three worked examples showing the drivers used for real STM32-to-Arduino
communication, each with wiring notes and a matching Arduino sketch:

| Folder | Protocol | What it does |
|---|---|---|
| `applications/01_usart_stm32_arduino_uart` | UART | STM32 sends a message once/sec, echoes anything it receives; Arduino relays both directions to its Serial Monitor over SoftwareSerial |
| `applications/02_spi_stm32_master_arduino_slave` | SPI | STM32 (master) sends an incrementing byte to an Arduino Uno running as an SPI slave (register-level slave setup) |
| `applications/03_i2c_stm32_arduino_slave` | I2C | STM32 (master) sends an incrementing byte to an Arduino Uno running as an I2C slave at address `0x08` via `Wire` |

**Read the wiring comment block at the top of each `main.c` before
connecting anything** — STM32 is 3.3V logic and most Arduino boards
are 5V logic, so the UART and SPI examples call out where a voltage
divider or level shifter is needed. I2C is open-drain so this is less
of a concern there, as long as pull-ups reference 3.3V. These
applications use the blocking API (simpler to read as a first demo);
swap in the `*_IT` calls from the section above if you want them
interrupt-driven.

## Notes on this repo's history

These drivers were written and debugged incrementally (GPIO → SPI → I2C
→ USART), catching bugs like:
- missing "clear field before OR-ing in" on multi-bit register fields
- `while(len >= 0)` on an unsigned length (infinite loop / underflow)
- read/write direction mixed up in receive functions
- wrong register (SR vs CR1) used for flag-clear vs config bits
- config-struct fields being checked against the wrong field entirely
  (copy-paste between similar peripherals)

`stm32f407xx.h` (base register map + struct layout + NVIC/IRQ numbers)
and the shared `stm32f407xx_nvic.c` helper were written specifically for
this repo, since the peripheral driver files that started it assumed a
base header existed already.

Tested by compilation/logic review only — not yet flashed to hardware.
Treat as a work-in-progress driver library, not a production HAL.

## Remaining TODO

- Slave-mode I2C event handling (currently master-only on the interrupt path)
- USART hardware flow control validation on real CTS/RTS-wired hardware
- A CMake/Makefile build target and a linker script, so this can be
  built standalone instead of dropped into an existing IDE project
