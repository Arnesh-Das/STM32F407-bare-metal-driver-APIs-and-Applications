/*
 * stm32f407xx_gpio_driver.c
 *
 * Adds (vs. the previous version of this repo):
 *  - SYSCFG_EXTICR port-line selection, so interrupt mode actually
 *    routes the right GPIO port into the right EXTI line (was a TODO —
 *    without this, EXTI would trigger off whatever port last configured
 *    that line, almost always the wrong one).
 *  - GPIO_IRQInterruptConfig / GPIO_IRQPriorityConfig / GPIO_IRQHandling,
 *    so pin-change interrupts are actually usable end-to-end.
 */

#include "stm32f407xx.h"

static uint8_t GPIO_PortCode(GPIO_RegDef_t *pGPIOx)
{
    if (pGPIOx == GPIOA) return 0;
    if (pGPIOx == GPIOB) return 1;
    if (pGPIOx == GPIOC) return 2;
    if (pGPIOx == GPIOD) return 3;
    if (pGPIOx == GPIOE) return 4;
    if (pGPIOx == GPIOF) return 5;
    if (pGPIOx == GPIOG) return 6;
    if (pGPIOx == GPIOH) return 7;
    if (pGPIOx == GPIOI) return 8;
    return 0xFF;
}

void GPIO_PeripheralClockControl(GPIO_RegDef_t *pGPIOx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        if (pGPIOx == GPIOA)      RCC->AHB1ENR |= (1 << 0);
        else if (pGPIOx == GPIOB) RCC->AHB1ENR |= (1 << 1);
        else if (pGPIOx == GPIOC) RCC->AHB1ENR |= (1 << 2);
        else if (pGPIOx == GPIOD) RCC->AHB1ENR |= (1 << 3);
        else if (pGPIOx == GPIOE) RCC->AHB1ENR |= (1 << 4);
        else if (pGPIOx == GPIOF) RCC->AHB1ENR |= (1 << 5);
        else if (pGPIOx == GPIOG) RCC->AHB1ENR |= (1 << 6);
        else if (pGPIOx == GPIOH) RCC->AHB1ENR |= (1 << 7);
        else if (pGPIOx == GPIOI) RCC->AHB1ENR |= (1 << 8);
    }
    else
    {
        if (pGPIOx == GPIOA)      RCC->AHB1ENR &= ~(1 << 0);
        else if (pGPIOx == GPIOB) RCC->AHB1ENR &= ~(1 << 1);
        else if (pGPIOx == GPIOC) RCC->AHB1ENR &= ~(1 << 2);
        else if (pGPIOx == GPIOD) RCC->AHB1ENR &= ~(1 << 3);
        else if (pGPIOx == GPIOE) RCC->AHB1ENR &= ~(1 << 4);
        else if (pGPIOx == GPIOF) RCC->AHB1ENR &= ~(1 << 5);
        else if (pGPIOx == GPIOG) RCC->AHB1ENR &= ~(1 << 6);
        else if (pGPIOx == GPIOH) RCC->AHB1ENR &= ~(1 << 7);
        else if (pGPIOx == GPIOI) RCC->AHB1ENR &= ~(1 << 8);
    }
}

/* Peripheral reset must be pulsed (set then clear), never left set. */
void GPIO_DeInit(GPIO_RegDef_t *pGPIOx)
{
    uint8_t bitpos = 0xFF;

    if (pGPIOx == GPIOA)      bitpos = 0;
    else if (pGPIOx == GPIOB) bitpos = 1;
    else if (pGPIOx == GPIOC) bitpos = 2;
    else if (pGPIOx == GPIOD) bitpos = 3;
    else if (pGPIOx == GPIOE) bitpos = 4;
    else if (pGPIOx == GPIOF) bitpos = 5;
    else if (pGPIOx == GPIOG) bitpos = 6;
    else if (pGPIOx == GPIOH) bitpos = 7;
    else if (pGPIOx == GPIOI) bitpos = 8;

    if (bitpos != 0xFF)
    {
        RCC->AHB1RSTR |= (1 << bitpos);
        RCC->AHB1RSTR &= ~(1 << bitpos);
    }
}

