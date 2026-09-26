/*
 * stm32f407xx_usart_driver.c
 *
 * Adds vs. the previous version of this repo:
 *  - PLL clock source support in the PCLK helper (previously HSI/HSE
 *    only), matching the same derivation added to the I2C driver.
 *  - Interrupt-driven USART_SendDataIT / USART_ReceiveDataIT and
 *    USART_IRQHandling, driven off TXEIE/RXNEIE/TCIE, reporting
 *    completion through a weak USART_ApplicationEventCallback.
 */

#include "stm32f407xx.h"

static uint32_t USART_GetPCLKValue(USART_RegDef_t *pUSARTx)
{
    uint32_t systemclock = 0;
    uint8_t  clksrc = (RCC->CFGR >> 2) & 0x3;

    if (clksrc == 0)
    {
        systemclock = 16000000U; /* HSI */
    }
    else if (clksrc == 1)
    {
        systemclock = 8000000U;  /* HSE (board-dependent) */
    }
    else if (clksrc == 2)
    {
        /* PLL: SYSCLK = ((PLL_SRC / PLLM) * PLLN) / PLLP */
        uint32_t pllSrcClk = (RCC->PLLCFGR & (1 << 22)) ? 8000000U : 16000000U;
        uint32_t pllm = (RCC->PLLCFGR >> 0)  & 0x3F;
        uint32_t plln = (RCC->PLLCFGR >> 6)  & 0x1FF;
        uint32_t pllpBits = (RCC->PLLCFGR >> 16) & 0x3;
        uint32_t pllp = (pllpBits + 1) * 2;

        if (pllm != 0 && pllp != 0)
            systemclock = ((pllSrcClk / pllm) * plln) / pllp;
        else
            systemclock = 16000000U;
    }
    else
    {
        systemclock = 16000000U;
    }

    uint16_t AHBPrescArr[8]  = {2, 4, 8, 16, 32, 64, 128, 256};
    uint8_t  APBPrescArr[4]  = {2, 4, 8, 16};

    uint32_t ahbTemp = (RCC->CFGR >> 4) & 0xF;
    uint32_t ahbPresc = (ahbTemp < 8) ? 1 : AHBPrescArr[ahbTemp - 8];

    uint32_t apbBitpos = (pUSARTx == USART1 || pUSARTx == USART6) ? 13 : 10; /* PPRE2 vs PPRE1 */
    uint32_t apbTemp = (RCC->CFGR >> apbBitpos) & 0x7;
    uint32_t apbPresc = (apbTemp < 4) ? 1 : APBPrescArr[apbTemp - 4];

    return (systemclock / ahbPresc) / apbPresc;
}

void USART_PeripheralClockControl(USART_RegDef_t *pUSARTx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        if (pUSARTx == USART1)      RCC->APB2ENR |= (1 << 4);
        else if (pUSARTx == USART2) RCC->APB1ENR |= (1 << 17);
        else if (pUSARTx == USART3) RCC->APB1ENR |= (1 << 18);
        else if (pUSARTx == UART4)  RCC->APB1ENR |= (1 << 19);
        else if (pUSARTx == UART5)  RCC->APB1ENR |= (1 << 20);
        else if (pUSARTx == USART6) RCC->APB2ENR |= (1 << 5);
    }
    else
    {
        if (pUSARTx == USART1)      RCC->APB2ENR &= ~(1 << 4);
        else if (pUSARTx == USART2) RCC->APB1ENR &= ~(1 << 17);
        else if (pUSARTx == USART3) RCC->APB1ENR &= ~(1 << 18);
        else if (pUSARTx == UART4)  RCC->APB1ENR &= ~(1 << 19);
        else if (pUSARTx == UART5)  RCC->APB1ENR &= ~(1 << 20);
        else if (pUSARTx == USART6) RCC->APB2ENR &= ~(1 << 5);
    }
}

