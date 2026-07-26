/**
  ******************************************************************************
  * @file    soft_i2c.c
  * @brief   基于 STM32 HAL GPIO 的阻塞式软件 I2C 驱动实现。
  *
  * @details
  *          开漏总线通过“写低”主动拉低，通过“写高”释放线路并由外部上拉
  *          电阻产生高电平。驱动在每次释放 SCL 后读取实际引脚电平，以支持
  *          从机时钟拉伸和线路异常检测。
  *
  *          所有公开传输函数都遵循相同状态机：验证参数 -> 要求 READY ->
  *          置 BUSY/清错误 -> 执行协议阶段 -> 无条件尝试 STOP -> 更新状态。
  *          NACK 后总线仍可回到 READY；SCL 超时或物理 BUS_BUSY 会进入 ERROR，
  *          调用者应重新执行 SoftI2C_Init() 检查并恢复总线。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "soft_i2c.h"

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  产生一个软件 I2C 时序延迟。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval None
  * @note   DelayCycles 是忙等待循环次数，不是固定时间单位；总线速度会随
  *         CPU 主频、编译优化等级和存储器等待状态变化。
  */
static void SoftI2C_Delay(const SoftI2C_HandleTypeDef *hi2c)
{
  for (volatile uint32_t i = 0u; i < hi2c->DelayCycles; ++i)
  {
    __NOP();
  }
}

/**
  * @brief  写入 SCL GPIO 输出锁存器。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  state RESET 主动拉低，SET 释放开漏线路。
  * @retval None
  */
static void SoftI2C_SetSCL(SoftI2C_HandleTypeDef *hi2c, GPIO_PinState state)
{
  HAL_GPIO_WritePin(hi2c->SCL_Port, hi2c->SCL_Pin, state);
}

/**
  * @brief  写入 SDA GPIO 输出锁存器。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  state RESET 主动拉低，SET 释放开漏线路。
  * @retval None
  */
static void SoftI2C_SetSDA(SoftI2C_HandleTypeDef *hi2c, GPIO_PinState state)
{
  HAL_GPIO_WritePin(hi2c->SDA_Port, hi2c->SDA_Pin, state);
}

/**
  * @brief  释放 SCL 并等待线路实际变高。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval SOFT_I2C_OK      SCL 已变高。
  * @retval SOFT_I2C_TIMEOUT 从机持续拉低 SCL 或线路异常。
  * @note   该等待实现 I2C 时钟拉伸支持。
  */
static SoftI2C_StatusTypeDef SoftI2C_ReleaseSCL(SoftI2C_HandleTypeDef *hi2c)
{
  SoftI2C_SetSCL(hi2c, GPIO_PIN_SET);

  /* SET 只释放开漏输出；必须轮询 IDR 确认线路真的被上拉到高电平。 */
  for (uint32_t i = 0u; i < hi2c->ClockStretchTimeout; ++i)
  {
    if (HAL_GPIO_ReadPin(hi2c->SCL_Port, hi2c->SCL_Pin) == GPIO_PIN_SET)
    {
      return SOFT_I2C_OK;
    }
    SoftI2C_Delay(hi2c);
  }

  hi2c->ErrorCode |= SOFT_I2C_ERROR_SCL_TIMEOUT;
  return SOFT_I2C_TIMEOUT;
}

/**
  * @brief  主动将 SCL 拉低。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval None
  */
static void SoftI2C_DriveSCLLow(SoftI2C_HandleTypeDef *hi2c)
{
  SoftI2C_SetSCL(hi2c, GPIO_PIN_RESET);
}

/**
  * @brief  释放 SDA，由外部上拉电阻将线路拉高。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval None
  */
static void SoftI2C_ReleaseSDA(SoftI2C_HandleTypeDef *hi2c)
{
  SoftI2C_SetSDA(hi2c, GPIO_PIN_SET);
}

/**
  * @brief  主动将 SDA 拉低。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval None
  */
