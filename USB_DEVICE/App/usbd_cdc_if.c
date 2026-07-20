/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v1.0_Cube
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */

/**
  * @brief CDC ACM SET_CONTROL_LINE_STATE 请求中 DTR 对应的位掩码。
  * @note  USB 主机把 wValue 的 bit0 置 1 表示“数据终端已就绪/串口已打开”。
  *        该信号比“设备完成枚举”更接近 VSCode 串口终端真正打开的时刻。
  */
#define CDC_CONTROL_LINE_DTR_MASK       (1U << 0)

/**
  * @brief DTR 上升沿后额外等待主机终端稳定的时间，单位 ms。
  * @note  某些 Windows 串口工具在发出 DTR 后仍需短时间才能显示首包。此等待
  *        不使用 HAL_Delay()；CDC_IsReady_FS() 只比较 tick 并立即返回。
  */
#define CDC_PORT_OPEN_SETTLE_TIME_MS    1000U

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Variables USBD_CDC_IF_Private_Variables
  * @brief Private variables.
  * @{
  */
/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

/* USER CODE BEGIN PRIVATE_VARIABLES */

/**
  * @brief 主机是否通过 CDC DTR 表示已经打开虚拟串口。
  * @note  在 USB 控制请求回调中写入，在主循环日志路径中读取，因此使用
  *        volatile 强制每次从内存取值；volatile 不提供线程互斥。
  */
static volatile uint8_t cdc_port_open_fs = 0U;

/** @brief 最近一次 DTR 从 0 变为 1 时的 HAL 毫秒时间戳。 */
static volatile uint32_t cdc_port_open_tick_fs = 0U;

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 3 */
  /* 每次 CDC 类重新初始化都视为一次新的主机连接，必须等待新的 DTR。 */
  cdc_port_open_fs = 0U;
  cdc_port_open_tick_fs = 0U;

  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  /* 类被释放后旧的 DTR/tick 已无效，防止重连时误判为可发送。 */
  cdc_port_open_fs = 0U;
  cdc_port_open_tick_fs = 0U;
  return (USBD_OK);
  /* USER CODE END 4 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 5 */
  UNUSED(length);

  switch(cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:

    break;

    case CDC_GET_ENCAPSULATED_RESPONSE:

    break;

    case CDC_SET_COMM_FEATURE:

    break;

    case CDC_GET_COMM_FEATURE:

    break;

    case CDC_CLEAR_COMM_FEATURE:

    break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
    case CDC_SET_LINE_CODING:

    break;

    case CDC_GET_LINE_CODING:

    break;

    case CDC_SET_CONTROL_LINE_STATE:
      if (pbuf != NULL)
      {
        /*
         * Cube USB CDC 类把原始 USB Setup Request 指针传给 Control 回调。
         * SET_CONTROL_LINE_STATE 没有数据阶段，DTR/RTS 位位于 wValue 中。
         * const 指针表明这里只读取请求，不会修改 USB 库拥有的结构体。
         */
        const USBD_SetupReqTypedef *request = (const USBD_SetupReqTypedef *)pbuf;
        const uint8_t port_open =
            ((request->wValue & CDC_CONTROL_LINE_DTR_MASK) != 0U) ? 1U : 0U;

        /* 仅记录 0 -> 1 上升沿；持续重复置 1 不应重新开始稳定计时。 */
        if ((port_open != 0U) && (cdc_port_open_fs == 0U))
        {
          cdc_port_open_tick_fs = HAL_GetTick();
        }

        /* DTR 清零表示终端关闭；下一次打开会重新记录上升沿时间。 */
        cdc_port_open_fs = port_open;
      }
    break;

    case CDC_SEND_BREAK:

    break;

  default:
    break;
  }

  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  UNUSED(Len);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  *         @note
  *
  *
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 7 */
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

  /*
   * pClassData 只有在 CDC 类完成初始化后才指向有效 Handle。先验证参数、
   * 类对象和枚举状态，避免解引用 NULL 或在未配置状态提交 IN 传输。
   */
  if ((Buf == NULL) ||
      (Len == 0U) ||
      (hcdc == NULL) ||
      (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED))
  {
    return USBD_FAIL;
  }

  /*
   * ST CDC 类内部把 TxState 当作二值忙标志：0U 为空闲，发送请求被接受时
   * 置 1U，IN 端点传输完成回调中恢复 0U。它不是一个公开枚举。
   */
  if (hcdc->TxState != 0U)
  {
    return USBD_BUSY;
  }
  /*
   * SetTxBuffer 只登记指针和长度，不复制 Buf。调用者必须保证 Buf 在异步
   * 传输完成前有效且内容不被覆盖；日志端口为此使用静态 TxBuffer。
   */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  /* USER CODE END 7 */
  return result;
}

/**
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted callback
  *
  *         @note
  *         This function is IN transfer complete callback used to inform user that
  *         the submitted Data is successfully sent over USB.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 13 */
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  /* USER CODE END 13 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @brief  查询 USB CDC 是否已经可以接收新的异步发送请求。
  * @details 同时检查设备枚举状态、主机 DTR、DTR 后稳定时间、CDC 类句柄
  *          和 ST CDC 发送忙标志。函数只读取状态，不等待也不发送数据。
  * @retval 1U USB 已配置、主机已打开串口、稳定期结束且发送端空闲。
  * @retval 0U 任一条件不满足；调用者应保留数据并稍后重试。
  */
uint8_t CDC_IsReady_FS(void)
{
  const USBD_CDC_HandleTypeDef *hcdc =
      (const USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

  /*
   * 条件顺序先检查不需要解引用 hcdc 的全局状态，再检查 hcdc 是否为空，
   * 最后读取 TxState，依赖 C 语言 || 的从左到右短路求值保证安全。
   */
  if ((hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) ||
      (cdc_port_open_fs == 0U) ||
      ((uint32_t)(HAL_GetTick() - cdc_port_open_tick_fs) <
       CDC_PORT_OPEN_SETTLE_TIME_MS) ||
      (hcdc == NULL) ||
      (hcdc->TxState != 0U))
  {
    return 0U;
  }

  return 1U;
}

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
