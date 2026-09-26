/*
 * stm32f407xx_spi_driver.c
 *
 * Adds interrupt-driven SPI_SendDataIT / SPI_ReceiveDataIT / SPI_IRQHandling
 * on top of the previously corrected blocking API. Uses TXEIE/RXNEIE/ERRIE
 * to drive a byte-at-a-time state machine and a weak
 * SPI_ApplicationEventCallback the application overrides to know when a
 * transfer finishes.
 */

#include "stm32f407xx.h"

void SPI_PeripheralClockControl(SPI_RegDef_t *pSPIx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        if (pSPIx == SPI1)      RCC->APB2ENR |= (1 << 12);
        else if (pSPIx == SPI2) RCC->APB1ENR |= (1 << 14);
        else if (pSPIx == SPI3) RCC->APB1ENR |= (1 << 15);
    }
    else
    {
        if (pSPIx == SPI1)      RCC->APB2ENR &= ~(1 << 12);
        else if (pSPIx == SPI2) RCC->APB1ENR &= ~(1 << 14);
        else if (pSPIx == SPI3) RCC->APB1ENR &= ~(1 << 15);
    }
}

void SPI_Init(SPI_Handle_t *pSPIHandle)
{
    uint32_t temp = 0;
    SPI_RegDef_t *pSPIx = pSPIHandle->pSPIx;

    pSPIHandle->TxState = SPI_READY;
    pSPIHandle->RxState = SPI_READY;

    if (pSPIHandle->SPI_Config.SPI_mode == SPI_MASTER)
        pSPIx->CR1 |= (1 << 2);
    else
        pSPIx->CR1 &= ~(1 << 2);

    if (pSPIHandle->SPI_Config.SPI_BUS_mode == SPI_FD)
    {
        pSPIx->CR1 &= ~(1 << 15);
    }
    else if (pSPIHandle->SPI_Config.SPI_BUS_mode == SPI_HD)
    {
        pSPIx->CR1 |= (1 << 15);
    }
    else if (pSPIHandle->SPI_Config.SPI_BUS_mode == SPI_SIMPLEX_RX_ONLY)
    {
        pSPIx->CR1 |= (1 << 15);
        pSPIx->CR1 |= (1 << 10);
    }

    if (pSPIHandle->SPI_Config.SPI_DFF == SPI_DFF_8)
        pSPIx->CR1 &= ~(1 << 11);
    else if (pSPIHandle->SPI_Config.SPI_DFF == SPI_DFF_16)
        pSPIx->CR1 |= (1 << 11);

    if (pSPIHandle->SPI_Config.SPI_CPOL == SPI_CPOL_LOW)
        pSPIx->CR1 &= ~(1 << 1);
    else
        pSPIx->CR1 |= (1 << 1);

    if (pSPIHandle->SPI_Config.SPI_CPHA == SPI_CPHA_LOW)
        pSPIx->CR1 &= ~(1 << 0);
    else
        pSPIx->CR1 |= (1 << 0);

    temp = (pSPIHandle->SPI_Config.SPI_Sclk_Speed & 0x7) << 3;
    pSPIx->CR1 &= ~(0x7 << 3);
    pSPIx->CR1 |= temp;

    if (pSPIHandle->SPI_Config.SPI_SSM == SPI_SSM_DIS)
    {
        pSPIx->CR1 &= ~(1 << 9);
    }
    else if (pSPIHandle->SPI_Config.SPI_SSM == SPI_SSM_EN)
    {
        pSPIx->CR1 |= (1 << 9);
        pSPIx->CR1 |= (1 << 8);
    }
}

void SPI_DeInit(SPI_RegDef_t *pSPIx)
{
    if (pSPIx == SPI1)      { RCC->APB2RSTR |= (1 << 12); RCC->APB2RSTR &= ~(1 << 12); }
    else if (pSPIx == SPI2) { RCC->APB1RSTR |= (1 << 14); RCC->APB1RSTR &= ~(1 << 14); }
    else if (pSPIx == SPI3) { RCC->APB1RSTR |= (1 << 15); RCC->APB1RSTR &= ~(1 << 15); }
}

uint8_t SPI_GetFlagStatus(SPI_RegDef_t *pSPIx, uint32_t FlagName)
{
    return (pSPIx->SR & FlagName) ? FLAG_SET : FLAG_RESET;
}

void SPI_Enable(SPI_RegDef_t *pSPIx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
        pSPIx->CR1 |= (1 << 6);
    else
        pSPIx->CR1 &= ~(1 << 6);
}

void SPI_SSIConfig(SPI_RegDef_t *pSPIx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
        pSPIx->CR1 |= (1 << 8);
    else
        pSPIx->CR1 &= ~(1 << 8);
}

void SPI_SSOEControl(SPI_RegDef_t *pSPIx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
        pSPIx->CR2 |= (1 << 2);
    else
        pSPIx->CR2 &= ~(1 << 2);
}

void SPI_SendData(SPI_RegDef_t *pSPIx, uint8_t *pTxBuffer, uint32_t len)
{
    while (len > 0)
    {
        while (SPI_GetFlagStatus(pSPIx, SPI_FLAG_TXE) == FLAG_RESET);

        if (pSPIx->CR1 & (1 << 11))
        {
            pSPIx->DR = *((uint16_t *)pTxBuffer);
            pTxBuffer += 2;
            len -= 2;
        }
        else
        {
            pSPIx->DR = *pTxBuffer;
            pTxBuffer++;
            len--;
        }
    }
}

