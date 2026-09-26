/*
 * stm32f407xx_i2c_driver.c
 *
 * Adds vs. the previous version of this repo:
 *  - TRISE register configuration in I2C_Init (was a TODO — without it,
 *    the bus timing is out of spec, which is a big part of why real
 *    I2C_Init implementations that skip it "mostly work on the bench,
 *    then flake out on a real bus").
 *  - PLL clock source support in RCC_GetPCLK1Value (previously HSI/HSE
 *    only, with a placeholder for PLL).
 *  - Interrupt-driven I2C_MasterSendDataIT / I2C_MasterReceiveDataIT with
 *    I2C_EV_IRQHandling / I2C_ER_IRQHandling implementing the SB -> ADDR
 *    -> (TXE/RXNE/BTF) -> STOP event sequence, plus basic error handling
 *    (BERR/ARLO/AF/OVR/TIMEOUT) reported through a weak
 *    I2C_ApplicationEventCallback.
 */

#include "stm32f407xx.h"

void I2C_PeripheralClockControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        if (pI2Cx == I2C1)      RCC->APB1ENR |= (1 << 21);
        else if (pI2Cx == I2C2) RCC->APB1ENR |= (1 << 22);
        else if (pI2Cx == I2C3) RCC->APB1ENR |= (1 << 23);
    }
    else
    {
        if (pI2Cx == I2C1)      RCC->APB1ENR &= ~(1 << 21);
        else if (pI2Cx == I2C2) RCC->APB1ENR &= ~(1 << 22);
        else if (pI2Cx == I2C3) RCC->APB1ENR &= ~(1 << 23);
    }
}

void I2C_PeripheralControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
        pI2Cx->CR1 |= (1 << 0);
    else
        pI2Cx->CR1 &= ~(1 << 0);
}

void I2C_DeInit(I2C_RegDef_t *pI2Cx)
{
    if (pI2Cx == I2C1)      { RCC->APB1RSTR |= (1 << 21); RCC->APB1RSTR &= ~(1 << 21); }
    else if (pI2Cx == I2C2) { RCC->APB1RSTR |= (1 << 22); RCC->APB1RSTR &= ~(1 << 22); }
    else if (pI2Cx == I2C3) { RCC->APB1RSTR |= (1 << 23); RCC->APB1RSTR &= ~(1 << 23); }
}

uint8_t I2C_GetFlagStatus(I2C_RegDef_t *pI2Cx, uint32_t FlagName)
{
    return (pI2Cx->SR1 & FlagName) ? FLAG_SET : FLAG_RESET;
}

/* Returns the APB1 peripheral clock frequency in Hz. Supports HSI, HSE,
 * and PLL as the SYSCLK source (RCC->CFGR bits 2:3, SWS). */
uint32_t RCC_GetPCLK1Value(void)
{
    uint32_t systemclock = 0;
    uint8_t  clksrc = (RCC->CFGR >> 2) & 0x3;

    if (clksrc == 0)
    {
        systemclock = 16000000U; /* HSI */
    }
    else if (clksrc == 1)
    {
        systemclock = 8000000U;  /* HSE (board-dependent; adjust for your crystal) */
    }
    else if (clksrc == 2)
    {
        /* PLL: SYSCLK = ((PLL_SRC / PLLM) * PLLN) / PLLP
         * PLL_SRC is HSI (16MHz) unless PLLCFGR bit 22 (PLLSRC) selects HSE. */
        uint32_t pllSrcClk = (RCC->PLLCFGR & (1 << 22)) ? 8000000U : 16000000U;
        uint32_t pllm = (RCC->PLLCFGR >> 0)  & 0x3F;
        uint32_t plln = (RCC->PLLCFGR >> 6)  & 0x1FF;
        uint32_t pllpBits = (RCC->PLLCFGR >> 16) & 0x3;
        uint32_t pllp = (pllpBits + 1) * 2; /* 00->2, 01->4, 10->6, 11->8 */

        if (pllm != 0 && pllp != 0)
            systemclock = ((pllSrcClk / pllm) * plln) / pllp;
        else
            systemclock = 16000000U; /* malformed PLL config — fall back safely */
    }
    else
    {
        systemclock = 16000000U; /* reserved value */
    }

    uint16_t AHBPrescArr[8]  = {2, 4, 8, 16, 32, 64, 128, 256};
    uint8_t  APB1PrescArr[4] = {2, 4, 8, 16};

    uint32_t ahbTemp = (RCC->CFGR >> 4) & 0xF;   /* HPRE[3:0] */
    uint32_t ahbPresc = (ahbTemp < 8) ? 1 : AHBPrescArr[ahbTemp - 8];

    uint32_t apbTemp = (RCC->CFGR >> 10) & 0x7;  /* PPRE1[2:0] */
    uint32_t apb1Presc = (apbTemp < 4) ? 1 : APB1PrescArr[apbTemp - 4];

    return (systemclock / ahbPresc) / apb1Presc;
}