static void SoftI2C_DriveSDALow(SoftI2C_HandleTypeDef *hi2c)
{
  SoftI2C_SetSDA(hi2c, GPIO_PIN_RESET);
}

/**
  * @brief  产生 I2C STOP 条件：SCL 为高时 SDA 从低变高。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval SOFT_I2C_OK      STOP 成功。
  * @retval SOFT_I2C_TIMEOUT SCL 无法释放。
  */
static SoftI2C_StatusTypeDef SoftI2C_Stop(SoftI2C_HandleTypeDef *hi2c)
{
  /* 先确保 SDA 为低，再在 SCL 高电平期间释放 SDA，形成唯一合法 STOP 边沿。 */
  SoftI2C_DriveSDALow(hi2c);
  SoftI2C_Delay(hi2c);

  SoftI2C_StatusTypeDef status = SoftI2C_ReleaseSCL(hi2c);
  SoftI2C_Delay(hi2c);
  SoftI2C_ReleaseSDA(hi2c);
  SoftI2C_Delay(hi2c);
  return status;
}

/**
  * @brief  对被从机卡住的总线执行内部恢复流程。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval SOFT_I2C_OK      SDA 已释放且 STOP 成功。
  * @retval SOFT_I2C_BUSY    发送最多 9 个 SCL 脉冲后 SDA 仍为低。
  * @retval SOFT_I2C_TIMEOUT 恢复期间 SCL 无法释放。
  * @note   最多产生 9 个时钟脉冲，使停留在字节传输中的从机有机会完成当前
  *         字节并释放 SDA，随后发送 STOP 让总线回到空闲状态。
  */
static SoftI2C_StatusTypeDef SoftI2C_RecoverInternal(SoftI2C_HandleTypeDef *hi2c)
{
  /* 主机必须先释放 SDA，避免与可能仍在输出数据的从机电气冲突。 */
  SoftI2C_ReleaseSDA(hi2c);

  for (uint32_t pulse = 0u; pulse < 9u; ++pulse)
  {
    /* 从机已经释放 SDA 就不再产生多余时钟。 */
    if (HAL_GPIO_ReadPin(hi2c->SDA_Port, hi2c->SDA_Pin) == GPIO_PIN_SET)
    {
      break;
    }

    /* 一个 I2C 字节最多为 8 位数据 + 1 位 ACK，故最多补 9 个时钟。 */
    SoftI2C_DriveSCLLow(hi2c);
    SoftI2C_Delay(hi2c);
    if (SoftI2C_ReleaseSCL(hi2c) != SOFT_I2C_OK)
    {
      return SOFT_I2C_TIMEOUT;
    }
    SoftI2C_Delay(hi2c);
  }

  /* 即便 SDA 已释放，也显式产生 STOP，使从机协议状态机退出当前事务。 */
  if (SoftI2C_Stop(hi2c) != SOFT_I2C_OK)
  {
    return SOFT_I2C_TIMEOUT;
  }

  if (HAL_GPIO_ReadPin(hi2c->SDA_Port, hi2c->SDA_Pin) == GPIO_PIN_RESET)
  {
    hi2c->ErrorCode |= SOFT_I2C_ERROR_BUS_BUSY;
    return SOFT_I2C_BUSY;
  }

  return SOFT_I2C_OK;
}

/**
  * @brief  产生 I2C START 或重复 START 条件。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval SOFT_I2C_OK      START 成功。
  * @retval SOFT_I2C_BUSY    SCL 为高时 SDA 已被其他设备拉低。
  * @retval SOFT_I2C_TIMEOUT SCL 无法释放。
  */