void USART_PeripheralControl(USART_RegDef_t *pUSARTx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
        pUSARTx->CR1 |= (1 << 13);
    else
        pUSARTx->CR1 &= ~(1 << 13);
}

uint8_t USART_GetFlagStatus(USART_RegDef_t *pUSARTx, uint32_t FlagName)
{
    return (pUSARTx->SR & FlagName) ? FLAG_SET : FLAG_RESET;
}

static void USART_SetBaudRate(USART_RegDef_t *pUSARTx, uint32_t BaudRate)
{
    uint32_t pclk = USART_GetPCLKValue(pUSARTx);
    uint32_t usartdiv;
    uint8_t  over8 = (pUSARTx->CR1 & (1 << 15)) ? 1 : 0;

    if (over8)
        usartdiv = (25 * pclk) / (2 * BaudRate);
    else
        usartdiv = (25 * pclk) / (4 * BaudRate);

    uint32_t mantissa = usartdiv / 100;
    uint32_t fracRaw  = usartdiv - (mantissa * 100);
    uint32_t fraction;

    if (over8)
        fraction = ((fracRaw * 8) + 50) / 100 & 0x7;
    else
        fraction = ((fracRaw * 16) + 50) / 100 & 0xF;

    pUSARTx->BRR = (mantissa << 4) | fraction;
}

void USART_Init(USART_Handle_t *pUSARTHandle)
{
    USART_RegDef_t *pUSARTx = pUSARTHandle->pUSARTx;
    uint32_t cr1 = 0, cr2 = 0, cr3 = 0;

    pUSARTHandle->TxBusyState = USART_READY;
    pUSARTHandle->RxBusyState = USART_READY;

    if (pUSARTHandle->USART_Config.USART_SamplingMode == USART_SAMPLING_8)
        cr1 |= (1 << 15);

    if (pUSARTHandle->USART_Config.USART_WordLength == USART_WORD_LEN_9)
        cr1 |= (1 << 12);

    if (pUSARTHandle->USART_Config.USART_ParityControl == USART_PARITY_EN_EVEN)
    {
        cr1 |= (1 << 10);
    }
    else if (pUSARTHandle->USART_Config.USART_ParityControl == USART_PARITY_EN_ODD)
    {
        cr1 |= (1 << 10);
        cr1 |= (1 << 9);
    }

    pUSARTx->CR1 = cr1;

    cr2 = (pUSARTHandle->USART_Config.USART_NoOfStopBits & 0x3) << 12;
    pUSARTx->CR2 = cr2;

    if (pUSARTHandle->USART_Config.USART_HWFlowControl == USART_HW_FLOW_CTRL_CTS)
        cr3 |= (1 << 9);
    else if (pUSARTHandle->USART_Config.USART_HWFlowControl == USART_HW_FLOW_CTRL_RTS)
        cr3 |= (1 << 8);
    else if (pUSARTHandle->USART_Config.USART_HWFlowControl == USART_HW_FLOW_CTRL_CTS_RTS)
        cr3 |= (1 << 8) | (1 << 9);

    pUSARTx->CR3 = cr3;

    USART_SetBaudRate(pUSARTx, pUSARTHandle->USART_Config.USART_Baud);
}

void USART_DeInit(USART_RegDef_t *pUSARTx)
{
    if (pUSARTx == USART1)      { RCC->APB2RSTR |= (1 << 4);  RCC->APB2RSTR &= ~(1 << 4); }
    else if (pUSARTx == USART2) { RCC->APB1RSTR |= (1 << 17); RCC->APB1RSTR &= ~(1 << 17); }
    else if (pUSARTx == USART3) { RCC->APB1RSTR |= (1 << 18); RCC->APB1RSTR &= ~(1 << 18); }
    else if (pUSARTx == UART4)  { RCC->APB1RSTR |= (1 << 19); RCC->APB1RSTR &= ~(1 << 19); }
    else if (pUSARTx == UART5)  { RCC->APB1RSTR |= (1 << 20); RCC->APB1RSTR &= ~(1 << 20); }
    else if (pUSARTx == USART6) { RCC->APB2RSTR |= (1 << 5);  RCC->APB2RSTR &= ~(1 << 5); }
}