void I2C_Init(I2C_Handle_t *pI2CHandle)
{
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;
    uint32_t pclk1 = RCC_GetPCLK1Value();

    pI2CHandle->TxRxState = I2C_READY;

    /* 1. ACK control (CR1 bit 10) */
    if (pI2CHandle->I2C_Config.I2C_ACKControl == I2C_ACK_ENABLE)
        pI2Cx->CR1 |= (1 << 10);
    else
        pI2Cx->CR1 &= ~(1 << 10);

    /* 2. FREQ field of CR2 (MHz value of pclk1) */
    pI2Cx->CR2 &= ~0x3F;
    pI2Cx->CR2 |= (pclk1 / 1000000U) & 0x3F;

    /* 3. Own address, 7-bit mode. Bit 14 must be kept set per reference manual. */
    pI2Cx->OAR1 &= ~(1 << 15);
    pI2Cx->OAR1 |= (1 << 14);
    pI2Cx->OAR1 |= (pI2CHandle->I2C_Config.I2C_DeviceAddress << 1);

    /* 4. CCR: speed + duty cycle, all derived from pclk1 */
    uint16_t ccr = 0;
    if (pI2CHandle->I2C_Config.SCL_Speed <= I2C_SCL_SPEED_SM)
    {
        pI2Cx->CCR &= ~(1 << 15); /* standard mode */
        ccr = (uint16_t)(pclk1 / (2 * pI2CHandle->I2C_Config.SCL_Speed));
    }
    else
    {
        pI2Cx->CCR |= (1 << 15); /* fast mode */
        if (pI2CHandle->I2C_Config.I2C_FMDutyCycle == I2C_FM_DUTY_2)
        {
            pI2Cx->CCR &= ~(1 << 14);
            ccr = (uint16_t)(pclk1 / (3 * pI2CHandle->I2C_Config.SCL_Speed));
        }
        else
        {
            pI2Cx->CCR |= (1 << 14);
            ccr = (uint16_t)(pclk1 / (25 * pI2CHandle->I2C_Config.SCL_Speed));
        }
    }
    pI2Cx->CCR &= ~0xFFF;
    pI2Cx->CCR |= (ccr & 0xFFF);

    /* 5. TRISE — max SCL rise time in bus cycles + 1.
     * Standard mode: (pclk1_MHz * 1) + 1
     * Fast mode:     (pclk1_MHz * 300 / 1000) + 1  (300 ns max rise time) */
    uint32_t pclk1MHz = pclk1 / 1000000U;
    uint32_t trise;
    if (pI2CHandle->I2C_Config.SCL_Speed <= I2C_SCL_SPEED_SM)
        trise = pclk1MHz + 1;
    else
        trise = ((pclk1MHz * 300) / 1000) + 1;

    pI2Cx->TRISE = trise & 0x3F;
}

static void I2C_ExecuteAddressPhaseWrite(I2C_RegDef_t *pI2Cx, uint8_t slaveaddress)
{
    slaveaddress <<= 1;
    slaveaddress &= ~1; /* R/W = 0 (write) */
    pI2Cx->DR = slaveaddress;
}

static void I2C_ExecuteAddressPhaseRead(I2C_RegDef_t *pI2Cx, uint8_t slaveaddress)
{
    slaveaddress <<= 1;
    slaveaddress |= 1; /* R/W = 1 (read) */
    pI2Cx->DR = slaveaddress;
}