static SoftI2C_StatusTypeDef SoftI2C_Start(SoftI2C_HandleTypeDef *hi2c)
{
  /* START 前先构造总线空闲态：SDA/SCL 均释放为高。 */
  SoftI2C_ReleaseSDA(hi2c);
  if (SoftI2C_ReleaseSCL(hi2c) != SOFT_I2C_OK)
  {
    return SOFT_I2C_TIMEOUT;
  }
  SoftI2C_Delay(hi2c);

  if (HAL_GPIO_ReadPin(hi2c->SDA_Port, hi2c->SDA_Pin) == GPIO_PIN_RESET)
  {
    hi2c->ErrorCode |= SOFT_I2C_ERROR_BUS_BUSY;
    return SOFT_I2C_BUSY;
  }

  /* SCL 保持高电平时 SDA 高->低即 START；随后拉低 SCL 进入数据阶段。 */
  SoftI2C_DriveSDALow(hi2c);
  SoftI2C_Delay(hi2c);
  SoftI2C_DriveSCLLow(hi2c);
  SoftI2C_Delay(hi2c);
  return SOFT_I2C_OK;
}

/**
  * @brief  按最高位优先方式发送一个字节并采样第 9 个时钟的 ACK。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  data 待发送字节。
  * @param  acknowledged ACK 结果输出；1 表示 SDA 被从机拉低，0 表示 NACK。
  * @retval SOFT_I2C_OK      字节和 ACK 时钟发送完成。
  * @retval SOFT_I2C_TIMEOUT 任一位期间 SCL 无法释放。
  */
static SoftI2C_StatusTypeDef SoftI2C_WriteByte(SoftI2C_HandleTypeDef *hi2c,
                                               uint8_t data,
                                               uint8_t *acknowledged)
{
  for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1u)
  {
    /* I2C 按 MSB first 发送；SCL 低电平期间改变 SDA，SCL 高时数据稳定。 */
    SoftI2C_DriveSCLLow(hi2c);
    if ((data & mask) != 0u)
    {
      SoftI2C_ReleaseSDA(hi2c);
    }
    else
    {
      SoftI2C_DriveSDALow(hi2c);
    }

    SoftI2C_Delay(hi2c);
    if (SoftI2C_ReleaseSCL(hi2c) != SOFT_I2C_OK)
    {
      return SOFT_I2C_TIMEOUT;
    }
    SoftI2C_Delay(hi2c);
  }

  /* 第 9 个时钟由从机驱动 ACK，因此主机先释放 SDA 再采样实际电平。 */
  SoftI2C_DriveSCLLow(hi2c);
  SoftI2C_ReleaseSDA(hi2c);
  SoftI2C_Delay(hi2c);
  if (SoftI2C_ReleaseSCL(hi2c) != SOFT_I2C_OK)
  {
    return SOFT_I2C_TIMEOUT;
  }
  SoftI2C_Delay(hi2c);

  *acknowledged = (HAL_GPIO_ReadPin(hi2c->SDA_Port, hi2c->SDA_Pin) == GPIO_PIN_RESET) ? 1u : 0u;
  SoftI2C_DriveSCLLow(hi2c);
  SoftI2C_Delay(hi2c);
  return SOFT_I2C_OK;
}

/**
  * @brief  按最高位优先方式接收一个字节，并在第 9 个时钟发送 ACK/NACK。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  data 接收字节输出指针。
  * @param  acknowledge 非 0 发送 ACK，0 发送 NACK。
  * @retval SOFT_I2C_OK      接收完成。
  * @retval SOFT_I2C_TIMEOUT 任一位期间 SCL 无法释放。
  * @note   连续读取时除最后一个字节外均应发送 ACK；最后一个字节发送 NACK。
  */