void SPI_ReceiveData(SPI_RegDef_t *pSPIx, uint8_t *pRxBuffer, uint32_t len)
{
    while (len > 0)
    {
        while (SPI_GetFlagStatus(pSPIx, SPI_FLAG_RXNE) == FLAG_RESET);

        if (pSPIx->CR1 & (1 << 11))
        {
            *((uint16_t *)pRxBuffer) = pSPIx->DR;
            pRxBuffer += 2;
            len -= 2;
        }
        else
        {
            *pRxBuffer = (uint8_t)pSPIx->DR;
            pRxBuffer++;
            len--;
        }
    }
}

/* ---------------------- Interrupt-driven API ---------------------- */

uint8_t SPI_SendDataIT(SPI_Handle_t *pSPIHandle, uint8_t *pTxBuffer, uint32_t len)
{
    uint8_t state = pSPIHandle->TxState;

    if (state != SPI_BUSY_TX)
    {
        pSPIHandle->pTxBuffer = pTxBuffer;
        pSPIHandle->TxLen     = len;
        pSPIHandle->TxState   = SPI_BUSY_TX;

        pSPIHandle->pSPIx->CR2 |= (1 << 7); /* TXEIE */
    }

    return state;
}

uint8_t SPI_ReceiveDataIT(SPI_Handle_t *pSPIHandle, uint8_t *pRxBuffer, uint32_t len)
{
    uint8_t state = pSPIHandle->RxState;

    if (state != SPI_BUSY_RX)
    {
        pSPIHandle->pRxBuffer = pRxBuffer;
        pSPIHandle->RxLen     = len;
        pSPIHandle->RxState   = SPI_BUSY_RX;

        pSPIHandle->pSPIx->CR2 |= (1 << 6); /* RXNEIE */
    }

    return state;
}

static void SPI_CloseTransmission(SPI_Handle_t *pSPIHandle)
{
    pSPIHandle->pSPIx->CR2 &= ~(1 << 7); /* disable TXEIE */
    pSPIHandle->pTxBuffer = NULL;
    pSPIHandle->TxLen = 0;
    pSPIHandle->TxState = SPI_READY;
}

static void SPI_CloseReception(SPI_Handle_t *pSPIHandle)
{
    pSPIHandle->pSPIx->CR2 &= ~(1 << 6); /* disable RXNEIE */
    pSPIHandle->pRxBuffer = NULL;
    pSPIHandle->RxLen = 0;
    pSPIHandle->RxState = SPI_READY;
}

static void SPI_ClearOVRFlag(SPI_RegDef_t *pSPIx)
{
    uint32_t temp;
    temp = pSPIx->DR;
    temp = pSPIx->SR;
    (void)temp;
}

void SPI_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi)
{
    generic_IRQInterruptConfig(IRQNumber, EnorDi);
}

void SPI_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority)
{
    generic_IRQPriorityConfig(IRQNumber, IRQPriority);
}

void SPI_IRQHandling(SPI_Handle_t *pSPIHandle)
{
    SPI_RegDef_t *pSPIx = pSPIHandle->pSPIx;
    uint8_t txeFlag  = pSPIx->SR & SPI_FLAG_TXE;
    uint8_t rxneFlag = pSPIx->SR & SPI_FLAG_RXNE;
    uint8_t ovrFlag  = pSPIx->SR & SPI_FLAG_OVR;
    uint8_t txeie  = pSPIx->CR2 & (1 << 7);
    uint8_t rxneie = pSPIx->CR2 & (1 << 6);
    uint8_t errie  = pSPIx->CR2 & (1 << 5);

    if (txeFlag && txeie)
    {
        if (pSPIHandle->TxLen > 0)
        {
            if (pSPIx->CR1 & (1 << 11))
            {
                pSPIx->DR = *((uint16_t *)pSPIHandle->pTxBuffer);
                pSPIHandle->pTxBuffer += 2;
                pSPIHandle->TxLen -= (pSPIHandle->TxLen >= 2) ? 2 : pSPIHandle->TxLen;
            }
            else
            {
                pSPIx->DR = *(pSPIHandle->pTxBuffer);
                pSPIHandle->pTxBuffer++;
                pSPIHandle->TxLen--;
            }
        }

        if (pSPIHandle->TxLen == 0)
        {
            SPI_CloseTransmission(pSPIHandle);
            SPI_ApplicationEventCallback(pSPIHandle, SPI_EVENT_TX_CMPLT);
        }
    }

    if (rxneFlag && rxneie)
    {
        if (pSPIHandle->RxLen > 0)
        {
            if (pSPIx->CR1 & (1 << 11))
            {
                *((uint16_t *)pSPIHandle->pRxBuffer) = pSPIx->DR;
                pSPIHandle->pRxBuffer += 2;
                pSPIHandle->RxLen -= (pSPIHandle->RxLen >= 2) ? 2 : pSPIHandle->RxLen;
            }
            else
            {
                *(pSPIHandle->pRxBuffer) = (uint8_t)pSPIx->DR;
                pSPIHandle->pRxBuffer++;
                pSPIHandle->RxLen--;
            }
        }

        if (pSPIHandle->RxLen == 0)
        {
            SPI_CloseReception(pSPIHandle);
            SPI_ApplicationEventCallback(pSPIHandle, SPI_EVENT_RX_CMPLT);
        }
    }

    if (ovrFlag && errie)
    {
        if (pSPIHandle->TxState != SPI_BUSY_TX)
            SPI_ClearOVRFlag(pSPIx);
        SPI_ApplicationEventCallback(pSPIHandle, SPI_EVENT_OVR_ERR);
    }
}

__attribute__((weak)) void SPI_ApplicationEventCallback(SPI_Handle_t *pSPIHandle, uint8_t AppEv)
{
    /* Application overrides this. Left empty so the driver still links
     * standalone if the app doesn't define its own version. */
    (void)pSPIHandle;
    (void)AppEv;
}
