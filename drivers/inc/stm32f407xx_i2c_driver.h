#ifndef INC_STM32F407XX_I2C_DRIVER_H_
#define INC_STM32F407XX_I2C_DRIVER_H_

#include <stdint.h>

typedef struct I2C_RegDef_t I2C_RegDef_t;
/*I2C Config structure:*/
typedef struct
{
    uint32_t SCL_Speed;
    uint8_t  I2C_DeviceAddress;
    uint8_t  I2C_ACKControl;
    uint8_t  I2C_FMDutyCycle;
} I2C_Config_t;

/* Interrupt-driven transfer state */
#define I2C_READY         0
#define I2C_BUSY_IN_RX    1
#define I2C_BUSY_IN_TX    2

/* Application event / error codes for I2C_ApplicationEventCallback as per timing diagram in RM0090 reference manual */
#define I2C_EV_TX_CMPLT   1
#define I2C_EV_RX_CMPLT   2
#define I2C_EV_STOP       3
#define I2C_ERROR_BERR    4
#define I2C_ERROR_ARLO    5
#define I2C_ERROR_AF      6
#define I2C_ERROR_OVR     7
#define I2C_ERROR_TIMEOUT 8
#define I2C_EV_DATA_REQ   9   /* slave mode: master wants a byte */
#define I2C_EV_DATA_RCV   10  /* slave mode: byte arrived */

/* repeated-start control for *_IT calls */
#define I2C_DISABLE_SR  0
#define I2C_ENABLE_SR   1


/*I2C Handle structure: */

typedef struct
{
    I2C_RegDef_t *pI2Cx;
    I2C_Config_t I2C_Config;

    /* used only by the *_IT (interrupt-driven) API below */
    uint8_t  *pTxBuffer;
    uint8_t  *pRxBuffer;
    uint32_t TxLen;
    uint32_t RxLen;
    uint8_t  TxRxState;
    uint8_t  DevAddr;
    uint32_t RxSize;
    uint8_t  Sr;
} I2C_Handle_t;
/*CONFIGURABLE MACROS: */

#define I2C_SCL_SPEED_SM  100000U
#define I2C_SCL_SPEED_FM  400000U

#define I2C_ACK_ENABLE   1
#define I2C_ACK_DISABLE  0

#define I2C_FM_DUTY_2     0
#define I2C_FM_DUTY_16_9  1

/* SR1 flag bit masks */
#define I2C_FLAG_SB     (1 << 0)
#define I2C_FLAG_ADDR   (1 << 1)
#define I2C_FLAG_BTF    (1 << 2)
#define I2C_FLAG_STOPF  (1 << 4)
#define I2C_FLAG_RXNE   (1 << 6)
#define I2C_FLAG_TXE    (1 << 7)
#define I2C_FLAG_BERR   (1 << 8)
#define I2C_FLAG_ARLO   (1 << 9)
#define I2C_FLAG_AF     (1 << 10)
#define I2C_FLAG_OVR    (1 << 11)
#define I2C_FLAG_TIMEOUT (1 << 14)
#define SB    I2C_FLAG_SB
#define ADDR  I2C_FLAG_ADDR
#define BTF   I2C_FLAG_BTF
#define RXNE  I2C_FLAG_RXNE
#define TXE   I2C_FLAG_TXE

/*I2C API Prototypes:*/

void I2C_PeripheralClockControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);
void I2C_PeripheralControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);
void I2C_Init(I2C_Handle_t *pI2CHandle);
void I2C_DeInit(I2C_RegDef_t *pI2Cx);

uint8_t I2C_GetFlagStatus(I2C_RegDef_t *pI2Cx, uint32_t FlagName);
uint32_t RCC_GetPCLK1Value(void);

/* blocking */
void I2C_MasterSendData(I2C_Handle_t *pI2CHandle, uint8_t *pTxBuffer, uint8_t slaveaddress, uint32_t len);
void I2C_MasterReceiveData(I2C_Handle_t *pI2CHandle, uint8_t *pRxBuffer, uint8_t slaveaddress, uint32_t len);

/* interrupt-driven */
uint8_t I2C_MasterSendDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pTxBuffer, uint8_t slaveaddress, uint32_t len, uint8_t Sr);
uint8_t I2C_MasterReceiveDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pRxBuffer, uint8_t slaveaddress, uint32_t len, uint8_t Sr);

void I2C_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi);
void I2C_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority);
void I2C_EV_IRQHandling(I2C_Handle_t *pI2CHandle);
void I2C_ER_IRQHandling(I2C_Handle_t *pI2CHandle);
void I2C_SlaveEnableDisableCallbackEvents(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);

void I2C_CloseSendData(I2C_Handle_t *pI2CHandle);
void I2C_CloseReceiveData(I2C_Handle_t *pI2CHandle);

/* Weak — override in application. */
void I2C_ApplicationEventCallback(I2C_Handle_t *pI2CHandle, uint8_t AppEv);

#endif /* INC_STM32F407XX_I2C_DRIVER_H_ */