static SoftI2C_StatusTypeDef SoftI2C_ReadByte(SoftI2C_HandleTypeDef *hi2c,
                                              uint8_t *data,
                                              uint8_t acknowledge)
{
  uint8_t value = 0u;
  /* 接收期间 SDA 必须始终释放，由从机驱动每一位。 */
  SoftI2C_ReleaseSDA(hi2c);

  for (uint8_t bit = 0u; bit < 8u; ++bit)
  {
    SoftI2C_DriveSCLLow(hi2c);
    SoftI2C_Delay(hi2c);
    if (SoftI2C_ReleaseSCL(hi2c) != SOFT_I2C_OK)
    {
      return SOFT_I2C_TIMEOUT;
    }
    SoftI2C_Delay(hi2c);

    /* 每次在 SCL 高电平稳定区采样，并按 MSB first 拼入结果。 */
    value <<= 1u;
    if (HAL_GPIO_ReadPin(hi2c->SDA_Port, hi2c->SDA_Pin) == GPIO_PIN_SET)
    {
      value |= 1u;
    }
  }

  /* 第 9 个时钟改由主机应答：低为 ACK，高（释放）为 NACK。 */
  SoftI2C_DriveSCLLow(hi2c);
  if (acknowledge != 0u)
  {
    SoftI2C_DriveSDALow(hi2c);
  }
  else
  {
    SoftI2C_ReleaseSDA(hi2c);
  }
  SoftI2C_Delay(hi2c);

  if (SoftI2C_ReleaseSCL(hi2c) != SOFT_I2C_OK)
  {
    return SOFT_I2C_TIMEOUT;
  }
  SoftI2C_Delay(hi2c);
  SoftI2C_DriveSCLLow(hi2c);
  SoftI2C_ReleaseSDA(hi2c);
  SoftI2C_Delay(hi2c);

  *data = value;
  return SOFT_I2C_OK;
}

/**
  * @brief  发送一个字节，并在收到 NACK 时记录指定错误位。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  data 待发送字节。
  * @param  nack_error 收到 NACK 时写入 ErrorCode 的错误位。
  * @retval SOFT_I2C_OK      从机返回 ACK。
  * @retval SOFT_I2C_ERROR   从机返回 NACK。
  * @retval SOFT_I2C_TIMEOUT SCL 无法释放。
  */
static SoftI2C_StatusTypeDef SoftI2C_WriteCheckedByte(SoftI2C_HandleTypeDef *hi2c,
                                                      uint8_t data,
                                                      uint32_t nack_error)
{
  uint8_t acknowledged = 0u;
  SoftI2C_StatusTypeDef status = SoftI2C_WriteByte(hi2c, data, &acknowledged);
  if (status != SOFT_I2C_OK)
  {
    return status;
  }

  if (acknowledged == 0u)
  {
    hi2c->ErrorCode |= nack_error;
    return SOFT_I2C_ERROR;
  }

  return SOFT_I2C_OK;
}

/**
  * @brief  发送 8 位或 16 位从机内部寄存器地址。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  mem_address 从机内部寄存器地址。
  * @param  mem_address_size 地址宽度。
  * @retval SoftI2C_StatusTypeDef 发送结果。
  * @note   16 位地址按照高字节、低字节顺序发送。
  */
static SoftI2C_StatusTypeDef SoftI2C_WriteMemoryAddress(SoftI2C_HandleTypeDef *hi2c,
                                                        uint16_t mem_address,
                                                        SoftI2C_MemAddrSizeTypeDef mem_address_size)
{
  SoftI2C_StatusTypeDef status;

  if (mem_address_size == SOFT_I2C_MEM_ADDR_16BIT)
  {
    status = SoftI2C_WriteCheckedByte(hi2c,
                                      (uint8_t)(mem_address >> 8u),
                                      SOFT_I2C_ERROR_NACK_DATA);
    if (status != SOFT_I2C_OK)
    {
      return status;
    }
  }

  return SoftI2C_WriteCheckedByte(hi2c,
                                  (uint8_t)mem_address,
                                  SOFT_I2C_ERROR_NACK_DATA);
}

/**
  * @brief  检查软件 I2C 句柄中的 GPIO 和超时参数是否有效。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval 1 句柄有效。
  * @retval 0 句柄为空或关键字段非法。
  */