static void I2C_ClearAddrFlag(I2C_Handle_t *pI2CHandle)
{
    uint32_t dummyread;
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;

    /* ADDR is cleared by reading SR1 followed by SR2 (SR2 read matters
     * even when we discard the value) — must not write to SR1 directly
     * to try to clear it. */
    if (pI2Cx->SR2 & (1 << 0)) /* master mode: extra care not required, single read is enough */
    {
        dummyread = pI2Cx->SR1;
        dummyread = pI2Cx->SR2;
    }
    else
    {
        dummyread = pI2Cx->SR1;
        dummyread = pI2Cx->SR2;
    }
    (void)dummyread;
}

void I2C_MasterSendData(I2C_Handle_t *pI2CHandle, uint8_t *pTxBuffer, uint8_t slaveaddress, uint32_t len)
{
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;

    pI2Cx->CR1 |= (1 << 8); /* generate START */
    while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_SB));

    I2C_ExecuteAddressPhaseWrite(pI2Cx, slaveaddress);
    while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_ADDR));
    I2C_ClearAddrFlag(pI2CHandle);

    while (len > 0)
    {
        while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_TXE));
        pI2Cx->DR = *pTxBuffer;
        pTxBuffer++;
        len--;
    }

    while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_TXE));
    while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_BTF));

    pI2Cx->CR1 |= (1 << 9); /* generate STOP */
}

void I2C_MasterReceiveData(I2C_Handle_t *pI2CHandle, uint8_t *pRxBuffer, uint8_t slaveaddress, uint32_t len)
{
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;

    pI2Cx->CR1 |= (1 << 8); /* generate START */
    while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_SB));

    I2C_ExecuteAddressPhaseRead(pI2Cx, slaveaddress);
    while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_ADDR));

    if (len == 1)
    {
        pI2Cx->CR1 &= ~(1 << 10);   /* disable ACK (CR1, not SR1) */
        I2C_ClearAddrFlag(pI2CHandle);
        pI2Cx->CR1 |= (1 << 9);     /* generate STOP */
        while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_RXNE));
        *pRxBuffer = pI2Cx->DR;
        return;
    }

    I2C_ClearAddrFlag(pI2CHandle);

    for (uint32_t i = len; i > 0; i--)
    {
        while (!I2C_GetFlagStatus(pI2Cx, I2C_FLAG_RXNE));

        if (i == 2)
        {
            pI2Cx->CR1 &= ~(1 << 10); /* disable ACK */
            pI2Cx->CR1 |= (1 << 9);   /* generate STOP */
        }

        *pRxBuffer = pI2Cx->DR;
        pRxBuffer++;
    }

    if (pI2CHandle->I2C_Config.I2C_ACKControl == I2C_ACK_ENABLE)
        pI2Cx->CR1 |= (1 << 10); /* re-enable ACK for the next transfer */
}

/* ---------------------- Interrupt-driven master API ---------------------- */

uint8_t I2C_MasterSendDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pTxBuffer, uint8_t slaveaddress, uint32_t len, uint8_t Sr)
{
    uint8_t state = pI2CHandle->TxRxState;

    if (state != I2C_BUSY_IN_TX && state != I2C_BUSY_IN_RX)
    {
        pI2CHandle->pTxBuffer  = pTxBuffer;
        pI2CHandle->TxLen      = len;
        pI2CHandle->TxRxState  = I2C_BUSY_IN_TX;
        pI2CHandle->DevAddr    = slaveaddress;
        pI2CHandle->Sr         = Sr;

        pI2CHandle->pI2Cx->CR1 |= (1 << 8); /* START */

        pI2CHandle->pI2Cx->CR2 |= (1 << 9); /* ITBUFEN */
        pI2CHandle->pI2Cx->CR2 |= (1 << 8); /* ITEVFEN */
        pI2CHandle->pI2Cx->CR2 |= (1 << 7); /* ITERREN */
    }

    return state;
}