void GPIO_Init(GPIO_Handle_t *pGPIOHandle)
{
    uint32_t temp = 0;
    uint8_t  pin  = pGPIOHandle->GPIO_Config.GPIO_PinNumber;

    /* 1. Mode */
    if (pGPIOHandle->GPIO_Config.GPIO_PinMode <= GPIO_MODE_ANALOG)
    {
        temp = pGPIOHandle->GPIO_Config.GPIO_PinMode << (2 * pin);
        pGPIOHandle->pGPIOx->MODER &= ~(0x3 << (2 * pin));
        pGPIOHandle->pGPIOx->MODER |= temp;
    }
    else
    {
        /* Interrupt modes: MODER stays in input mode (00) */
        pGPIOHandle->pGPIOx->MODER &= ~(0x3 << (2 * pin));

        if (pGPIOHandle->GPIO_Config.GPIO_PinMode == GPIO_MODE_RT)
        {
            EXTI->RTSR |= (1 << pin);
            EXTI->FTSR &= ~(1 << pin);
        }
        else if (pGPIOHandle->GPIO_Config.GPIO_PinMode == GPIO_MODE_FT)
        {
            EXTI->FTSR |= (1 << pin);
            EXTI->RTSR &= ~(1 << pin);
        }
        else
        {
            EXTI->RTSR |= (1 << pin);
            EXTI->FTSR |= (1 << pin);
        }

        /* Route this GPIO port onto the EXTI line for this pin number.
         * EXTICR[reg] holds 4 lines, 4 bits each: reg = pin/4, field = pin%4 */
        SYSCFG_PCLK_EN();
        uint8_t exticrIndex = pin / 4;
        uint8_t exticrShift = (pin % 4) * 4;
        uint8_t portcode = GPIO_PortCode(pGPIOHandle->pGPIOx);

        SYSCFG->EXTICR[exticrIndex] &= ~(0xF << exticrShift);
        SYSCFG->EXTICR[exticrIndex] |= (portcode << exticrShift);

        EXTI->IMR |= (1 << pin);
    }

    /* 2. Output type */
    temp = pGPIOHandle->GPIO_Config.GPIO_OutputType << pin;
    pGPIOHandle->pGPIOx->OTYPER &= ~(0x1 << pin);
    pGPIOHandle->pGPIOx->OTYPER |= temp;

    /* 3. Output speed */
    temp = pGPIOHandle->GPIO_Config.GPIO_OutputSpeed << (2 * pin);
    pGPIOHandle->pGPIOx->OSPEEDR &= ~(0x3 << (2 * pin));
    pGPIOHandle->pGPIOx->OSPEEDR |= temp;

    /* 4. Pull-up/pull-down */
    temp = pGPIOHandle->GPIO_Config.GPIO_PinPUPD << (2 * pin);
    pGPIOHandle->pGPIOx->PUPDR &= ~(0x3 << (2 * pin));
    pGPIOHandle->pGPIOx->PUPDR |= temp;

    /* 5. Alternate function */
    if (pGPIOHandle->GPIO_Config.GPIO_PinMode == GPIO_MODE_ALTFN)
    {
        if (pin <= GPIO_PIN_NO_7)
        {
            temp = pGPIOHandle->GPIO_Config.GPIO_AlternateFun << (4 * pin);
            pGPIOHandle->pGPIOx->AFRL &= ~(0xF << (4 * pin));
            pGPIOHandle->pGPIOx->AFRL |= temp;
        }
        else
        {
            temp = pGPIOHandle->GPIO_Config.GPIO_AlternateFun << (4 * (pin - 8));
            pGPIOHandle->pGPIOx->AFRH &= ~(0xF << (4 * (pin - 8)));
            pGPIOHandle->pGPIOx->AFRH |= temp;
        }
    }
}

uint8_t GPIO_ReadFromInputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber)
{
    return (uint8_t)((pGPIOx->IDR >> PinNumber) & 0x00000001);
}

uint16_t GPIO_ReadFromInputPort(GPIO_RegDef_t *pGPIOx)
{
    return (uint16_t)pGPIOx->IDR;
}

void GPIO_WriteToOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber, uint8_t Value)
{
    if (Value == GPIO_PIN_SET)
        pGPIOx->ODR |= (1 << PinNumber);
    else
        pGPIOx->ODR &= ~(1 << PinNumber);
}

void GPIO_WriteToOutputPort(GPIO_RegDef_t *pGPIOx, uint16_t Value)
{
    pGPIOx->ODR = Value;
}

void GPIO_ToggleOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber)
{
    pGPIOx->ODR ^= (1 << PinNumber);
}

void GPIO_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi)
{
    generic_IRQInterruptConfig(IRQNumber, EnorDi);
}

void GPIO_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority)
{
    generic_IRQPriorityConfig(IRQNumber, IRQPriority);
}

void GPIO_IRQHandling(uint8_t PinNumber)
{
    /* Clear the EXTI pending bit for this line (write 1 to clear) */
    if (EXTI->PR & (1 << PinNumber))
        EXTI->PR |= (1 << PinNumber);
}
