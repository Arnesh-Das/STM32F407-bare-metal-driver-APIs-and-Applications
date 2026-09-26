/*
 * main.c — STM32 (SPI master) -> Arduino Uno (SPI slave)
 *
 * WIRING (Arduino Uno hardware SPI pins are fixed):
 *   STM32 PA5 (SPI1_SCK)  -> Arduino pin 13 (SCK)
 *   STM32 PA6 (SPI1_MISO) <- Arduino pin 12 (MISO)   [not used in this demo]
 *   STM32 PA7 (SPI1_MOSI) -> Arduino pin 11 (MOSI)
 *   STM32 PB6 (manual NSS/CS, GPIO output) -> Arduino pin 10 (SS)
 *   STM32 GND              <-> Arduino GND
 *
 * NOTE ON VOLTAGE LEVELS: STM32 is 3.3V logic, Arduino Uno is 5V logic.
 * SCK/MOSI/CS going STM32 -> Arduino is generally fine in practice; MISO
 * going Arduino -> STM32 technically needs a level shifter for a fully
 * safe setup. This demo doesn't use MISO, so it's a non-issue here.
 *
 * BEHAVIOR: sends an incrementing byte to the Arduino once a second,
 * pulling CS (PB6) low around each transfer since this driver's
 * SPI_SendData doesn't manage NSS/CS itself.
 */

#include "stm32f407xx.h"

#define CS_GPIO   GPIOB
#define CS_PIN    GPIO_PIN_NO_6

static void delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++);
}

static void SPI1_GPIO_Init(void)
{
    GPIO_Handle_t spiPins = {0};
    spiPins.pGPIOx = GPIOA;
    spiPins.GPIO_Config.GPIO_PinMode      = GPIO_MODE_ALTFN;
    spiPins.GPIO_Config.GPIO_OutputType   = GPIO_OP_TYPE_PP;
    spiPins.GPIO_Config.GPIO_PinPUPD      = GPIO_NO_PUPD;
    spiPins.GPIO_Config.GPIO_OutputSpeed  = GPIO_SPEED_FAST;
    spiPins.GPIO_Config.GPIO_AlternateFun = 5; /* AF5 = SPI1 */

    GPIO_PeripheralClockControl(GPIOA, ENABLE);

    spiPins.GPIO_Config.GPIO_PinNumber = GPIO_PIN_NO_5; /* SCK */
    GPIO_Init(&spiPins);

    spiPins.GPIO_Config.GPIO_PinNumber = GPIO_PIN_NO_7; /* MOSI */
    GPIO_Init(&spiPins);

    spiPins.GPIO_Config.GPIO_PinNumber = GPIO_PIN_NO_6; /* MISO, unused but configured */
    GPIO_Init(&spiPins);

    GPIO_Handle_t csPin = {0};
    csPin.pGPIOx = CS_GPIO;
    csPin.GPIO_Config.GPIO_PinNumber   = CS_PIN;
    csPin.GPIO_Config.GPIO_PinMode     = GPIO_MODE_OUT;
    csPin.GPIO_Config.GPIO_OutputType  = GPIO_OP_TYPE_PP;
    csPin.GPIO_Config.GPIO_OutputSpeed = GPIO_SPEED_FAST;
    csPin.GPIO_Config.GPIO_PinPUPD     = GPIO_NO_PUPD;

    GPIO_PeripheralClockControl(CS_GPIO, ENABLE);
    GPIO_Init(&csPin);
    GPIO_WriteToOutputPin(CS_GPIO, CS_PIN, GPIO_PIN_SET); /* idle high */
}

int main(void)
{
    SPI1_GPIO_Init();
    SPI_PeripheralClockControl(SPI1, ENABLE);

    SPI_Handle_t spi1 = {0};
    spi1.pSPIx = SPI1;
    spi1.SPI_Config.SPI_mode        = SPI_MASTER;
    spi1.SPI_Config.SPI_BUS_mode    = SPI_FD;
    spi1.SPI_Config.SPI_DFF         = SPI_DFF_8;
    spi1.SPI_Config.SPI_CPOL        = SPI_CPOL_LOW;
    spi1.SPI_Config.SPI_CPHA        = SPI_CPHA_LOW;
    spi1.SPI_Config.SPI_SSM         = SPI_SSM_EN;  /* software NSS, since we drive CS manually */
    spi1.SPI_Config.SPI_Sclk_Speed  = 3;           /* fPCLK/16 — slow/safe for jumper wires */

    SPI_Init(&spi1);
    SPI_Enable(SPI1, ENABLE);

    uint8_t counter = 0;

    while (1)
    {
        GPIO_WriteToOutputPin(CS_GPIO, CS_PIN, GPIO_PIN_RESET);
        SPI_SendData(SPI1, &counter, 1);
        GPIO_WriteToOutputPin(CS_GPIO, CS_PIN, GPIO_PIN_SET);

        counter++;
        delay(1000000);
    }
}