uint8_t I2C_MasterReceiveDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pRxBuffer, uint8_t slaveaddress, uint32_t len, uint8_t Sr)
{
    uint8_t state = pI2CHandle->TxRxState;

    if (state != I2C_BUSY_IN_TX && state != I2C_BUSY_IN_RX)
    {
        pI2CHandle->pRxBuffer  = pRxBuffer;
        pI2CHandle->RxLen      = len;
        pI2CHandle->TxRxState  = I2C_BUSY_IN_RX;
        pI2CHandle->RxSize     = len;
        pI2CHandle->DevAddr    = slaveaddress;
        pI2CHandle->Sr         = Sr;

        pI2CHandle->pI2Cx->CR1 |= (1 << 8); /* START */

        pI2CHandle->pI2Cx->CR2 |= (1 << 9); /* ITBUFEN */
        pI2CHandle->pI2Cx->CR2 |= (1 << 8); /* ITEVFEN */
        pI2CHandle->pI2Cx->CR2 |= (1 << 7); /* ITERREN */
    }

    return state;
}

void I2C_CloseSendData(I2C_Handle_t *pI2CHandle)
{
    pI2CHandle->pI2Cx->CR2 &= ~(1 << 9); /* ITBUFEN */
    pI2CHandle->pI2Cx->CR2 &= ~(1 << 8); /* ITEVFEN */
    pI2CHandle->TxRxState = I2C_READY;
    pI2CHandle->pTxBuffer = NULL;
    pI2CHandle->TxLen = 0;
}

void I2C_CloseReceiveData(I2C_Handle_t *pI2CHandle)
{
    pI2CHandle->pI2Cx->CR2 &= ~(1 << 9);
    pI2CHandle->pI2Cx->CR2 &= ~(1 << 8);
    pI2CHandle->TxRxState = I2C_READY;
    pI2CHandle->pRxBuffer = NULL;
    pI2CHandle->RxLen = 0;
    pI2CHandle->RxSize = 0;

    if (pI2CHandle->I2C_Config.I2C_ACKControl == I2C_ACK_ENABLE)
        pI2CHandle->pI2Cx->CR1 |= (1 << 10);
}

void I2C_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi)
{
    generic_IRQInterruptConfig(IRQNumber, EnorDi);
}

void I2C_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority)
{
    generic_IRQPriorityConfig(IRQNumber, IRQPriority);
}

void I2C_SlaveEnableDisableCallbackEvents(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    if (EnorDi == ENABLE)
    {
        pI2Cx->CR2 |= (1 << 9);
        pI2Cx->CR2 |= (1 << 8);
        pI2Cx->CR2 |= (1 << 7);
    }
    else
    {
        pI2Cx->CR2 &= ~(1 << 9);
        pI2Cx->CR2 &= ~(1 << 8);
        pI2Cx->CR2 &= ~(1 << 7);
    }
}

static void I2C_MasterHandleTXEInterrupt(I2C_Handle_t *pI2CHandle)
{
    if (pI2CHandle->TxLen > 0)
    {
        pI2CHandle->pI2Cx->DR = *(pI2CHandle->pTxBuffer);
        pI2CHandle->TxLen--;
        pI2CHandle->pTxBuffer++;
    }
}

static void I2C_MasterHandleRXNEInterrupt(I2C_Handle_t *pI2CHandle)
{
    if (pI2CHandle->RxSize == 1)
    {
        *pI2CHandle->pRxBuffer = pI2CHandle->pI2Cx->DR;
        pI2CHandle->RxLen--;
    }
    else if (pI2CHandle->RxSize > 1)
    {
        if (pI2CHandle->RxLen == 2)
            pI2CHandle->pI2Cx->CR1 &= ~(1 << 10); /* disable ACK for last byte */

        *pI2CHandle->pRxBuffer = pI2CHandle->pI2Cx->DR;
        pI2CHandle->pRxBuffer++;
        pI2CHandle->RxLen--;
    }

    if (pI2CHandle->RxLen == 0)
    {
        if (pI2CHandle->Sr == I2C_DISABLE_SR)
            pI2CHandle->pI2Cx->CR1 |= (1 << 9); /* STOP */

        I2C_CloseReceiveData(pI2CHandle);
        I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_RX_CMPLT);
    }
}