void USART_SendData(USART_Handle_t *pUSARTHandle, uint8_t *pTxBuffer, uint32_t len)
{
    USART_RegDef_t *pUSARTx = pUSARTHandle->pUSARTx;
    uint8_t is9bit = (pUSARTHandle->USART_Config.USART_WordLength == USART_WORD_LEN_9);

    pUSARTx->CR1 |= (1 << 13); /* UE */
    pUSARTx->CR1 |= (1 << 3);  /* TE */

    while (len > 0)
    {
        while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == FLAG_RESET);

        if (is9bit)
        {
            pUSARTx->DR = (*(uint16_t *)pTxBuffer) & 0x01FF;
            pTxBuffer += 2;
            len -= 2;
        }
        else
        {
            pUSARTx->DR = (*pTxBuffer & 0xFF);
            pTxBuffer++;
            len--;
        }
    }

    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TC) == FLAG_RESET);
    pUSARTx->SR &= ~(1 << 6);
}

void USART_ReceiveData(USART_Handle_t *pUSARTHandle, uint8_t *pRxBuffer, uint32_t len)
{
    USART_RegDef_t *pUSARTx = pUSARTHandle->pUSARTx;
    uint8_t is9bit = (pUSARTHandle->USART_Config.USART_WordLength == USART_WORD_LEN_9);
    uint32_t dummyread;

    pUSARTx->CR1 |= (1 << 13); /* UE */
    pUSARTx->CR1 |= (1 << 2);  /* RE */

    while (len > 0)
    {
        while (USART_GetFlagStatus(pUSARTx, USART_FLAG_RXNE) == FLAG_RESET);

        if (is9bit)
        {
            *(uint16_t *)pRxBuffer = pUSARTx->DR & 0x01FF;
            pRxBuffer += 2;
            len -= 2;
        }
        else
        {
            *pRxBuffer = (uint8_t)(pUSARTx->DR & 0xFF);
            pRxBuffer++;
            len--;
        }
    }

    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_IDLE) == FLAG_RESET);
    dummyread = pUSARTx->SR;
    dummyread = pUSARTx->DR;
    (void)dummyread;
}

/* ---------------------- Interrupt-driven API ---------------------- */

uint8_t USART_SendDataIT(USART_Handle_t *pUSARTHandle, uint8_t *pTxBuffer, uint32_t len)
{
    uint8_t state = pUSARTHandle->TxBusyState;

    if (state != USART_BUSY_TX)
    {
        pUSARTHandle->pTxBuffer   = pTxBuffer;
        pUSARTHandle->TxLen       = len;
        pUSARTHandle->TxBusyState = USART_BUSY_TX;

        pUSARTHandle->pUSARTx->CR1 |= (1 << 6); /* TCIE */
        pUSARTHandle->pUSARTx->CR1 |= (1 << 7); /* TXEIE */
    }

    return state;
}

uint8_t USART_ReceiveDataIT(USART_Handle_t *pUSARTHandle, uint8_t *pRxBuffer, uint32_t len)
{
    uint8_t state = pUSARTHandle->RxBusyState;

    if (state != USART_BUSY_RX)
    {
        pUSARTHandle->pRxBuffer   = pRxBuffer;
        pUSARTHandle->RxLen       = len;
        pUSARTHandle->RxBusyState = USART_BUSY_RX;

        pUSARTHandle->pUSARTx->CR1 |= (1 << 5); /* RXNEIE */
    }

    return state;
}

void USART_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi)
{
    generic_IRQInterruptConfig(IRQNumber, EnorDi);
}

void USART_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority)
{
    generic_IRQPriorityConfig(IRQNumber, IRQPriority);
}