static uint8_t SoftI2C_IsHandleValid(const SoftI2C_HandleTypeDef *hi2c)
{
  return (hi2c != NULL) &&
         (hi2c->SCL_Port != NULL) &&
         (hi2c->SCL_Pin != 0u) &&
         (hi2c->SDA_Port != NULL) &&
         (hi2c->SDA_Pin != 0u) &&
         (hi2c->ClockStretchTimeout != 0u);
}

/**
  * @brief  检查寄存器读写传输的全部参数。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址。
  * @param  mem_address_size 寄存器地址宽度。
  * @param  data 数据缓冲区。
  * @param  size 数据长度。
  * @retval 1 参数有效。
  * @retval 0 至少一个参数非法。
  */
static uint8_t SoftI2C_IsTransferValid(const SoftI2C_HandleTypeDef *hi2c,
                                       uint8_t device_address_7bit,
                                       SoftI2C_MemAddrSizeTypeDef mem_address_size,
                                       const uint8_t *data,
                                       uint16_t size)
{
  return (SoftI2C_IsHandleValid(hi2c) != 0u) &&
         (device_address_7bit <= 0x7Fu) &&
         ((mem_address_size == SOFT_I2C_MEM_ADDR_8BIT) ||
          (mem_address_size == SOFT_I2C_MEM_ADDR_16BIT)) &&
         (data != NULL) &&
         (size != 0u);
}

/**
  * @brief  检查普通主机发送/接收传输参数。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址。
  * @param  data 数据缓冲区。
  * @param  size 数据长度。
  * @retval 1 参数有效。
  * @retval 0 至少一个参数非法。
  */
static uint8_t SoftI2C_IsBufferTransferValid(const SoftI2C_HandleTypeDef *hi2c,
                                             uint8_t device_address_7bit,
                                             const uint8_t *data,
                                             uint16_t size)
{
  return (SoftI2C_IsHandleValid(hi2c) != 0u) &&
         (device_address_7bit <= 0x7Fu) &&
         (data != NULL) &&
         (size != 0u);
}

/**
  * @brief  发送 STOP，合并 STOP 结果并更新句柄状态。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  status STOP 前的传输状态。
  * @retval SoftI2C_StatusTypeDef 最终传输状态。
  * @note   若数据阶段成功但 STOP 失败，返回 STOP 错误；TIMEOUT/BUSY 会让
  *         句柄进入 ERROR，NACK 等普通错误结束后句柄恢复 READY。
  */