void I2C_EV_IRQHandling(I2C_Handle_t *pI2CHandle)
{
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;
    uint32_t itevfen = pI2Cx->CR2 & (1 << 8);
    uint32_t itbufen = pI2Cx->CR2 & (1 << 9);

    uint32_t sb   = pI2Cx->SR1 & I2C_FLAG_SB;
    uint32_t addr = pI2Cx->SR1 & I2C_FLAG_ADDR;
    uint32_t btf  = pI2Cx->SR1 & I2C_FLAG_BTF;
    uint32_t stopf = pI2Cx->SR1 & I2C_FLAG_STOPF;
    uint32_t txe  = pI2Cx->SR1 & I2C_FLAG_TXE;
    uint32_t rxne = pI2Cx->SR1 & I2C_FLAG_RXNE;

    if (itevfen && sb)
    {
        /* START generated: send the address byte */
        if (pI2CHandle->TxRxState == I2C_BUSY_IN_TX)
            I2C_ExecuteAddressPhaseWrite(pI2Cx, pI2CHandle->DevAddr);
        else if (pI2CHandle->TxRxState == I2C_BUSY_IN_RX)
            I2C_ExecuteAddressPhaseRead(pI2Cx, pI2CHandle->DevAddr);
    }

    if (itevfen && addr)
    {
        if (pI2CHandle->TxRxState == I2C_BUSY_IN_RX && pI2CHandle->RxSize == 1)
            pI2Cx->CR1 &= ~(1 << 10); /* single-byte read: NACK this one */

        I2C_ClearAddrFlag(pI2CHandle);
    }

    if (itevfen && btf)
    {
        if (pI2CHandle->TxRxState == I2C_BUSY_IN_TX && txe && pI2CHandle->TxLen == 0)
        {
            if (pI2CHandle->Sr == I2C_DISABLE_SR)
                pI2Cx->CR1 |= (1 << 9); /* STOP */

            I2C_CloseSendData(pI2CHandle);
            I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_TX_CMPLT);
        }
    }

    if (itevfen && stopf)
    {
        pI2Cx->CR1 |= 0x0000; /* STOPF is cleared by reading SR1 (already done above) then writing CR1 */
        I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_STOP);
    }

    if (itbufen && txe && pI2CHandle->TxRxState == I2C_BUSY_IN_TX)
    {
        I2C_MasterHandleTXEInterrupt(pI2CHandle);
    }

    if (itbufen && rxne && pI2CHandle->TxRxState == I2C_BUSY_IN_RX)
    {
        I2C_MasterHandleRXNEInterrupt(pI2CHandle);
    }
}

void I2C_ER_IRQHandling(I2C_Handle_t *pI2CHandle)
{
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;
    uint32_t iterren = pI2Cx->CR2 & (1 << 7);

    if (iterren && (pI2Cx->SR1 & I2C_FLAG_BERR))
    {
        pI2Cx->SR1 &= ~I2C_FLAG_BERR;
        I2C_ApplicationEventCallback(pI2CHandle, I2C_ERROR_BERR);
    }
    if (iterren && (pI2Cx->SR1 & I2C_FLAG_ARLO))
    {
        pI2Cx->SR1 &= ~I2C_FLAG_ARLO;
        I2C_ApplicationEventCallback(pI2CHandle, I2C_ERROR_ARLO);
    }
    if (iterren && (pI2Cx->SR1 & I2C_FLAG_AF))
    {
        pI2Cx->SR1 &= ~I2C_FLAG_AF;
        I2C_ApplicationEventCallback(pI2CHandle, I2C_ERROR_AF);
    }
    if (iterren && (pI2Cx->SR1 & I2C_FLAG_OVR))
    {
        pI2Cx->SR1 &= ~I2C_FLAG_OVR;
        I2C_ApplicationEventCallback(pI2CHandle, I2C_ERROR_OVR);
    }
    if (iterren && (pI2Cx->SR1 & I2C_FLAG_TIMEOUT))
    {
        pI2Cx->SR1 &= ~I2C_FLAG_TIMEOUT;
        I2C_ApplicationEventCallback(pI2CHandle, I2C_ERROR_TIMEOUT);
    }
}

__attribute__((weak)) void I2C_ApplicationEventCallback(I2C_Handle_t *pI2CHandle, uint8_t AppEv)
{
    (void)pI2CHandle;
    (void)AppEv;
}