void USART_IRQHandling(USART_Handle_t *pUSARTHandle)
{
    USART_RegDef_t *pUSARTx = pUSARTHandle->pUSARTx;
    uint8_t is9bit = (pUSARTHandle->USART_Config.USART_WordLength == USART_WORD_LEN_9);

    uint32_t txe  = pUSARTx->SR & USART_FLAG_TXE;
    uint32_t txeie = pUSARTx->CR1 & (1 << 7);
    uint32_t tc   = pUSARTx->SR & USART_FLAG_TC;
    uint32_t tcie = pUSARTx->CR1 & (1 << 6);
    uint32_t rxne = pUSARTx->SR & USART_FLAG_RXNE;
    uint32_t rxneie = pUSARTx->CR1 & (1 << 5);
    uint32_t ore  = pUSARTx->SR & USART_FLAG_ORE;
    uint32_t fe   = pUSARTx->SR & USART_FLAG_FE;

    if (txe && txeie && pUSARTHandle->TxBusyState == USART_BUSY_TX)
    {
        if (pUSARTHandle->TxLen > 0)
        {
            if (is9bit)
            {
                pUSARTx->DR = (*(uint16_t *)pUSARTHandle->pTxBuffer) & 0x01FF;
                pUSARTHandle->pTxBuffer += 2;
                pUSARTHandle->TxLen -= (pUSARTHandle->TxLen >= 2) ? 2 : pUSARTHandle->TxLen;
            }
            else
            {
                pUSARTx->DR = *pUSARTHandle->pTxBuffer & 0xFF;
                pUSARTHandle->pTxBuffer++;
                pUSARTHandle->TxLen--;
            }
        }

        if (pUSARTHandle->TxLen == 0)
            pUSARTx->CR1 &= ~(1 << 7); /* stop feeding TXE, wait for TC instead */
    }

    if (tc && tcie && pUSARTHandle->TxBusyState == USART_BUSY_TX && pUSARTHandle->TxLen == 0)
    {
        pUSARTx->CR1 &= ~(1 << 6); /* TCIE */
        pUSARTx->SR &= ~(1 << 6);
        pUSARTHandle->TxBusyState = USART_READY;
        USART_ApplicationEventCallback(pUSARTHandle, USART_EVENT_TX_CMPLT);
    }

    if (rxne && rxneie && pUSARTHandle->RxBusyState == USART_BUSY_RX)
    {
        if (is9bit)
        {
            *(uint16_t *)pUSARTHandle->pRxBuffer = pUSARTx->DR & 0x01FF;
            pUSARTHandle->pRxBuffer += 2;
            pUSARTHandle->RxLen -= (pUSARTHandle->RxLen >= 2) ? 2 : pUSARTHandle->RxLen;
        }
        else
        {
            *pUSARTHandle->pRxBuffer = (uint8_t)(pUSARTx->DR & 0xFF);
            pUSARTHandle->pRxBuffer++;
            pUSARTHandle->RxLen--;
        }

        if (pUSARTHandle->RxLen == 0)
        {
            pUSARTx->CR1 &= ~(1 << 5); /* RXNEIE */
            pUSARTHandle->RxBusyState = USART_READY;
            USART_ApplicationEventCallback(pUSARTHandle, USART_EVENT_RX_CMPLT);
        }
    }

    if (ore)
    {
        uint32_t dummyread = pUSARTx->SR;
        dummyread = pUSARTx->DR;
        (void)dummyread;
        USART_ApplicationEventCallback(pUSARTHandle, USART_ERR_ORE);
    }

    if (fe)
    {
        uint32_t dummyread = pUSARTx->SR;
        dummyread = pUSARTx->DR;
        (void)dummyread;
        USART_ApplicationEventCallback(pUSARTHandle, USART_ERR_FE);
    }
}

__attribute__((weak)) void USART_ApplicationEventCallback(USART_Handle_t *pUSARTHandle, uint8_t AppEv)
{
    (void)pUSARTHandle;
    (void)AppEv;
}