static SoftI2C_StatusTypeDef SoftI2C_FinishTransfer(SoftI2C_HandleTypeDef *hi2c,
                                                    SoftI2C_StatusTypeDef status)
{
  /* 无论前面成功、NACK 或超时都尝试 STOP，尽量把物理总线带回空闲。 */
  SoftI2C_StatusTypeDef stop_status = SoftI2C_Stop(hi2c);

  if ((status == SOFT_I2C_OK) && (stop_status != SOFT_I2C_OK))
  {
    status = stop_status;
  }

  /* 物理线路异常需要重新 Init；普通 NACK 不表示总线本身已经损坏。 */
  if ((status == SOFT_I2C_TIMEOUT) || (status == SOFT_I2C_BUSY))
  {
    hi2c->State = SOFT_I2C_STATE_ERROR;
  }
  else
  {
    hi2c->State = SOFT_I2C_STATE_READY;
  }

  return status;
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化软件 I2C 并在需要时自动恢复被占用的总线。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval SoftI2C_StatusTypeDef 初始化结果。
  */
SoftI2C_StatusTypeDef SoftI2C_Init(SoftI2C_HandleTypeDef *hi2c)
{
  if (SoftI2C_IsHandleValid(hi2c) == 0u)
  {
    if (hi2c != NULL)
    {
      hi2c->State = SOFT_I2C_STATE_ERROR;
      hi2c->ErrorCode = SOFT_I2C_ERROR_INVALID_PARAM;
    }
    return SOFT_I2C_ERROR;
  }

  hi2c->State = SOFT_I2C_STATE_BUSY;
  hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
  /* 初始化不假定上一次执行状态，先释放 SDA/SCL 并检查真实线路电平。 */
  SoftI2C_ReleaseSDA(hi2c);

  if (SoftI2C_ReleaseSCL(hi2c) != SOFT_I2C_OK)
  {
    hi2c->State = SOFT_I2C_STATE_ERROR;
    return SOFT_I2C_TIMEOUT;
  }
  SoftI2C_Delay(hi2c);

  if (HAL_GPIO_ReadPin(hi2c->SDA_Port, hi2c->SDA_Pin) == GPIO_PIN_RESET)
  {
    /* SDA 低可能是上次传输中断后从机停在发送态，自动执行 9 脉冲恢复。 */
    SoftI2C_StatusTypeDef status = SoftI2C_RecoverInternal(hi2c);
    if (status != SOFT_I2C_OK)
    {
      hi2c->State = SOFT_I2C_STATE_ERROR;
      return status;
    }
  }

  hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
  hi2c->State = SOFT_I2C_STATE_READY;
  return SOFT_I2C_OK;
}

/**
  * @brief  通过发送地址写字节轮询从机是否就绪。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  trials 最大探测次数。
  * @retval SoftI2C_StatusTypeDef 探测结果。
  */
SoftI2C_StatusTypeDef SoftI2C_IsDeviceReady(SoftI2C_HandleTypeDef *hi2c,
                                            uint8_t device_address_7bit,
                                            uint32_t trials)
{
  if ((SoftI2C_IsHandleValid(hi2c) == 0u) ||
      (device_address_7bit > 0x7Fu) ||
      (trials == 0u))
  {
    if (hi2c != NULL)
    {
      hi2c->ErrorCode = SOFT_I2C_ERROR_INVALID_PARAM;
    }
    return SOFT_I2C_ERROR;
  }

  if (hi2c->State != SOFT_I2C_STATE_READY)
  {
    return SOFT_I2C_BUSY;
  }

  hi2c->State = SOFT_I2C_STATE_BUSY;

  for (uint32_t trial = 0u; trial < trials; ++trial)
  {
    uint8_t acknowledged = 0u;
    hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
    SoftI2C_StatusTypeDef status = SoftI2C_Start(hi2c);

    /* 探测只发送写方向地址字节，不发送寄存器地址或数据。 */
    if (status == SOFT_I2C_OK)
    {
      status = SoftI2C_WriteByte(hi2c, (uint8_t)(device_address_7bit << 1u), &acknowledged);
    }

    /* 每次试探都是独立事务，收到 ACK/NACK 后均结束为 STOP。 */
    SoftI2C_StatusTypeDef stop_status = SoftI2C_Stop(hi2c);
    if ((status == SOFT_I2C_OK) && (stop_status == SOFT_I2C_OK) && (acknowledged != 0u))
    {
      hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
      hi2c->State = SOFT_I2C_STATE_READY;
      return SOFT_I2C_OK;
    }

    if (status != SOFT_I2C_OK)
    {
      hi2c->State = SOFT_I2C_STATE_ERROR;
      return status;
    }
  }

  hi2c->ErrorCode = SOFT_I2C_ERROR_NACK_ADDRESS;
  hi2c->State = SOFT_I2C_STATE_READY;
  return SOFT_I2C_ERROR;
}

/**
  * @brief  以主机发送模式连续发送数据。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  data 待发送缓冲区。
  * @param  size 待发送字节数。
  * @retval SoftI2C_StatusTypeDef 传输结果。
  */
SoftI2C_StatusTypeDef SoftI2C_MasterTransmit(SoftI2C_HandleTypeDef *hi2c,
                                             uint8_t device_address_7bit,
                                             const uint8_t *data,
                                             uint16_t size)
{
  if (SoftI2C_IsBufferTransferValid(hi2c, device_address_7bit, data, size) == 0u)
  {
    if (hi2c != NULL)
    {
      hi2c->ErrorCode = SOFT_I2C_ERROR_INVALID_PARAM;
    }
    return SOFT_I2C_ERROR;
  }

  if (hi2c->State != SOFT_I2C_STATE_READY)
  {
    return SOFT_I2C_BUSY;
  }

  hi2c->State = SOFT_I2C_STATE_BUSY;
  hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;

  SoftI2C_StatusTypeDef status = SoftI2C_Start(hi2c);
  /* 普通发送序列：START -> 地址+W -> N 个数据字节 -> STOP。 */
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_WriteCheckedByte(hi2c,
                                      (uint8_t)(device_address_7bit << 1u),
                                      SOFT_I2C_ERROR_NACK_ADDRESS);
  }

  for (uint16_t i = 0u; (i < size) && (status == SOFT_I2C_OK); ++i)
  {
    status = SoftI2C_WriteCheckedByte(hi2c, data[i], SOFT_I2C_ERROR_NACK_DATA);
  }

  status = SoftI2C_FinishTransfer(hi2c, status);
  if (status == SOFT_I2C_OK)
  {
    hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
  }
  return status;
}

