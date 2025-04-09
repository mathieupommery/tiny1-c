
#ifndef  HAL_I2C
#define HAL_I2C

#include <stdint.h>

#define I2C_MEMADD_SIZE_8BIT            ((uint32_t)0x00000001U)
#define I2C_MEMADD_SIZE_16BIT           ((uint32_t)0x00000002U)

typedef enum 
{
  HAL_OK       = 0x00U,
  HAL_ERROR    = 0x01U,
  HAL_BUSY     = 0x02U,
  HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;


void HAL_Delay(int n);
HAL_StatusTypeDef HAL_I2C_Init();
HAL_StatusTypeDef HAL_I2C_Mem_Write(uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);

#endif
