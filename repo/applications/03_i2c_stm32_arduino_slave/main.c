/*
 * main.c — STM32 (I2C master) -> Arduino Uno (I2C slave)
 *
 * WIRING:
 *   STM32 PB6 (I2C1_SCL) -> Arduino A5 (SCL)
 *   STM32 PB7 (I2C1_SDA) -> Arduino A4 (SDA)
 *   STM32 GND             <-> Arduino GND
 *   Pull-up resistors (~4.7k) from SDA and SCL to 3.3V.
 *
 * NOTE: I2C is open-drain, so unlike SPI/UART there's no "5V driving a
 * 3.3V pin" concern here as long as your pull-ups reference 3.3V.
 *
 * BEHAVIOR: sends a 1-byte incrementing counter to Arduino slave address
 * 0x08 once a second.
 */

#include "stm32f407xx.h"

#define ARDUINO_I2C_ADDR  0x08

static void delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++);
}

static void I2C1_GPIO_Init(void)
{
    GPIO_Handle_t i2cPins = {0};
    i2cPins.pGPIOx = GPIOB;
    i2cPins.GPIO_Config.GPIO_PinMode      = GPIO_MODE_ALTFN;
    i2cPins.GPIO_Config.GPIO_OutputType   = GPIO_OP_TYPE_OD; /* I2C is open-drain */
    i2cPins.GPIO_Config.GPIO_PinPUPD      = GPIO_PIN_PU;
    i2cPins.GPIO_Config.GPIO_OutputSpeed  = GPIO_SPEED_FAST;
    i2cPins.GPIO_Config.GPIO_AlternateFun = 4; /* AF4 = I2C1 */

    GPIO_PeripheralClockControl(GPIOB, ENABLE);

    i2cPins.GPIO_Config.GPIO_PinNumber = GPIO_PIN_NO_6; /* SCL */
    GPIO_Init(&i2cPins);

    i2cPins.GPIO_Config.GPIO_PinNumber = GPIO_PIN_NO_7; /* SDA */
    GPIO_Init(&i2cPins);
}

int main(void)
{
    I2C1_GPIO_Init();
    I2C_PeripheralClockControl(I2C1, ENABLE);

    I2C_Handle_t i2c1 = {0};
    i2c1.pI2Cx = I2C1;
    i2c1.I2C_Config.SCL_Speed      = I2C_SCL_SPEED_SM; /* 100kHz standard mode */
    i2c1.I2C_Config.I2C_ACKControl = I2C_ACK_ENABLE;
    i2c1.I2C_Config.I2C_DeviceAddress = 0x61; /* STM32's own address, arbitrary here since it's master-only */

    I2C_Init(&i2c1);
    I2C_PeripheralControl(I2C1, ENABLE);

    uint8_t counter = 0;

    while (1)
    {
        I2C_MasterSendData(&i2c1, &counter, ARDUINO_I2C_ADDR, 1);
        counter++;
        delay(1000000);
    }
}
