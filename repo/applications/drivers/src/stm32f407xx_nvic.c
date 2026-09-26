/*
 * stm32f407xx_nvic.c — one shared implementation of IRQ enable/disable
 * and priority configuration, since the logic is identical regardless
 * of which peripheral's interrupt is being configured (only the IRQ
 * number differs). Each driver's own *_IRQInterruptConfig /
 * *_IRQPriorityConfig wrapper just forwards to these.
 */

#include "stm32f407xx.h"

void generic_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        if (IRQNumber <= 31)
            *NVIC_ISER0 |= (1 << IRQNumber);
        else if (IRQNumber <= 63)
            *NVIC_ISER1 |= (1 << (IRQNumber % 32));
        else if (IRQNumber <= 95)
            *NVIC_ISER2 |= (1 << (IRQNumber % 64));
    }
    else
    {
        if (IRQNumber <= 31)
            *NVIC_ICER0 |= (1 << IRQNumber);
        else if (IRQNumber <= 63)
            *NVIC_ICER1 |= (1 << (IRQNumber % 32));
        else if (IRQNumber <= 95)
            *NVIC_ICER2 |= (1 << (IRQNumber % 64));
    }
}

void generic_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority)
{
    uint8_t iprx        = IRQNumber / 4;
    uint8_t iprx_section = IRQNumber % 4;
    uint8_t shift_amount = (8 * iprx_section) + (8 - NO_PR_BITS_IMPLEMENTED);

    *(NVIC_PR_BASEADDR + iprx) &= ~(0xFF << (8 * iprx_section));
    *(NVIC_PR_BASEADDR + iprx) |= (IRQPriority << shift_amount);
}
