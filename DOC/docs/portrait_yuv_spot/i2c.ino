

void HAL_PrintErrorDesc(uint8_t err)
{
  switch (err) {
    case 1:
      Serial.printf("Data too long\n");
      break;
    case 2:
      Serial.printf("NACK on address\n");
      break;
    case 3:
      Serial.printf("NACK on data\n");
      break;
    case 4:
      Serial.printf("Other error\n");
      break;
    case 5:
      Serial.printf("Timeout\n");
      break;
    default:
      Serial.printf("Unknown error\n");
  }
}


void HAL_Delay(int n)
{
  delay(n);
}


HAL_StatusTypeDef HAL_I2C_Init()
{
  Wire.begin();
  Wire.setTimeout(2*1000);
  return HAL_OK;
}


HAL_StatusTypeDef HAL_I2C_Mem_Write(uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  int i;
  uint8_t ret;

  /*
  Serial.printf("write (%d): %x %x ", Size+2, MemAddress >> 8, MemAddress & 0xFF);
  for (i=0; i<Size; i++) {
    Serial.printf("%x ", *(pData+i));
  }
  Serial.println();
  */
  
  Wire.beginTransmission((uint8_t) DevAddress >> 1);
  Wire.write(MemAddress >> 8);
  Wire.write(MemAddress & 0xFF);
  for (i=0; i<Size; i++) {
    Wire.write(*pData++);
  }
  ret = Wire.endTransmission();

  if (ret != 0) {
    Serial.printf("Write address 0x%x failed with %d: ", MemAddress, ret);
    HAL_PrintErrorDesc(ret);
    return HAL_ERROR;
  }

  return HAL_OK;
}


HAL_StatusTypeDef HAL_I2C_Mem_Read(uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
  int i;
  uint8_t ret;

  Wire.beginTransmission((uint8_t) DevAddress >> 1);
  Wire.write(MemAddress >> 8);
  Wire.write(MemAddress & 0xFF);
  ret = Wire.endTransmission();
  if (ret != 0) {
    digitalWrite(33, HIGH);
    delayMicroseconds(50);
    digitalWrite(33, LOW);
    Serial.printf("Read set address 0x%x failed with %d: ", MemAddress, ret);
    HAL_PrintErrorDesc(ret);
    return HAL_ERROR;
  }

  ret = Wire.requestFrom((int) DevAddress >> 1, (int) Size, (int) true);
  if (ret == Size) {
    for (i=0; i<Size; i++) {
      *pData++ = Wire.read();
    }
  } else {
    digitalWrite(33, HIGH);
    delayMicroseconds(50);
    digitalWrite(33, LOW);
    Serial.printf("Read data from 0x%x failed with %d of %d bytes read\n", MemAddress, ret, Size);
    return HAL_ERROR;
  }

  return HAL_OK;
}
