/*
 * main.c — STM32 <-> Arduino over USART2
 *
 * WIRING (IMPORTANT — READ THIS FIRST):
 * STM32F407 GPIO pins run at 3.3V logic. Most Arduino boards (Uno, Nano,
 * Mega) run at 5V logic. NEVER connect an Arduino's 5V TX pin directly
 * into an STM32 RX pin — it can damage the STM32.
 *
 *   Arduino TX (5V)  --[voltage divider, e.g. 1k + 2k]-->  STM32 RX (PA3)
 *   Arduino RX (5V)  <-------------------------------------  STM32 TX (PA2)
 *   Arduino GND      <-------------------------------------  STM32 GND
 *
 * STM32 -> Arduino direction is fine without a divider (3.3V is read as
 * HIGH by a 5V Arduino input). Arduino -> STM32 direction needs the
 * divider (or a proper level shifter) so the Arduino's 5V never reaches
 * the STM32 pin directly. If you're using a 3.3V Arduino (e.g. a 3.3V
 * Pro Mini), you can skip the divider and wire TX/RX straight across.
 *
 * PINS USED (STM32F407, USART2, AF7):
 *   PA2 -> USART2_TX
 *   PA3 -> USART2_RX
 *
 * BEHAVIOR:
 *   STM32 sends "Hello from STM32!\r\n" once a second.
 *   STM32 also echoes back anything it receives from the Arduino.
 *   Pair with arduino_sketch.ino in this folder.
 */

#include "stm32f407xx.h"
#include <string.h>

static void delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++);
}

static void USART2_GPIO_Init(void)
{
    GPIO_Handle_t usartPins = {0};
    usartPins.pGPIOx = GPIOA;
    usartPins.GPIO_Config.GPIO_PinMode     = GPIO_MODE_ALTFN;
    usartPins.GPIO_Config.GPIO_OutputType  = GPIO_OP_TYPE_PP;
    usartPins.GPIO_Config.GPIO_PinPUPD     = GPIO_PIN_PU;
    usartPins.GPIO_Config.GPIO_OutputSpeed = GPIO_SPEED_FAST;
    usartPins.GPIO_Config.GPIO_AlternateFun = 7; /* AF7 = USART2 on PA2/PA3 */

    GPIO_PeripheralClockControl(GPIOA, ENABLE);

    usartPins.GPIO_Config.GPIO_PinNumber = GPIO_PIN_NO_2; /* TX */
    GPIO_Init(&usartPins);

    usartPins.GPIO_Config.GPIO_PinNumber = GPIO_PIN_NO_3; /* RX */
    GPIO_Init(&usartPins);
}

int main(void)
{
    USART2_GPIO_Init();
    USART_PeripheralClockControl(USART2, ENABLE);

    USART_Handle_t usart2 = {0};
    usart2.pUSARTx = USART2;
    usart2.USART_Config.USART_Baud            = 9600;
    usart2.USART_Config.USART_WordLength      = USART_WORD_LEN_8;
    usart2.USART_Config.USART_ParityControl   = USART_PARITY_DISABLE;
    usart2.USART_Config.USART_NoOfStopBits    = USART_STOP_BITS_1;
    usart2.USART_Config.USART_HWFlowControl   = USART_HW_FLOW_CTRL_NONE;
    usart2.USART_Config.USART_SamplingMode    = USART_SAMPLING_16;

    USART_Init(&usart2);

    const char msg[] = "Hello from STM32!\r\n";
    uint8_t rxByte;

    while (1)
    {
        USART_SendData(&usart2, (uint8_t *)msg, strlen(msg));

        for (int i = 0; i < 50000; i++)
        {
            if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == FLAG_SET)
            {
                USART_ReceiveData(&usart2, &rxByte, 1);
                USART_SendData(&usart2, &rxByte, 1);
            }
        }

        delay(500000);
    }
}