/**
  * @brief  以主机接收模式连续读取数据。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  data 接收缓冲区。
  * @param  size 待读取字节数。
  * @retval SoftI2C_StatusTypeDef 传输结果。
  */
SoftI2C_StatusTypeDef SoftI2C_MasterReceive(SoftI2C_HandleTypeDef *hi2c,
                                            uint8_t device_address_7bit,
                                            uint8_t *data,
                                            uint16_t size)
{
  if (SoftI2C_IsBufferTransferValid(hi2c, device_address_7bit, data, size) == 0u)
  {
    if (hi2c != NULL)
    {
      hi2c->ErrorCode = SOFT_I2C_ERROR_INVALID_PARAM;
    }
    return SOFT_I2C_ERROR;
  }

  if (hi2c->State != SOFT_I2C_STATE_READY)
  {
    return SOFT_I2C_BUSY;
  }

  hi2c->State = SOFT_I2C_STATE_BUSY;
  hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;

  SoftI2C_StatusTypeDef status = SoftI2C_Start(hi2c);
  /* 普通接收序列：START -> 地址+R -> N 个数据字节 -> STOP。 */
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_WriteCheckedByte(hi2c,
                                      (uint8_t)((device_address_7bit << 1u) | 1u),
                                      SOFT_I2C_ERROR_NACK_ADDRESS);
  }

  for (uint16_t i = 0u; (i < size) && (status == SOFT_I2C_OK); ++i)
  {
    /* 还有后续字节时回 ACK；最后一字节回 NACK，通知从机结束发送。 */
    status = SoftI2C_ReadByte(hi2c, &data[i], (i + 1u < size) ? 1u : 0u);
  }

  status = SoftI2C_FinishTransfer(hi2c, status);
  if (status == SOFT_I2C_OK)
  {
    hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
  }
  return status;
}

/**
  * @brief  使用写地址、寄存器地址、重复 START 和读地址序列读取寄存器。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  mem_address 从机内部寄存器地址。
  * @param  mem_address_size 寄存器地址宽度。
  * @param  data 接收缓冲区。
  * @param  size 待读取字节数。
  * @retval SoftI2C_StatusTypeDef 传输结果。
  */
SoftI2C_StatusTypeDef SoftI2C_MemRead(SoftI2C_HandleTypeDef *hi2c,
                                      uint8_t device_address_7bit,
                                      uint16_t mem_address,
                                      SoftI2C_MemAddrSizeTypeDef mem_address_size,
                                      uint8_t *data,
                                      uint16_t size)
{
  if (SoftI2C_IsTransferValid(hi2c, device_address_7bit, mem_address_size, data, size) == 0u)
  {
    if (hi2c != NULL)
    {
      hi2c->ErrorCode = SOFT_I2C_ERROR_INVALID_PARAM;
    }
    return SOFT_I2C_ERROR;
  }

  if (hi2c->State != SOFT_I2C_STATE_READY)
  {
    return SOFT_I2C_BUSY;
  }

  hi2c->State = SOFT_I2C_STATE_BUSY;
  hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;

  SoftI2C_StatusTypeDef status = SoftI2C_Start(hi2c);
  /* 阶段 1：写方向寻址，用于设置从机内部地址指针。 */
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_WriteCheckedByte(hi2c,
                                      (uint8_t)(device_address_7bit << 1u),
                                      SOFT_I2C_ERROR_NACK_ADDRESS);
  }
  /* 阶段 2：发送 8/16 位内部地址，不插入 STOP。 */
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_WriteMemoryAddress(hi2c, mem_address, mem_address_size);
  }
  /* 阶段 3：重复 START，保持本次总线事务的连续性。 */
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_Start(hi2c);
  }
  /* 阶段 4：读方向重新寻址，然后连续接收数据。 */
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_WriteCheckedByte(hi2c,
                                      (uint8_t)((device_address_7bit << 1u) | 1u),
                                      SOFT_I2C_ERROR_NACK_ADDRESS);
  }

  for (uint16_t i = 0u; (i < size) && (status == SOFT_I2C_OK); ++i)
  {
    status = SoftI2C_ReadByte(hi2c, &data[i], (i + 1u < size) ? 1u : 0u);
  }

  status = SoftI2C_FinishTransfer(hi2c, status);
  if (status == SOFT_I2C_OK)
  {
    hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
  }
  return status;
}

/**
  * @brief  使用写地址、寄存器地址和数据序列写入从机寄存器。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  mem_address 从机内部寄存器地址。
  * @param  mem_address_size 寄存器地址宽度。
  * @param  data 待发送缓冲区。
  * @param  size 待写入字节数。
  * @retval SoftI2C_StatusTypeDef 传输结果。
  */
SoftI2C_StatusTypeDef SoftI2C_MemWrite(SoftI2C_HandleTypeDef *hi2c,
                                       uint8_t device_address_7bit,
                                       uint16_t mem_address,
                                       SoftI2C_MemAddrSizeTypeDef mem_address_size,
                                       const uint8_t *data,
                                       uint16_t size)
{
  if (SoftI2C_IsTransferValid(hi2c, device_address_7bit, mem_address_size,
                              data, size) == 0u)
  {
    if (hi2c != NULL)
    {
      hi2c->ErrorCode = SOFT_I2C_ERROR_INVALID_PARAM;
    }
    return SOFT_I2C_ERROR;
  }

  if (hi2c->State != SOFT_I2C_STATE_READY)
  {
    return SOFT_I2C_BUSY;
  }

  hi2c->State = SOFT_I2C_STATE_BUSY;
  hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;

  SoftI2C_StatusTypeDef status = SoftI2C_Start(hi2c);
  /* 写寄存器序列：START -> 地址+W -> 内部地址 -> 数据 -> STOP。 */
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_WriteCheckedByte(hi2c,
                                      (uint8_t)(device_address_7bit << 1u),
                                      SOFT_I2C_ERROR_NACK_ADDRESS);
  }
  if (status == SOFT_I2C_OK)
  {
    status = SoftI2C_WriteMemoryAddress(hi2c, mem_address, mem_address_size);
  }

  for (uint16_t i = 0u; (i < size) && (status == SOFT_I2C_OK); ++i)
  {
    status = SoftI2C_WriteCheckedByte(hi2c, data[i], SOFT_I2C_ERROR_NACK_DATA);
  }

  status = SoftI2C_FinishTransfer(hi2c, status);
  if (status == SOFT_I2C_OK)
  {
    hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
  }
  return status;
}
